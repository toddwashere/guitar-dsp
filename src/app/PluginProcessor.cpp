#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

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

PluginProcessor::PluginProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::mono(), true)
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
        });

    if (const char* env = std::getenv("GUITAR_DSP_HOT_RELOAD"); env && std::string(env) == "1") {
        assetsPoller_ = std::make_unique<AssetsPoller>(*this);
        assetsPoller_->startTimer(2000);
    }
}

PluginProcessor::~PluginProcessor() {
    // Mark dead before member destructors run. Any callAsync lambdas
    // already queued by MidiRouter will see this and bail out instead
    // of dereferencing a half-destroyed `this`.
    alive_->store(false, std::memory_order_release);
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    graph_.prepare(sampleRate, samplesPerBlock);
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

bool PluginProcessor::sayText(const std::string& text, const std::string& voiceId) {
    if (text.empty() || !appleTtsSource_) return false;
    if (!voiceId.empty()) appleTtsSource_->setVoice(voiceId);
    auto clip = appleTtsSource_->synthesize(text);
    if (!clip) return false;
    // Bypass currentTtsClipKey_ tracking — this is an ad-hoc overlay, not a
    // scene-driven clip. The next scene change will replace it normally.
    currentTtsClipKey_.clear();
    graph_.ttsClipPlayer().setClip(clip);
    return true;
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto inputs = layouts.getMainInputChannelSet();
    const auto outputs = layouts.getMainOutputChannelSet();
    if (inputs.isDisabled() || outputs.isDisabled()) return false;
    const bool inputOk = (inputs == juce::AudioChannelSet::mono()
                       || inputs == juce::AudioChannelSet::stereo());
    const bool outputOk = (outputs == juce::AudioChannelSet::mono()
                        || outputs == juce::AudioChannelSet::stereo());
    return inputOk && outputOk;
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
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

            const auto cfg = sceneEngine_.activeTtsConfig();

            // Build a per-source key (matches what synthesize() expects).
            std::string key;
            if (cfg.source == "prebaked") key = cfg.clip;
            else                          key = cfg.text;  // live sources (apple, piper)

            if (key == currentTtsClipKey_) return;
            currentTtsClipKey_ = key;

            if (key.empty()) {
                graph_.ttsClipPlayer().setClip(nullptr);
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

            graph_.ttsClipPlayer().setClip(clip);  // nullptr is OK (silences)
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

} // namespace guitar_dsp

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new guitar_dsp::PluginProcessor();
}
