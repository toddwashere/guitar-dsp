#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "audio/AudioGraph.h"
#include "audio/AppleTTSSource.h"
#include "audio/MicCapture.h"
#include "audio/PhonemeAlignedClipBuilder.h"
#include "audio/PhonemeExtractor.h"
#include "audio/PiperTTSSource.h"
#include "audio/PrebakedTTSSource.h"
#include "audio/TTSPrewarmer.h"
#include "audio/WordAligner.h"
#include "midi/FCB1010Mapping.h"
#include "midi/HostMidiSceneRouter.h"
#include "midi/MidiRouter.h"
#include "scenes/SceneEngine.h"

#include "ai/AppPreferences.h"
#include "ai/PersonaRegistry.h"
#include "ai/ConversationBuffer.h"
#include "ai/ConversationEngine.h"
#include "ai/JuceHttpTransport.h"
#include "ai/WhisperTranscriber.h"
#include "ai/AnthropicClient.h"
#include "ai/OllamaClient.h"

#include "PluginState.h"

namespace guitar_dsp {

class PluginProcessor : public juce::AudioProcessor {
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Guitar DSP"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // Diagnostics — read from the message thread (UI), written from the
    // audio thread. Peak values are linear (0..1+); convert to dBFS at the
    // display layer. Each block overwrites the snapshot, so values are at
    // most one block stale.
    float getInputPeak()  const noexcept { return inputPeak_.load(std::memory_order_relaxed); }
    float getOutputPeak() const noexcept { return outputPeak_.load(std::memory_order_relaxed); }
    float getGateGain()   const noexcept { return gateGain_.load(std::memory_order_relaxed); }
    int   getLastInputChannelCount()  const noexcept { return lastInputChannels_.load(std::memory_order_relaxed); }
    int   getLastOutputChannelCount() const noexcept { return lastOutputChannels_.load(std::memory_order_relaxed); }

    // Snapshot the most recent `count` post-DSP mono samples into `dest`.
    // Safe to call from any thread; uses a power-of-two ring buffer
    // updated atomically from the audio thread. Reads near the write
    // boundary may tear by a sample, which is invisible in visualization.
    static constexpr int kAudioRingSize = 4096;  // ~93 ms at 44.1 kHz
    void snapshotRecentSamples(float* dest, int count) const noexcept;

    // True when the user has routed a sidechain source for the mic input bus.
    bool micBusIsActive() const noexcept;

    scenes::SceneEngine& sceneEngine() { return sceneEngine_; }

    audio::MicCapture& micCapture() noexcept { return micCapture_; }

    // AI feature accessors used by the editor/UI panels.
    ai::ConversationEngine& conversationEngine() noexcept { return *engine_; }
    ai::ConversationBuffer& conversationBuffer() noexcept { return convBuf_; }
    ai::PersonaRegistry&    personaRegistry()    noexcept { return personas_; }
    ai::AppPreferences&     appPreferences()     noexcept { return *prefs_; }
    ai::IHttpTransport&     httpTransport()      noexcept { return http_; }

    // Called from AiSettingsPanel when user picks a model in the dropdown.
    void        selectModelId(std::string id);
    std::string selectedModelId() const { return selectedModelId_; }

    // Called from AiSettingsPanel when user picks a persona.
    void          setCurrentPersona(ai::PersonaId p, std::string customPrompt = "");
    ai::PersonaId currentPersonaId() const noexcept { return currentPersonaId_; }

    int getLastMidiSummary() const noexcept { return lastMidiSummary_.load(std::memory_order_relaxed); }

    // TTS engine status for the visibility readout (message thread).
    bool piperReady() const noexcept;                 // binary + voice + dylibs present
    std::string piperStatusDetail() const;            // empty when ready, else reason
    juce::String activeTtsSourceName() const;         // declared source of the active scene ("" if none)
    juce::String lastResolvedSource() const noexcept; // source that actually produced the last clip

