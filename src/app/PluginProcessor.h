#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "audio/AudioGraph.h"
#include "midi/FCB1010Mapping.h"
#include "midi/MidiRouter.h"
#include "scenes/SceneEngine.h"

namespace guitar_dsp {

class PluginProcessor : public juce::AudioProcessor {
public:
    PluginProcessor();
    ~PluginProcessor() override = default;

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

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

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

    scenes::SceneEngine& sceneEngine() { return sceneEngine_; }

    int getLastMidiSummary() const noexcept { return lastMidiSummary_.load(std::memory_order_relaxed); }

private:
    audio::AudioGraph graph_;
    std::vector<float> monoScratch_;

    std::atomic<float> inputPeak_   {0.0f};
    std::atomic<float> outputPeak_  {0.0f};
    std::atomic<float> gateGain_    {1.0f};
    std::atomic<int>   lastInputChannels_  {0};
    std::atomic<int>   lastOutputChannels_ {0};

    std::array<float, kAudioRingSize> audioRing_{};
    std::atomic<int>                  audioRingWriteIdx_{0};

    scenes::SceneEngine sceneEngine_;

    midi::FCB1010Mapping              midiMapping_ {midi::FCB1010Mapping::stockDefaults()};
    std::unique_ptr<midi::MidiRouter> midiRouter_;
    std::atomic<int>                  lastMidiSummary_ {0};

    class AssetsPoller;
    std::unique_ptr<AssetsPoller> assetsPoller_;
};

} // namespace guitar_dsp
