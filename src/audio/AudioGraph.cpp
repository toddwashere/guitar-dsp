#include "AudioGraph.h"

#include <algorithm>
#include <cmath>

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
    drySpeechBuffer_.assign(static_cast<std::size_t>(blockSize), 0.0f);

    pitchCarrier_.prepare(sampleRate, blockSize);
    pitchCarrierBuffer_.assign(static_cast<std::size_t>(blockSize), 0.0f);

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
    pitchCarrier_.reset();
    std::fill(pitchCarrierBuffer_.begin(), pitchCarrierBuffer_.end(), 0.0f);
    carousel_.reset();
    std::fill(postInputBuffer_.begin(), postInputBuffer_.end(), 0.0f);
    std::fill(wetBuffer_.begin(), wetBuffer_.end(), 0.0f);
    std::fill(carrierBuffer_.begin(), carrierBuffer_.end(), 0.0f);
    std::fill(drySpeechBuffer_.begin(), drySpeechBuffer_.end(), 0.0f);
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
            const float clarity = clarity_.load(std::memory_order_relaxed);
            // Speak-clearly mode: snapshot the raw modulator BEFORE the vocoder
            // overwrites wetBuffer_, so we can blend the unvocoded speech back
            // in at the end. Skip the copy when clarity is 0 (the common case).
            if (clarity > 0.0f) {
                std::copy(wetBuffer_.begin(),
                          wetBuffer_.begin() + static_cast<std::ptrdiff_t>(numSamples),
                          drySpeechBuffer_.begin());
            }

            // Pitch-tracked carrier always runs so the UI readout is live whether
            // the toggle is on or off; its output is only routed when the toggle is on.
            const auto pitchState = pitchCarrier_.process(
                postInputBuffer_.data(), pitchCarrierBuffer_.data(), numSamples);
            detectedNoteMidi_.store(pitchState.midiNote, std::memory_order_relaxed);
            detectedCents_.store(pitchState.cents,       std::memory_order_relaxed);
            detectedHz_.store(pitchState.freqHz,         std::memory_order_relaxed);

            const bool pitchSinging = pitchSinging_.load(std::memory_order_relaxed);

            // Carrier = guitar (default), or (diagnostic) broadband white noise, or
            // (pitch-singing) guitar + carrierNoise * pitched_saw.
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
            } else if (pitchSinging) {
                const float cn = vocoder_.carrierNoise();
                for (std::size_t i = 0; i < numSamples; ++i) {
                    carrierBuffer_[i] = postInputBuffer_.data()[i]
                                      + cn * pitchCarrierBuffer_[i];
                }
                carrier = carrierBuffer_.data();
            }

            vocoder_.setSibilance(
                diagSibilanceOff_.load(std::memory_order_relaxed)
                    ? 0.0f
                    : vocoderSibilance_.load(std::memory_order_relaxed));

            // When pitch-singing is on, disable the vocoder's internal noise floor so
            // the pitched saw is the sole "floor" contribution.
            const float savedCarrierNoise = vocoder_.carrierNoise();
            if (pitchSinging) vocoder_.setCarrierNoise(0.0f);
            vocoder_.process(carrier, wetBuffer_.data(), wetBuffer_.data(), numSamples);
            if (pitchSinging) vocoder_.setCarrierNoise(savedCarrierNoise);

            // Crossfade vocoded wet -> raw modulator (boosted via the same
            // makeup gain + tanh limiter as the vocoded path, so they sit at
            // comparable levels and the dry path is ear-safe at any setting).
            if (clarity > 0.0f) {
                const float gain = vocoder_.outputGain();
                const float oneMinusC = 1.0f - clarity;
                for (std::size_t i = 0; i < numSamples; ++i) {
                    const float dry = std::tanh(drySpeechBuffer_[i] * gain);
                    wetBuffer_[i] = oneMinusC * wetBuffer_[i] + clarity * dry;
                }
            }
        }
    }

    // Mixer: dry = post-input guitar, wet = selected branch output.
    mixer_.process(postInputBuffer_.data(), wetBuffer_.data(), out, numSamples);
}

} // namespace guitar_dsp::audio
