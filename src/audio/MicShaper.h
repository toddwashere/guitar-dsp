#pragma once

#include <cstddef>

namespace guitar_dsp::audio {

// Fixed noise gate + makeup gain stage for routing the mic into the vocoder
// modulator input. Gate prevents room noise from gating the vocoder open
// during silence; makeup gain compensates for typical mic level (-30 dBFS
// peaks → -6 dBFS peaks) so the modulator excites the envelope follower
// reliably. Output is hard-limited to ±1.0 so a clipped modulator doesn't
// smear envelopes across the vocoder bands.
//
// Coefficients are not user-tunable in v1.
class MicShaper {
public:
    void prepare(double sampleRate);
    void reset();

    void process(const float* in, float* out, std::size_t numSamples) noexcept;

private:
    static constexpr float kGateThreshold     = 0.0025f;  // ~-52 dBFS
    static constexpr float kGateAttackMs      = 5.0f;
    static constexpr float kGateReleaseMs     = 50.0f;
    static constexpr float kMakeupGainLinear  = 4.0f;     // +12 dB

    float gateAttackCoef_  = 0.0f;
    float gateReleaseCoef_ = 0.0f;
    float gateGain_        = 0.0f;   // smoothed 0..1
};

} // namespace guitar_dsp::audio
