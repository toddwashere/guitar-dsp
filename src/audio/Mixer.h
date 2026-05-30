#pragma once

#include <cstddef>

namespace guitar_dsp::audio {

// Two-input mixer: dry input + wet input, blended by a single dry/wet
// control (0.0 = fully dry, 1.0 = fully wet, equal-power crossfade in
// between), then scaled by a master gain. Parameters are smoothed over
// `rampSamples` to avoid zipper noise on parameter changes.
class Mixer {
public:
    Mixer();

    void prepare(double sampleRate, int blockSize);
    void reset();

    // dryWet: 0..1. Equal-power crossfade.
    void setDryWet(float dryWet);
    void setMasterGainDb(float db);
    void setRampMs(float ms);

    void process(const float* dry,
                 const float* wet,
                 float* out,
                 std::size_t numSamples);

private:
    double sampleRate_ = 48000.0;

    float targetDryGain_ = 1.0f;
    float targetWetGain_ = 0.0f;
    float currentDryGain_ = 1.0f;
    float currentWetGain_ = 0.0f;

    float targetMasterGain_ = 1.0f;
    float currentMasterGain_ = 1.0f;

    float rampCoef_ = 0.0f;
    float rampMs_ = 5.0f;

    void recomputeCoefficients();
};

} // namespace guitar_dsp::audio