    // Current spoken word index for the active note-triggered scene (-1 idle).
    int currentSpokenWordIndex() const noexcept {
        return graph_.noteSteppedPlayer().currentWordIndex();
    }
    // Current syllable index for the v2 phoneme-stepped player (-1 idle).
    int currentSyllableIndex() const noexcept {
        return graph_.phonemeSteppedPlayer().currentSyllableIndex();
    }
    // Number of syllables in the active v2 clip (0 if no clip loaded).
    int currentSyllableCount() const noexcept {
        return static_cast<int>(graph_.phonemeSteppedPlayer().syllableCount());
    }
    // True when the active scene uses the v2 phoneme-stepped player.
    bool activeSceneIsPhoneme() const noexcept {
        return graph_.activeSpeechPlayer() ==
               audio::AudioGraph::ActiveSpeechPlayer::PhonemeStepped;
    }
    // The active scene's words (split on whitespace). Message thread.
    std::vector<std::string> activeSceneWords() const;
    // Active scene color (0xRRGGBB). Returns a neutral mid-gray (0x9090A0) if
    // no scene is active. Message-thread only.
    std::uint32_t activeSceneColorRgb() const noexcept {
        const auto& s = sceneEngine_.getActiveScene();
        return (s.id >= 0) ? s.colorRgb : 0x9090A0u;
    }
    // Active scene id (matches scene JSON "id" field). Returns -1 if no scene
    // is active. Message-thread only.
    int activeSceneId() const noexcept {
        return sceneEngine_.getActiveSceneId();
    }

    // --- Vocoder diagnostic toggles (message thread) --------------------
    // Forward to the AudioGraph. Used by the DiagToggleBar UI + V/N/S keys
    // to isolate, by ear, why vocoded speech is unintelligible.
    void toggleDiagBypassVocoder() noexcept { graph_.setDiagBypassVocoder(!graph_.diagBypassVocoder()); }
    void toggleDiagNoiseCarrier()  noexcept { graph_.setDiagNoiseCarrier(!graph_.diagNoiseCarrier()); }
    void toggleDiagSibilanceOff()  noexcept { graph_.setDiagSibilanceOff(!graph_.diagSibilanceOff()); }
    bool diagBypassVocoder() const noexcept { return graph_.diagBypassVocoder(); }
    bool diagNoiseCarrier()  const noexcept { return graph_.diagNoiseCarrier(); }
    bool diagSibilanceOff()  const noexcept { return graph_.diagSibilanceOff(); }

    // --- Live vocoder controls (message thread) -------------------------
    // Drive the VocoderPanel sliders. Forward to the AudioGraph.
    void setVocoderMakeup(float linear) noexcept { graph_.setVocoderMakeup(linear); }
    void setVocoderCarrierNoise(float mix) noexcept { graph_.setVocoderCarrierNoise(mix); }
    void setVocoderSibilance(float v) noexcept { graph_.setVocoderSibilance(v); }
    void setVocoderClarity(float c) noexcept { graph_.setClarity(c); }
    float vocoderMakeup() const noexcept { return graph_.vocoderMakeup(); }
    float vocoderCarrierNoise() const noexcept { return graph_.vocoderCarrierNoise(); }
    float vocoderSibilance() const noexcept { return graph_.vocoderSibilance(); }
    float vocoderClarity() const noexcept { return graph_.clarity(); }

    // Pitch-singing toggle (message thread).
    void setPitchSinging(bool on) noexcept  { graph_.setPitchSinging(on); }
    void togglePitchSinging() noexcept      { graph_.setPitchSinging(!graph_.pitchSinging()); }
    bool pitchSinging() const noexcept      { return graph_.pitchSinging(); }

    // Sing-mode toggle (vibrato + pitch quantize when on).
    void setSinging(bool on)    noexcept { graph_.setSinging(on); }
    void toggleSinging()        noexcept { graph_.setSinging(!graph_.singing()); }
    bool singing()        const noexcept { return graph_.singing(); }

    // Word-sync mode (Latch / Advance / Syllable) for note-triggered TTS.
    void setWordSyncMode(audio::WordSyncMode m) noexcept { graph_.setWordSyncMode(m); }
    audio::WordSyncMode wordSyncMode() const noexcept    { return graph_.wordSyncMode(); }

    // Rewind the note-triggered TTS sequence to the start.
    void rewindSpoken() noexcept { graph_.rewindSpoken(); }

    // Live pitch readout published by AudioGraph (audio thread -> UI).
    int   detectedNoteMidi() const noexcept { return graph_.detectedNoteMidi(); }
    float detectedCents()    const noexcept { return graph_.detectedCents(); }
    float detectedHz()       const noexcept { return graph_.detectedHz(); }

    // Noise gate threshold (dBFS). Lower = more permissive.
    void  setNoiseGateThresholdDb(float dB) noexcept { graph_.setNoiseGateThresholdDb(dB); }
    float noiseGateThresholdDb() const noexcept      { return graph_.noiseGateThresholdDb(); }

    // The currently active scene's declared clarity (0..1), for the visibility
    // readout — so the operator can see when the live slider has drifted from
    // the scene's authored default.
    float activeSceneClarity() const { return sceneEngine_.activeTtsConfig().clarity; }

