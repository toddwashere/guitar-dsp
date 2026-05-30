#pragma once

#include <atomic>

#include <juce_audio_processors/juce_audio_processors.h>

#include "audio/AudioGraph.h"

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

private:
    audio::AudioGraph graph_;
    std::vector<float> monoScratch_;

    std::atomic<float> inputPeak_   {0.0f};
    std::atomic<float> outputPeak_  {0.0f};
    std::atomic<float> gateGain_    {1.0f};
    std::atomic<int>   lastInputChannels_  {0};
    std::atomic<int>   lastOutputChannels_ {0};
};

} // namespace guitar_dsp
