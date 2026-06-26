#pragma once

namespace guitar_dsp::audio {

// Note-attack detector for a clean (pre-effect) guitar signal. A peak
// envelope follower (instant attack, exponential release) plus hysteresis
// (arm / re-arm) and a debounce window. processSample returns true on the
// single sample where an onset fires. Allocation-free.
class OnsetDetector {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setAttackThreshold(float linear) noexcept { attackThresh_ = linear; }
    void setRearmThreshold(float linear) noexcept  { rearmThresh_ = linear; }
    void setDebounceMs(float ms) noexcept;

    bool processSample(float x) noexcept;

private:
    double sampleRate_   = 48000.0;
    float  env_          = 0.0f;
    float  releaseCoef_  = 0.0f;
    // Default thresholds correspond to -32 dBFS attack, -40 dBFS re-arm
    // (8 dB hysteresis). Tuned for soft fingerstyle plucks on a clean
    // pickup — the previous defaults (-26 / -34 dB) required hard strums
    // to trigger. Overridable per-player via setAttackThreshold/
    // setRearmThreshold.
    float  attackThresh_ = 0.025f;
    float  rearmThresh_  = 0.010f;
    bool   armed_        = true;
    int    debounceSamples_ = 0;
    int    sinceOnset_      = 0;
};

} // namespace guitar_dsp::audio
