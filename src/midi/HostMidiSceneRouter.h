#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace guitar_dsp::midi {

class FCB1010Mapping;

int  sceneFromMidiBuffer(const juce::MidiBuffer& midi, const FCB1010Mapping& mapping);

// Returns true if the buffer contains at least one CC matching the
// FCB1010Mapping's pitchSinging toggle (CC#80 by default, value >= 64).
// Each such message represents one user "press"; the caller flips the
// app's toggle once per call where this returns true (debounce is on the
// FCB side — single press = single CC>=64 message).
bool pitchSingingToggleFromMidiBuffer(const juce::MidiBuffer& midi,
                                      const FCB1010Mapping& mapping);

} // namespace guitar_dsp::midi
