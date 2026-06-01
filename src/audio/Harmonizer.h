#pragma once

#include "PitchShifter.h"

namespace guitar_dsp::audio {

// Up to 4 parallel PitchShifter voices at fixed semitone+cents intervals,
// summed (normalized by voice count) and blended with the dry input.
// mix: 0 = pure dry, 1 = pure harmonized sum.
class Harmonizer {
public:
    static constexpr int kMaxVoices = 4;

    void prepare(double sampleRate, int maxGrainSamples);
    void reset();

    // semitones[]/detuneCents[] may be nullptr when count==0.
    void setVoices(const int* semitones, const int* detuneCents, int count,
                   float grainMs) noexcept;
    void setMix(float mix) noexcept { mix_ = mix; }

    float processSample(float x) noexcept;

private:
    PitchShifter voices_[kMaxVoices];
    int   count_     = 0;
    float voiceGain_ = 0.0f;
    float mix_       = 0.0f;
    double sampleRate_ = 48000.0;
};

} // namespace guitar_dsp::audio
