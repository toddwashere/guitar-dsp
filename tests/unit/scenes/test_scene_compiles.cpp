#include <catch2/catch_test_macros.hpp>
#include "scenes/Scene.h"

TEST_CASE("Scene: defaults() produces a usable scene", "[scenes][smoke]") {
    const auto s = guitar_dsp::scenes::Scene::defaults(3);
    REQUIRE(s.id == 3);
    REQUIRE(s.name == "Scene 3");
    REQUIRE(s.mixer.masterGainDb == 0.0f);
    REQUIRE(s.mixer.dryWet == 0.0f);
}
