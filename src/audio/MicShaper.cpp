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
    // Stub: copy input straight through. Behavior added in next tasks.
    std::copy(in, in + numSamples, out);
}

} // namespace guitar_dsp::audio
