#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

namespace guitar_dsp::midi {

// Opens MIDI inputs via JUCE and forwards messages to a single callback
// on the message thread. If any input device name contains "FCB1010"
// (case-insensitive), only that device is opened; otherwise all available
// inputs are opened. Re-scans on every refresh() call.
class MidiRouter : private juce::MidiInputCallback {
public:
    using MessageCallback = std::function<void(const juce::MidiMessage&)>;

    explicit MidiRouter(MessageCallback onMessage);
    ~MidiRouter() override;

    // Re-scan available devices and (re)open the matching ones. Safe to
    // call repeatedly; idempotent for devices already open.
    void refresh();

    // Names of currently-open MIDI input devices (for the diagnostic UI).
    std::vector<juce::String> openDeviceNames() const;

private:
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

    MessageCallback callback_;
    std::vector<std::unique_ptr<juce::MidiInput>> openInputs_;
};

} // namespace guitar_dsp::midi
