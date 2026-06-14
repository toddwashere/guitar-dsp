#pragma once

#include <cstddef>
#include <vector>

#include "OnsetDetector.h"

namespace guitar_dsp::audio {

// Drives a Formant's vowel position from a sequence of breakpoints + a
// timebase. Three modes:
//   - Static:   position is a single constant (setStaticPosition).
//   - Lfo:      position is a triangle wave through breakpoints at rateHz,
//               wrapping after the last.
//   - Envelope: position advances one breakpoint per detected onset, with
//               a linear attack ramp into the new position.
class FormantModulator {
public:
    enum class Mode { Static, Lfo, Envelope };

    void prepare(double sampleRate);
    void reset();

    void setMode(Mode m) noexcept;
    void setStaticPosition(float p) noexcept;
    void setBreakpoints(std::vector<float> bp) noexcept;
    void setLfoRateHz(float hz) noexcept;
    void setEnvelopeAttackMs(float ms) noexcept;

    // Audio thread.
    //   onsetSrc — required only in Envelope mode (ignored in Static/Lfo).
    //   posOut   — filled with one position value per sample.
    void process(const float* onsetSrc, float* posOut, std::size_t numSamples) noexcept;

private:
    Mode  mode_              = Mode::Static;
    float staticPos_         = 0.0f;
    std::vector<float> breakpoints_;
    double sampleRate_       = 48000.0;
    float  lfoPhase_         = 0.0f;
    float  lfoIncrPerSample_ = 0.0f;
    int    envIndex_         = -1;
    float  envCurrentPos_    = 0.0f;
    float  envTargetPos_     = 0.0f;
    float  envRampPerSample_ = 0.0f;
    float  envAttackMs_      = 30.0f;
    OnsetDetector onset_;
};

} // namespace guitar_dsp::audio
