#include "AudioGraph.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

AudioGraph::AudioGraph() = default;

void AudioGraph::setOnsetSensitivityDb(float dB) noexcept {
    onsetSensitivityDb_.store(dB, std::memory_order_relaxed);
    // Broadcast to every onset-driven player. Each forwards to its embedded
    // OnsetDetector and recomputes attack + rearm thresholds.
    clipBankPlayer_.setOnsetSensitivityDb(dB);
    noteSteppedPlayer_.setOnsetSensitivityDb(dB);
    phonemeSteppedPlayer_.setOnsetSensitivityDb(dB);
    sungDirectPath_.setOnsetSensitivityDb(dB);
    carousel_.setOnsetSensitivityDb(dB);
}

void AudioGraph::prepare(double sampleRate, int blockSize) {
    inputStage_.prepare(sampleRate, blockSize);
    mixer_.prepare(sampleRate, blockSize);
    ttsClipPlayer_.prepare(sampleRate, blockSize);
    noteSteppedPlayer_.prepare(sampleRate, blockSize);
    phonemeSteppedPlayer_.prepare(sampleRate, blockSize);
    clipBankPlayer_.prepare(sampleRate, blockSize);
    micShaper_.prepare(sampleRate);
    micScratchBuffer_.assign(static_cast<std::size_t>(blockSize), 0.0f);
    micScratchValidSamples_ = 0;
    vocoder_.prepare(sampleRate, blockSize);
    vocoder_.setWetLevel(1.0f);
    vocoder_.setSibilance(0.3f);
    // Audible vocoder defaults: the raw carrier*envelope product is ~15 dB
    // too quiet, and a clean guitar note is too sparse to carry formants.
    // Makeup gain + a broadband carrier floor fix both (tune live in the UI).
    vocoder_.setOutputGain(4.0f);     // ~ +12 dB, tanh-limited downstream
    vocoder_.setCarrierNoise(0.30f);
    carousel_.prepare(sampleRate, blockSize);
    sungDirectPath_.prepare(sampleRate, blockSize);
    rave_.prepare(sampleRate, blockSize);
    limiter_.prepare(sampleRate);

    // Re-apply the cached onset sensitivity so a sample-rate change /
    // re-prepare doesn't snap detectors back to the OnsetDetector default.
    setOnsetSensitivityDb(onsetSensitivityDb_.load(std::memory_order_relaxed));

    postInputBuffer_.assign(static_cast<std::size_t>(blockSize), 0.0f);
    wetBuffer_.assign(static_cast<std::size_t>(blockSize), 0.0f);
    carrierBuffer_.assign(static_cast<std::size_t>(blockSize), 0.0f);
    drySpeechBuffer_.assign(static_cast<std::size_t>(blockSize), 0.0f);

    pitchCarrier_.prepare(sampleRate, blockSize);
    pitchCarrierBuffer_.assign(static_cast<std::size_t>(blockSize), 0.0f);

    // Wet-path LPF coefficient for the current sample rate.
    wetLpfAlpha_ = 1.0f - std::exp(
        -2.0f * 3.14159265358979323846f * kWetLpfHz / static_cast<float>(sampleRate));
    wetLpfState_ = 0.0f;

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
    phonemeSteppedPlayer_.reset();
    clipBankPlayer_.reset();
    micShaper_.reset();
    std::fill(micScratchBuffer_.begin(), micScratchBuffer_.end(), 0.0f);
    micScratchValidSamples_ = 0;
    vocoder_.reset();
    pitchCarrier_.reset();
    std::fill(pitchCarrierBuffer_.begin(), pitchCarrierBuffer_.end(), 0.0f);
    wetLpfState_ = 0.0f;
    carousel_.reset();
    sungDirectPath_.reset();
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

    // Pitch tracking ALWAYS runs, regardless of wet source. The UI
    // readout, ClipBankPlayer anchor selection, and SungDirectPath ratio
    // computation all read from these atomics — previously the carrier
    // only ran inside the Vocoder branch, so scene 12 (SungDirect) saw
    // stale detectedHz and the shifter never tracked the guitar.
    {
        const auto pitchState = pitchCarrier_.process(
            postInputBuffer_.data(), pitchCarrierBuffer_.data(), numSamples);
        detectedNoteMidi_.store(pitchState.midiNote, std::memory_order_relaxed);
        detectedCents_.store(pitchState.cents,       std::memory_order_relaxed);
        detectedHz_.store(pitchState.freqHz,         std::memory_order_relaxed);
        clipBankPlayer_.setDetectedPitchHz(pitchState.freqHz);
    }

    const auto src = static_cast<WetSource>(wetSource_.load(std::memory_order_relaxed));
    if (src == WetSource::SungDirect) {
        // SungDirect branch: pitch-shift and formant-preserve the sung vowel
        // grains directly to the wet buffer, driven by the detected guitar pitch.
        sungDirectPath_.process(postInputBuffer_.data(),
                                detectedHz_.load(std::memory_order_relaxed),
                                wetBuffer_.data(), numSamples);
    } else if (src == WetSource::Rave) {
        // RAVE branch: route guitar through the neural vocoder. The RaveSynthesizer
        // writes to wetBuffer_; the dry/wet blend is applied via the Mixer's dryWet.
        const float dw = raveDryWet_.load(std::memory_order_relaxed);
        rave_.processBlock(postInputBuffer_.data(), wetBuffer_.data(), numSamples);
        mixer_.setDryWet(dw);
    } else if (src == WetSource::Carousel) {
        // Carousel branch: transform the guitar directly into the wet buffer.
        carousel_.process(postInputBuffer_.data(), wetBuffer_.data(), numSamples);
    } else {
        // Vocoder branch: modulator from the selected TTS player, carrier =
        // post-input guitar.
        const int modSrc = modulatorSource_.load(std::memory_order_relaxed);
        if (modSrc == static_cast<int>(ModulatorSource::NoteStepped)) {
            // Onset source = clean guitar (postInputBuffer_); writes modulator.
            // Dispatch to v1 or v2 player based on the active-speech-player selector.
            if (activeSpeechPlayer_.load(std::memory_order_relaxed)
                    == static_cast<int>(ActiveSpeechPlayer::PhonemeStepped)) {
                phonemeSteppedPlayer_.process(postInputBuffer_.data(),
                                              wetBuffer_.data(), numSamples);
            } else {
                noteSteppedPlayer_.process(postInputBuffer_.data(),
                                           wetBuffer_.data(), numSamples);
            }
        } else if (modSrc == static_cast<int>(ModulatorSource::ClipBank)) {
            // Same shape as NoteStepped — onset source is the clean guitar.
            clipBankPlayer_.process(postInputBuffer_.data(),
                                    wetBuffer_.data(), numSamples);
        } else if (modSrc == static_cast<int>(ModulatorSource::Mic)) {
            // micScratchBuffer_ was populated by setMicBlock() earlier this
            // block. Shape (gate + makeup gain) and write to wetBuffer_ as
            // the vocoder modulator.
            micShaper_.process(micScratchBuffer_.data(),
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

            // Pitch tracking already ran at the top of process(); the saw
            // carrier buffer is populated and atomics published. Read here.
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

            // Wet-path 1-pole LPF: tames any high-band scratch that survived
            // the per-saw LPF + the sibilance generator + the high vocoder bands.
            // Applied BEFORE the clarity blend so the dry-TTS path stays
            // unfiltered (intelligibility wins over scratch reduction there).
            for (std::size_t i = 0; i < numSamples; ++i) {
                wetLpfState_ += wetLpfAlpha_ * (wetBuffer_[i] - wetLpfState_);
                wetBuffer_[i] = wetLpfState_;
            }

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

    // Master limiter — last thing before output, catches any scene's peaks
    // (loud vocoder bursts, hot sung-vowel grains, etc.) and keeps them at
    // or below the configured threshold. No-op when disabled.
    limiter_.process(out, static_cast<int>(numSamples));
}

void AudioGraph::setMicBlock(const float* mono, std::size_t numSamples) noexcept {
    const std::size_t n = std::min(numSamples, micScratchBuffer_.size());
    if (mono != nullptr) {
        std::copy(mono, mono + n, micScratchBuffer_.begin());
        if (n < micScratchBuffer_.size())
            std::fill(micScratchBuffer_.begin() + static_cast<std::ptrdiff_t>(n),
                      micScratchBuffer_.end(), 0.0f);
        micScratchValidSamples_ = n;

        float peak = 0.0f;
        for (std::size_t i = 0; i < n; ++i)
            peak = std::max(peak, std::fabs(mono[i]));
        micPeak_.store(peak, std::memory_order_relaxed);
    } else {
        std::fill(micScratchBuffer_.begin(), micScratchBuffer_.end(), 0.0f);
        micScratchValidSamples_ = 0;

        micPeak_.store(0.0f, std::memory_order_relaxed);
    }
}

void AudioGraph::loadRaveModel(const std::string& path) {
    rave_.loadModel(path);
}

AudioGraph::RaveStatusForUI AudioGraph::raveStatusForUI() const noexcept {
    switch (rave_.status()) {
        case RaveBranchStatus::Loading:     return RaveStatusForUI::Loading;
        case RaveBranchStatus::Loaded:      return RaveStatusForUI::Loaded;
        case RaveBranchStatus::Unavailable: return RaveStatusForUI::Unavailable;
        case RaveBranchStatus::Stalled:     return RaveStatusForUI::Stalled;
    }
    return RaveStatusForUI::Unavailable;
}

} // namespace guitar_dsp::audio
