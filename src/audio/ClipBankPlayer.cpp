#include "ClipBankPlayer.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

        // Recompute anchor-mode + unique key list.
        anchorMode_  = false;
        uniqueKeys_.clear();
        keyCursor_   = -1;
        if (! activeBank_.empty() && activeBank_[0] &&
            ! activeBank_[0]->bankKey.empty()) {
            anchorMode_ = true;
            for (const auto& c : activeBank_) {
                if (! c) continue;
                const auto& k = c->bankKey;
                if (std::find(uniqueKeys_.begin(), uniqueKeys_.end(), k) ==
                    uniqueKeys_.end()) {
                    uniqueKeys_.push_back(k);
                }
            }
        }
    }

    // Drain pending rewind.
    if (pendingRewind_.exchange(false, std::memory_order_acquire)) {
        cursor_    = -1;
        playPos_   = 0;
        playing_   = false;
        keyCursor_ = -1;   // I8: reset anchor-mode key cursor so next onset starts at key 0
        onset_.reset();
        currentClipIndex_.store(-1, std::memory_order_relaxed);
    }

    const bool haveBank = !activeBank_.empty();

    for (std::size_t i = 0; i < numSamples; ++i) {
        if (haveBank && onset_.processSample(onsetSrc[i])) {
            const int n = static_cast<int>(activeBank_.size());
            int next = -1;
            if (anchorMode_ && ! uniqueKeys_.empty()) {
                // Cycle keys; pick closest anchor within the new key.
                keyCursor_ = (keyCursor_ + 1) % static_cast<int>(uniqueKeys_.size());
                const auto& wantKey = uniqueKeys_[(std::size_t) keyCursor_];
                const float detected = detectedPitchHz_.load(std::memory_order_relaxed);
                if (detected <= 0.0f) {
                    // Pitch unknown — pick first grain of the key.
                    for (int j = 0; j < n; ++j) {
                        const auto& c = activeBank_[(std::size_t) j];
                        if (c && c->bankKey == wantKey) { next = j; break; }
                    }
                } else {
                    float bestDist = std::numeric_limits<float>::infinity();
                    for (int j = 0; j < n; ++j) {
                        const auto& c = activeBank_[(std::size_t) j];
                        if (! c || c->bankKey != wantKey) continue;
                        const float d = std::fabs(c->anchorPitchHz - detected);
                        if (d < bestDist) { bestDist = d; next = j; }
                    }
                }
            }
            if (next < 0) {
                next = (cursor_ + 1) % n;  // legacy round-robin
            }
            cursor_  = next;
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
