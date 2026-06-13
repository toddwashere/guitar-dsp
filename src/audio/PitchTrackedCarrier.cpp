#include "PitchTrackedCarrier.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

PitchTrackedCarrier::PitchTrackedCarrier() = default;

void PitchTrackedCarrier::prepare(double sampleRate, int blockSize) {
    (void) blockSize;
    sampleRate_ = sampleRate;
    ring_.assign(kWindowSize, 0.0f);
    diff_.assign(kWindowSize / 2, 0.0f);
    reset();
}

void PitchTrackedCarrier::reset() {
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    ringWriteIdx_ = 0;
    samplesUntilNextHop_ = kHopSize;
    sawPhase_ = 0.0;
    currentFreqHz_ = 0.0f;
    lastVoicedFreqHz_ = 0.0f;
    holdSamplesRemaining_ = 0;
    decaySamplesRemaining_ = 0;
    decayGain_ = 0.0f;
    currentlyVoiced_ = false;
    currentMidiNote_ = -1;
    currentCents_ = 0.0f;
}

void PitchTrackedCarrier::setHoldMs(float ms)  noexcept { holdMs_  = ms; }
void PitchTrackedCarrier::setDecayMs(float ms) noexcept { decayMs_ = ms; }
void PitchTrackedCarrier::setFrequencyRange(float minHz, float maxHz) noexcept {
    minHz_ = minHz;
    maxHz_ = maxHz;
}

PitchTrackedCarrier::State PitchTrackedCarrier::process(
        const float* guitarIn, float* out, std::size_t numSamples) {
    // Implementation arrives in Tasks 2-4. For now, emit silence so the
    // module is usable as a no-op in the AudioGraph wiring task.
    (void) guitarIn;
    for (std::size_t i = 0; i < numSamples; ++i) out[i] = 0.0f;
    return State{};
}

float PitchTrackedCarrier::runYin() noexcept {
    return 0.0f;  // filled in Task 2
}

float PitchTrackedCarrier::nextSawSample(float /*freqHz*/) noexcept {
    return 0.0f;  // filled in Task 3
}

} // namespace guitar_dsp::audio
