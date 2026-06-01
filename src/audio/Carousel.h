#pragma once

#include <atomic>
#include <cstddef>

#include <juce_dsp/juce_dsp.h>

#include "Crusher.h"
#include "CarouselMod.h"
#include "PitchShifter.h"
#include "Harmonizer.h"
#include "Comb.h"
#include "Formant.h"
#include "scenes/Scene.h"

namespace guitar_dsp::audio {

// Pedal-style mono effects chain for the instrument-carousel scenes.
// Fixed order: drive -> waveshaper -> crusher -> resonant filter
// (static/envelope/LFO) -> chorus -> reverb -> output trim. Each stage
// bypasses when its config is inert. Built from juce::dsp modules plus
// the bespoke Crusher / EnvelopeFollower / Lfo.
//
// Threading mirrors TTSClipPlayer: setConfig() (message thread) stashes a
// pending config + flag; process() (audio thread) picks it up at block
// start. process() never allocates.
class Carousel {
public:
    Carousel();

    void prepare(double sampleRate, int blockSize);
    void reset();

    // Message-thread API.
    void setConfig(const scenes::CarouselConfig& cfg);

    // Audio-thread API. in and out may alias.
    void process(const float* in, float* out, std::size_t numSamples) noexcept;

private:
    void applyConfig(const scenes::CarouselConfig& cfg) noexcept;  // audio thread

    double sampleRate_ = 48000.0;

    std::atomic<bool>       newConfigFlag_ {false};
    scenes::CarouselConfig  pendingConfig_;   // message thread
    scenes::CarouselConfig  active_;          // audio thread

    juce::dsp::StateVariableTPTFilter<float> filter_;
    juce::dsp::Chorus<float>                 chorus_;
    juce::dsp::Reverb                        reverb_;
    Crusher                                  crusher_;
    EnvelopeFollower                         env_;
    Lfo                                      lfo_;

    PitchShifter pitch_;
    Harmonizer   harm_;
    Comb         comb_;
    Formant      formant_;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> driveGain_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> trimGain_;
};

} // namespace guitar_dsp::audio
