#include "ChannelVocoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace guitar_dsp::audio {

namespace {

// Bark-scale band centers for 24 bands from 80 Hz to 10 kHz, evenly
// spaced on the Bark axis (Zwicker scale).
constexpr std::array<float, 24> kBarkCenters{{
     80.0f,  120.0f,  170.0f,  230.0f,  300.0f,  385.0f,
    495.0f,  630.0f,  790.0f,  980.0f, 1205.0f, 1470.0f,
   1785.0f, 2160.0f, 2615.0f, 3160.0f, 3800.0f, 4540.0f,
   5395.0f, 6385.0f, 7530.0f, 8845.0f, 9750.0f, 10000.0f,
}};

} // namespace

ChannelVocoder::ChannelVocoder() {
    std::copy(kBarkCenters.begin(), kBarkCenters.end(), bandCenters_.begin());
}

void ChannelVocoder::prepare(double sampleRate, int /*blockSize*/) {
    sampleRate_ = sampleRate;
    recomputeCoefficients();
    reset();
}

void ChannelVocoder::reset() {
    for (auto& s : carrierState_)    s = {};
    for (auto& s : modulatorState_)  s = {};
    std::fill(envelope_.begin(), envelope_.end(), 0.0f);
}

void ChannelVocoder::recomputeCoefficients() {
    // Q ≈ 6.0 gives reasonable band overlap on a Bark scale.
    constexpr float Q = 6.0f;
    const float fs = static_cast<float>(sampleRate_);

    for (int i = 0; i < kNumBands; ++i) {
        const float f0 = bandCenters_[static_cast<std::size_t>(i)];
        const float w0 = 2.0f * 3.14159265358979323846f * f0 / fs;
        const float cos_w0 = std::cos(w0);
        const float sin_w0 = std::sin(w0);
        const float alpha  = sin_w0 / (2.0f * Q);

        // Constant-skirt-gain 2-pole BPF (RBJ cookbook).
        const float a0 = 1.0f + alpha;
        BiquadCoefs c;
        c.b0 =  sin_w0 / 2.0f / a0;
        c.b1 =  0.0f;
        c.b2 = -sin_w0 / 2.0f / a0;
        c.a1 = -2.0f * cos_w0 / a0;
        c.a2 = (1.0f - alpha) / a0;
        coefs_[static_cast<std::size_t>(i)] = c;
    }

    // 15 ms envelope follower one-pole: coef = exp(-1/(t*fs)).
    constexpr float envT = 0.015f;
    envelopeCoef_ = std::exp(-1.0f / (envT * fs));
}

void ChannelVocoder::setWetLevel(float v) {
    wetLevel_ = std::clamp(v, 0.0f, 1.0f);
}

void ChannelVocoder::setSibilance(float v) {
    sibilance_ = std::clamp(v, 0.0f, 1.0f);
}

float ChannelVocoder::singleBiquad(float x,
                                   const BiquadCoefs& c,
                                   BiquadState& s) noexcept {
    const float y = c.b0 * x + c.b1 * s.xz1 + c.b2 * s.xz2
                  - c.a1 * s.yz1 - c.a2 * s.yz2;
    s.xz2 = s.xz1;  s.xz1 = x;
    s.yz2 = s.yz1;  s.yz1 = y;
    return y;
}

void ChannelVocoder::process(const float* carrier,
                             const float* modulator,
                             float* output,
                             std::size_t numSamples) {
    const float oneMinusEnv = 1.0f - envelopeCoef_;

    for (std::size_t i = 0; i < numSamples; ++i) {
        const float c = carrier[i];
        const float m = modulator[i];

        float sum = 0.0f;
        for (int b = 0; b < kNumBands; ++b) {
            // Band-filter the modulator and track per-band envelope.
            const float mBand = singleBiquad(m, coefs_[static_cast<std::size_t>(b)],
                                                modulatorState_[static_cast<std::size_t>(b)]);
            const float absM = std::abs(mBand);
            envelope_[static_cast<std::size_t>(b)] =
                envelopeCoef_ * envelope_[static_cast<std::size_t>(b)] + oneMinusEnv * absM;

            // Band-filter the carrier and scale by modulator envelope.
            const float cBand = singleBiquad(c, coefs_[static_cast<std::size_t>(b)],
                                                carrierState_[static_cast<std::size_t>(b)]);
            sum += cBand * envelope_[static_cast<std::size_t>(b)];
        }

        output[i] = sum * wetLevel_;
    }
}

} // namespace guitar_dsp::audio
