#include <catch2/catch_test_macros.hpp>
#include "midi/FCB1010Mapping.h"

#include <juce_audio_basics/juce_audio_basics.h>

using guitar_dsp::midi::FCB1010Mapping;
using guitar_dsp::midi::SceneCommand;
using guitar_dsp::midi::SceneCommandType;

TEST_CASE("FCB1010Mapping: Program Change 0..9 -> ActivateScene 0..9", "[midi][fcb]") {
    FCB1010Mapping m = FCB1010Mapping::stockDefaults();
    for (int pc = 0; pc < 10; ++pc) {
        const auto msg = juce::MidiMessage::programChange(1, pc);
        auto cmd = m.translate(msg);
        REQUIRE(cmd.has_value());
        REQUIRE(cmd->type == SceneCommandType::ActivateScene);
        REQUIRE(cmd->payload == pc);
    }
}

TEST_CASE("FCB1010Mapping: Program Change 14..127 -> nullopt (unmapped)", "[midi][fcb]") {
    FCB1010Mapping m = FCB1010Mapping::stockDefaults();
    REQUIRE_FALSE(m.translate(juce::MidiMessage::programChange(1, 14)).has_value());
    REQUIRE_FALSE(m.translate(juce::MidiMessage::programChange(1, 99)).has_value());
}

TEST_CASE("FCB1010Mapping: CC 27 -> SetWetDry with raw CC value", "[midi][fcb]") {
    FCB1010Mapping m = FCB1010Mapping::stockDefaults();
    auto cmd = m.translate(juce::MidiMessage::controllerEvent(1, 27, 64));
    REQUIRE(cmd.has_value());
    REQUIRE(cmd->type == SceneCommandType::SetWetDry);
    REQUIRE(cmd->payload == 64);
}

TEST_CASE("FCB1010Mapping: CC 7 -> SetMasterGain with raw CC value", "[midi][fcb]") {
    FCB1010Mapping m = FCB1010Mapping::stockDefaults();
    auto cmd = m.translate(juce::MidiMessage::controllerEvent(1, 7, 100));
    REQUIRE(cmd.has_value());
    REQUIRE(cmd->type == SceneCommandType::SetMasterGain);
    REQUIRE(cmd->payload == 100);
}

TEST_CASE("FCB1010Mapping: unmapped CC -> nullopt", "[midi][fcb]") {
    FCB1010Mapping m = FCB1010Mapping::stockDefaults();
    REQUIRE_FALSE(m.translate(juce::MidiMessage::controllerEvent(1, 99, 64)).has_value());
}

TEST_CASE("FCB1010Mapping: note-on, pitch bend, etc. -> nullopt", "[midi][fcb]") {
    FCB1010Mapping m = FCB1010Mapping::stockDefaults();
    REQUIRE_FALSE(m.translate(juce::MidiMessage::noteOn(1, 60, juce::uint8(100))).has_value());
    REQUIRE_FALSE(m.translate(juce::MidiMessage::pitchWheel(1, 8192)).has_value());
}

TEST_CASE("FCB1010Mapping: loadFromJson with custom mapping overrides defaults",
          "[midi][fcb]") {
    const auto json = R"({
        "deviceMatch": "FCB1010",
        "programChangeToScene": { "100": 5, "101": 6 },
        "expressionPedalCcs": { "wetDry": 11, "masterGain": 12 }
    })";
    auto mapping = FCB1010Mapping::loadFromJson(json);
    REQUIRE(mapping.has_value());

    auto pcCmd = mapping->translate(juce::MidiMessage::programChange(1, 100));
    REQUIRE(pcCmd.has_value());
    REQUIRE(pcCmd->payload == 5);

    auto ccCmd = mapping->translate(juce::MidiMessage::controllerEvent(1, 11, 50));
    REQUIRE(ccCmd.has_value());
    REQUIRE(ccCmd->type == SceneCommandType::SetWetDry);

    // PC 0 should NOT activate scene 0 anymore — overridden away.
    REQUIRE_FALSE(mapping->translate(juce::MidiMessage::programChange(1, 0)).has_value());
}

TEST_CASE("FCB1010Mapping: loadFromJson with garbage returns nullopt",
          "[midi][fcb]") {
    REQUIRE_FALSE(FCB1010Mapping::loadFromJson("this is not json").has_value());
}

TEST_CASE("FCB1010Mapping: CC 80 value >= 64 -> TogglePitchSinging",
          "[midi][fcb][pitch_singing]") {
    FCB1010Mapping m = FCB1010Mapping::stockDefaults();
    auto cmd = m.translate(juce::MidiMessage::controllerEvent(1, 80, 127));
    REQUIRE(cmd.has_value());
    REQUIRE(cmd->type == SceneCommandType::TogglePitchSinging);
}

TEST_CASE("FCB1010Mapping: CC 80 value < 64 -> nullopt (latch release)",
          "[midi][fcb][pitch_singing]") {
    FCB1010Mapping m = FCB1010Mapping::stockDefaults();
    REQUIRE_FALSE(m.translate(juce::MidiMessage::controllerEvent(1, 80, 0)).has_value());
    REQUIRE_FALSE(m.translate(juce::MidiMessage::controllerEvent(1, 80, 63)).has_value());
}

TEST_CASE("FCB1010Mapping: loadFromJson can override pitchSingingToggle CC",
          "[midi][fcb][pitch_singing]") {
    const auto json = R"({
        "pitchSingingToggleCc": 85
    })";
    auto m = FCB1010Mapping::loadFromJson(json);
    REQUIRE(m.has_value());
    auto cmd = m->translate(juce::MidiMessage::controllerEvent(1, 85, 100));
    REQUIRE(cmd.has_value());
    REQUIRE(cmd->type == SceneCommandType::TogglePitchSinging);
}

TEST_CASE("FCB1010 stock defaults map PC 0..13 to scenes 0..13",
          "[fcb1010][stock-defaults]") {
    using guitar_dsp::midi::FCB1010Mapping;
    using guitar_dsp::midi::SceneCommandType;
    auto m = FCB1010Mapping::stockDefaults();
    for (int pc = 0; pc < 14; ++pc) {
        const auto msg = juce::MidiMessage::programChange(1, pc);
        auto cmd = m.translate(msg);
        REQUIRE(cmd.has_value());
        REQUIRE(cmd->type == SceneCommandType::ActivateScene);
        REQUIRE(cmd->payload == pc);
    }
}
