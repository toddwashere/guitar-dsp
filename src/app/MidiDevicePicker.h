#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class PluginProcessor;

// One-row UI strip showing all available CoreMIDI input devices in a
// ComboBox. Selecting one calls MidiRouter::setPreferredDeviceName and
// the router opens that device. Polls the device list every 2 s so a
// newly-connected FCB1010 shows up without an app restart.
class MidiDevicePicker : public juce::Component,
                         private juce::Timer {
public:
    explicit MidiDevicePicker(PluginProcessor& processor);
    ~MidiDevicePicker() override;

    void resized() override;

private:
    void timerCallback() override;
    void refreshItems();
    void onSelectionChange();

    PluginProcessor& processor_;

    juce::Label    label_ {"midiLabel", "MIDI in:"};
    juce::ComboBox combo_;

    // Track the populated item list so we only repopulate when devices
    // change (avoid blowing away the user's selection on every tick).
    juce::StringArray currentDeviceNames_;
};

} // namespace guitar_dsp
