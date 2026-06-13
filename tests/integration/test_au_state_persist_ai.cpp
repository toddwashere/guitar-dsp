#include <catch2/catch_test_macros.hpp>
#include "app/PluginState.h"
#include "ai/PersonaRegistry.h"

using guitar_dsp::app::PluginStateData;
using guitar_dsp::app::PluginState;
using guitar_dsp::ai::PersonaId;

// Note: this test exercises the PluginStateData round-trip at the JSON layer.
// The full PluginProcessor::getStateInformation/setStateInformation path is
// covered by manual checklist (Task 11.2) since PluginProcessor can't be
// instantiated in the test binary without juce_add_plugin codegen.

TEST_CASE("AU state persist: AI fields round-trip via JSON",
          "[app][state][ai][integration]") {
    PluginStateData in;
    in.sceneId          = 4;
    in.makeup           = 6.5f;
    in.selectedModelId  = "ollama:llama3.2:3b";
    in.personaId        = PersonaId::Snarky;
    in.customPromptByPersona[PersonaId::Snarky]    = "be brutal but witty";
    in.customPromptByPersona[PersonaId::CuriousAi] = "I wonder \"why\" — Æ ✨";
    in.maxSentences     = 3;
    in.maxWords         = 30;
    in.sttModelId       = "whisper-small.en";
    in.pttPedalId       = 5;
    in.clearChatPedalId = 7;

    const auto json = PluginState::toJson(in);
    const auto out  = PluginState::fromJson(json);

    REQUIRE(out.sceneId          == 4);
    REQUIRE(out.makeup            == 6.5f);
    REQUIRE(out.selectedModelId   == "ollama:llama3.2:3b");
    REQUIRE(out.personaId         == PersonaId::Snarky);
    REQUIRE(out.customPromptByPersona.at(PersonaId::Snarky)    == "be brutal but witty");
    REQUIRE(out.customPromptByPersona.at(PersonaId::CuriousAi) == "I wonder \"why\" — Æ ✨");
    REQUIRE(out.maxSentences      == 3);
    REQUIRE(out.maxWords          == 30);
    REQUIRE(out.sttModelId        == "whisper-small.en");
    REQUIRE(out.pttPedalId        == 5);
    REQUIRE(out.clearChatPedalId  == 7);
}
