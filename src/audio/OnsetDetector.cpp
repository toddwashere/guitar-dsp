#include "OnsetDetector.h"

#include <cmath>

namespace guitar_dsp::audio {

void OnsetDetector::prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate;
    const double releaseMs = 30.0;
    releaseCoef_ = static_cast<float>(std::exp(-1.0 / (sampleRate * releaseMs / 1000.0)));
    setDebounceMs(80.0f);
    reset();
}

void OnsetDetector::reset() noexcept {
    env_ = 0.0f;
    armed_ = true;
    sinceOnset_ = debounceSamples_;   // allow an onset immediately
}

void OnsetDetector::setDebounceMs(float ms) noexcept {
    debounceSamples_ = static_cast<int>(ms * 0.001 * sampleRate_);
    if (debounceSamples_ < 1) debounceSamples_ = 1;
}

bool OnsetDetector::processSample(float x) noexcept {
    const float a = std::fabs(x);
    env_ = (a > env_) ? a : (env_ * releaseCoef_);   // instant attack, exp release

    if (sinceOnset_ < debounceSamples_) ++sinceOnset_;

    bool onset = false;
    if (armed_ && env_ >= attackThresh_ && sinceOnset_ >= debounceSamples_) {
        onset = true;
        armed_ = false;
        sinceOnset_ = 0;
    } else if (!armed_ && env_ <= rearmThresh_) {
        armed_ = true;
    }
    return onset;
}

} // namespace guitar_dsp::audio
