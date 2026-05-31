#pragma once

#include <cstddef>

namespace guitar_dsp::audio {

// Vocoder interface. Two audio streams in, one out:
//   carrier  — the harmonic source (live guitar)
//   modulator — the formant/envelope source (TTS audio)
// Output is the carrier shaped by the modulator's per-band envelopes —
// the signature "talkbox" or "vocoder" sound.
//
// All three buffers are mono, `numSamples` long. In-place aliasing
// (output == carrier) is permitted.
class IVocoder {
public:
    virtual ~IVocoder() = default;

    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void reset() = 0;
    virtual void process(const float* carrier,
                         const float* modulator,
                         float* output,
                         std::size_t numSamples) = 0;

    // Wet level 0..1 — caller-side gain for the vocoder's contribution
    // to the wet bus. The mixer's dryWet is separate.
    virtual void setWetLevel(float v) = 0;

    // Sibilance noise injection 0..1 — adds noise where the modulator
    // has unvoiced high-frequency energy. 0 = none, 1 = full sibilance.
    virtual void setSibilance(float v) = 0;
};

} // namespace guitar_dsp::audio
