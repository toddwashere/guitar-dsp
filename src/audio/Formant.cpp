#include "Formant.h"

namespace guitar_dsp::audio {

namespace {
struct VowelPeaks { float f[3]; };
VowelPeaks peaksFor(scenes::CarouselConfig::Vowel v) {
    using V = scenes::CarouselConfig::Vowel;
    switch (v) {
        case V::Ah: return {{ 700.0f, 1220.0f, 2600.0f }};
        case V::Oh: return {{ 500.0f, 1000.0f, 2400.0f }};
        case V::Ee: return {{ 270.0f, 2300.0f, 3000.0f }};
        case V::None:
        default:    return {{ 0.0f, 0.0f, 0.0f }};
    }
}
} // namespace

void Formant::prepare(double sampleRate) {
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = 1;
    spec.numChannels = 1;
    for (auto& p : peaks_) {
        p.prepare(spec);
        p.setType(juce::dsp::StateVariableTPTFilterType::bandpass);
        p.setResonance(0.7f);
    }
    reset();
}

void Formant::reset() {
    for (auto& p : peaks_) p.reset();
}

void Formant::setVowel(scenes::CarouselConfig::Vowel v) noexcept {
    vowel_ = v;
    const auto vp = peaksFor(v);
    for (int i = 0; i < kPeaks; ++i)
        if (vp.f[i] > 0.0f) peaks_[i].setCutoffFrequency(vp.f[i]);
}

float Formant::processSample(float x) noexcept {
    if (vowel_ == scenes::CarouselConfig::Vowel::None) return x;
    float sum = 0.0f;
    for (auto& p : peaks_) sum += p.processSample(0, x);
    return (1.0f - amount_) * x + amount_ * sum;
}

} // namespace guitar_dsp::audio
