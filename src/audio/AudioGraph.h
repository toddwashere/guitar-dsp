#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Carousel.h"
#include "ChannelVocoder.h"
#include "InputStage.h"
#include "MicShaper.h"
#include "Mixer.h"
#include "ClipBankPlayer.h"
#include "NoteSteppedTTSPlayer.h"
#include "PitchTrackedCarrier.h"
#include "TTSClipPlayer.h"

namespace guitar_dsp::audio {

// The top-level audio processing graph.
//   InputStage -> dry path -> Mixer -> Output
//                          -> TTSClipPlayer -> ChannelVocoder -> wet path -> Mixer
// All buffers used internally are sized at prepare() time; processing is
// allocation-free.
class AudioGraph {
public:
    AudioGraph();

    void prepare(double sampleRate, int blockSize);
    void reset();

    void process(const float* in, float* out, std::size_t numSamples);

    InputStage& input() { return inputStage_; }

    // Noise-gate threshold convenience (forwards to InputStage).
    void  setNoiseGateThresholdDb(float dB) noexcept { inputStage_.setNoiseGateThreshold(dB); }
    float noiseGateThresholdDb()      const noexcept { return inputStage_.noiseGateThresholdDb(); }
    Mixer& mixer() { return mixer_; }
    TTSClipPlayer& ttsClipPlayer() { return ttsClipPlayer_; }
    NoteSteppedTTSPlayer& noteSteppedPlayer() { return noteSteppedPlayer_; }
    const NoteSteppedTTSPlayer& noteSteppedPlayer() const { return noteSteppedPlayer_; }
    ClipBankPlayer& clipBankPlayer() { return clipBankPlayer_; }
    const ClipBankPlayer& clipBankPlayer() const { return clipBankPlayer_; }
    ChannelVocoder& vocoder() { return vocoder_; }
    Carousel& carousel() { return carousel_; }

    enum class WetSource { Vocoder, Carousel };
    // Message-thread: choose which branch feeds the Mixer's wet input.
    void setWetSource(WetSource s) noexcept {
        wetSource_.store(static_cast<int>(s), std::memory_order_relaxed);
    }

    enum class ModulatorSource { Linear, NoteStepped, ClipBank, Mic };
    // Message-thread: choose which TTS player feeds the vocoder modulator.
    void setModulatorSource(ModulatorSource s) noexcept {
        modulatorSource_.store(static_cast<int>(s), std::memory_order_relaxed);
    }

    // Audio thread (called once per block from PluginProcessor::processBlock
    // BEFORE AudioGraph::process). Stores the mic samples for use by the Mic
    // modulator branch in process(). Truncates if numSamples > prepared block.
    // Pass nullptr to clear (and zero the level meter). RT-safe.
    void setMicBlock(const float* mono, std::size_t numSamples) noexcept;

    // --- Diagnostic isolation toggles -----------------------------------
    // For troubleshooting vocoder intelligibility by ear. Message-thread
    // setters, audio-thread reads; all allocation/lock-free.
    //   BypassVocoder: route the raw modulator (TTS) straight to the wet
    //                  bus, skipping the vocoder — confirms the source.
    //   NoiseCarrier:  replace the guitar carrier with broadband white
    //                  noise — confirms whether the sparse guitar carrier
    //                  is what's starving the formants.
    //   SibilanceOff:  force the vocoder's sibilance/noise path to zero —
    //                  confirms whether that path is the "maraca" sound.
    void setDiagBypassVocoder(bool on) noexcept { diagBypassVocoder_.store(on, std::memory_order_relaxed); }
    void setDiagNoiseCarrier(bool on)  noexcept { diagNoiseCarrier_.store(on,  std::memory_order_relaxed); }
    void setDiagSibilanceOff(bool on)  noexcept { diagSibilanceOff_.store(on,  std::memory_order_relaxed); }
    bool diagBypassVocoder() const noexcept { return diagBypassVocoder_.load(std::memory_order_relaxed); }
    bool diagNoiseCarrier()  const noexcept { return diagNoiseCarrier_.load(std::memory_order_relaxed); }
    bool diagSibilanceOff()  const noexcept { return diagSibilanceOff_.load(std::memory_order_relaxed); }

    // --- Live vocoder controls (message thread) -------------------------
    // Makeup gain (linear) + broadband carrier floor live on the vocoder;
    // sibilance base lives here because the SibilanceOff diagnostic overrides
    // it per block. These drive the VocoderPanel sliders.
    void setVocoderMakeup(float linear) noexcept { vocoder_.setOutputGain(linear); }
    void setVocoderCarrierNoise(float mix) noexcept { vocoder_.setCarrierNoise(mix); }
    void setVocoderSibilance(float v) noexcept {
        vocoderSibilance_.store(v, std::memory_order_relaxed);
    }
    float vocoderMakeup() const noexcept { return vocoder_.outputGain(); }
    float vocoderCarrierNoise() const noexcept { return vocoder_.carrierNoise(); }
    float vocoderSibilance() const noexcept { return vocoderSibilance_.load(std::memory_order_relaxed); }

