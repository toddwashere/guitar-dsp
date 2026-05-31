#pragma once

namespace guitar_dsp::audio {

// One-pole peak envelope follower (fixed ~5 ms attack, ~120 ms release).
// Output is a smoothed absolute-amplitude estimate in [0, ~1].
class EnvelopeFollower {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept { env_ = 0.0f; }
    float processSample(float x) noexcept;

private:
    float env_         = 0.0f;
    float attackCoef_  = 0.0f;
    float releaseCoef_ = 0.0f;
};

// Sine LFO phasor in [-1, 1].
class Lfo {
public:
    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate; }
    void setRateHz(float hz) noexcept { rateHz_ = hz; }
    void reset() noexcept { phase_ = 0.0f; }
    float processSample() noexcept;

private:
    double sampleRate_ = 48000.0;
    float  rateHz_     = 0.0f;
    float  phase_      = 0.0f;  // 0..1
};

} // namespace guitar_dsp::audio
