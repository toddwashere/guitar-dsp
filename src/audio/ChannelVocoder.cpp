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

    // 25 ms envelope follower one-pole: coef = exp(-1/(t*fs)).
    constexpr float envT = 0.025f;  // softer release, sounds less robotic
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
    const float outputGain   = outputGain_.load(std::memory_order_relaxed);
    const float carrierNoise = carrierNoiseMix_.load(std::memory_order_relaxed);

    for (std::size_t i = 0; i < numSamples; ++i) {
        // Broadband carrier floor: mix white noise into the carrier so a
        // sparse clean-guitar note still excites every band (the formant
        // regions a single note's harmonics leave empty).
        carrierNoiseState_ ^= carrierNoiseState_ << 13;
        carrierNoiseState_ ^= carrierNoiseState_ >> 17;
        carrierNoiseState_ ^= carrierNoiseState_ << 5;
        const float cNoise = (static_cast<float>(carrierNoiseState_) / 2147483648.0f) - 1.0f;
        const float c = carrier[i] + carrierNoise * cNoise;
        const float m = modulator[i];

        float sum = 0.0f;
        float sibEnvelope = 0.0f;  // sum of envelopes from high bands

        for (int b = 0; b < kNumBands; ++b) {
            const float mBand = singleBiquad(m, coefs_[static_cast<std::size_t>(b)],
                                                modulatorState_[static_cast<std::size_t>(b)]);
            const float absM = std::abs(mBand);
            envelope_[static_cast<std::size_t>(b)] =
                envelopeCoef_ * envelope_[static_cast<std::size_t>(b)] + oneMinusEnv * absM;

            const float cBand = singleBiquad(c, coefs_[static_cast<std::size_t>(b)],
                                                carrierState_[static_cast<std::size_t>(b)]);
            sum += cBand * envelope_[static_cast<std::size_t>(b)];

            // Bands 18..23 are >5 kHz; treat as sibilance candidates.
            if (b >= 18) sibEnvelope += envelope_[static_cast<std::size_t>(b)];
        }

        // Generate white noise via xorshift32, then attenuate by the
        // high-band modulator energy and the user's sibilance setting.
        noiseState_ ^= noiseState_ << 13;
        noiseState_ ^= noiseState_ >> 17;
        noiseState_ ^= noiseState_ << 5;
        const float noise = (static_cast<float>(noiseState_) / 2147483648.0f) - 1.0f;
        const float sibContrib = noise * sibEnvelope * sibilance_;

        // Apply makeup gain, then a tanh soft-limiter so the output can never
        // exceed ±1 no matter how high the makeup knob is driven (ear safety).
        const float y = (sum + sibContrib) * wetLevel_ * outputGain;
        output[i] = std::tanh(y);
    }
}

} // namespace guitar_dsp::audio
