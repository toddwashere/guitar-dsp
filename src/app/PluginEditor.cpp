#include "PluginEditor.h"

namespace guitar_dsp {

PluginEditor::PluginEditor(PluginProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      diagnosticPanel_(p) {
    setSize(640, 380);
    setResizable(true, true);
    setResizeLimits(480, 300, 1600, 1000);
    addAndMakeVisible(diagnosticPanel_);
}

void PluginEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(18, 20, 26));
}

void PluginEditor::resized() {
    diagnosticPanel_.setBounds(getLocalBounds());
}

} // namespace guitar_dsp