    // Phase A — Vocal Guitar (Scene 2). True when the active scene uses
    // the clip-bank modulator source.
    bool activeSceneIsClipBank() const {
        return sceneEngine_.activeTtsConfig().source == "clipBank";
    }

    // Phase B — Mic Talkbox (Scene 3). True when the active scene uses
    // the mic modulator source.
    bool activeSceneIsMic() const {
        return sceneEngine_.activeTtsConfig().source == "mic";
    }
    // When true, PluginEditor shows the ConversationPanel (chat UI) on this
    // scene. Default false; only scenes with "showChat": true expose it.
    bool activeSceneShowsChat() const {
        return sceneEngine_.getActiveScene().showChat;
    }
    // Default text for the SayPanel input box — typically the scene's
    // tts.text, so e.g. Scene 1 ("Developers!") populates the input with
    // "Developers" so the operator can edit/re-trigger it directly.
    std::string activeSceneTtsText() const {
        return sceneEngine_.activeTtsConfig().text;
    }
    // Peak mic level in [0, 1] for the always-visible level meter in VocoderPanel.
    float micPeak() const noexcept { return graph_.micPeak(); }
    // 0=none, 1=sidechain (AU), 2=standalone ch 2, 3=self-modulation (mono input).
    // Used by VocoderPanel to label which physical input is being routed as the mic.
    int   micRoutingSource() const noexcept {
        return micRoutingSource_.load(std::memory_order_relaxed);
    }
    // Linear gain applied to the mic input before BOTH the meter (via
    // setMicBlock) and MicCapture (for whisper). Tunable on VocoderPanel.
    // 1.0 = unity, 4.0 = +12 dB, 16.0 = +24 dB. Persisted in PluginState.
    void setMicCaptureGain(float linear) noexcept {
        micCaptureGain_.store(juce::jlimit(0.25f, 32.0f, linear),
                              std::memory_order_relaxed);
    }
    float micCaptureGain() const noexcept {
        return micCaptureGain_.load(std::memory_order_relaxed);
    }
    int  clipBankCursor() const { return graph_.clipBankPlayer().currentClipIndex(); }
    int  clipBankSize()   const { return graph_.clipBankPlayer().bankSize(); }
    // Returns the current clip's key (e.g. "03_new") or empty when idle.
    std::string clipBankCurrentKey() const {
        const auto cfg = sceneEngine_.activeTtsConfig();
        const int  idx = clipBankCursor();
        if (idx < 0 || idx >= static_cast<int>(cfg.bank.size())) return {};
        return cfg.bank[static_cast<std::size_t>(idx)];
    }
    // Issued by the Rewind pill on WordReadout. Picks the right player.
    void rewindActive() noexcept {
        if (activeSceneIsMic())          /* no-op: no clip to rewind */ return;
        if (activeSceneIsClipBank())     graph_.rewindClipBank();
        else                              graph_.rewindSpoken();
    }

    // Apple-TTS "type and say" plumbing for the message-thread UI.
    //
    // enqueueSayText() kicks off background synthesis via the existing
    // Apple TTS prewarmer (which has its own worker thread — calling
    // synthesize() directly from the message thread would deadlock
    // AVSpeechSynthesizer's main-queue callback against itself, then
    // crash when the late callback dereferences destroyed locals).
    //
    // tryInstallSayText() checks whether the prewarmer has finished and,
    // if so, swaps the resulting clip into the audio graph. Caller (the
    // SayPanel) polls this on a juce::Timer while the spinner is showing.
    // Returns: 1 = installed (stop polling), 0 = not ready yet, -1 = the
    // synthesis finished but failed (stop polling, show error).
    void enqueueSayText(const std::string& text, const std::string& voiceId = {});
    int  tryInstallSayText(const std::string& text);

    // Called by the ConversationEngine worker when the LLM produces an
    // Assistant reply. Forwards to enqueueSayText AND stores the text for
    // the SayPanel timer to pick up — the panel will populate the input
    // field and trigger the Say flow so the user can pluck through the
    // reply word-by-word.
    void onLlmResponse(const std::string& text);

    // Message-thread: SayPanel pulls the pending LLM text (if any) and
    // clears the slot. Empty string when nothing is pending.
    std::string takePendingAutoSay();

    // Pass-through to MidiRouter::setPreferredDeviceName. Empty = auto-pick.
    void setMidiPreferredDeviceName(const juce::String& name);

private:
    audio::AudioGraph graph_;
    audio::MicCapture micCapture_;
    std::vector<float> monoScratch_;

