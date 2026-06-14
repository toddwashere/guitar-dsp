#include "MicShaper.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

void MicShaper::prepare(double sampleRate) {
    const auto coefForMs = [sampleRate](float ms) {
        const float samples = static_cast<float>(sampleRate * ms / 1000.0);
        return samples > 0.0f
            ? 1.0f - std::exp(-1.0f / samples)
            : 1.0f;
    };
    gateAttackCoef_  = coefForMs(kGateAttackMs);
    gateReleaseCoef_ = coefForMs(kGateReleaseMs);
    reset();
}

void MicShaper::reset() {
    gateGain_ = 0.0f;
}

void MicShaper::process(const float* in, float* out, std::size_t numSamples) noexcept {
    for (std::size_t i = 0; i < numSamples; ++i) {
        const float x = in[i];
        const float absx = std::fabs(x);

        // Per-sample envelope follower with attack/release smoothing on the
        // gate's open/closed target. The target is 1 when |x| > threshold,
        // 0 otherwise; the smoothed gateGain_ approaches the target.
        const float target = (absx > kGateThreshold) ? 1.0f : 0.0f;
        const float coef   = (target > gateGain_) ? gateAttackCoef_ : gateReleaseCoef_;
        gateGain_ += coef * (target - gateGain_);

        // Apply gate + makeup gain. Hard-limit to ±1.0 to protect the vocoder.
        float y = x * gateGain_ * kMakeupGainLinear;
        if (y >  1.0f) y =  1.0f;
        if (y < -1.0f) y = -1.0f;
        out[i] = y;
    }
}

} // namespace guitar_dsp::audio
