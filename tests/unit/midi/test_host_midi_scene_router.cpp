#include <catch2/catch_test_macros.hpp>

#include "midi/HostMidiSceneRouter.h"
#include "midi/FCB1010Mapping.h"

#include <juce_audio_basics/juce_audio_basics.h>

using guitar_dsp::midi::FCB1010Mapping;
using guitar_dsp::midi::sceneFromMidiBuffer;

TEST_CASE("sceneFromMidiBuffer: program change maps to scene id", "[midi][hostmidi]") {
    const auto mapping = FCB1010Mapping::stockDefaults();  // PC n -> scene n
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::programChange(1, 7), 0);  // PC #7
    REQUIRE(sceneFromMidiBuffer(buf, mapping) == 7);
}

TEST_CASE("sceneFromMidiBuffer: no scene message returns -1", "[midi][hostmidi]") {
    const auto mapping = FCB1010Mapping::stockDefaults();
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);
    REQUIRE(sceneFromMidiBuffer(buf, mapping) == -1);
}

TEST_CASE("sceneFromMidiBuffer: last scene message in the block wins", "[midi][hostmidi]") {
    const auto mapping = FCB1010Mapping::stockDefaults();
    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::programChange(1, 2), 0);
    buf.addEvent(juce::MidiMessage::programChange(1, 5), 10);
    REQUIRE(sceneFromMidiBuffer(buf, mapping) == 5);
}

TEST_CASE("pitchSingingToggleFromMidiBuffer: returns true when CC#80 >=64 present",
          "[midi][host_router][pitch_singing]") {
    using namespace guitar_dsp::midi;
    FCB1010Mapping mapping = FCB1010Mapping::stockDefaults();

    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::controllerEvent(1, 80, 100), 0);

    REQUIRE(pitchSingingToggleFromMidiBuffer(buf, mapping) == true);
}

TEST_CASE("pitchSingingToggleFromMidiBuffer: returns false on empty / unrelated msgs",
          "[midi][host_router][pitch_singing]") {
    using namespace guitar_dsp::midi;
    FCB1010Mapping mapping = FCB1010Mapping::stockDefaults();

    juce::MidiBuffer empty;
    REQUIRE(pitchSingingToggleFromMidiBuffer(empty, mapping) == false);

    juce::MidiBuffer pcOnly;
    pcOnly.addEvent(juce::MidiMessage::programChange(1, 3), 0);
    REQUIRE(pitchSingingToggleFromMidiBuffer(pcOnly, mapping) == false);
}
