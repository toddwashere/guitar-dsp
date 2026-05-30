#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace guitar_dsp {

PluginProcessor::PluginProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::mono(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

void PluginProcessor::prepareToPlay(double, int) {
    // No-op for now; real prepare logic arrives with AudioGraph in Task 15.
}

void PluginProcessor::releaseResources() {}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto inputs = layouts.getMainInputChannelSet();
    const auto outputs = layouts.getMainOutputChannelSet();
    if (inputs.isDisabled() || outputs.isDisabled())
        return false;
    return inputs == juce::AudioChannelSet::mono()
        && (outputs == juce::AudioChannelSet::mono()
            || outputs == juce::AudioChannelSet::stereo());
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    // Trivial mono->stereo passthrough; replaced by AudioGraph in Task 15.
    const auto numSamples = buffer.getNumSamples();
    const auto totalOut = getTotalNumOutputChannels();
    const auto totalIn = getTotalNumInputChannels();

    if (totalIn == 1 && totalOut == 2) {
        const float* in = buffer.getReadPointer(0);
        float* outL = buffer.getWritePointer(0);
        float* outR = buffer.getWritePointer(1);
        for (int i = 0; i < numSamples; ++i) {
            const float s = in[i];
            outL[i] = s;
            outR[i] = s;
        }
    }
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() {
    return new PluginEditor(*this);
}

} // namespace guitar_dsp

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new guitar_dsp::PluginProcessor();
}
