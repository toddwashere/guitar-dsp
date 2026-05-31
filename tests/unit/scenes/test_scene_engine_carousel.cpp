#include <catch2/catch_test_macros.hpp>
#include "scenes/SceneEngine.h"

using guitar_dsp::scenes::Scene;
using guitar_dsp::scenes::SceneEngine;
using guitar_dsp::scenes::CarouselConfig;

TEST_CASE("SceneEngine: activeCarouselConfig reflects active scene",
          "[scenes][engine][carousel]") {
    Scene a = Scene::defaults(0);  // no carousel
    Scene b = Scene::defaults(1);
    b.carousel.enabled = true;
    b.carousel.filterMode = CarouselConfig::FilterMode::LowPass;
    b.carousel.filterCutoffHz = 1234.0f;

    SceneEngine engine;
    engine.loadScenes({a, b});

    engine.activateScene(0);
    REQUIRE_FALSE(engine.activeCarouselConfig().enabled);

    engine.activateScene(1);
    const auto c = engine.activeCarouselConfig();
    REQUIRE(c.enabled);
    REQUIRE(c.filterMode == CarouselConfig::FilterMode::LowPass);
    REQUIRE(c.filterCutoffHz == 1234.0f);
}

TEST_CASE("SceneEngine: activeCarouselConfig empty when no active scene",
          "[scenes][engine][carousel]") {
    SceneEngine engine;
    REQUIRE_FALSE(engine.activeCarouselConfig().enabled);
}
