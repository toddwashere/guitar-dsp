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
    for (std::size_t i = 0; i < numSamples; ++i) {
        ring_[ringWriteIdx_] = guitarIn[i];
        ringWriteIdx_ = (ringWriteIdx_ + 1) % kWindowSize;

        if (--samplesUntilNextHop_ == 0) {
            samplesUntilNextHop_ = kHopSize;
            const float f0 = runYin();
            currentlyVoiced_ = (f0 > 0.0f);
            if (currentlyVoiced_) {
                currentFreqHz_ = f0;
                lastVoicedFreqHz_ = f0;
                const float midi = 69.0f + 12.0f * std::log2(f0 / 440.0f);
                currentMidiNote_ = static_cast<int>(std::lround(midi));
                currentCents_ = 100.0f * (midi - currentMidiNote_);
            }
        }

        out[i] = 0.0f;  // Task 3 will fill this with the saw
    }

    State s;
    s.voiced   = currentlyVoiced_;
    s.freqHz   = currentlyVoiced_ ? currentFreqHz_ : 0.0f;
    s.midiNote = currentlyVoiced_ ? currentMidiNote_ : -1;
    s.cents    = currentlyVoiced_ ? currentCents_ : 0.0f;
    return s;
}

float PitchTrackedCarrier::runYin() noexcept {
    // YIN steps 2-4 (de Cheveigné & Kawahara, 2002):
    //   1) Difference function: d_t(tau) = sum_{j=0..W-1} (x[j] - x[j+tau])^2
    //      Computed over the most recent kWindowSize samples in the ring.
    //   2) Cumulative mean normalized difference (CMNDF):
    //      d'_t(0) = 1; d'_t(tau) = d_t(tau) / ((1/tau) * sum_{j=1..tau} d_t(j))
    //   3) Absolute threshold: pick the smallest tau with d'_t(tau) < threshold
    //      such that it's a local minimum; if none below threshold, unvoiced.
    //   4) Parabolic interpolation around the chosen tau for sub-sample precision.

    const int W = kWindowSize / 2;
    auto sampleAt = [this](int idxFromOldest) {
        // idxFromOldest=0 is the oldest sample in the ring;
        // kWindowSize-1 is the newest.
        return ring_[(ringWriteIdx_ + idxFromOldest) % kWindowSize];
    };

    // Step 1: difference function
    for (int tau = 0; tau < W; ++tau) {
        float sum = 0.0f;
        for (int j = 0; j < W; ++j) {
            const float a = sampleAt(j);
            const float b = sampleAt(j + tau);
            const float d = a - b;
            sum += d * d;
        }
        diff_[tau] = sum;
    }

    // Step 2: cumulative mean normalized difference.
    // Silence guard: if the difference function is identically zero (silent
    // input), the CMNDF collapses to 0/0 and any guard that maps it to a
    // non-positive value would let the threshold pick fire on noise. Treat
    // an all-zero difference function as unvoiced explicitly.
    float diffEnergy = 0.0f;
    for (int tau = 1; tau < W; ++tau) diffEnergy += diff_[tau];
    if (diffEnergy <= 0.0f) return 0.0f;

    diff_[0] = 1.0f;
    float runningSum = 0.0f;
    for (int tau = 1; tau < W; ++tau) {
        runningSum += diff_[tau];
        diff_[tau] = diff_[tau] * tau / (runningSum > 0.0f ? runningSum : 1.0f);
    }

    // Step 3: absolute threshold + first local minimum
    int chosenTau = -1;
    const int minTau = std::max(2, static_cast<int>(sampleRate_ / maxHz_));
    const int maxTau = std::min(W - 1, static_cast<int>(sampleRate_ / minHz_));
    for (int tau = minTau; tau <= maxTau; ++tau) {
        if (diff_[tau] < kYinThreshold) {
            // Walk forward while the function is still decreasing.
            while (tau + 1 <= maxTau && diff_[tau + 1] < diff_[tau]) ++tau;
            chosenTau = tau;
            break;
        }
    }
    if (chosenTau < 0) return 0.0f;  // unvoiced

    // Step 4: parabolic interpolation around chosenTau
    float refined = static_cast<float>(chosenTau);
    if (chosenTau > 0 && chosenTau < W - 1) {
        const float y0 = diff_[chosenTau - 1];
        const float y1 = diff_[chosenTau];
        const float y2 = diff_[chosenTau + 1];
        const float denom = 2.0f * (y0 - 2.0f * y1 + y2);
        if (std::abs(denom) > 1e-12f)
            refined += (y0 - y2) / denom;
    }

    const float f0 = static_cast<float>(sampleRate_) / refined;
    if (f0 < minHz_ || f0 > maxHz_) return 0.0f;
    return f0;
}

float PitchTrackedCarrier::nextSawSample(float /*freqHz*/) noexcept {
    return 0.0f;  // filled in Task 3
}

} // namespace guitar_dsp::audio
