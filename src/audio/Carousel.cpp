#include "Carousel.h"

#include <algorithm>
#include <cstring>

namespace guitar_dsp::audio {

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

    // Stages added across Tasks 6-10. Skeleton: drive + trim only.
    for (std::size_t i = 0; i < numSamples; ++i) {
        float x = in[i] * driveGain_.getNextValue();
        x *= trimGain_.getNextValue();
        out[i] = x;
    }
}

} // namespace guitar_dsp::audio
