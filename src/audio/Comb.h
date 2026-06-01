#pragma once

#include <vector>

namespace guitar_dsp::audio {

// Feedback comb filter: y[n] = x[n] + fb * y[n-D], D = sampleRate / freqHz.
// freqHz == 0 bypasses. Feedback is clamped to [0, 0.95] for stability.
// mix blends dry/comb (0 = dry, 1 = comb).
class Comb {
public:
    void prepare(double sampleRate, int maxDelaySamples);
    void reset();

    void setFreqHz(float hz) noexcept;
    void setFeedback(float fb) noexcept;
    void setMix(float mix) noexcept { mix_ = mix; }

    float processSample(float x) noexcept;

private:
    std::vector<float> buffer_;
    int    bufSize_      = 0;
    int    writePos_     = 0;
    int    delaySamples_ = 0;
    float  feedback_     = 0.0f;
    float  mix_          = 0.0f;
    double sampleRate_   = 48000.0;
};

} // namespace guitar_dsp::audio
