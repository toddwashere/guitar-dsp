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
    const int maxGrain = static_cast<int>(sampleRate * 0.1);  // 100 ms
    pitch_.prepare(sampleRate, maxGrain);
    harm_.prepare(sampleRate, maxGrain);
    comb_.prepare(sampleRate, maxGrain);
    formant_.prepare(sampleRate);
    formantMod_.prepare(sampleRate);
    formantPosBuffer_.assign(static_cast<std::size_t>(blockSize), 0.0f);
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
    pitch_.reset();
    harm_.reset();
    comb_.reset();
    formant_.reset();
    formantMod_.reset();
    std::fill(formantPosBuffer_.begin(), formantPosBuffer_.end(), 0.0f);
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
    pitch_.setGrainSamples(static_cast<int>(cfg.pitchGrainMs * 0.001 * sampleRate_));
    pitch_.setRatio(std::pow(2.0f, cfg.pitchSemitones / 12.0f));
    harm_.setVoices(cfg.harmSemitones, cfg.harmDetuneCents,
                    cfg.harmVoiceCount, cfg.pitchGrainMs);
    harm_.setMix(juce::jlimit(0.0f, 1.0f, cfg.harmMix));
    comb_.setFreqHz(cfg.combFreqHz);
    comb_.setFeedback(cfg.combFeedback);
    comb_.setMix(juce::jlimit(0.0f, 1.0f, cfg.combMix));
    formant_.setVowel(cfg.formantVowel);
    formant_.setAmount(juce::jlimit(0.0f, 1.0f, cfg.formantAmount));

    {
        using FM = FormantModulator::Mode;
        switch (cfg.formantMode) {
            case scenes::CarouselConfig::FormantMode::Static:
                formantMod_.setMode(FM::Static);
                formantMod_.setStaticPosition(
                    cfg.formantVowel == scenes::CarouselConfig::Vowel::Ah ? 0.5f :
                    cfg.formantVowel == scenes::CarouselConfig::Vowel::Oh ? 0.75f :
                    /* Ee or None */                                         0.0f);
                break;
            case scenes::CarouselConfig::FormantMode::Lfo:
                formantMod_.setMode(FM::Lfo);
                formantMod_.setBreakpoints(cfg.formantBreakpoints);
                formantMod_.setLfoRateHz(cfg.formantLfoHz);
                break;
            case scenes::CarouselConfig::FormantMode::Envelope:
                formantMod_.setMode(FM::Envelope);
                formantMod_.setBreakpoints(cfg.formantBreakpoints);
                formantMod_.setEnvelopeAttackMs(cfg.formantEnvAttackMs);
                break;
        }
    }

    driveGain_.setTargetValue(juce::Decibels::decibelsToGain(cfg.drive));
    trimGain_.setTargetValue(juce::Decibels::decibelsToGain(cfg.outputTrimDb));

    using FM = scenes::CarouselConfig::FilterMode;
    switch (cfg.filterMode) {
        case FM::LowPass:  filter_.setType(juce::dsp::StateVariableTPTFilterType::lowpass); break;
        case FM::BandPass: filter_.setType(juce::dsp::StateVariableTPTFilterType::bandpass); break;
        case FM::HighPass: filter_.setType(juce::dsp::StateVariableTPTFilterType::highpass); break;
        case FM::Off: default: break;
    }
    filter_.setResonance(juce::jlimit(0.05f, 0.95f, cfg.filterResonance));
    filter_.setCutoffFrequency(juce::jlimit(20.0f, 18000.0f, cfg.filterCutoffHz));
    lfo_.setRateHz(cfg.filterLfoHz);

    chorus_.setRate(cfg.chorusRateHz);
    chorus_.setDepth(juce::jlimit(0.0f, 1.0f, cfg.chorusDepth));
    chorus_.setMix(juce::jlimit(0.0f, 1.0f, cfg.chorusMix));
    chorus_.setCentreDelay(7.0f);
    chorus_.setFeedback(0.0f);

    juce::dsp::Reverb::Parameters rp;
    rp.roomSize = juce::jlimit(0.0f, 1.0f, cfg.reverbRoomSize);
    rp.wetLevel = juce::jlimit(0.0f, 1.0f, cfg.reverbWet);
    rp.dryLevel = 1.0f - rp.wetLevel * 0.5f;
    rp.width = 1.0f;
    reverb_.setParameters(rp);
}

void Carousel::process(const float* in, float* out, std::size_t numSamples) noexcept {
    if (newConfigFlag_.exchange(false, std::memory_order_acquire))
        applyConfig(pendingConfig_);

    if (!active_.enabled) {
        if (in != out) std::memcpy(out, in, numSamples * sizeof(float));
        return;
    }

    const bool filterOn = active_.filterMode != scenes::CarouselConfig::FilterMode::Off;

    const bool formantActive =
        (active_.formantMode != scenes::CarouselConfig::FormantMode::Static)
        || (active_.formantVowel != scenes::CarouselConfig::Vowel::None);

    if (formantActive
            && active_.formantMode != scenes::CarouselConfig::FormantMode::Static) {
        formantMod_.process(in, formantPosBuffer_.data(), numSamples);
    }

    using FMod = scenes::CarouselConfig::FilterMod;
    for (std::size_t i = 0; i < numSamples; ++i) {
        float v = in[i];
        if (active_.harmVoiceCount > 0) {
            v = harm_.processSample(v);
        } else if (active_.pitchSemitones != 0.0f) {
            const float shifted = pitch_.processSample(v);
            v = (1.0f - active_.pitchMix) * v + active_.pitchMix * shifted;
        }
        float x = v * driveGain_.getNextValue() * active_.shaperAmount;
        x = shape(x, active_.shaper);
        x = crusher_.processSample(x);
        if (active_.combFreqHz > 0.0f) x = comb_.processSample(x);

        if (filterOn) {
            if (active_.filterMod == FMod::Envelope) {
                const float e = env_.processSample(x);
                filter_.setCutoffFrequency(juce::jlimit(20.0f, 18000.0f,
                    active_.filterCutoffHz + active_.filterEnvAmount * e));
            } else if (active_.filterMod == FMod::Lfo) {
                const float l = lfo_.processSample();
                filter_.setCutoffFrequency(juce::jlimit(20.0f, 18000.0f,
                    active_.filterCutoffHz * std::pow(2.0f, l)));
            }
            x = filter_.processSample(0, x);
        }

        if (formantActive) {
            if (active_.formantMode != scenes::CarouselConfig::FormantMode::Static)
                formant_.setPosition(formantPosBuffer_[i]);
            x = formant_.processSample(x);
        }

        x *= trimGain_.getNextValue();
        out[i] = x;
    }

    if (active_.chorusMix > 0.0f && active_.chorusRateHz > 0.0f) {
        juce::dsp::AudioBlock<float> block(&out, 1, numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        chorus_.process(ctx);
    }
    if (active_.reverbWet > 0.0f) {
        juce::dsp::AudioBlock<float> block(&out, 1, numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        reverb_.process(ctx);
    }

    // Brick-wall safety limiter. A resonant filter + reverb tail can boost
    // past full scale; this is a live-PA signal, so guarantee the carousel
    // never emits beyond [-1, 1] regardless of preset or input. Also scrubs
    // any non-finite sample (NaN/Inf) to silence so a bad value can never
    // reach the audio device.
    for (std::size_t i = 0; i < numSamples; ++i) {
        float x = out[i];
        if (!std::isfinite(x)) x = 0.0f;
        out[i] = std::clamp(x, -1.0f, 1.0f);
    }
}

} // namespace guitar_dsp::audio
