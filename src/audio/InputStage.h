#pragma once

#include <cstddef>

namespace guitar_dsp::audio {

// Front-of-chain processing for the live guitar signal.
// Provides DC removal (essential for any AC-coupled input), a noise gate
// (suppresses background hum / amp hiss during silence), and input gain
// trim. Operates in-place on mono float buffers.
class InputStage {
public:
    InputStage();
    ~InputStage();

    void prepare(double sampleRate, int blockSize);
    void reset();

    // Process numSamples in/out. `in` and `out` may alias.
    void process(const float* in, float* out, std::size_t numSamples);

    // Threshold in dBFS below which the gate closes. Set very low (e.g.
    // -200) to effectively disable the gate.
    void setNoiseGateThreshold(float thresholdDb);
    void setInputGainDb(float gainDb);

private:
    double sampleRate_ = 48000.0;

    // DC blocker (one-pole high-pass at ~10 Hz).
    float dcBlockerXPrev_ = 0.0f;
    float dcBlockerYPrev_ = 0.0f;
    float dcBlockerR_ = 0.0f;

    // Noise gate (peak envelope follower + soft gain control).
    float gateThresholdLin_ = 0.0f;
    float gateEnvelope_ = 0.0f;
    float gateAttackCoef_ = 0.0f;
    float gateReleaseCoef_ = 0.0f;
    float gateCurrentGain_ = 1.0f;

    // Input gain.
    float inputGainLin_ = 1.0f;

    void recomputeCoefficients();
};

} // namespace guitar_dsp::audio
