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

void ClipBankPlayer::process(const float* /*onsetSrc*/, float* modOut,
                             std::size_t numSamples) noexcept {
    std::fill(modOut, modOut + numSamples, 0.0f);
}

} // namespace guitar_dsp::audio
