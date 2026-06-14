#include "FormantModulator.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

void FormantModulator::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    onset_.prepare(sampleRate);
    reset();
}

void FormantModulator::reset() {
    onset_.reset();
    lfoPhase_      = 0.0f;
    envIndex_      = -1;
    envCurrentPos_ = 0.0f;
    envTargetPos_  = 0.0f;
}

void FormantModulator::setMode(Mode m) noexcept { mode_ = m; }
void FormantModulator::setStaticPosition(float p) noexcept { staticPos_ = p; }
void FormantModulator::setBreakpoints(std::vector<float> bp) noexcept {
    breakpoints_ = std::move(bp);
}
void FormantModulator::setLfoRateHz(float hz) noexcept {
    lfoIncrPerSample_ = (sampleRate_ > 0.0) ? static_cast<float>(hz / sampleRate_) : 0.0f;
}
void FormantModulator::setEnvelopeAttackMs(float ms) noexcept {
    envAttackMs_ = ms;
    const float samples = static_cast<float>(sampleRate_ * ms / 1000.0);
    envRampPerSample_ = (samples > 0.0f) ? (1.0f / samples) : 1.0f;
}

void FormantModulator::process(const float* /*onsetSrc*/, float* posOut,
                                std::size_t numSamples) noexcept {
    // Stub: fill with the static position so Static mode "works" already.
    std::fill(posOut, posOut + numSamples, staticPos_);
}

} // namespace guitar_dsp::audio
