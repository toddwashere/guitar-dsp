#include "PluginEditor.h"

namespace guitar_dsp {

PluginEditor::PluginEditor(PluginProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      diagnosticPanel_(p),
      oscilloscope_(p),
      spectrumAnalyzer_(p) {
    setSize(720, 460);
    setResizable(true, true);
    setResizeLimits(520, 300, 1800, 1200);
    addAndMakeVisible(diagnosticPanel_);
    addAndMakeVisible(oscilloscope_);
    addAndMakeVisible(spectrumAnalyzer_);
}

void PluginEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(18, 20, 26));
}

void PluginEditor::resized() {
    auto bounds = getLocalBounds();

    // Compact diagnostic strip at the top.
    diagnosticPanel_.setBounds(bounds.removeFromTop(62));

    // Oscilloscope and spectrum share the remaining space evenly.
    const int remaining = bounds.getHeight();
    oscilloscope_.setBounds(bounds.removeFromTop(remaining / 2));
    spectrumAnalyzer_.setBounds(bounds);
}

} // namespace guitar_dsp
