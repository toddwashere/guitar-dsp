#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>

#include <juce_events/juce_events.h>

#include "AssetLocator.h"
#include "audio/TTSSynthChain.h"
#include "scenes/SceneLibrary.h"

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
        if (s >= 0) p_.sceneEngine_.activateScene(s);
    }
private:
    PluginProcessor& p_;
};

PluginProcessor::PluginProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withInput ("Input",  juce::AudioChannelSet::mono(),   true)
        .withInput ("Mic",    juce::AudioChannelSet::mono(),   false)  // sidechain, disabled by default
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
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
                    sceneEngine_.activateScene(cmd->payload);
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

    engine_ = std::make_unique<ai::ConversationEngine>(
        *whisper_, *llm_, micCapture_, convBuf_, personas_,
        [this](std::string text){ enqueueSayText(text); });
}

PluginProcessor::~PluginProcessor() {
    // Mark dead before member destructors run. Any callAsync lambdas
    // already queued by MidiRouter will see this and bail out instead
    // of dereferencing a half-destroyed `this`.
    alive_->store(false, std::memory_order_release);
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    graph_.prepare(sampleRate, samplesPerBlock);
    micCapture_.prepare(sampleRate, 1);
    monoScratch_.assign(static_cast<std::size_t>(samplesPerBlock), 0.0f);

    if (sceneEngine_.getSceneCount() == 0) {
        const auto dir = AssetLocator::scenesDirectory();
        auto scenes = scenes::SceneLibrary::loadDirectory(dir);
        if (scenes.empty()) scenes.push_back(scenes::Scene::defaults(0));
        sceneEngine_.loadScenes(std::move(scenes));
    }

    prebakedTtsSource_ = std::make_unique<audio::PrebakedTTSSource>(
        AssetLocator::ttsDirectory());
    prebakedTtsSource_->prepare(sampleRate);

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

    currentTtsClipKey_.clear();
    lastSeenSceneId_ = -1;
    graph_.ttsClipPlayer().setClip(nullptr);
}

void PluginProcessor::releaseResources() {
    graph_.reset();
}

void PluginProcessor::setMidiPreferredDeviceName(const juce::String& name) {
    if (midiRouter_) midiRouter_->setPreferredDeviceName(name);
}

