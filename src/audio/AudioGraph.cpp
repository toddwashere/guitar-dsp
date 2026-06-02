#include "AudioGraph.h"

#include <algorithm>

namespace guitar_dsp::audio {

AudioGraph::AudioGraph() = default;

void AudioGraph::prepare(double sampleRate, int blockSize) {
    inputStage_.prepare(sampleRate, blockSize);
    mixer_.prepare(sampleRate, blockSize);
    ttsClipPlayer_.prepare(sampleRate, blockSize);
    noteSteppedPlayer_.prepare(sampleRate, blockSize);
    vocoder_.prepare(sampleRate, blockSize);
    vocoder_.setWetLevel(1.0f);
    vocoder_.setSibilance(0.5f);
    // Audible vocoder defaults: the raw carrier*envelope product is ~15 dB
    // too quiet, and a clean guitar note is too sparse to carry formants.
    // Makeup gain + a broadband carrier floor fix both (tune live in the UI).
    vocoder_.setOutputGain(5.0f);     // ~ +14 dB, tanh-limited downstream
    vocoder_.setCarrierNoise(0.30f);
    carousel_.prepare(sampleRate, blockSize);

    postInputBuffer_.assign(static_cast<std::size_t>(blockSize), 0.0f);
    wetBuffer_.assign(static_cast<std::size_t>(blockSize), 0.0f);
    carrierBuffer_.assign(static_cast<std::size_t>(blockSize), 0.0f);

    // Default mixer to fully dry until later phases install branches.
    mixer_.setDryWet(0.0f);
    mixer_.setMasterGainDb(0.0f);
    // Snap to desired initial state without ramping.
    mixer_.reset();
}

void AudioGraph::reset() {
    inputStage_.reset();
    mixer_.reset();
    ttsClipPlayer_.reset();
    noteSteppedPlayer_.reset();
    vocoder_.reset();
    carousel_.reset();
    std::fill(postInputBuffer_.begin(), postInputBuffer_.end(), 0.0f);
    std::fill(wetBuffer_.begin(), wetBuffer_.end(), 0.0f);
    std::fill(carrierBuffer_.begin(), carrierBuffer_.end(), 0.0f);
}

void AudioGraph::process(const float* in, float* out, std::size_t numSamples) {
    // Guard against caller passing a larger block than we prepared for.
    // Truncate rather than allocate. Tests should always call prepare()
    // with the expected max block size.
    numSamples = std::min(numSamples, postInputBuffer_.size());

    inputStage_.process(in, postInputBuffer_.data(), numSamples);

    if (wetSource_.load(std::memory_order_relaxed)
            == static_cast<int>(WetSource::Carousel)) {
        // Carousel branch: transform the guitar directly into the wet buffer.
        carousel_.process(postInputBuffer_.data(), wetBuffer_.data(), numSamples);
    } else {
        // Vocoder branch: modulator from the selected TTS player, carrier =
        // post-input guitar.
        if (modulatorSource_.load(std::memory_order_relaxed)
                == static_cast<int>(ModulatorSource::NoteStepped)) {
            // Onset source = clean guitar (postInputBuffer_); writes modulator.
            noteSteppedPlayer_.process(postInputBuffer_.data(),
                                       wetBuffer_.data(), numSamples);
        } else {
            ttsClipPlayer_.process(wetBuffer_.data(), numSamples);
        }

        if (diagBypassVocoder_.load(std::memory_order_relaxed)) {
            // Diagnostic: skip vocoding entirely. wetBuffer_ already holds the
            // raw modulator, so the operator auditions the TTS source directly.
        } else {
            // Carrier = guitar, or (diagnostic) broadband white noise so every
            // band has energy to modulate.
            const float* carrier = postInputBuffer_.data();
            if (diagNoiseCarrier_.load(std::memory_order_relaxed)) {
                for (std::size_t i = 0; i < numSamples; ++i) {
                    diagNoiseState_ ^= diagNoiseState_ << 13;
                    diagNoiseState_ ^= diagNoiseState_ >> 17;
                    diagNoiseState_ ^= diagNoiseState_ << 5;
                    carrierBuffer_[i] =
                        0.5f * ((static_cast<float>(diagNoiseState_) / 2147483648.0f) - 1.0f);
                }
                carrier = carrierBuffer_.data();
            }
            vocoder_.setSibilance(
                diagSibilanceOff_.load(std::memory_order_relaxed)
                    ? 0.0f
                    : vocoderSibilance_.load(std::memory_order_relaxed));
            vocoder_.process(carrier, wetBuffer_.data(), wetBuffer_.data(), numSamples);
        }
    }

    // Mixer: dry = post-input guitar, wet = selected branch output.
    mixer_.process(postInputBuffer_.data(), wetBuffer_.data(), out, numSamples);
}

} // namespace guitar_dsp::audio
