#include "Comb.h"

#include <algorithm>

namespace guitar_dsp::audio {

void Comb::prepare(double sampleRate, int maxDelaySamples) {
    sampleRate_ = sampleRate;
    bufSize_ = std::max(maxDelaySamples + 1, 2);
    buffer_.assign(static_cast<std::size_t>(bufSize_), 0.0f);
    reset();
}

void Comb::reset() {
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writePos_ = 0;
}

void Comb::setFreqHz(float hz) noexcept {
    if (hz <= 0.0f) { delaySamples_ = 0; return; }
    const int d = static_cast<int>(sampleRate_ / hz);
    delaySamples_ = std::clamp(d, 1, bufSize_ - 1);
}

void Comb::setFeedback(float fb) noexcept {
    feedback_ = std::clamp(fb, 0.0f, 0.95f);
}

float Comb::processSample(float x) noexcept {
    if (delaySamples_ <= 0) return x;
    int readPos = writePos_ - delaySamples_;
    if (readPos < 0) readPos += bufSize_;
    const float delayed = buffer_[static_cast<std::size_t>(readPos)];
    const float y = x + feedback_ * delayed;
    buffer_[static_cast<std::size_t>(writePos_)] = y;
    writePos_ = (writePos_ + 1) % bufSize_;
    return (1.0f - mix_) * x + mix_ * y;
}

} // namespace guitar_dsp::audio