    // "Speak clearly" mode — per-scene crossfade 0..1 between the vocoded wet
    // signal (0, current behavior) and the raw TTS modulator (1, the unvocoded
    // speech). At 1 the guitar still mixes in via the Mixer's dryWet — so you
    // hear "dry TTS over the wet guitar." Foundation for the conversational-AI
    // direction, where the AI voice needs to be intelligible.
    void setClarity(float c) noexcept {
        clarity_.store(std::clamp(c, 0.0f, 1.0f), std::memory_order_relaxed);
    }
    float clarity() const noexcept { return clarity_.load(std::memory_order_relaxed); }

    // Pitch-tracked singing carrier. When ON, AudioGraph composes the carrier
    // as `guitar + carrierNoise * pitched_saw` (with ChannelVocoder's internal
    // noise floor disabled), so the spoken voice sings the guitar's note.
    // Default OFF — preserves today's behavior exactly.
    void setPitchSinging(bool on) noexcept {
        pitchSinging_.store(on, std::memory_order_relaxed);
    }
    bool pitchSinging() const noexcept {
        return pitchSinging_.load(std::memory_order_relaxed);
    }

    // Forwarders to PitchTrackedCarrier's Sing-mode toggle.
    void setSinging(bool on)  noexcept { pitchCarrier_.setSinging(on); }
    bool singing()      const noexcept { return pitchCarrier_.singing(); }

    // Word-sync mode for note-triggered TTS scenes.
    void setWordSyncMode(WordSyncMode m) noexcept {
        noteSteppedPlayer_.setMode(m);
    }
    WordSyncMode wordSyncMode() const noexcept {
        return noteSteppedPlayer_.mode();
    }

    // Rewind the note-triggered TTS sequence to the start (next onset
    // plays segment 0). Message thread; RT-safe via pending flag.
    void rewindSpoken() noexcept { noteSteppedPlayer_.rewind(); }

    // Rewind the clip-bank cursor (Scene 2 / Phase A). Independent of the
    // note-stepped rewind. Message thread; RT-safe via the player's pending flag.
    void rewindClipBank() noexcept { clipBankPlayer_.rewind(); }

    // Latest detected pitch published from the audio thread for the UI. -1
    // midiNote / 0 Hz when unvoiced AND hold-decay has expired.
    int   detectedNoteMidi() const noexcept { return detectedNoteMidi_.load(std::memory_order_relaxed); }
    float detectedCents()    const noexcept { return detectedCents_.load(std::memory_order_relaxed); }
    float detectedHz()       const noexcept { return detectedHz_.load(std::memory_order_relaxed); }

private:
    InputStage inputStage_;
    Mixer mixer_;
    TTSClipPlayer ttsClipPlayer_;
    NoteSteppedTTSPlayer noteSteppedPlayer_;
    ClipBankPlayer clipBankPlayer_;
    ChannelVocoder vocoder_;
    Carousel carousel_;
    MicShaper micShaper_;

    std::atomic<int> wetSource_ {static_cast<int>(WetSource::Vocoder)};
    std::atomic<int> modulatorSource_ {static_cast<int>(ModulatorSource::Linear)};

    std::atomic<bool> diagBypassVocoder_ {false};
    std::atomic<bool> diagNoiseCarrier_  {false};
    std::atomic<bool> diagSibilanceOff_  {false};
    std::uint32_t     diagNoiseState_ {0x9E3779B9u};  // carrier-noise xorshift state

    std::atomic<float> vocoderSibilance_ {0.3f};  // base sibilance (diag can override to 0)
    std::atomic<float> clarity_ {0.80f};           // "speak clearly" crossfade 0..1

    std::vector<float> postInputBuffer_;
    std::vector<float> wetBuffer_;
    std::vector<float> carrierBuffer_;   // scratch for the noise-carrier diagnostic
    std::vector<float> drySpeechBuffer_; // raw modulator snapshot for clarity blend
    std::vector<float> micScratchBuffer_;
    std::size_t        micScratchValidSamples_ = 0;

    PitchTrackedCarrier pitchCarrier_;
    std::atomic<bool>   pitchSinging_      {false};
    std::atomic<int>    detectedNoteMidi_  {-1};
    std::atomic<float>  detectedCents_     {0.0f};
    std::atomic<float>  detectedHz_        {0.0f};
    std::vector<float>  pitchCarrierBuffer_;

    // Wet-path 1-pole LPF on the vocoder output. Catches high-band scratch
    // from any source (saw harmonics that survived the pre-vocoder LPF,
    // sibilance noise, vocoder's own high bands). Applied to wetBuffer_
    // BEFORE the clarity blend so the dry-TTS path stays unfiltered.
    float wetLpfState_ = 0.0f;
    float wetLpfAlpha_ = 0.0f;  // recomputed in prepare() from kWetLpfHz / sampleRate
    static constexpr float kWetLpfHz = 5000.0f;
};

} // namespace guitar_dsp::audio
