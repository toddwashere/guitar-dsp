#include "Harmonizer.h"

#include <cmath>

namespace guitar_dsp::audio {

void Harmonizer::prepare(double sampleRate, int maxGrainSamples) {
    sampleRate_ = sampleRate;
    for (auto& v : voices_) v.prepare(sampleRate, maxGrainSamples);
    reset();
}

void Harmonizer::reset() {
    for (auto& v : voices_) v.reset();
}

void Harmonizer::setVoices(const int* semitones, const int* detuneCents,
                           int count, float grainMs) noexcept {
    count_ = count < 0 ? 0 : (count > kMaxVoices ? kMaxVoices : count);
    voiceGain_ = count_ > 0 ? 1.0f / static_cast<float>(count_) : 0.0f;
    const int grain = static_cast<int>(grainMs * 0.001 * sampleRate_);
    for (int i = 0; i < count_; ++i) {
        const float semis = static_cast<float>(semitones[i])
                          + static_cast<float>(detuneCents[i]) / 100.0f;
        voices_[i].setGrainSamples(grain);
        voices_[i].setRatio(std::pow(2.0f, semis / 12.0f));
    }
}

float Harmonizer::processSample(float x) noexcept {
    if (count_ == 0) return x;
    float wet = 0.0f;
    for (int i = 0; i < count_; ++i) wet += voices_[i].processSample(x);
    wet *= voiceGain_;
    return (1.0f - mix_) * x + mix_ * wet;
}

} // namespace guitar_dsp::audio
