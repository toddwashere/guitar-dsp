#include "ClipBankPlayer.h"

#include <algorithm>

namespace guitar_dsp::audio {

ClipBankPlayer::ClipBankPlayer() = default;

void ClipBankPlayer::prepare(double sampleRate, int /*blockSize*/) {
    onset_.prepare(sampleRate);
    reset();
}

void ClipBankPlayer::reset() {
    onset_.reset();
    cursor_  = -1;
    playPos_ = 0;
    playing_ = false;
    currentClipIndex_.store(-1, std::memory_order_relaxed);
}

void ClipBankPlayer::setBank(std::vector<TTSClipPtr> clips) {
    pendingBank_ = std::move(clips);
    bankSize_.store(static_cast<int>(pendingBank_.size()),
                    std::memory_order_relaxed);
    newBankFlag_.store(true, std::memory_order_release);
}

void ClipBankPlayer::rewind() noexcept {
    pendingRewind_.store(true, std::memory_order_release);
}

void ClipBankPlayer::process(const float* onsetSrc, float* modOut,
                             std::size_t numSamples) noexcept {
    // Drain pending bank swap (audio-thread-safe pointer move).
    if (newBankFlag_.exchange(false, std::memory_order_acquire)) {
        activeBank_ = std::move(pendingBank_);
        cursor_  = -1;
        playPos_ = 0;
        playing_ = false;
        onset_.reset();
        currentClipIndex_.store(-1, std::memory_order_relaxed);
    }

    // Drain pending rewind.
    if (pendingRewind_.exchange(false, std::memory_order_acquire)) {
        cursor_  = -1;
        playPos_ = 0;
        playing_ = false;
        onset_.reset();
        currentClipIndex_.store(-1, std::memory_order_relaxed);
    }

    const bool haveBank = !activeBank_.empty();

    for (std::size_t i = 0; i < numSamples; ++i) {
        if (haveBank && onset_.processSample(onsetSrc[i])) {
            // Each onset advances the cursor AND restarts playback from
            // the new clip's sample 0 — no latch, even if the prior clip
            // is still playing. This is the punchy "every-pick-different"
            // behavior the spec calls for.
            const int n = static_cast<int>(activeBank_.size());
            cursor_  = (cursor_ + 1) % n;
            playPos_ = 0;
            playing_ = true;
            currentClipIndex_.store(cursor_, std::memory_order_relaxed);
        }

        float s = 0.0f;
        if (playing_) {
            const auto& clip = activeBank_[static_cast<std::size_t>(cursor_)];
            if (clip && playPos_ < clip->samples.size()) {
                s = clip->samples[playPos_++];
            } else {
                playing_ = false;
            }
        }
        modOut[i] = s;
    }
}

} // namespace guitar_dsp::audio
