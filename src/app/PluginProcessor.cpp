#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "AssetLocator.h"
#include "scenes/SceneLibrary.h"

namespace guitar_dsp {

PluginProcessor::PluginProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::mono(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    graph_.prepare(sampleRate, samplesPerBlock);
    monoScratch_.assign(static_cast<std::size_t>(samplesPerBlock), 0.0f);

    if (sceneEngine_.getSceneCount() == 0) {
        const auto dir = AssetLocator::scenesDirectory();
        auto scenes = scenes::SceneLibrary::loadDirectory(dir);
        if (scenes.empty()) scenes.push_back(scenes::Scene::defaults(0));
        sceneEngine_.loadScenes(std::move(scenes));
    }
}

void PluginProcessor::releaseResources() {
    graph_.reset();
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
