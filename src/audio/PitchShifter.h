#pragma once

#include <vector>

namespace guitar_dsp::audio {

// Two-tap granular (overlap-add) pitch shifter at a fixed ratio. Mono,
// allocation-free after prepare(). No pitch detection: the read phase moves
// at (1 - ratio) per sample over a grain window, with two taps half a grain
// apart crossfaded by triangular windows to hide the wrap discontinuity.
//
// ratio = 2^(semitones/12). ratio 1.0 ≈ identity (with grain latency).
class PitchShifter {
public:
    void prepare(double sampleRate, int maxGrainSamples);
    void reset();

    void setRatio(float ratio) noexcept { ratio_ = ratio; }
    void setGrainSamples(int n) noexcept;

    float processSample(float x) noexcept;

private:
    float readInterp(float delaySamples) const noexcept;

    std::vector<float> buffer_;
    int   bufSize_     = 0;
    int   writePos_    = 0;
    int   grainSamples_= 1920;
    float ratio_       = 1.0f;
    float phase_       = 0.0f;
};

} // namespace guitar_dsp::audio
