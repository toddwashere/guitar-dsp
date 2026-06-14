#include "Formant.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

void Formant::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
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
    lastComputedPos_ = -1.0f;
}

void Formant::setVowel(scenes::CarouselConfig::Vowel v) noexcept {
    using V = scenes::CarouselConfig::Vowel;
    if (v == V::None) {
        bypass_ = true;
        return;
    }
    bypass_ = false;
    float p = 0.0f;
    switch (v) {
        case V::Ee: p = 0.0f;  break;
        case V::Ah: p = 0.5f;  break;
        case V::Oh: p = 0.75f; break;
        default:    p = 0.0f;  break;
    }
    setPosition(p);
}

void Formant::setPosition(float p) noexcept {
    bypass_ = false;
    float wrapped = p - std::floor(p);
    position_ = wrapped;
}

void Formant::recomputeCoefs(float position) noexcept {
    const float scaled = position * 4.0f;
    const int   lo     = static_cast<int>(scaled);
    const int   hi     = (lo + 1) % static_cast<int>(kAnchors.size());
    const float frac   = scaled - static_cast<float>(lo);

    const auto& A = kAnchors[static_cast<std::size_t>(lo)];
    const auto& B = kAnchors[static_cast<std::size_t>(hi)];

    const float f1 = (1.0f - frac) * A.f1 + frac * B.f1;
    const float f2 = (1.0f - frac) * A.f2 + frac * B.f2;
    const float f3 = (1.0f - frac) * A.f3 + frac * B.f3;

    peaks_[0].setCutoffFrequency(f1);
    peaks_[1].setCutoffFrequency(f2);
    peaks_[2].setCutoffFrequency(f3);
    lastComputedPos_ = position;
}

float Formant::processSample(float x) noexcept {
    if (bypass_) return x;
    if (std::fabs(position_ - lastComputedPos_) > 0.005f) {
        recomputeCoefs(position_);
    }
    float sum = 0.0f;
    for (auto& p : peaks_) sum += p.processSample(0, x);
    return (1.0f - amount_) * x + amount_ * sum;
}

} // namespace guitar_dsp::audio
