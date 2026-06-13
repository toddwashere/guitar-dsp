#include <catch2/catch_test_macros.hpp>
#include "midi/FCB1010Mapping.h"

using guitar_dsp::midi::FCB1010Mapping;
using guitar_dsp::midi::AiAction;
using guitar_dsp::midi::AiPedalBindings;

TEST_CASE("FCB1010 AI: stock defaults have AI bindings disabled",
          "[midi][fcb][ai]") {
    auto m = FCB1010Mapping::stockDefaults();
    REQUIRE(m.decodeAi(0, /*long=*/false) == AiAction::None);
    REQUIRE(m.decodeAi(9, /*long=*/false) == AiAction::None);
}

TEST_CASE("FCB1010 AI: configured PTT pedal short press -> PttToggle",
          "[midi][fcb][ai]") {
    auto m = FCB1010Mapping::stockDefaults();
    AiPedalBindings b;
    b.pttProgramChange       = 8;
    b.clearChatProgramChange = 9;
    m.setAiBindings(b);
    REQUIRE(m.decodeAi(8, false) == AiAction::PttToggle);
}

TEST_CASE("FCB1010 AI: clear pedal press always fires ClearChat (v1)",
          "[midi][fcb][ai]") {
    auto m = FCB1010Mapping::stockDefaults();
    AiPedalBindings b;
    b.pttProgramChange       = 8;
    b.clearChatProgramChange = 9;
    m.setAiBindings(b);
    REQUIRE(m.decodeAi(9, false) == AiAction::ClearChat);
    REQUIRE(m.decodeAi(9, true)  == AiAction::ClearChat);
}

TEST_CASE("FCB1010 AI: unmapped pedal returns None",
          "[midi][fcb][ai]") {
    auto m = FCB1010Mapping::stockDefaults();
    AiPedalBindings b;
    b.pttProgramChange       = 8;
    b.clearChatProgramChange = 9;
    m.setAiBindings(b);
    REQUIRE(m.decodeAi(3, false) == AiAction::None);
}

TEST_CASE("FCB1010 AI: rebinding PTT to a different PC honored",
          "[midi][fcb][ai]") {
    auto m = FCB1010Mapping::stockDefaults();
    AiPedalBindings b;
    b.pttProgramChange = 5;
    m.setAiBindings(b);
    REQUIRE(m.decodeAi(5, false) == AiAction::PttToggle);
    REQUIRE(m.decodeAi(8, false) == AiAction::None);
}

TEST_CASE("FCB1010 AI: loadFromJson reads aiPedals block",
          "[midi][fcb][ai]") {
    auto opt = FCB1010Mapping::loadFromJson(
        R"({"aiPedals":{"ptt":8,"clearChat":9,"longPressMs":600}})");
    REQUIRE(opt.has_value());
    auto& m = *opt;
    REQUIRE(m.decodeAi(8, false) == AiAction::PttToggle);
    REQUIRE(m.decodeAi(9, true)  == AiAction::ClearChat);
}
