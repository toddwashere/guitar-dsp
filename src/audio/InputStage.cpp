#include "InputStage.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

namespace {
    constexpr float kEpsilon = 1.0e-9f;

    float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }
}

InputStage::InputStage() {
    setNoiseGateThreshold(-60.0f);
    setInputGainDb(0.0f);
}

InputStage::~InputStage() = default;

void InputStage::prepare(double sampleRate, int /*blockSize*/) {
    sampleRate_ = sampleRate;
    recomputeCoefficients();
    reset();
}

void InputStage::reset() {
    dcBlockerXPrev_ = 0.0f;
    dcBlockerYPrev_ = 0.0f;
    gateEnvelope_ = 0.0f;
    gateCurrentGain_ = 1.0f;
}

void InputStage::recomputeCoefficients() {
    // DC blocker: y[n] = x[n] - x[n-1] + R*y[n-1]; R ≈ 1 - (2π*fc/fs).
    // fc = 10 Hz.
    constexpr float fc = 10.0f;
    dcBlockerR_ = 1.0f - (6.28318530717958647692f * fc /
                          static_cast<float>(sampleRate_));

    // Gate: 5 ms attack, 80 ms release.
    const float attackTime = 0.005f;
    const float releaseTime = 0.080f;
    gateAttackCoef_ = std::exp(-1.0f / (attackTime * static_cast<float>(sampleRate_)));
    gateReleaseCoef_ = std::exp(-1.0f / (releaseTime * static_cast<float>(sampleRate_)));
}

void InputStage::setNoiseGateThreshold(float thresholdDb) {
    gateThresholdLin_ = dbToLinear(thresholdDb);
}

void InputStage::setInputGainDb(float gainDb) {
    inputGainLin_ = dbToLinear(gainDb);
}

void InputStage::process(const float* in, float* out, std::size_t numSamples) {
    for (std::size_t i = 0; i < numSamples; ++i) {
        // 1. Input gain.
        float x = in[i] * inputGainLin_;

        // 2. DC blocker.
        const float y = x - dcBlockerXPrev_ + dcBlockerR_ * dcBlockerYPrev_;
        dcBlockerXPrev_ = x;
        dcBlockerYPrev_ = y;
        x = y;

        // 3. Noise gate (peak envelope follower, hard threshold for now).
        const float absX = std::abs(x);
        const float coef = (absX > gateEnvelope_) ? gateAttackCoef_ : gateReleaseCoef_;
        gateEnvelope_ = coef * gateEnvelope_ + (1.0f - coef) * absX;

        const float targetGain = (gateEnvelope_ > gateThresholdLin_) ? 1.0f : 0.0f;
        gateCurrentGain_ = gateCurrentGain_ * 0.99f + targetGain * 0.01f;

        out[i] = x * gateCurrentGain_;
    }
    // Denormal guard.
    if (std::abs(dcBlockerYPrev_) < kEpsilon) dcBlockerYPrev_ = 0.0f;
    if (gateEnvelope_ < kEpsilon) gateEnvelope_ = 0.0f;
}

} // namespace guitar_dsp::audio
