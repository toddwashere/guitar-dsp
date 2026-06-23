#include "VoicePackPicker.h"

namespace guitar_dsp::app {

VoicePackPicker::VoicePackPicker() {
    label_.setText("Voice", juce::dontSendNotification);
    label_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(label_);
    combo_.addListener(this);
    addAndMakeVisible(combo_);
}

VoicePackPicker::~VoicePackPicker() { combo_.removeListener(this); }

void VoicePackPicker::setPacks(
    const std::vector<std::pair<std::string, std::string>>& labelPathPairs,
    int activeIndex) {
    combo_.clear(juce::dontSendNotification);
    if (labelPathPairs.empty()) {
        setVisible(false);
        return;
    }
    setVisible(true);
    int id = 1;
    for (const auto& [label, path] : labelPathPairs) {
        combo_.addItem(juce::String(label), id++);
    }
    if (activeIndex < 0 || activeIndex >= static_cast<int>(labelPathPairs.size()))
        activeIndex = 0;
    combo_.setSelectedId(activeIndex + 1, juce::dontSendNotification);
}

void VoicePackPicker::resized() {
    auto r = getLocalBounds();
    label_.setBounds(r.removeFromLeft(56));
    combo_.setBounds(r);
}

void VoicePackPicker::paint(juce::Graphics&) {}

void VoicePackPicker::comboBoxChanged(juce::ComboBox* cb) {
    if (cb != &combo_) return;
    const int idx = combo_.getSelectedId() - 1;
    if (idx < 0) return;
    if (onChange) onChange(idx);
}

} // namespace guitar_dsp::app
