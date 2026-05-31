#include "CarouselMod.h"

#include <cmath>

namespace guitar_dsp::audio {

void EnvelopeFollower::prepare(double sampleRate) noexcept {
    const double atkMs = 5.0, relMs = 120.0;
    attackCoef_  = static_cast<float>(std::exp(-1.0 / (sampleRate * atkMs / 1000.0)));
    releaseCoef_ = static_cast<float>(std::exp(-1.0 / (sampleRate * relMs / 1000.0)));
    env_ = 0.0f;
}

float EnvelopeFollower::processSample(float x) noexcept {
    const float a = std::fabs(x);
    const float coef = (a > env_) ? attackCoef_ : releaseCoef_;
    env_ = a + coef * (env_ - a);
    return env_;
}

float Lfo::processSample() noexcept {
    if (rateHz_ <= 0.0f) return 0.0f;
    const float v = std::sin(2.0f * 3.14159265358979f * phase_);
    phase_ += static_cast<float>(rateHz_ / sampleRate_);
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    return v;
}

} // namespace guitar_dsp::audio
