#include "VowelGrainLoop.h"

namespace guitar_dsp::audio {

void VowelGrainLoop::prepare(double sr) {
    sampleRate_ = sr;
    xfadeLen_   = static_cast<std::size_t>(0.005 * sr);  // 5 ms
    reset();
}

void VowelGrainLoop::reset() {
    samples_ = nullptr;
    loopStart_ = 0;
    loopLen_ = static_cast<std::size_t>(0.020 * sampleRate_);  // 20 ms
    cursor_ = 0;
}

void VowelGrainLoop::beginLoop(const float* samples,
                               std::size_t anchorSample) noexcept {
    samples_ = samples;
    // Center the loop on the anchor.
    if (anchorSample >= loopLen_ / 2) loopStart_ = anchorSample - loopLen_ / 2;
    else                              loopStart_ = 0;
    cursor_ = 0;
}

float VowelGrainLoop::next() noexcept {
    if (samples_ == nullptr || loopLen_ == 0) return 0.0f;
    const std::size_t pos = loopStart_ + cursor_;
    float s = samples_[pos];
    // Crossfade the last `xfadeLen_` samples of the loop with the first
    // `xfadeLen_` of the next iteration. Cheap linear xfade.
    if (cursor_ + xfadeLen_ >= loopLen_) {
        const std::size_t into = cursor_ + xfadeLen_ - loopLen_;
        const float w = static_cast<float>(into)
                      / static_cast<float>(xfadeLen_);
        const float head = samples_[loopStart_ + into];
        s = (1.0f - w) * s + w * head;
    }
    if (++cursor_ >= loopLen_) cursor_ = 0;
    return s;
}

} // namespace guitar_dsp::audio
