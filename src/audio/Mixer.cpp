#include "Mixer.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

namespace {
    constexpr float kPiOverTwo = 1.57079632679489661923f;
    float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }
}

Mixer::Mixer() = default;

void Mixer::prepare(double sampleRate, int /*blockSize*/) {
    sampleRate_ = sampleRate;
    recomputeCoefficients();
    reset();
}

void Mixer::reset() {
    currentDryGain_ = targetDryGain_;
    currentWetGain_ = targetWetGain_;
    currentMasterGain_ = targetMasterGain_;
}

void Mixer::setRampMs(float ms) {
    rampMs_ = std::max(0.1f, ms);
    recomputeCoefficients();
}

void Mixer::recomputeCoefficients() {
    const float t = rampMs_ * 0.001f;
    rampCoef_ = std::exp(-1.0f / (t * static_cast<float>(sampleRate_)));
}

void Mixer::setDryWet(float dryWet) {
    dryWet = std::clamp(dryWet, 0.0f, 1.0f);
    targetDryGain_ = std::cos(dryWet * kPiOverTwo);
    targetWetGain_ = std::sin(dryWet * kPiOverTwo);
    // Snap current to target so that the first process() block starts
    // from the correct gain. Mid-stream changes will ramp because
    // current retains the previous value when process() is already running.
    currentDryGain_ = targetDryGain_;
    currentWetGain_ = targetWetGain_;
}

void Mixer::setMasterGainDb(float db) {
    targetMasterGain_ = dbToLinear(db);
    currentMasterGain_ = targetMasterGain_;
}

void Mixer::process(const float* dry,
                    const float* wet,
                    float* out,
                    std::size_t numSamples) {
    for (std::size_t i = 0; i < numSamples; ++i) {
        currentDryGain_ = rampCoef_ * currentDryGain_ + (1.0f - rampCoef_) * targetDryGain_;
        currentWetGain_ = rampCoef_ * currentWetGain_ + (1.0f - rampCoef_) * targetWetGain_;
        currentMasterGain_ = rampCoef_ * currentMasterGain_ + (1.0f - rampCoef_) * targetMasterGain_;

        const float mixed = dry[i] * currentDryGain_ + wet[i] * currentWetGain_;
        out[i] = mixed * currentMasterGain_;
    }
}

} // namespace guitar_dsp::audio
