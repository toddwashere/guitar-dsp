#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "IVocoder.h"

namespace guitar_dsp::audio {

// 24-band channel vocoder, Bark-scale band layout, biquad filter banks
// for both carrier and modulator, one-pole envelope follower per band,
// optional sibilance noise injection for fricatives.
//
// Algorithm per spec §5.2:
//   1. Split modulator into N bandpass filters.
//   2. Per band: one-pole envelope follower (~15 ms time constant).
//   3. Split carrier into the same N bands.
//   4. Multiply each carrier band by its modulator envelope.
//   5. Sum all bands.
//   6. Sibilance: detect high-band unvoiced energy in modulator, mix
//      noise shaped by the high bands into the sum.
class ChannelVocoder : public IVocoder {
public:
    static constexpr int kNumBands = 24;

    ChannelVocoder();
    ~ChannelVocoder() override = default;

    void prepare(double sampleRate, int blockSize) override;
    void reset() override;
    void process(const float* carrier,
                 const float* modulator,
                 float* output,
                 std::size_t numSamples) override;
    void setWetLevel(float v) override;
    void setSibilance(float v) override;

private:
    double sampleRate_ = 48000.0;

    // Bark-spaced center frequencies for the 24 bands (Hz).
    // Populated in prepare().
    std::array<float, kNumBands> bandCenters_{};

    // Per-band filter state (carrier and modulator share band layout).
    // Each entry is a 2-pole bandpass (Direct Form I), so we store
    // x[n-1], x[n-2], y[n-1], y[n-2] per filter.
    struct BiquadState {
        float xz1 = 0.0f, xz2 = 0.0f;
        float yz1 = 0.0f, yz2 = 0.0f;
    };
    struct BiquadCoefs {
        // Standard 2-pole bandpass biquad: a0 normalized to 1.
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
    };

    std::array<BiquadCoefs,  kNumBands> coefs_{};
    std::array<BiquadState,  kNumBands> carrierState_{};
    std::array<BiquadState,  kNumBands> modulatorState_{};

    // Per-band envelope follower output (one-pole LP of |modulator band|).
    std::array<float, kNumBands> envelope_{};
    float envelopeCoef_ = 0.0f;  // computed from ~15 ms time constant

    // Output level + sibilance, smoothed per sample.
    float wetLevel_   = 1.0f;
    float sibilance_  = 0.5f;

    // Sibilance noise generator state (white noise via xorshift).
    std::uint32_t noiseState_ = 0xC0FFEE01u;

    void recomputeCoefficients();
    static float singleBiquad(float x,
                              const BiquadCoefs& c,
                              BiquadState& s) noexcept;
};

} // namespace guitar_dsp::audio
