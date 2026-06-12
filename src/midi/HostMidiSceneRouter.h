#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace guitar_dsp::midi {

class FCB1010Mapping;

// Scan a MIDI buffer for scene-activating messages, returning the scene id
// of the LAST such message in the block, or -1 if none. Allocation-free and
// safe to call on the audio thread (processBlock); the caller stores the
// result for the message thread to apply (activateScene is message-thread API).
int sceneFromMidiBuffer(const juce::MidiBuffer& midi, const FCB1010Mapping& mapping);

} // namespace guitar_dsp::midi
