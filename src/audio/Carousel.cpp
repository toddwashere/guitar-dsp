#include "Carousel.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace guitar_dsp::audio {

namespace {
inline float shape(float x, scenes::CarouselConfig::Shaper s) noexcept {
    using S = scenes::CarouselConfig::Shaper;
    switch (s) {
        case S::Tanh:     return std::tanh(x);
        case S::HardClip: return std::clamp(x, -1.0f, 1.0f);
        case S::Foldback:
            while (x > 1.0f || x < -1.0f) {
                if (x > 1.0f)  x = 2.0f - x;
                if (x < -1.0f) x = -2.0f - x;
            }
            return x;
        case S::None:
        default:          return x;
    }
}
} // namespace

Carousel::Carousel() = default;

void Carousel::prepare(double sampleRate, int blockSize) {
    sampleRate_ = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(blockSize);
    spec.numChannels = 1;

    filter_.prepare(spec);
    chorus_.prepare(spec);
    reverb_.prepare(spec);
    crusher_.reset();
    env_.prepare(sampleRate);
    lfo_.prepare(sampleRate);

    driveGain_.reset(sampleRate, 0.02);
    trimGain_.reset(sampleRate, 0.02);
    driveGain_.setCurrentAndTargetValue(1.0f);
    trimGain_.setCurrentAndTargetValue(1.0f);

    reset();
}

void Carousel::reset() {
    filter_.reset();
    chorus_.reset();
    reverb_.reset();
    crusher_.reset();
    env_.reset();
    lfo_.reset();
}

void Carousel::setConfig(const scenes::CarouselConfig& cfg) {
    pendingConfig_ = cfg;
    newConfigFlag_.store(true, std::memory_order_release);
}

void Carousel::applyConfig(const scenes::CarouselConfig& cfg) noexcept {
    active_ = cfg;
    crusher_.setBits(cfg.crusherBits);
    crusher_.setDownsample(cfg.crusherDownsample);
    driveGain_.setTargetValue(juce::Decibels::decibelsToGain(cfg.drive));
    trimGain_.setTargetValue(juce::Decibels::decibelsToGain(cfg.outputTrimDb));
}

void Carousel::process(const float* in, float* out, std::size_t numSamples) noexcept {
    if (newConfigFlag_.exchange(false, std::memory_order_acquire))
        applyConfig(pendingConfig_);

    if (!active_.enabled) {
        if (in != out) std::memcpy(out, in, numSamples * sizeof(float));
        return;
    }

    for (std::size_t i = 0; i < numSamples; ++i) {
        float x = in[i] * driveGain_.getNextValue() * active_.shaperAmount;
        x = shape(x, active_.shaper);
        x = crusher_.processSample(x);
        x *= trimGain_.getNextValue();
        out[i] = x;
    }
}

} // namespace guitar_dsp::audio
