#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

namespace guitar_dsp::midi {

// Opens MIDI inputs via JUCE and forwards messages to a single callback
// on the message thread.
//
// Device selection: if a preferred device name has been set (via
// setPreferredDeviceName), only the device whose name contains that
// substring (case-insensitive) is opened. With no preference set, the
// default is to prefer "FCB1010" if any device matches, otherwise open
// every available input.
//
// Hot-plug: an internal Timer re-scans `juce::MidiInput::getAvailableDevices`
// every 2 seconds. If the current open set diverges from what the current
// preference says should be open, the router closes/opens as needed —
// preserving in-flight messages on devices that don't change.
class MidiRouter : private juce::MidiInputCallback {
public:
    using MessageCallback = std::function<void(const juce::MidiMessage&)>;

    explicit MidiRouter(MessageCallback onMessage);
    ~MidiRouter() override;

    // Set the preferred device. Empty string = auto-pick (FCB1010 or all).
    // Triggers an immediate refresh.
    void setPreferredDeviceName(juce::String name);
    juce::String getPreferredDeviceName() const { return preferredName_; }

    // List all currently-visible MIDI input devices on the system (not
    // just the open ones). Use for the UI picker.
    static std::vector<juce::MidiDeviceInfo> availableDevices();

    // Names of currently-open MIDI input devices (for the diagnostic UI).
    std::vector<juce::String> openDeviceNames() const;

private:
    // Re-scan available devices and (re)open the matching ones.
    void refresh();

    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

    // Timer-driven hot-plug rescan.
    class RescanTimer;
    std::unique_ptr<RescanTimer> rescanTimer_;

    MessageCallback callback_;
    juce::String    preferredName_;  // empty = auto-pick
    std::vector<std::unique_ptr<juce::MidiInput>> openInputs_;
};

} // namespace guitar_dsp::midi
