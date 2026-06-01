#pragma once

#include <juce_dsp/juce_dsp.h>

#include "scenes/Scene.h"

namespace guitar_dsp::audio {

// Fixed vowel-emphasis filter: 3 parallel resonant bandpass peaks at a
// vowel's formant frequencies, summed and blended with the input by `amount`.
// Vowel::None bypasses. NOT pitch-independent formant shifting.
class Formant {
public:
    void prepare(double sampleRate);
    void reset();

    void setVowel(scenes::CarouselConfig::Vowel v) noexcept;
    void setAmount(float a) noexcept { amount_ = a; }

    float processSample(float x) noexcept;

private:
    static constexpr int kPeaks = 3;
    juce::dsp::StateVariableTPTFilter<float> peaks_[kPeaks];
    scenes::CarouselConfig::Vowel vowel_ = scenes::CarouselConfig::Vowel::None;
    float amount_ = 0.0f;
};

} // namespace guitar_dsp::audio
