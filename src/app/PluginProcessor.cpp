#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>

namespace guitar_dsp {

PluginProcessor::PluginProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::mono(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    graph_.prepare(sampleRate, samplesPerBlock);
    monoScratch_.assign(static_cast<std::size_t>(samplesPerBlock), 0.0f);
}

void PluginProcessor::releaseResources() {
    graph_.reset();
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto inputs = layouts.getMainInputChannelSet();
    const auto outputs = layouts.getMainOutputChannelSet();
    if (inputs.isDisabled() || outputs.isDisabled()) return false;
    return inputs == juce::AudioChannelSet::mono()
        && (outputs == juce::AudioChannelSet::mono()
            || outputs == juce::AudioChannelSet::stereo());
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int totalOut = getTotalNumOutputChannels();
    if (totalOut == 0) return;

    // Grow scratch only if buffer is larger than expected; this happens
    // off the hot path only when the host changes block size at runtime.
    if (monoScratch_.size() < static_cast<std::size_t>(numSamples)) {
        monoScratch_.assign(static_cast<std::size_t>(numSamples), 0.0f);
        graph_.prepare(getSampleRate(), numSamples);
    }

    const float* in = buffer.getReadPointer(0);
    graph_.process(in, monoScratch_.data(), static_cast<std::size_t>(numSamples));

    for (int ch = 0; ch < totalOut; ++ch) {
        float* out = buffer.getWritePointer(ch);
        std::memcpy(out, monoScratch_.data(), sizeof(float) * static_cast<std::size_t>(numSamples));
    }
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() {
    return new PluginEditor(*this);
}

} // namespace guitar_dsp

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new guitar_dsp::PluginProcessor();
}
