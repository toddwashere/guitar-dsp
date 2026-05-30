#include "PluginEditor.h"

namespace guitar_dsp {

PluginEditor::PluginEditor(PluginProcessor& p)
    : juce::AudioProcessorEditor(&p), processor_(p) {
    setSize(800, 480);
    setResizable(true, true);
}

void PluginEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(20, 20, 28));
    g.setColour(juce::Colours::white);
    g.setFont(28.0f);
    g.drawFittedText("Guitar DSP — passthrough",
                     getLocalBounds(),
                     juce::Justification::centred,
                     1);
}

void PluginEditor::resized() {}

} // namespace guitar_dsp
