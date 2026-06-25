#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>

#include <juce_events/juce_events.h>

#include "AssetLocator.h"
#include "audio/GspeakBundle.h"
#include "audio/PrerenderCache.h"
#include "audio/TTSSynthChain.h"
#include "scenes/SceneLibrary.h"
#include "TtsStatusBar.h"
#include "SayPanel.h"
#include "util/PluginLogger.h"

namespace guitar_dsp {

class PluginProcessor::AssetsPoller : public juce::Timer {
public:
    explicit AssetsPoller(PluginProcessor& p) : owner_(p) {}
    void timerCallback() override {
        owner_.sceneEngine_.reloadFrom(AssetLocator::scenesDirectory());
    }
private:
    PluginProcessor& owner_;
};

class PluginProcessor::HostMidiPoller : public juce::Timer {
public:
    explicit HostMidiPoller(PluginProcessor& p) : p_(p) { startTimerHz(30); }
    ~HostMidiPoller() override { stopTimer(); }
    void timerCallback() override {
        const int s = p_.pendingHostScene_.exchange(-1, std::memory_order_acquire);
        if (s >= 0) {
            log::info("scene -> " + juce::String(s) + " (host MIDI)");
            p_.sceneEngine_.activateScene(s);
        }

        if (p_.pendingPitchSingingToggle_.exchange(false, std::memory_order_acquire)) {
            log::info("pitch-singing toggle (host MIDI)");
            p_.togglePitchSinging();
        }
    }
private:
    PluginProcessor& p_;
};

PluginProcessor::PluginProcessor()
    : juce::AudioProcessor(BusesProperties()
        // Input bus default = stereo for both targets:
        //   Standalone: the JUCE wrapper exposes 2 channels when the user
        //     enables a stereo input device (e.g. Scarlett "Input 1+2").
        //     ch 0 = guitar, ch 1 = mic.
        //   AU (Logic): stereo aligns with how Logic creates audio tracks.
        //     With a mono default + stereo audio track, Logic's aufx
        //     negotiation can silently drop region playback while live input
        //     monitoring still works. Stereo default avoids that path.
        // isBusesLayoutSupported accepts mono OR stereo for either target,
        // so either wrapper can still negotiate down to mono if it wants.
        .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
        .withInput ("Mic",    juce::AudioChannelSet::mono(),   false)  // sidechain, disabled by default
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    // When stderr is redirected to a file (e.g. `open --stderr foo.log`),
    // it's fully buffered, so the conversational-AI diagnostics never
    // appear until process exit. Force unbuffered so live tailing works.
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    // File logger: ~/Library/Logs/Guitar Speak/Guitar Speak <date>.log
    // (idempotent; first plugin instance wins). Critical for AU diagnosis —
    // stderr is dropped inside Logic's sandboxed AUHostingServiceXPC.
    log::init();
    log::info("PluginProcessor ctor wrapperType=" + juce::String((int)wrapperType));

    midiRouter_ = std::make_unique<midi::MidiRouter>(
        [this, weakAlive = std::weak_ptr<std::atomic<bool>>(alive_)]
        (const juce::MidiMessage& msg) {
            // This runs on the message thread after a callAsync hop. If we
            // were destroyed between the hop and the dispatch, the weak_ptr
            // will fail to lock (or the atomic will read false) — bail out
            // before touching any members on a dead `this`.
            auto alive = weakAlive.lock();
            if (!alive || !alive->load(std::memory_order_acquire)) return;

            // Record last summary for the diagnostic UI: pack the first two
            // bytes of the MIDI message into one int (status<<16 | data1).
            const auto* raw = msg.getRawData();
            const int byte0 = (msg.getRawDataSize() >= 1) ? (raw[0] & 0xFF) : 0;
            const int byte1 = (msg.getRawDataSize() >= 2) ? (raw[1] & 0xFF) : 0;
            const int packed = (byte0 << 16) | byte1;
            lastMidiSummary_.store(packed, std::memory_order_relaxed);

            if (auto cmd = midiMapping_.translate(msg)) {
                if (cmd->type == midi::SceneCommandType::ActivateScene) {
                    log::info("scene -> " + juce::String(cmd->payload) + " (direct MIDI)");
                    sceneEngine_.activateScene(cmd->payload);
                } else if (cmd->type == midi::SceneCommandType::TogglePitchSinging) {
                    log::info("pitch-singing toggle (direct MIDI)");
                    graph_.setPitchSinging(!graph_.pitchSinging());
                }
                // SetWetDry / SetMasterGain are recognized but no-op in
                // Phase 2; expression-pedal continuous control wires
                // through in Phase 3.
            }

            // AI pedal dispatch — runs after scene-change handling so a pedal can
            // (in principle) trigger both. In stock defaults the AI bindings are -1
            // so this is a no-op until the user configures FCB AI pedals.
            if (msg.isProgramChange() && engine_) {
                const int pc = msg.getProgramChangeNumber();
                auto action = midiMapping_.decodeAi(pc, false);
                switch (action) {
                    case midi::AiAction::PttToggle:  engine_->startTurn();         break;
                    case midi::AiAction::ClearChat:  engine_->clearConversation(); break;
                    case midi::AiAction::CancelTurn: engine_->cancelTurn();        break;
                    case midi::AiAction::None:        break;
                }
            }
        });

    if (const char* env = std::getenv("GUITAR_DSP_HOT_RELOAD"); env && std::string(env) == "1") {
        assetsPoller_ = std::make_unique<AssetsPoller>(*this);
        assetsPoller_->startTimer(2000);
    }

    // Only honor host MIDI when running as a plugin; the standalone uses its
    // own direct-CoreMIDI MidiRouter (avoids double-triggering).
    if (wrapperType != wrapperType_Standalone)
        hostMidiPoller_ = std::make_unique<HostMidiPoller>(*this);

    // Conversational AI subsystem.
    prefs_ = std::make_unique<ai::AppPreferences>(ai::AppPreferences::defaultPath());

    whisper_ = std::make_unique<ai::WhisperTranscriber>(
        juce::File(juce::String(AssetLocator::whisperModelPath())));

    rebuildLlmClient();

    personas_.setSessionQaDoc(&sessionQaDoc_);

    engine_ = std::make_unique<ai::ConversationEngine>(
        *whisper_, llm_, micCapture_, convBuf_, personas_,
        [this](std::string text){ onLlmResponse(text); });
    engine_->setCannedFallbackEnabled(prefs_->cannedFallbackOnLlmError());
}

PluginProcessor::~PluginProcessor() {
    // Mark dead before member destructors run. Any callAsync lambdas
    // already queued by MidiRouter will see this and bail out instead
    // of dereferencing a half-destroyed `this`.
    alive_->store(false, std::memory_order_release);
    log::info("PluginProcessor dtor");
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    log::info("prepareToPlay sr=" + juce::String(sampleRate, 1)
              + " block=" + juce::String(samplesPerBlock));
    graph_.prepare(sampleRate, samplesPerBlock);
    micCapture_.prepare(sampleRate, 1);
    monoScratch_.assign(static_cast<std::size_t>(samplesPerBlock), 0.0f);

    if (sceneEngine_.getSceneCount() == 0) {
        const auto dir = AssetLocator::scenesDirectory();
        auto scenes = scenes::SceneLibrary::loadDirectory(dir);
        if (scenes.empty()) scenes.push_back(scenes::Scene::defaults(0));
        sceneEngine_.loadScenes(std::move(scenes));
        // Default to Bypass (id 13, dryWet=0) on first load so a freshly
        // inserted AU passes audio through. The live demo scenes are 85-95%
        // wet and silence any source that doesn't trigger guitar onsets
        // (recorded clips, other tracks). setStateInformation (called next
        // by the host for saved projects) will override this with the saved
        // sceneId — fresh insertion lands on Bypass; reopened projects
        // restore whatever scene was active when saved.
        // Bypass scene is now at slot 13 (was 11 before sung-vowels landed).
        log::info("scene -> 13 (Bypass on fresh load)");
        sceneEngine_.activateScene(13);
    }

    prebakedTtsSource_ = std::make_unique<audio::PrebakedTTSSource>(
        AssetLocator::ttsDirectory());
    prebakedTtsSource_->prepare(sampleRate);

    vocalGuitarSource_ = std::make_unique<audio::PrebakedTTSSource>(
        AssetLocator::vocalGuitarClipsDirectory());
    vocalGuitarSource_->prepare(sampleRate);

    // Tear down prewarmer (joins its worker thread) BEFORE replacing the
    // AppleTTSSource it references. Otherwise the worker could call
    // synthesize() on a destroyed source if prepareToPlay() is invoked
    // a second time (e.g. host sample-rate change).
    applePrewarmer_.reset();
    appleTtsSource_ = std::make_unique<audio::AppleTTSSource>();
    appleTtsSource_->prepare(sampleRate);

    applePrewarmer_ = std::make_unique<audio::TTSPrewarmer>(*appleTtsSource_);

    // Enqueue every Apple-source scene's text for background synthesis.
    sceneEngine_.forEachScene([this](const scenes::Scene& s) {
        if (s.tts.source == "apple" && !s.tts.text.empty()) {
            if (!s.tts.voice.empty()) appleTtsSource_->setVoice(s.tts.voice);
            applePrewarmer_->enqueue(s.tts.text);
        }
    });

    // Tear down the piper prewarmer first if it exists (same lifetime
    // rule as apple: prewarmer's worker may be calling synthesize on
    // the old source).
    piperPrewarmer_.reset();
    piperTtsSource_ = std::make_unique<audio::PiperTTSSource>(
        AssetLocator::piperBinaryPath(),
        AssetLocator::defaultPiperVoicePath());
    piperTtsSource_->prepare(sampleRate);

    piperPrewarmer_ = std::make_unique<audio::TTSPrewarmer>(*piperTtsSource_);

    // Enqueue every Piper-source scene's text for background synthesis.
    sceneEngine_.forEachScene([this](const scenes::Scene& s) {
        if (s.tts.source == "piper" && !s.tts.text.empty()) {
            piperPrewarmer_->enqueue(s.tts.text);
        }
    });

    // PhonemeExtractor uses the espeak-ng binary shipped next to Piper.
    phonemeExtractor_ = std::make_unique<audio::PhonemeExtractor>(
        AssetLocator::espeakBinaryPath());

    currentTtsClipKey_.clear();
    lastSeenSceneId_ = -1;
    graph_.ttsClipPlayer().setClip(nullptr);
}

void PluginProcessor::releaseResources() {
    log::info("releaseResources");
    graph_.reset();
}

void PluginProcessor::numChannelsChanged() {
    log::info("numChannelsChanged in=" + juce::String(getTotalNumInputChannels())
              + " out=" + juce::String(getTotalNumOutputChannels()));
}

void PluginProcessor::processorLayoutsChanged() {
    juce::String desc;
    for (int b = 0; b < getBusCount(true);  ++b)
        desc += "in["  + juce::String(b) + "]=" + getChannelLayoutOfBus(true,  b).getDescription() + " ";
    for (int b = 0; b < getBusCount(false); ++b)
        desc += "out[" + juce::String(b) + "]=" + getChannelLayoutOfBus(false, b).getDescription() + " ";
    log::info("processorLayoutsChanged " + desc);
}

void PluginProcessor::setMidiPreferredDeviceName(const juce::String& name) {
    if (midiRouter_) midiRouter_->setPreferredDeviceName(name);
}

void PluginProcessor::installEditedPhonemeClip(audio::TTSClipPtr clip) {
    if (!clip) return;
    graph_.phonemeSteppedPlayer().setClip(clip);
    lastPhonemeClip_ = clip;
}

void PluginProcessor::installEditedV1Clip(audio::TTSClipPtr clip) {
    lastV1Clip_ = clip;
    graph_.noteSteppedPlayer().setClip(clip);
}

void PluginProcessor::installImportedClip(audio::TTSClipPtr clip) {
    if (!clip || clip->samples.empty()) return;

    // Match the v1 branch of tryAutoLoadGspeak_ (see PluginProcessor.cpp:297-305).
    graph_.clipBankPlayer().setBank({});
    graph_.setModulatorSource(audio::AudioGraph::ModulatorSource::NoteStepped);
    graph_.setActiveSpeechPlayer(audio::AudioGraph::ActiveSpeechPlayer::NoteStepped);
    graph_.ttsClipPlayer().setClip(nullptr);
    graph_.phonemeSteppedPlayer().setClip(nullptr);
    lastPhonemeClip_.reset();
    installEditedV1Clip(clip);
    graph_.noteSteppedPlayer().setLoop(true);
}

juce::String PluginProcessor::activeSceneGspeakPath() const {
    return juce::String(sceneEngine_.getActiveScene().gspeakPath);
}

juce::String PluginProcessor::currentSayText() const {
    return sayPanel_ ? sayPanel_->currentText() : juce::String{};
}

void PluginProcessor::setSayPanelText(juce::String t) {
    if (sayPanel_) sayPanel_->setText(std::move(t));
}

void PluginProcessor::flashStatusMessage(juce::String msg, int durationMs) {
    if (ttsStatusBar_) ttsStatusBar_->flashMessage(std::move(msg), durationMs);
}

double PluginProcessor::currentSampleRate() const noexcept {
    return getSampleRate();
}

bool PluginProcessor::tryAutoLoadGspeak_(const scenes::Scene& scene) {
    // Use voice-pack-aware resolved path when set.
    const int idx = activeVoiceIndex();
    const auto resolved = scene.resolvedGspeakPath(idx);
    if (resolved.empty() || !scene.gspeakAutoLoad) return false;

    const auto path = AssetLocator::resolveForRead(resolved);
    if (path.empty()) {
        if (ttsStatusBar_)
            ttsStatusBar_->flashMessage(
                juce::String(resolved) + " missing — using fallback", 5000);
        return false;
    }
    juce::File file(path);
    auto loaded = audio::GspeakBundle::read(file, getSampleRate());
    if (!loaded.has_value()) {
        if (ttsStatusBar_)
            ttsStatusBar_->flashMessage(
                juce::String(resolved) + " missing — using fallback", 5000);
        return false;
    }

    // Configure the player + modulator state for the scene, then install the clip.
    graph_.setClarity(scene.tts.clarity);
    if (scene.tts.wordSync == "latch")
        graph_.setWordSyncMode(audio::WordSyncMode::Latch);
    else if (scene.tts.wordSync == "advance")
        graph_.setWordSyncMode(audio::WordSyncMode::Advance);
    else if (scene.tts.wordSync == "syllable")
        graph_.setWordSyncMode(audio::WordSyncMode::Syllable);

    // SungDirect branch (scene 12): directShift.enabled drives routing to the
    // new SungDirectPath wet-bus. Split the master clip into per-grain sub-clips
    // keyed by bankKey and feed them to SungDirectPath. The vocoder + clip-bank
    // player are cleared so they don't bleed into this scene's output.
    if (scene.directShift.enabled) {
        auto bank = splitMasterClipIntoBank_(loaded->clip);
        // Hash the master clip's audio so SungDirectPath can locate (or
        // create) the matching on-disk .bake cache. Re-built bundles get a
        // different hash → different cache file → forced re-render.
        std::string bundleHash;
        if (loaded->clip && ! loaded->clip->samples.empty()) {
            bundleHash = audio::PrerenderCache::hashBytes(
                loaded->clip->samples.data(),
                loaded->clip->samples.size() * sizeof(float));
        }
        // Push the bank into SungDirectPath. First activation per voice
        // pre-renders (~90 s, background thread). Subsequent activations
        // mmap the cached file (~instant).
        graph_.sungDirectPath().setGrainsForBank(bank, bundleHash);
        graph_.sungDirectPath().setPortamentoMs(scene.directShift.portamentoMs);
        graph_.sungDirectPath().setFormantTintSemitones(
            scene.directShift.formantTintSemitones);
        log::info("wetSource -> SungDirect (gspeak autoload)");
        graph_.setWetSource(audio::AudioGraph::WetSource::SungDirect);

        // Clear vocoder + bank players so a prior scene doesn't bleed in.
        graph_.clipBankPlayer().setBank({});
        graph_.setModulatorSource(audio::AudioGraph::ModulatorSource::NoteStepped);
        graph_.setActiveSpeechPlayer(
            audio::AudioGraph::ActiveSpeechPlayer::NoteStepped);
        graph_.ttsClipPlayer().setClip(nullptr);
        graph_.noteSteppedPlayer().setClip(nullptr);
        graph_.phonemeSteppedPlayer().setClip(nullptr);
        lastPhonemeClip_.reset();

        if (sayPanel_) sayPanel_->setText(juce::String(loaded->text));
        currentTtsClipKey_ = std::string("gspeak:") + resolved;
        return true;
    }

    // I7: Reset WetSource to Vocoder when leaving a directShift scene (or when
    // landing on a non-directShift scene that skipped the directShift branch above).
    // Without this, scene 12 → scene 11 transitions leave the wet bus on SungDirect.
    log::info("wetSource -> Vocoder (gspeak autoload non-directShift)");
    graph_.setWetSource(audio::AudioGraph::WetSource::Vocoder);

    // Clear any leftover clip-bank state from a previous scene (e.g. scene 2
    // Vocal-Guitar). The normal scene-activation paths do this branch-by-branch;
    // the autoload helper has to be explicit because it short-circuits them.
    graph_.clipBankPlayer().setBank({});
    // Modulator routes through the note-stepped (v1) / phoneme-stepped (v2)
    // player so onset-driven plucks actually advance through words / syllables.
    // The normal-path equivalents are at PluginProcessor.cpp ~797 (v2) and ~850 (v1).
    graph_.setModulatorSource(audio::AudioGraph::ModulatorSource::NoteStepped);

    if (loaded->isV2) {
        graph_.setActiveSpeechPlayer(
            audio::AudioGraph::ActiveSpeechPlayer::PhonemeStepped);
        graph_.phonemeSteppedPlayer().setMaxSustainMs(scene.speech.maxSustainMs);
        graph_.ttsClipPlayer().setClip(nullptr);
        installEditedPhonemeClip(loaded->clip);
        graph_.phonemeSteppedPlayer().setLoop(true);
        // Push to the v1 player too, mirroring the normal v2 path — safe
        // because the inactive player is muted by setActiveSpeechPlayer.
        graph_.noteSteppedPlayer().setClip(loaded->clip);
    } else {
        graph_.setActiveSpeechPlayer(
            audio::AudioGraph::ActiveSpeechPlayer::NoteStepped);
        graph_.ttsClipPlayer().setClip(nullptr);
        graph_.phonemeSteppedPlayer().setClip(nullptr);
        lastPhonemeClip_.reset();
        installEditedV1Clip(loaded->clip);
        graph_.noteSteppedPlayer().setLoop(true);
    }

    if (sayPanel_) sayPanel_->setText(juce::String(loaded->text));

    // Mark the clip-key as "consumed" so the normal-path early-return
    // (`if (key == currentTtsClipKey_) return;`) fires if this scene is
    // re-activated without changing the bundle.
    currentTtsClipKey_ = std::string("gspeak:") + resolved;
    return true;
}

std::vector<audio::TTSClipPtr>
PluginProcessor::splitMasterClipIntoBank_(audio::TTSClipPtr master) {
    std::vector<audio::TTSClipPtr> out;
    if (!master) return out;
    const std::size_t totalSamples = master->samples.size();
    for (const auto& p : master->phonemes) {
        // Guard against out-of-range phoneme boundaries (can happen if the
        // resample scale nudged an endSample past the real end).
        const std::size_t start = std::min(p.startSample, totalSamples);
        const std::size_t end   = std::min(p.endSample,   totalSamples);
        if (end <= start) continue;  // skip zero-length grains

        auto sub = std::make_shared<audio::TTSClip>();
        sub->sampleRate = master->sampleRate;
        sub->samples.assign(master->samples.begin() + static_cast<std::ptrdiff_t>(start),
                            master->samples.begin() + static_cast<std::ptrdiff_t>(end));

        // Prefer per-phoneme bankKey (populated by GspeakBundle::read for v2
        // multi-grain bundles). Fall back to a vowel-label heuristic for older
        // bundles that don't carry per-phoneme bankKey.
        if (!p.bankKey.empty()) {
            sub->bankKey       = p.bankKey;
            sub->anchorPitchHz = p.anchorPitchHz;
        } else {
            // Heuristic: map vowel label → bankKey.
            //
            // Convention (I6): bundles built by tools/build_sung_vowel_bundle.py
            // write single-character IPA labels ("a","e","i","o","u"). This
            // heuristic matches that convention only. Legacy v2 bundles produced
            // by espeak-ng use multi-character labels (e.g. "AE", "IH", "OW")
            // which will fall through to the "sung_ah" default here — acceptable
            // because those bundles do not carry anchorPitchHz metadata either,
            // so the per-grain anchor path (I1) would have fallen back to 220 Hz
            // regardless. Do not change the single-char mapping without updating
            // tools/build_sung_vowel_bundle.py to match.
            if      (p.label == "a")  sub->bankKey = "sung_ah";
            else if (p.label == "e")  sub->bankKey = "sung_eh";
            else if (p.label == "i")  sub->bankKey = "sung_ee";
            else if (p.label == "o")  sub->bankKey = "sung_oh";
            else if (p.label == "u")  sub->bankKey = "sung_oo";
            else                       sub->bankKey = "sung_ah";  // safe default for unknown labels
            sub->anchorPitchHz = 0.0f;  // unknown in legacy bundles
        }
        sub->variantTag = master->variantTag;

        out.push_back(std::const_pointer_cast<const audio::TTSClip>(sub));
    }
    return out;
}

int PluginProcessor::activeVoiceIndex() const noexcept {
    const int id = sceneEngine_.getActiveSceneId();
    auto it = stateData_.activeVoiceIndexByScene.find(id);
    if (it != stateData_.activeVoiceIndexByScene.end()) return it->second;
    const auto& s = sceneEngine_.getActiveScene();
    return s.defaultVoiceIndex;
}

void PluginProcessor::setActiveVoiceIndex(int idx) {
    const auto& scene = sceneEngine_.getActiveScene();
    if (scene.voicePacks.empty()) return;
    if (idx < 0) idx = 0;
    if (idx >= static_cast<int>(scene.voicePacks.size()))
        idx = static_cast<int>(scene.voicePacks.size()) - 1;
    const int id = scene.id;
    stateData_.activeVoiceIndexByScene[id] = idx;

    // Arm the 50 ms output fade BEFORE the bundle reload so the audio
    // thread starts ramping immediately.
    voicePackSwapFadeSamples_.store(
        static_cast<int>(0.050 * getSampleRate()),
        std::memory_order_relaxed);
    voicePackSwapFadeArmed_.store(true, std::memory_order_release);

    // Re-use the existing auto-load path. tryAutoLoadGspeak_ reads
    // scene.gspeakPath; temporarily swap in the resolved path.
    auto sceneCopy        = scene;
    sceneCopy.gspeakPath  = scene.resolvedGspeakPath(idx);
    sceneCopy.gspeakAutoLoad = true;
    (void) tryAutoLoadGspeak_(sceneCopy);
}

void PluginProcessor::rebuildLlmClient() {
    if (selectedModelId_.rfind("ollama:", 0) == 0) {
        const auto tag = selectedModelId_.substr(7);
        llm_ = std::make_shared<ai::OllamaClient>(
            http_, prefs_->ollamaEndpoint(), tag);
    } else {
        llm_ = std::make_shared<ai::AnthropicClient>(
            http_, prefs_->anthropicApiKey(), selectedModelId_);
    }
    if (engine_) engine_->setLlmClient(llm_);
}

void PluginProcessor::selectModelId(std::string id) {
    selectedModelId_ = std::move(id);
    rebuildLlmClient();
}

void PluginProcessor::setCurrentPersona(ai::PersonaId p, std::string custom) {
    currentPersonaId_ = p;
    if (! custom.empty()) personas_.setCustomPrompt(p, std::move(custom));
    if (engine_) engine_->setPersona(p, "");
}

void PluginProcessor::generateSong(
        ai::PersonaId p,
        std::function<void(std::string, std::string)> onDone) {
    if (!llm_) {
        if (onDone) onDone({}, "LLM client not initialised");
        return;
    }
    // Snapshot what the background thread needs. NOTE we capture llm_ by
    // shared_ptr so a model swap mid-flight doesn't invalidate the pointer.
    auto llm    = llm_;
    auto prompt = personas_.promptFor(p);
    auto weak   = std::weak_ptr<std::atomic<bool>>(alive_);

    juce::Thread::launch([llm, prompt, onDone = std::move(onDone), weak]() {
        ai::LlmRequest req;
        req.messages = {
            {ai::Message::Role::System, prompt},
            {ai::Message::Role::User,   "Write the song now."},
        };
        // Songs are ~80-150 syllables of structured verse; the 80-token
        // conversational default truncates them mid-chorus. Bump generously.
        req.maxTokens = 800;
        // LLM round-trips for ~800 tokens routinely take 5-15s; the
        // conversational 10s default sometimes truncates mid-song.
        req.timeout = std::chrono::milliseconds(30000);
        const auto reply = llm->generate(req);
        juce::MessageManager::callAsync([reply, onDone, weak]() {
            auto alive = weak.lock();
            if (!alive || !alive->load(std::memory_order_acquire)) return;
            if (onDone) onDone(reply.text, reply.error);
        });
    });
}

bool PluginProcessor::piperReady() const noexcept {
    return piperTtsSource_ && piperTtsSource_->isReady();
}

std::string PluginProcessor::piperStatusDetail() const {
    if (!piperTtsSource_) return "piper source not initialized";
    return piperTtsSource_->statusDetail();
}

juce::String PluginProcessor::activeTtsSourceName() const {
    return juce::String(sceneEngine_.activeTtsConfig().source);
}

juce::String PluginProcessor::lastResolvedSource() const noexcept {
    switch (lastResolvedSource_.load(std::memory_order_relaxed)) {
        case 1: return "prebaked";
        case 2: return "apple";
        case 3: return "piper";
        default: return "";
    }
}

std::vector<std::string> PluginProcessor::activeSceneWords() const {
    const auto cfg = sceneEngine_.activeTtsConfig();
    // If the user installed custom text via the Say textbox, that text wins
    // — otherwise the readout would still show the scene's default words
    // while the audio plays the typed clip.
    const std::string& sourceText = !currentSayText_.empty()
        ? currentSayText_
        : cfg.text;
    std::vector<std::string> tokens;
    // In Syllable mode, the player is stepping through TTSClip::syllables —
    // text "gui-tar" produced two segments ("gui", "tar"). The display needs
    // to match that segmentation, so split on hyphen as well as whitespace.
    // Otherwise the player advances past "gui" / "tar" / "gent" / "ly" while
    // the display ticks one token per WORD ("gui-tar", "gent-ly") and the
    // two indices diverge by one per hyphenated word.
    const bool syllableMode =
        (graph_.wordSyncMode() == audio::WordSyncMode::Syllable);
    if (syllableMode) {
        std::string cur;
        for (char c : sourceText) {
            if (c == ' ' || c == '\t' || c == '-' || c == '\n') {
                if (!cur.empty()) { tokens.push_back(cur); cur.clear(); }
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) tokens.push_back(cur);
    } else {
        std::istringstream iss(sourceText);
        std::string w;
        while (iss >> w) tokens.push_back(w);
    }
    return tokens;
}

void PluginProcessor::enqueueSayText(const std::string& text,
                                      const std::string& voiceId) {
    if (text.empty()) return;
    // On phoneme-stepped scenes, the install path will build the clip
    // synchronously via Piper + PhonemeAlignedClipBuilder (no prewarmer
    // queue exists for that path yet). Don't waste an Apple synthesis.
    if (activeSceneIsPhoneme()) return;
    if (!appleTtsSource_ || !applePrewarmer_) return;
    if (!voiceId.empty()) appleTtsSource_->setVoice(voiceId);
    applePrewarmer_->enqueue(text);
}

int PluginProcessor::tryInstallSayText(const std::string& text) {
    if (text.empty()) return -1;

    // Phoneme-stepped scenes: build the phoneme-aligned clip synchronously
    // via Piper. This blocks the message thread for ~300-800 ms (Piper
    // subprocess), which is acceptable for a user-triggered Say action.
    if (activeSceneIsPhoneme()
        && piperTtsSource_ && phonemeExtractor_) {
        audio::PhonemeAlignedClipBuilder builder(
            piperTtsSource_.get(), phonemeExtractor_.get());
        auto phonClip = builder.build(text);
        if (!phonClip || phonClip->samples.empty()) return -1;
        currentTtsClipKey_.clear();
        currentSayText_ = text;
        graph_.setActiveSpeechPlayer(
            audio::AudioGraph::ActiveSpeechPlayer::PhonemeStepped);
        graph_.phonemeSteppedPlayer().setClip(phonClip);
        // Say is one-shot, like the existing v1 Say flow.
        graph_.phonemeSteppedPlayer().setLoop(false);
        lastPhonemeClip_ = phonClip;
        graph_.setModulatorSource(
            audio::AudioGraph::ModulatorSource::NoteStepped);
        graph_.ttsClipPlayer().setClip(nullptr);
        graph_.noteSteppedPlayer().setClip(phonClip);   // safety; inactive
        return 1;
    }

    if (!applePrewarmer_) return -1;
    if (!applePrewarmer_->isCached(text)) return 0;
    auto clip = applePrewarmer_->takeIfReady(text);
    if (!clip) return -1;
    // Bypass currentTtsClipKey_ tracking — this is an ad-hoc overlay, not a
    // scene-driven clip. The next scene change will replace it normally.
    currentTtsClipKey_.clear();
    // Remember the typed text so the WordReadout shows it instead of the
    // scene's default tts.text. Scene-change clears this.
    currentSayText_ = text;

    // If the active scene is note-triggered (per-pluck word advance), run
    // the same segmentation pipeline scene-activation uses so the typed
    // text plays word-by-word. Otherwise fall back to whole-clip playback.
    const auto cfg = sceneEngine_.activeTtsConfig();
    if (cfg.trigger == "note" && !clip->samples.empty()) {
        auto seg = std::make_shared<audio::TTSClip>(*clip);
        std::vector<std::string> plainWords;
        std::vector<std::string> hyphenatedWords;
        {
            std::istringstream iss(text);
            std::string token;
            while (iss >> token) {
                hyphenatedWords.push_back(token);
                std::string plain;
                for (char c : token) if (c != '-') plain += c;
                plainWords.push_back(plain);
            }
        }
        if (!plainWords.empty())
            seg->words = audio::WordAligner::align(seg->samples, plainWords,
                                                   seg->sampleRate);
        const bool anyHyphen = text.find('-') != std::string::npos;
        if (anyHyphen && !plainWords.empty() && seg->syllables.empty())
            seg->syllables = audio::WordAligner::alignSyllables(
                seg->samples, plainWords, hyphenatedWords, seg->sampleRate);
        graph_.noteSteppedPlayer().setClip(seg);
        lastV1Clip_ = seg;
        // Say/LLM text is one-shot — disable loop so plucks past the last
        // word are ignored. Scene-activation paths set their own clips and
        // re-enable loop via the existing default (or scene-config later).
        graph_.noteSteppedPlayer().setLoop(false);
        graph_.setModulatorSource(audio::AudioGraph::ModulatorSource::NoteStepped);
        graph_.ttsClipPlayer().setClip(nullptr);
    } else {
        graph_.setModulatorSource(audio::AudioGraph::ModulatorSource::Linear);
        graph_.ttsClipPlayer().setClip(clip);
    }
    return 1;
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto inputs  = layouts.getMainInputChannelSet();
    const auto outputs = layouts.getMainOutputChannelSet();
    if (inputs.isDisabled() || outputs.isDisabled()) return false;

    const bool inputOk = (inputs  == juce::AudioChannelSet::mono()
                       || inputs  == juce::AudioChannelSet::stereo());
    const bool outputOk = (outputs == juce::AudioChannelSet::mono()
                        || outputs == juce::AudioChannelSet::stereo());
    if (! inputOk || ! outputOk) return false;

    // Optional sidechain mic input bus: disabled, mono, or stereo (we'll downmix).
    if (layouts.inputBuses.size() >= 2) {
        const auto sc = layouts.inputBuses[1];
        if (! sc.isDisabled()
            && sc != juce::AudioChannelSet::mono()
            && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return true;
}

bool PluginProcessor::micBusIsActive() const noexcept {
    if (getBusCount(/*isInput=*/true) < 2) return false;
    return getChannelCountOfBus(/*isInput=*/true, /*busIndex=*/1) > 0;
}

void PluginProcessor::onLlmResponse(const std::string& text) {
    if (text.empty()) return;
    // Synthesize via Apple TTS in the background.
    enqueueSayText(text);
    // Hand off to SayPanel's timer to populate the input field and
    // auto-trigger the Say flow — that installs the clip via
    // tryInstallSayText, which (because Scene 4 has trigger=note) routes
    // through WordAligner + noteSteppedPlayer so the user can pluck
    // through the reply word-by-word.
    std::lock_guard lk(pendingAutoSayMutex_);
    pendingAutoSay_ = text;
}

std::string PluginProcessor::takePendingAutoSay() {
    std::lock_guard lk(pendingAutoSayMutex_);
    auto r = std::move(pendingAutoSay_);
    pendingAutoSay_.clear();
    return r;
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    // Plugin host MIDI -> pending scene (applied on the message thread by
    // HostMidiPoller). Lock-free; safe on the audio thread.
    if (wrapperType != wrapperType_Standalone) {
        const int s = midi::sceneFromMidiBuffer(midiMessages, midiMapping_);
        if (s >= 0) pendingHostScene_.store(s, std::memory_order_release);
    }

    if (wrapperType != wrapperType_Standalone) {
        if (midi::pitchSingingToggleFromMidiBuffer(midiMessages, midiMapping_))
            pendingPitchSingingToggle_.store(true, std::memory_order_release);
    }

    juce::ScopedNoDenormals noDenormals;

    const auto sceneParams = sceneEngine_.currentMixerParams();
    graph_.mixer().setMasterGainDb(sceneParams.masterGainDb);
    graph_.mixer().setDryWet(sceneParams.dryWet);

    // Check if the active scene id changed. If so, hop to the message
    // thread to (a) re-check (scene may have moved again), (b) read the
    // scene's TTS key, and (c) load + setClip the corresponding clip.
    // This keeps the audio thread allocation-free; the actual file load
    // happens on the message thread.
    const int activeSceneId = sceneEngine_.getActiveSceneId();
    if (activeSceneId != lastSeenSceneId_) {
        lastSeenSceneId_ = activeSceneId;
        // Any pending Say overlay is per-scene — clear it so the new scene's
        // default text wins.
        currentSayText_.clear();
        juce::MessageManager::callAsync([this, activeSceneId] {
            if (sceneEngine_.getActiveSceneId() != activeSceneId) return;

            // Gspeak auto-load: if the scene declares a .gspeak bundle with
            // gspeakAutoLoad=true and we can read it, install the clip and
            // skip the normal TTS source dispatch.
            if (tryAutoLoadGspeak_(sceneEngine_.getActiveScene())) return;

            // Carousel branch selection + config push (message thread).
            const auto carouselCfg = sceneEngine_.activeCarouselConfig();
            graph_.carousel().setConfig(carouselCfg);
            log::info(juce::String("wetSource -> ")
                      + (carouselCfg.enabled ? "Carousel" : "Vocoder")
                      + " (scene-change callAsync)");
            graph_.setWetSource(carouselCfg.enabled
                ? audio::AudioGraph::WetSource::Carousel
                : audio::AudioGraph::WetSource::Vocoder);
            if (carouselCfg.enabled) {
                // Instrument scene: no TTS clip plays under it. Clear both TTS
                // players + revert the modulator so a prior note-triggered scene
                // can't keep speaking.
                currentTtsClipKey_.clear();
                graph_.setActiveSpeechPlayer(
                    audio::AudioGraph::ActiveSpeechPlayer::NoteStepped);
                graph_.setModulatorSource(audio::AudioGraph::ModulatorSource::Linear);
                graph_.ttsClipPlayer().setClip(nullptr);
                graph_.noteSteppedPlayer().setClip(nullptr);
                graph_.phonemeSteppedPlayer().setClip(nullptr);
                lastPhonemeClip_.reset();
                return;
            }

            const auto cfg = sceneEngine_.activeTtsConfig();

            // Per-scene "speak-clearly" blend. Applied on every scene change
            // (even if the TTS clip key is unchanged) so toggling scenes always
            // refreshes the clarity setting.
            graph_.setClarity(cfg.clarity);

            // Per-scene word-sync mode override. Applied on every scene change
            // so a scene's declared wordSync always wins over the UI selection.
            // "global" (or unrecognized): leave whatever the UI selected in place.
            if (cfg.wordSync == "latch")
                graph_.setWordSyncMode(audio::WordSyncMode::Latch);
            else if (cfg.wordSync == "advance")
                graph_.setWordSyncMode(audio::WordSyncMode::Advance);
            else if (cfg.wordSync == "syllable")
                graph_.setWordSyncMode(audio::WordSyncMode::Syllable);

            // -------------------------------------------------------------
            // Mic source (Phase B — Mic Talkbox, Scene 3)
            // -------------------------------------------------------------
            // No clip to load — the mic stream becomes the modulator. Clear
            // the linear / note-stepped / clipBank players so a prior scene's
            // state doesn't bleed into this scene's audio.
            if (cfg.source == "mic") {
                static const std::string kMicKey = "mic:";
                if (currentTtsClipKey_ == kMicKey) return;
                currentTtsClipKey_ = kMicKey;

                graph_.setActiveSpeechPlayer(
                    audio::AudioGraph::ActiveSpeechPlayer::NoteStepped);
                graph_.setModulatorSource(audio::AudioGraph::ModulatorSource::Mic);
                graph_.ttsClipPlayer().setClip(nullptr);
                graph_.noteSteppedPlayer().setClip(nullptr);
                graph_.phonemeSteppedPlayer().setClip(nullptr);
                lastPhonemeClip_.reset();
                graph_.clipBankPlayer().setBank({});
                lastResolvedSource_.store(0, std::memory_order_relaxed);  // "none"
                return;
            }

            // -------------------------------------------------------------
            // Clip-bank source (Phase A — Vocal Guitar, Scene 2)
            // -------------------------------------------------------------
            // Distinct from "prebaked": the unit of playback is the BANK,
            // not a single clip. Each onset advances to the next clip.
            // Skip the synthesizeWithFallback chain and the
            // currentTtsClipKey_ short-circuit entirely.
            if (cfg.source == "clipBank") {
                // Build a stable key from the bank contents so a re-entry
                // to the same scene with an unchanged bank short-circuits.
                std::string bankKey = "clipBank:";
                for (const auto& k : cfg.bank) { bankKey += k; bankKey += '|'; }
                if (bankKey == currentTtsClipKey_) return;
                currentTtsClipKey_ = bankKey;

                std::vector<audio::TTSClipPtr> clips;
                clips.reserve(cfg.bank.size());
                if (vocalGuitarSource_) {
                    for (const auto& clipKey : cfg.bank) {
                        auto clip = vocalGuitarSource_->synthesize(clipKey);
                        if (clip && !clip->samples.empty())
                            clips.push_back(std::move(clip));
                    }
                }

                graph_.clipBankPlayer().setBank(std::move(clips));
                graph_.clipBankPlayer().rewind();  // start at clip 0 on entry
                graph_.setActiveSpeechPlayer(
                    audio::AudioGraph::ActiveSpeechPlayer::NoteStepped);
                graph_.setModulatorSource(audio::AudioGraph::ModulatorSource::ClipBank);
                graph_.ttsClipPlayer().setClip(nullptr);
                graph_.noteSteppedPlayer().setClip(nullptr);
                graph_.phonemeSteppedPlayer().setClip(nullptr);
                lastPhonemeClip_.reset();
                lastResolvedSource_.store(1, std::memory_order_relaxed);  // "prebaked-ish"
                return;
            }

            // Build a per-source key (matches what synthesize() expects).
            std::string key;
            if (cfg.source == "prebaked") key = cfg.clip;
            else                          key = cfg.text;  // live sources (apple, piper)

            if (key == currentTtsClipKey_) return;
            currentTtsClipKey_ = key;

            if (key.empty()) {
                // No-TTS vocoder scene (e.g. clean), OR a phoneme-stepped scene
                // whose text hasn't been set yet (e.g. Conversation scene before
                // the first LLM reply). In both cases silence the clip players +
                // revert the modulator.
                // Pre-emptively set the active player based on the scene's
                // speech.player declaration so UI affordances (Ph pill,
                // WaveformView, syllable readout) reflect intent immediately —
                // even before a clip has been built.
                const auto speechCfgEmpty = sceneEngine_.getActiveScene().speech;
                const bool usePhonemeEmpty =
                    (speechCfgEmpty.player == scenes::Scene::Speech::Player::PhonemeStepped);
                if (usePhonemeEmpty) {
                    graph_.setActiveSpeechPlayer(
                        audio::AudioGraph::ActiveSpeechPlayer::PhonemeStepped);
                    graph_.phonemeSteppedPlayer().setMaxSustainMs(speechCfgEmpty.maxSustainMs);
                } else {
                    graph_.setActiveSpeechPlayer(
                        audio::AudioGraph::ActiveSpeechPlayer::NoteStepped);
                }
                graph_.setModulatorSource(audio::AudioGraph::ModulatorSource::Linear);
                graph_.ttsClipPlayer().setClip(nullptr);
                graph_.noteSteppedPlayer().setClip(nullptr);
                graph_.phonemeSteppedPlayer().setClip(nullptr);
                lastPhonemeClip_.reset();
                return;
            }

            // Build a registry of sources for the chain helper.
            audio::TTSSourceRegistry registry;
            if (prebakedTtsSource_) registry["prebaked"] = prebakedTtsSource_.get();
            if (appleTtsSource_)    registry["apple"]    = appleTtsSource_.get();
            if (piperTtsSource_)    registry["piper"]    = piperTtsSource_.get();

            // keyFor: prebaked uses cfg.clip; live sources use cfg.text.
            auto keyFor = [&cfg](const std::string& sourceName) -> std::string {
                if (sourceName == "prebaked") return cfg.clip;
                return cfg.text;  // apple, piper, anything else
            };

            // Set voice on apple before synthesis (no-op if other sources are picked).
            if (!cfg.voice.empty() && appleTtsSource_) {
                appleTtsSource_->setVoice(cfg.voice);
            }

            // Prefer the prewarmer cache for live sources when the primary is apple
            // or piper — falls through to synthesizeWithFallback on cache miss.
            audio::TTSClipPtr clip;
            if (cfg.source == "apple" && applePrewarmer_ && applePrewarmer_->isCached(cfg.text)) {
                clip = applePrewarmer_->takeIfReady(cfg.text);
            } else if (cfg.source == "piper" && piperPrewarmer_ && piperPrewarmer_->isCached(cfg.text)) {
                clip = piperPrewarmer_->takeIfReady(cfg.text);
            } else {
                clip = audio::synthesizeWithFallback(cfg, registry, keyFor);
            }

            // If the prewarmer cache held a *failure* (nullptr) — e.g. a live
            // engine like Piper isn't installed — the branches above yield no
            // clip and would leave the scene silent. Walk to the scene's
            // declared fallback source directly so it still speaks. We do NOT
            // re-invoke the live primary here (the prewarmer already tried it),
            // and we never synthesize apple on the message thread (that would
            // deadlock AVSpeechSynthesizer). The fallback is normally a
            // prebaked file load, which is message-thread-safe.
            if (!clip && !cfg.fallback.empty() && cfg.fallback != "apple") {
                if (auto it = registry.find(cfg.fallback);
                        it != registry.end() && it->second) {
                    clip = it->second->synthesize(keyFor(cfg.fallback));
                }
            }

            // Record the source that actually produced the clip (for the status
            // readout). If piper was requested but isn't ready, the fallback
            // source produced it; otherwise it's cfg.source. nullptr -> none.
            int resolved = 0;
            if (clip) {
                const std::string& s = (cfg.source == "piper" && !piperReady())
                    ? cfg.fallback : cfg.source;
                if      (s == "prebaked") resolved = 1;
                else if (s == "apple")    resolved = 2;
                else if (s == "piper")    resolved = 3;
            }
            lastResolvedSource_.store(resolved, std::memory_order_relaxed);

            // Read the active scene's speech config to pick v1 vs v2 player.
            const auto speechCfg = sceneEngine_.getActiveScene().speech;
            const bool usePhoneme =
                (speechCfg.player == scenes::Scene::Speech::Player::PhonemeStepped);

            // Pre-emptively switch the active player so UI affordances (Ph pill,
            // WaveformView, syllable readout) reflect the scene's intent even
            // before a clip is built. The clip-building branches below may
            // rebuild this or fall back; in the empty-text case (e.g. the
            // Conversation scene where the LLM hasn't replied yet), this is the
            // only place it gets set (the key.empty() branch above handles the
            // truly-empty-text path, so we arrive here only when cfg.text is
            // non-empty; still, set it unconditionally for robustness).
            if (usePhoneme) {
                graph_.setActiveSpeechPlayer(
                    audio::AudioGraph::ActiveSpeechPlayer::PhonemeStepped);
                graph_.phonemeSteppedPlayer().setMaxSustainMs(speechCfg.maxSustainMs);
            } else {
                graph_.setActiveSpeechPlayer(
                    audio::AudioGraph::ActiveSpeechPlayer::NoteStepped);
            }

            // Route the clip per the scene's tts.trigger and speech.player.
            // "note" + NoteStepped (v1) → word-per-pluck (unchanged v1 path).
            // PhonemeStepped (v2) → phoneme-aligned syllable-per-onset path.
            // Anything else (default "auto") → linear whole-clip playback.
            if (usePhoneme && piperTtsSource_ && phonemeExtractor_
                    && !cfg.text.empty()) {
                // Build a phoneme-aligned clip via PhonemeAlignedClipBuilder.
                // This re-synthesizes via Piper + espeak-ng even if a plain clip
                // was already fetched above, because the prewarmer cache holds
                // plain clips (no phoneme timing data). Ideally the prewarmer
                // would use PhonemeAlignedClipBuilder directly — that's a future
                // optimisation; for now re-synthesize is acceptable (same Piper
                // subprocess, ~300-800 ms, happens on the message thread).
                audio::PhonemeAlignedClipBuilder builder(
                    piperTtsSource_.get(), phonemeExtractor_.get());
                auto phonClip = builder.build(cfg.text);
                if (phonClip && !phonClip->samples.empty()) {
                    // setActiveSpeechPlayer + setMaxSustainMs already called
                    // in the pre-emptive block above; just install the clip.
                    graph_.phonemeSteppedPlayer().setClip(phonClip);
                    lastPhonemeClip_ = phonClip;
                    graph_.phonemeSteppedPlayer().setLoop(true);
                    // Also push to v1 player so it has a clip in case the selector
                    // ever reverts mid-scene (safe — inactive player ignores it).
                    graph_.noteSteppedPlayer().setClip(phonClip);
                    graph_.setModulatorSource(
                        audio::AudioGraph::ModulatorSource::NoteStepped);
                    graph_.ttsClipPlayer().setClip(nullptr);
                } else {
                    // PhonemeAlignedClipBuilder failed (e.g. Piper not installed).
                    // Fall back to linear playback of whatever clip we have.
                    graph_.setActiveSpeechPlayer(
                        audio::AudioGraph::ActiveSpeechPlayer::NoteStepped);
                    graph_.setModulatorSource(
                        audio::AudioGraph::ModulatorSource::Linear);
                    graph_.ttsClipPlayer().setClip(clip);
                    graph_.noteSteppedPlayer().setClip(nullptr);
                }
            } else if (cfg.trigger == "note" && clip && !clip->samples.empty()) {
                // v1 word-per-pluck path (unchanged).
                // WordAligner needs a mutable copy (TTSClipPtr is shared<const>).
                auto seg = std::make_shared<audio::TTSClip>(*clip);
                // Build per-word hyphenated + plain lists. clip->words labels
                // must never contain hyphens; clip->syllables is populated only
                // when the source text actually contains hyphens (deterministic
                // — no automatic syllabification of unhyphenated text).
                const std::string& sourceText =
                    cfg.text.empty() ? clip->name : cfg.text;
                std::vector<std::string> plainWords;
                std::vector<std::string> hyphenatedWords;
                {
                    std::istringstream iss(sourceText);
                    std::string token;
                    while (iss >> token) {
                        hyphenatedWords.push_back(token);
                        std::string plain;
                        for (char c : token) if (c != '-') plain += c;
                        plainWords.push_back(plain);
                    }
                }
                if (!plainWords.empty())
                    seg->words = audio::WordAligner::align(seg->samples, plainWords,
                                                           seg->sampleRate);
                const bool anyHyphen = sourceText.find('-') != std::string::npos;
                // PrebakedTTSSource may have already populated `syllables`
                // from a hand-authored `syllableTimingsMs` in the clip's
                // meta.json. Those override the energy-gap heuristic.
                if (anyHyphen && !plainWords.empty() && seg->syllables.empty())
                    seg->syllables = audio::WordAligner::alignSyllables(
                        seg->samples, plainWords, hyphenatedWords, seg->sampleRate);
                graph_.setActiveSpeechPlayer(
                    audio::AudioGraph::ActiveSpeechPlayer::NoteStepped);
                graph_.noteSteppedPlayer().setClip(seg);
                lastV1Clip_ = seg;
                // Scene-activation installs default to loop=true so chant
                // scenes (e.g. Developers!) repeat. tryInstallSayText flips
                // this to false for one-shot Say / LLM replies.
                graph_.noteSteppedPlayer().setLoop(true);
                graph_.setModulatorSource(audio::AudioGraph::ModulatorSource::NoteStepped);
                graph_.ttsClipPlayer().setClip(nullptr);
            } else {
                graph_.setActiveSpeechPlayer(
                    audio::AudioGraph::ActiveSpeechPlayer::NoteStepped);
                graph_.setModulatorSource(audio::AudioGraph::ModulatorSource::Linear);
                graph_.ttsClipPlayer().setClip(clip);  // nullptr is OK (silences)
            }
        });
    }

    const int totalOut = getTotalNumOutputChannels();
    const int totalIn = getTotalNumInputChannels();
    if (totalOut == 0) return;

    lastInputChannels_.store(totalIn, std::memory_order_relaxed);
    lastOutputChannels_.store(totalOut, std::memory_order_relaxed);

    // Never allocate on the audio thread. If the host sends a larger
    // block than declared in prepareToPlay, process only what fits in
    // the pre-sized scratch; tail samples are left as-is.
    const int numSamples = std::min(buffer.getNumSamples(),
                                    static_cast<int>(monoScratch_.size()));

    // ------------------------------------------------------------------
    // Decide mic + guitar routing once, then extract both before
    // graph_.process(). Three cases:
    //   1. AU plugin with sidechain ENABLED → bus 1 = mic, bus 0 = guitar
    //      (mono or stereo, downmixed if stereo).
    //   2. Standalone with 2 input channels on bus 0 (e.g., Scarlett with
    //      "Input 1+2" active) → bus 0 ch 0 = guitar, bus 0 ch 1 = mic.
    //   3. Single input channel (bus 0 mono) → mic == guitar (self-mod).
    // ------------------------------------------------------------------
    const bool hasSidechainMic = (getBusCount(/*isInput=*/true) >= 2
                                  && getChannelCountOfBus(true, 1) > 0);
    auto bus0 = getBusBuffer(buffer, /*isInput=*/true, /*busIdx=*/0);
    const int bus0Channels = bus0.getNumChannels();
    // Only standalone treats bus 0 ch 1 as the mic. In AU/AAX/VST3 hosts,
    // both bus 0 channels are guitar (e.g., a stereo guitar track in Logic).
    const bool standaloneStereo = (wrapperType == wrapperType_Standalone)
                                  && !hasSidechainMic
                                  && bus0Channels >= 2;

    // --- Mic extraction --------------------------------------------------
    {
        const float* micPtr = nullptr;
        int           micLen = 0;
        constexpr int kMaxBlock = 8192;
        float         micTmp[kMaxBlock];
        int           micSourceTag = 0;  // 0=none, 1=sidechain, 2=ch2, 3=self-mod

        if (hasSidechainMic) {
            auto micBus = getBusBuffer(buffer, /*isInput=*/true, /*busIdx=*/1);
            const int n = std::min(micBus.getNumSamples(), kMaxBlock);
            if (micBus.getNumChannels() == 1) {
                micPtr = micBus.getReadPointer(0);
                micLen = n;
            } else if (micBus.getNumChannels() >= 2) {
                const float* L = micBus.getReadPointer(0);
                const float* R = micBus.getReadPointer(1);
                for (int i = 0; i < n; ++i) micTmp[i] = 0.5f * (L[i] + R[i]);
                micPtr = micTmp;
                micLen = n;
            }
            micSourceTag = 1;
        } else if (standaloneStereo) {
            // Standalone stereo input: channel 1 is the mic.
            micPtr = bus0.getReadPointer(1);
            micLen = bus0.getNumSamples();
            micSourceTag = 2;
        } else if (bus0Channels >= 1) {
            // Single-channel input: self-modulation fallback.
            micPtr = bus0.getReadPointer(0);
            micLen = bus0.getNumSamples();
            micSourceTag = 3;
        }

        micRoutingSource_.store(micSourceTag, std::memory_order_relaxed);

        if (micPtr != nullptr && micLen > 0) {
            // Apply the user-tunable mic capture gain (default 4x = +12 dB)
            // so quiet mic interfaces still drive whisper and the meter
            // reliably. Clamp into [-1, 1] so we don't feed clipped samples
            // into the vocoder modulator path. Same boosted buffer is then
            // used for both the meter (via setMicBlock) and recording.
            const float gain = micCaptureGain_.load(std::memory_order_relaxed);
            float boostedBuf[kMaxBlock];
            const int boostN = std::min(micLen, kMaxBlock);
            for (int i = 0; i < boostN; ++i) {
                const float s = micPtr[i] * gain;
                boostedBuf[i] = std::clamp(s, -1.0f, 1.0f);
            }
            graph_.setMicBlock(boostedBuf, static_cast<std::size_t>(boostN));
            micScope_.push(boostedBuf, static_cast<std::size_t>(boostN));
            if (micCapture_.isCapturing())
                micCapture_.appendFromAudioBlock(boostedBuf, boostN);
        } else {
            graph_.setMicBlock(nullptr, 0);
        }
    }

    // --- Guitar carrier --------------------------------------------------
    // The guitar carrier always comes from bus 0. We downmix to mono only
    // when bus 0 is genuinely stereo guitar (AU plugin sidechain case);
    // when bus 0 channel 1 is the mic (standalone stereo case), we use
    // channel 0 only so the mic doesn't contaminate the carrier.
    float inPeak = 0.0f;
    if (bus0Channels == 0) {
        std::fill_n(monoScratch_.begin(), numSamples, 0.0f);
    } else if (hasSidechainMic && bus0Channels >= 2) {
        // AU with stereo guitar bus: downmix L+R.
        const float* l = bus0.getReadPointer(0);
        const float* r = bus0.getReadPointer(1);
        for (int i = 0; i < numSamples; ++i) {
            const float m = 0.5f * (l[i] + r[i]);
            monoScratch_[static_cast<std::size_t>(i)] = m;
            const float a = std::abs(m);
            if (a > inPeak) inPeak = a;
        }
        graph_.process(monoScratch_.data(), monoScratch_.data(),
                       static_cast<std::size_t>(numSamples));
    } else {
        // Standalone stereo (ch 0 = guitar), or any single-channel bus 0.
        const float* in = bus0.getReadPointer(0);
        for (int i = 0; i < numSamples; ++i) {
            const float a = std::abs(in[i]);
            if (a > inPeak) inPeak = a;
        }
        graph_.process(in, monoScratch_.data(),
                       static_cast<std::size_t>(numSamples));
    }

    // Output peak from the post-DSP mono scratch.
    float outPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        const float a = std::abs(monoScratch_[static_cast<std::size_t>(i)]);
        if (a > outPeak) outPeak = a;
    }

    inputPeak_.store(inPeak, std::memory_order_relaxed);
    outputPeak_.store(outPeak, std::memory_order_relaxed);
    gateGain_.store(graph_.input().currentGateGain(), std::memory_order_relaxed);

    // Write post-DSP mono samples into the visualization ring buffer.
    // Bitmask wraparound (kAudioRingSize is power-of-two).
    constexpr int mask = kAudioRingSize - 1;
    int idx = audioRingWriteIdx_.load(std::memory_order_relaxed);
    for (int i = 0; i < numSamples; ++i) {
        audioRing_[static_cast<std::size_t>(idx & mask)] = monoScratch_[static_cast<std::size_t>(i)];
        ++idx;
    }
    audioRingWriteIdx_.store(idx & mask, std::memory_order_release);

    // Voice-pack swap fade: triangular dip across 50 ms masks the bundle
    // handover transient. Runs on monoScratch_ before fan-out to channels.
    if (voicePackSwapFadeArmed_.exchange(false, std::memory_order_acquire)) {
        voicePackSwapFadeCounter_ =
            voicePackSwapFadeSamples_.load(std::memory_order_relaxed);
        voicePackSwapFadeTotal_ = voicePackSwapFadeCounter_;  // captured once
    }
    if (voicePackSwapFadeCounter_ > 0) {
        const int n = numSamples;
        // Use the total captured at arm-time so the gain denominator is stable
        // across block boundaries — produces a true triangle 1→0→1 over the
        // full 50 ms window rather than a per-block sawtooth.
        const int total = std::max(1, voicePackSwapFadeTotal_);
        auto* w = monoScratch_.data();
        for (int i = 0; i < n && voicePackSwapFadeCounter_ > 0; ++i) {
            // Symmetric: ramp DOWN for the first half, UP for the second.
            const float halfPos = (total - voicePackSwapFadeCounter_)
                                / static_cast<float>(total);
            const float gain = halfPos < 0.5f
                ? 1.0f - 2.0f * halfPos
                : 2.0f * (halfPos - 0.5f);
            w[i] *= gain;
            --voicePackSwapFadeCounter_;
        }
    }

    // Fan the mono graph output to all output channels.
    for (int ch = 0; ch < totalOut; ++ch) {
        float* out = buffer.getWritePointer(ch);
        std::memcpy(out, monoScratch_.data(), sizeof(float) * static_cast<std::size_t>(numSamples));
    }

}

void PluginProcessor::snapshotRecentSamples(float* dest, int count) const noexcept {
    constexpr int N = kAudioRingSize;
    const int safeCount = std::min(count, N);
    const int writeIdx = audioRingWriteIdx_.load(std::memory_order_acquire);
    const int startIdx = (writeIdx - safeCount + N) % N;
    for (int i = 0; i < safeCount; ++i) {
        dest[i] = audioRing_[static_cast<std::size_t>((startIdx + i) % N)];
    }
    for (int i = safeCount; i < count; ++i) dest[i] = 0.0f;
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() {
    return new PluginEditor(*this);
}

// getStateInformation / setStateInformation: called by the AU host (Logic) to
// save/restore the plugin's state inside a project, AND by JUCE's
// StandalonePluginHolder to persist the standalone's last UI state across
// launches via its juce::PropertiesFile. One code path covers both formats.
void PluginProcessor::getStateInformation(juce::MemoryBlock& dest) {
    app::PluginStateData d;
    d.sceneId      = sceneEngine_.getActiveSceneId();
    d.makeup       = graph_.vocoderMakeup();
    d.carrierNoise = graph_.vocoderCarrierNoise();
    d.sibilance    = graph_.vocoderSibilance();
    d.gateThresholdDb = graph_.noiseGateThresholdDb();
    d.micCaptureGain  = micCaptureGain();
    d.pitchSinging = graph_.pitchSinging();
    d.singing = graph_.singing();
    d.wordSyncMode = static_cast<int>(graph_.wordSyncMode());
    // clarity intentionally not persisted — it's per-scene. See PluginState.h.

    d.selectedModelId = selectedModelId_;
    d.personaId       = currentPersonaId_;

    d.activeVoiceIndexByScene = stateData_.activeVoiceIndexByScene;
    d.sungVowelMask            = sungVowelMask();
    d.limiterEnabled           = limiterEnabled();
    d.limiterThresholdDb       = limiterThresholdDb();

    // Snapshot only custom prompts that differ from defaults.
    static constexpr ai::PersonaId kAllPersonas[] = {
        ai::PersonaId::Interviewer, ai::PersonaId::Snarky,
        ai::PersonaId::WeatheredGuitar, ai::PersonaId::StudioEngineer,
        ai::PersonaId::CuriousAi, ai::PersonaId::PlainAssistant,
        ai::PersonaId::SongOldGuitar, ai::PersonaId::SongRockingGuitar,
    };
    for (auto id : kAllPersonas) {
        auto current = personas_.promptFor(id);
        if (current != ai::PersonaRegistry::defaultPromptFor(id))
            d.customPromptByPersona[id] = current;
    }

    const auto json = app::PluginState::toJson(d);
    dest.replaceAll(json.toRawUTF8(), json.getNumBytesAsUTF8());
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes) {
    const juce::String json = juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes);
    const auto d = app::PluginState::fromJson(json);
    stateData_.activeVoiceIndexByScene = d.activeVoiceIndexByScene;
    graph_.setVocoderMakeup(d.makeup);
    graph_.setVocoderCarrierNoise(d.carrierNoise);
    graph_.setVocoderSibilance(d.sibilance);
    graph_.setNoiseGateThresholdDb(d.gateThresholdDb);
    setMicCaptureGain(d.micCaptureGain);
    graph_.setPitchSinging(d.pitchSinging);
    graph_.setSinging(d.singing);
    graph_.setWordSyncMode(static_cast<audio::WordSyncMode>(d.wordSyncMode));
    setSungVowelMask(d.sungVowelMask);
    setLimiterEnabled(d.limiterEnabled);
    setLimiterThresholdDb(d.limiterThresholdDb);
    log::info("scene -> " + juce::String(d.sceneId) + " (setStateInformation)");
    sceneEngine_.activateScene(d.sceneId);

    selectModelId(d.selectedModelId);
    currentPersonaId_ = d.personaId;
    for (auto& [id, prompt] : d.customPromptByPersona)
        personas_.setCustomPrompt(id, prompt);
    if (engine_) engine_->setPersona(currentPersonaId_, "");
}

} // namespace guitar_dsp

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new guitar_dsp::PluginProcessor();
}