void PluginProcessor::rebuildLlmClient() {
    if (selectedModelId_.rfind("ollama:", 0) == 0) {
        const auto tag = selectedModelId_.substr(7);
        llm_ = std::make_unique<ai::OllamaClient>(
            http_, prefs_->ollamaEndpoint(), tag);
    } else {
        llm_ = std::make_unique<ai::AnthropicClient>(
            http_, prefs_->anthropicApiKey(), selectedModelId_);
    }
    if (engine_) engine_->setLlmClient(*llm_);
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

bool PluginProcessor::piperReady() const noexcept {
    return piperTtsSource_ && piperTtsSource_->isReady();
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
    std::vector<std::string> words;
    std::istringstream iss(cfg.text);
    std::string w;
    while (iss >> w) words.push_back(w);
    return words;
}

void PluginProcessor::enqueueSayText(const std::string& text,
                                      const std::string& voiceId) {
    if (text.empty() || !appleTtsSource_ || !applePrewarmer_) return;
    if (!voiceId.empty()) appleTtsSource_->setVoice(voiceId);
    applePrewarmer_->enqueue(text);
}

int PluginProcessor::tryInstallSayText(const std::string& text) {
    if (text.empty() || !applePrewarmer_) return -1;
    if (!applePrewarmer_->isCached(text)) return 0;
    auto clip = applePrewarmer_->takeIfReady(text);
    // Bypass currentTtsClipKey_ tracking — this is an ad-hoc overlay, not a
    // scene-driven clip. The next scene change will replace it normally.
    currentTtsClipKey_.clear();
    graph_.ttsClipPlayer().setClip(clip);
    return clip ? 1 : -1;
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

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    // Plugin host MIDI -> pending scene (applied on the message thread by
    // HostMidiPoller). Lock-free; safe on the audio thread.
    if (wrapperType != wrapperType_Standalone) {
        const int s = midi::sceneFromMidiBuffer(midiMessages, midiMapping_);
        if (s >= 0) pendingHostScene_.store(s, std::memory_order_release);
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
        juce::MessageManager::callAsync([this, activeSceneId] {
            if (sceneEngine_.getActiveSceneId() != activeSceneId) return;

            // Carousel branch selection + config push (message thread).
            const auto carouselCfg = sceneEngine_.activeCarouselConfig();
            graph_.carousel().setConfig(carouselCfg);
            graph_.setWetSource(carouselCfg.enabled
                ? audio::AudioGraph::WetSource::Carousel
                : audio::AudioGraph::WetSource::Vocoder);
            if (carouselCfg.enabled) {
                // Instrument scene: no TTS clip plays under it. Clear both TTS
                // players + revert the modulator so a prior note-triggered scene
                // can't keep speaking.
                currentTtsClipKey_.clear();
                graph_.setModulatorSource(audio::AudioGraph::ModulatorSource::Linear);
                graph_.ttsClipPlayer().setClip(nullptr);
                graph_.noteSteppedPlayer().setClip(nullptr);
                return;
            }

            const auto cfg = sceneEngine_.activeTtsConfig();

            // Per-scene "speak-clearly" blend. Applied on every scene change
            // (even if the TTS clip key is unchanged) so toggling scenes always
            // refreshes the clarity setting.
            graph_.setClarity(cfg.clarity);

            // Build a per-source key (matches what synthesize() expects).
            std::string key;
            if (cfg.source == "prebaked") key = cfg.clip;
            else                          key = cfg.text;  // live sources (apple, piper)

            if (key == currentTtsClipKey_) return;
            currentTtsClipKey_ = key;

            if (key.empty()) {
                // No-TTS vocoder scene (e.g. clean): silence both players +
                // revert the modulator so a prior note-triggered scene stops.
                graph_.setModulatorSource(audio::AudioGraph::ModulatorSource::Linear);
                graph_.ttsClipPlayer().setClip(nullptr);
                graph_.noteSteppedPlayer().setClip(nullptr);
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

            // Route the clip per the scene's tts.trigger. "note" → segment into
            // words and feed the note-stepped player (word-per-pluck). Anything
            // else (default "auto") → linear whole-clip playback.
            if (cfg.trigger == "note" && clip && !clip->samples.empty()) {
                // WordAligner needs a mutable copy (TTSClipPtr is shared<const>).
                auto seg = std::make_shared<audio::TTSClip>(*clip);
                std::vector<std::string> words;
                {
                    std::istringstream iss(cfg.text.empty() ? clip->name : cfg.text);
                    std::string w;
                    while (iss >> w) words.push_back(w);
                }
                if (!words.empty())
                    seg->words = audio::WordAligner::align(seg->samples, words,
                                                           seg->sampleRate);
                graph_.noteSteppedPlayer().setClip(seg);
                graph_.setModulatorSource(audio::AudioGraph::ModulatorSource::NoteStepped);
                graph_.ttsClipPlayer().setClip(nullptr);
            } else {
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

    // Build a mono input — downmixing stereo by averaging — so the DSP graph
    // sees a single channel regardless of how the host configures the bus.
    float inPeak = 0.0f;
    if (totalIn >= 2) {
        const float* l = buffer.getReadPointer(0);
        const float* r = buffer.getReadPointer(1);
        for (int i = 0; i < numSamples; ++i) {
            const float m = 0.5f * (l[i] + r[i]);
            monoScratch_[static_cast<std::size_t>(i)] = m;
            const float a = std::abs(m);
            if (a > inPeak) inPeak = a;
        }
        graph_.process(monoScratch_.data(), monoScratch_.data(),
                       static_cast<std::size_t>(numSamples));
    } else if (totalIn == 1) {
        const float* in = buffer.getReadPointer(0);
        for (int i = 0; i < numSamples; ++i) {
            const float a = std::abs(in[i]);
            if (a > inPeak) inPeak = a;
        }
        graph_.process(in, monoScratch_.data(),
                       static_cast<std::size_t>(numSamples));
    } else {
        std::fill_n(monoScratch_.begin(), numSamples, 0.0f);
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

    // Fan the mono graph output to all output channels.
    for (int ch = 0; ch < totalOut; ++ch) {
        float* out = buffer.getWritePointer(ch);
        std::memcpy(out, monoScratch_.data(), sizeof(float) * static_cast<std::size_t>(numSamples));
    }

    // MicCapture sidechain routing (RT-safe, allocation-free).
    // In AU: bus 1 carries the sidechain mic; in standalone, bus 1 is absent so
    // we fall back to bus 0 (the user routes their mic to the single input device
    // selected in the JUCE standalone host).
    if (micCapture_.isCapturing()) {
        if (getBusCount(/*isInput=*/true) >= 2 && getChannelCountOfBus(true, 1) > 0) {
            auto micBus = getBusBuffer(buffer, /*isInput=*/true, /*busIdx=*/1);
            if (micBus.getNumChannels() == 1) {
                micCapture_.appendFromAudioBlock(
                    micBus.getReadPointer(0), micBus.getNumSamples());
            } else if (micBus.getNumChannels() >= 2) {
                // Downmix stereo sidechain to mono using a stack buffer (no malloc).
                constexpr int kMaxBlock = 8192;
                float tmp[kMaxBlock];
                const int n = std::min(micBus.getNumSamples(), kMaxBlock);
                const float* L = micBus.getReadPointer(0);
                const float* R = micBus.getReadPointer(1);
                for (int i = 0; i < n; ++i) tmp[i] = 0.5f * (L[i] + R[i]);
                micCapture_.appendFromAudioBlock(tmp, n);
            }
        } else {
            // Standalone (no sidechain): use main input as the mic source.
            // Note: in standalone, the user picks an input device in the JUCE
            // standalone host — that device feeds bus 0. So in standalone the
            // user routes their mic to that single input. (Trade-off: in
            // standalone, mic and guitar can't be on separate channels.)
            auto mainBus = getBusBuffer(buffer, /*isInput=*/true, /*busIdx=*/0);
            if (mainBus.getNumChannels() >= 1) {
                micCapture_.appendFromAudioBlock(
                    mainBus.getReadPointer(0), mainBus.getNumSamples());
            }
        }
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
    // clarity intentionally not persisted — it's per-scene. See PluginState.h.

    d.selectedModelId = selectedModelId_;
    d.personaId       = currentPersonaId_;

    // Snapshot only custom prompts that differ from defaults.
    static constexpr ai::PersonaId kAllPersonas[] = {
        ai::PersonaId::Interviewer, ai::PersonaId::Snarky,
        ai::PersonaId::WeatheredGuitar, ai::PersonaId::StudioEngineer,
        ai::PersonaId::CuriousAi, ai::PersonaId::PlainAssistant,
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
    graph_.setVocoderMakeup(d.makeup);
    graph_.setVocoderCarrierNoise(d.carrierNoise);
    graph_.setVocoderSibilance(d.sibilance);
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
