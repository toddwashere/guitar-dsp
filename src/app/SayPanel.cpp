#include "SayPanel.h"

#include "PluginProcessor.h"

namespace guitar_dsp {

SayPanel::SayPanel(PluginProcessor& processor) : processor_(processor) {
    input_.setMultiLine(false);
    input_.setReturnKeyStartsNewLine(false);
    input_.setTextToShowWhenEmpty("Type a phrase, then press Enter or click Say",
                                  juce::Colours::grey);
    input_.onReturnKey = [this] { say(); };
    addAndMakeVisible(input_);

    sayButton_.onClick = [this] { say(); };
    addAndMakeVisible(sayButton_);
}

void SayPanel::resized() {
    auto bounds = getLocalBounds().reduced(6, 6);
    constexpr int buttonWidth = 64;
    constexpr int gap = 6;
    auto buttonBounds = bounds.removeFromRight(buttonWidth);
    bounds.removeFromRight(gap);
    input_.setBounds(bounds);
    sayButton_.setBounds(buttonBounds);
}

void SayPanel::say() {
    const auto text = input_.getText().trim().toStdString();
    if (text.empty()) return;
    processor_.sayText(text);
}

} // namespace guitar_dsp
