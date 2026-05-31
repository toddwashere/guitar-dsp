#include "MidiDevicePicker.h"

#include "PluginProcessor.h"
#include "midi/MidiRouter.h"

namespace guitar_dsp {

namespace {
constexpr int kPollHz = 2;  // 2 polls per second is plenty for hot-plug UI
constexpr int kAutoItemId = 1;
constexpr int kFirstDeviceItemId = 100;
} // namespace

MidiDevicePicker::MidiDevicePicker(PluginProcessor& processor)
    : processor_(processor) {
    label_.setFont(juce::Font{juce::FontOptions{}.withHeight(12.0f)});
    label_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(150, 160, 175));
    label_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(label_);

    combo_.setTextWhenNothingSelected("(no devices)");
    combo_.onChange = [this] { onSelectionChange(); };
    addAndMakeVisible(combo_);

    refreshItems();
    startTimerHz(kPollHz);
}

MidiDevicePicker::~MidiDevicePicker() {
    stopTimer();
}

void MidiDevicePicker::resized() {
    auto bounds = getLocalBounds().reduced(6, 4);
    label_.setBounds(bounds.removeFromLeft(60));
    combo_.setBounds(bounds);
}

void MidiDevicePicker::timerCallback() {
    refreshItems();
}

void MidiDevicePicker::refreshItems() {
    const auto devices = midi::MidiRouter::availableDevices();
    juce::StringArray names;
    for (const auto& d : devices) names.add(d.name);

    if (names == currentDeviceNames_) return;  // no change; preserve selection
    currentDeviceNames_ = names;

    const auto prevId = combo_.getSelectedId();
    combo_.clear(juce::dontSendNotification);
    combo_.addItem("(auto-pick)", kAutoItemId);
    int id = kFirstDeviceItemId;
    for (const auto& name : names) combo_.addItem(name, id++);

    // Restore selection if the previously-selected device is still present;
    // otherwise default to (auto-pick).
    if (prevId == kAutoItemId || prevId == 0) {
        combo_.setSelectedId(kAutoItemId, juce::dontSendNotification);
    } else if (combo_.indexOfItemId(prevId) >= 0) {
        combo_.setSelectedId(prevId, juce::dontSendNotification);
    } else {
        combo_.setSelectedId(kAutoItemId, juce::dontSendNotification);
    }
}

void MidiDevicePicker::onSelectionChange() {
    const auto id = combo_.getSelectedId();
    if (id == kAutoItemId) {
        processor_.setMidiPreferredDeviceName({});
        return;
    }
    const int idx = id - kFirstDeviceItemId;
    if (idx >= 0 && idx < currentDeviceNames_.size()) {
        processor_.setMidiPreferredDeviceName(currentDeviceNames_[idx]);
    }
}

} // namespace guitar_dsp
