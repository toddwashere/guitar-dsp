#pragma once

#include <atomic>

namespace guitar_dsp::audio {

// Single-band peak limiter for the master output.
//
// Algorithm:
//   * Peak-following envelope detector. Attack is instant (input above
//     current envelope snaps to input); release is exponential with an
//     ~80 ms time constant.
//   * Brickwall gain: when the envelope exceeds the threshold, gain is
//     set so the envelope is multiplied back down to the threshold.
//     Below threshold, gain is 1.0.
//   * Gain smoothing: a separate one-pole on the GAIN itself prevents
//     audible clicks when the limiter kicks in or releases. Reduction
//     ramps in with a ~0.5 ms time constant; release ramps out with the
//     same ~80 ms release constant as the envelope follower.
//
// No lookahead — keeps latency at zero. Inter-sample peaks may briefly
// exceed the threshold by less than ~0.5 dB; fine for monitors / live use.
//
// RT-safe: process() allocates nothing. All cross-thread state is in
// atomics. Gain-reduction telemetry for the UI is updated once per block
// (atomic store of the max GR observed during the block).
class Limiter {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Message thread.
    void setEnabled(bool on) noexcept {
        enabled_.store(on, std::memory_order_relaxed);
    }
    bool enabled() const noexcept {
        return enabled_.load(std::memory_order_relaxed);
    }
    void setThresholdDb(float dbfs) noexcept;
    float thresholdDb() const noexcept {
        return thresholdDb_.load(std::memory_order_relaxed);
    }

    // Audio thread. In-place limit on a mono buffer.
    void process(float* buf, int numSamples) noexcept;

    // UI telemetry. Max gain reduction observed during the most recent
    // process() call, in dB. 0 = no reduction.
    float currentGainReductionDb() const noexcept {
        return reportedGRdb_.load(std::memory_order_relaxed);
    }

private:
    double sampleRate_ = 48000.0;

    std::atomic<bool>  enabled_      {false};
    std::atomic<float> thresholdDb_  {-10.0f};
    std::atomic<float> thresholdLin_ {0.3162f};  // 10^(-10/20)
    std::atomic<float> reportedGRdb_ {0.0f};

    // Audio-thread-only state.
    float envelope_     = 0.0f;
    float currentGain_  = 1.0f;
    float gainAttackCoef_  = 0.0f;  // recomputed in prepare()
    float envReleaseCoef_  = 0.0f;
};

} // namespace guitar_dsp::audio
