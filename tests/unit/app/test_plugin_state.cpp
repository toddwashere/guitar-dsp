#include <catch2/catch_test_macros.hpp>
#include "app/PluginState.h"

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
    REQUIRE(out.makeup == 5.0f);
    REQUIRE(out.carrierNoise == 0.30f);
    REQUIRE(out.sibilance == 0.5f);
}
