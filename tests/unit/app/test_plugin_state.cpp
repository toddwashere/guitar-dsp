#include <catch2/catch_test_macros.hpp>
#include "app/PluginState.h"
#include "ai/PersonaRegistry.h"

using guitar_dsp::app::PluginStateData;
using guitar_dsp::app::PluginState;

TEST_CASE("PluginState: round-trips scene id and vocoder knobs", "[app][state]") {
    PluginStateData in; in.sceneId = 7; in.makeup = 6.5f; in.carrierNoise = 0.4f; in.sibilance = 0.2f;
    const auto json = PluginState::toJson(in);
    const auto out = PluginState::fromJson(json);
    REQUIRE(out.sceneId == 7);
    REQUIRE(out.makeup == 6.5f);
    REQUIRE(out.carrierNoise == 0.4f);
    REQUIRE(out.sibilance == 0.2f);
}

TEST_CASE("PluginState: missing/garbage input yields defaults", "[app][state]") {
    const auto out = PluginState::fromJson("not json");
    REQUIRE(out.sceneId == 0);
    REQUIRE(out.makeup == 4.0f);
    REQUIRE(out.carrierNoise == 0.30f);
    REQUIRE(out.sibilance == 0.3f);
}

TEST_CASE("PluginState: new AI fields have correct defaults", "[app][state][ai]") {
    PluginStateData s;
    REQUIRE(s.selectedModelId == "claude-haiku-4-5");
    REQUIRE(s.personaId == guitar_dsp::ai::PersonaId::Interviewer);
    REQUIRE(s.customPromptByPersona.empty());
    REQUIRE(s.maxSentences == 2);
    REQUIRE(s.maxWords == 25);
    REQUIRE(s.sttModelId == "whisper-base.en");
    REQUIRE(s.pttPedalId == 9);
    REQUIRE(s.clearChatPedalId == 10);
}

TEST_CASE("PluginState: legacy JSON (pre-AI fields) loads with AI defaults",
          "[app][state][ai]") {
    // Equivalent to what an older build produced — only the original 4 fields
    const juce::String legacy =
        R"({"sceneId":3,"makeup":4.5,"carrierNoise":0.25,"sibilance":0.4})";
    auto s = PluginState::fromJson(legacy);
    REQUIRE(s.sceneId == 3);
    REQUIRE(s.makeup == 4.5f);
    REQUIRE(s.personaId == guitar_dsp::ai::PersonaId::Interviewer);
    REQUIRE(s.selectedModelId == "claude-haiku-4-5");
    REQUIRE(s.maxWords == 25);
}

TEST_CASE("PluginState: round-trip preserves AI fields", "[app][state][ai]") {
    PluginStateData in;
    in.personaId        = guitar_dsp::ai::PersonaId::Snarky;
    in.customPromptByPersona[guitar_dsp::ai::PersonaId::Snarky] = "be brutal but witty";
    in.selectedModelId  = "ollama:llama3.2:3b";
    in.maxWords         = 30;
    in.pttPedalId       = 5;
    const auto json = PluginState::toJson(in);
    const auto out  = PluginState::fromJson(json);
    REQUIRE(out.personaId == guitar_dsp::ai::PersonaId::Snarky);
    REQUIRE(out.customPromptByPersona.at(guitar_dsp::ai::PersonaId::Snarky)
            == "be brutal but witty");
    REQUIRE(out.selectedModelId == "ollama:llama3.2:3b");
    REQUIRE(out.maxWords == 30);
    REQUIRE(out.pttPedalId == 5);
}

TEST_CASE("PluginState: persona id out-of-range falls back to Interviewer",
          "[app][state][ai]") {
    const juce::String bad = R"({"sceneId":0,"personaId":99})";
    auto s = PluginState::fromJson(bad);
    REQUIRE(s.personaId == guitar_dsp::ai::PersonaId::Interviewer);
}

TEST_CASE("PluginState: custom prompts with UTF-8 + quotes round-trip",
          "[app][state][ai]") {
    PluginStateData in;
    in.customPromptByPersona[guitar_dsp::ai::PersonaId::CuriousAi]
        = "I wonder \"why\" — Æ ✨ — let's see.";
    const auto json = PluginState::toJson(in);
    const auto out  = PluginState::fromJson(json);
    REQUIRE(out.customPromptByPersona.at(guitar_dsp::ai::PersonaId::CuriousAi)
            == "I wonder \"why\" — Æ ✨ — let's see.");
}

TEST_CASE("PluginState: pitchSinging round-trips through JSON",
          "[app][state][pitch_singing]") {
    guitar_dsp::app::PluginStateData d;
    d.pitchSinging = true;
    const auto json = guitar_dsp::app::PluginState::toJson(d);
    const auto out  = guitar_dsp::app::PluginState::fromJson(json);
    REQUIRE(out.pitchSinging == true);
}

TEST_CASE("PluginState: pitchSinging defaults to false when absent",
          "[app][state][pitch_singing]") {
    const juce::String json = R"({ "sceneId": 0 })";
    const auto out = guitar_dsp::app::PluginState::fromJson(json);
    REQUIRE(out.pitchSinging == false);
}

TEST_CASE("PluginState: singing round-trips through JSON",
          "[app][state][singing]") {
    guitar_dsp::app::PluginStateData d;
    d.singing = true;
    const auto json = guitar_dsp::app::PluginState::toJson(d);
    const auto out  = guitar_dsp::app::PluginState::fromJson(json);
    REQUIRE(out.singing == true);
}

TEST_CASE("PluginState: singing defaults to false when absent",
          "[app][state][singing]") {
    const juce::String json = R"({ "sceneId": 0 })";
    const auto out = guitar_dsp::app::PluginState::fromJson(json);
    REQUIRE(out.singing == false);
}
