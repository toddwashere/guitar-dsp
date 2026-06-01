#include "PitchShifter.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

void PitchShifter::prepare(double /*sampleRate*/, int maxGrainSamples) {
    bufSize_ = std::max(maxGrainSamples * 2, 256);
    buffer_.assign(static_cast<std::size_t>(bufSize_), 0.0f);
    grainSamples_ = std::min(grainSamples_, maxGrainSamples);
    reset();
}

void PitchShifter::reset() {
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writePos_ = 0;
    phase_ = 0.0f;
}

void PitchShifter::setGrainSamples(int n) noexcept {
    grainSamples_ = std::clamp(n, 64, bufSize_ > 0 ? bufSize_ / 2 : n);
}

float PitchShifter::readInterp(float delaySamples) const noexcept {
    float pos = static_cast<float>(writePos_) - delaySamples;
    while (pos < 0.0f) pos += static_cast<float>(bufSize_);
    const int i0 = static_cast<int>(pos);
    const float frac = pos - static_cast<float>(i0);
    const int i1 = (i0 + 1) % bufSize_;
    return (1.0f - frac) * buffer_[static_cast<std::size_t>(i0 % bufSize_)]
         + frac * buffer_[static_cast<std::size_t>(i1)];
}

float PitchShifter::processSample(float x) noexcept {
    buffer_[static_cast<std::size_t>(writePos_)] = x;
    writePos_ = (writePos_ + 1) % bufSize_;

    // Phase accumulator: read pointer advances at ratio_ samples per output
    // sample. Since write also advances at 1/sample, delay changes by
    // (1 - ratio_) per sample. Phase tracks how far we've drifted from the
    // grain start: phase += (ratio_ - 1) so that delay = G - phase decreases
    // at the right rate. The absolute read position therefore advances at
    // ratio_ per sample.
    phase_ += (ratio_ - 1.0f);
    const float g = static_cast<float>(grainSamples_);
    while (phase_ >= g)   phase_ -= g;
    while (phase_ < 0.0f) phase_ += g;

    const float p1 = phase_;
    float p2 = p1 + g * 0.5f;
    if (p2 >= g) p2 -= g;

    // Triangular window: peaks at grain centre, zero at edges.
    const float w1 = 1.0f - std::fabs(2.0f * p1 / g - 1.0f);
    const float w2 = 1.0f - std::fabs(2.0f * p2 / g - 1.0f);

    // Delay in samples: read position is (writePos - delay).
    // delay = G - p means the read pointer is p samples behind the "grain start"
    // which is G samples behind write. As p increases, read advances.
    const float s1 = readInterp(g - p1);
    const float s2 = readInterp(g - p2);

    return s1 * w1 + s2 * w2;
}

} // namespace guitar_dsp::audio