    std::atomic<float> inputPeak_   {0.0f};
    std::atomic<float> outputPeak_  {0.0f};
    std::atomic<float> gateGain_    {1.0f};
    std::atomic<int>   lastInputChannels_  {0};
    std::atomic<int>   lastOutputChannels_ {0};

    std::array<float, kAudioRingSize> audioRing_{};
    std::atomic<int>                  audioRingWriteIdx_{0};

    scenes::SceneEngine sceneEngine_;

    // Liveness flag for queued MIDI callbacks. The MidiRouter callback
    // hops to the message thread via callAsync, so a callback can still
    // be in flight after PluginProcessor is destroyed. The callback
    // captures a weak_ptr to this atomic and bails out if the strong
    // ref has been dropped (i.e. we're gone).
    std::shared_ptr<std::atomic<bool>> alive_
        {std::make_shared<std::atomic<bool>>(true)};

    midi::FCB1010Mapping              midiMapping_ {midi::FCB1010Mapping::stockDefaults()};
    std::unique_ptr<midi::MidiRouter> midiRouter_;
    std::atomic<int>                  lastMidiSummary_ {0};

    class AssetsPoller;
    std::unique_ptr<AssetsPoller> assetsPoller_;

    std::unique_ptr<audio::PrebakedTTSSource> prebakedTtsSource_;
    std::unique_ptr<audio::PrebakedTTSSource> vocalGuitarSource_;
    std::unique_ptr<audio::AppleTTSSource>    appleTtsSource_;
    std::unique_ptr<audio::TTSPrewarmer>      applePrewarmer_;
    std::unique_ptr<audio::PiperTTSSource>    piperTtsSource_;
    std::unique_ptr<audio::TTSPrewarmer>      piperPrewarmer_;
    std::unique_ptr<audio::PhonemeExtractor>  phonemeExtractor_;
    std::string                                currentTtsClipKey_;  // audio thread perspective (only mutated via message-thread callAsync)
    std::atomic<int> lastResolvedSource_ {0};  // 0 none,1 prebaked,2 apple,3 piper
    std::atomic<int> micRoutingSource_   {0};  // 0 none,1 sidechain,2 ch2,3 self-mod
    std::atomic<float> micCaptureGain_   {4.0f};  // +12 dB default (helps quiet mics)

    // When the ConversationEngine produces an LLM reply, the worker thread
    // pushes the text here and the SayPanel timer picks it up on the message
    // thread — populates the textbox AND fires the Say flow so the response
    // is synthesized + installed into the note-stepped player. Result: the
    // user plucks notes to speak the LLM reply word-by-word.
    mutable std::mutex pendingAutoSayMutex_;
    std::string        pendingAutoSay_;

    // When the user types into the Say textbox and the typed clip is
    // installed via tryInstallSayText, the WordReadout should show the
    // typed text — NOT the scene's default text. Stored on the message
    // thread; read via activeSceneWords(). Cleared on scene change.
    std::string      currentSayText_;
    int                                        lastSeenSceneId_ = -1;  // audio thread

    // Host-MIDI scene control (plugin only). processBlock stores a pending
    // scene id (audio thread, lock-free); HostMidiPoller applies it on the
    // message thread (activateScene is message-thread API). Standalone uses
    // MidiRouter instead, so this stays inert there.
    std::atomic<int> pendingHostScene_ {-1};
    std::atomic<bool> pendingPitchSingingToggle_ {false};
    class HostMidiPoller;
    std::unique_ptr<HostMidiPoller> hostMidiPoller_;

    // Conversational AI subsystem.
    std::unique_ptr<ai::AppPreferences>     prefs_;
    ai::PersonaRegistry                     personas_;
    ai::ConversationBuffer                  convBuf_;
    ai::JuceHttpTransport                   http_;
    std::unique_ptr<ai::WhisperTranscriber> whisper_;
    // shared_ptr (not unique_ptr) so rebuildLlmClient() can swap to a new
    // client while the ConversationEngine worker thread may still be inside
    // llm_->generate(). The engine takes its own shared_ptr; the old client
    // stays alive until that worker call returns.
    std::shared_ptr<ai::ILlmClient>         llm_;
    std::string                             selectedModelId_ {"claude-haiku-4-5"};
    ai::PersonaId                           currentPersonaId_ {ai::PersonaId::Interviewer};
    std::unique_ptr<ai::ConversationEngine> engine_;

    void rebuildLlmClient();   // re-create llm_ based on selectedModelId_
};

} // namespace guitar_dsp
