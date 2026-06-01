#include "PluginEditor.h"

namespace guitar_dsp {

PluginEditor::PluginEditor(PluginProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      diagnosticPanel_(p),
      sceneIndicator_(p),
      wordReadout_(p),
      midiDevicePicker_(p),
      sayPanel_(p),
      oscilloscope_(p),
      spectrumAnalyzer_(p) {
    setSize(720, 572);
    setResizable(true, true);
    setResizeLimits(520, 400, 1800, 1200);
    addAndMakeVisible(diagnosticPanel_);
    addAndMakeVisible(sceneIndicator_);
    addAndMakeVisible(wordReadout_);
    addAndMakeVisible(midiDevicePicker_);
    addAndMakeVisible(sayPanel_);
    addAndMakeVisible(oscilloscope_);
    addAndMakeVisible(spectrumAnalyzer_);
    setWantsKeyboardFocus(true);
    addKeyListener(this);
}

void PluginEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(18, 20, 26));
}

void PluginEditor::resized() {
    auto bounds = getLocalBounds();
    diagnosticPanel_.setBounds(bounds.removeFromTop(62));
    sceneIndicator_.setBounds(bounds.removeFromTop(48));
    wordReadout_.setBounds(bounds.removeFromTop(44));
    midiDevicePicker_.setBounds(bounds.removeFromTop(28));
    sayPanel_.setBounds(bounds.removeFromTop(40));
    const int remaining = bounds.getHeight();
    oscilloscope_.setBounds(bounds.removeFromTop(remaining / 2));
    spectrumAnalyzer_.setBounds(bounds);
}

bool PluginEditor::keyPressed(const juce::KeyPress& key, juce::Component*) {
    const auto kc = key.getKeyCode();

    int sceneId = -1;
    if (kc >= '1' && kc <= '9')      sceneId = kc - '1';   // 1 -> 0, 9 -> 8
    else if (kc == '0')              sceneId = 9;          // 0 -> 9
    if (sceneId < 0) return false;

    processor_.sceneEngine().activateScene(sceneId);
    return true;
}

} // namespace guitar_dsp
