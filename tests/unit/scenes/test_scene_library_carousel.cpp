#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "scenes/SceneLibrary.h"

#include <filesystem>

using Catch::Matchers::WithinAbs;
using guitar_dsp::scenes::SceneLibrary;
using guitar_dsp::scenes::CarouselConfig;

namespace {
std::string fixturePath(const std::string& rel) {
    auto p = std::filesystem::current_path();
    for (int i = 0; i < 6; ++i) {
        auto c = p / "tests" / "fixtures" / rel;
        if (std::filesystem::exists(c.parent_path())) return c.string();
        p = p.parent_path();
    }
    throw std::runtime_error("fixtures not found");
}
}

TEST_CASE("SceneLibrary: parses carousel block", "[scenes][library][carousel]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_carousel.json"));
    REQUIRE(s.has_value());
    const auto& c = s->carousel;
    REQUIRE(c.enabled);
    REQUIRE_THAT(c.drive, WithinAbs(6.0f, 1e-4f));
    REQUIRE(c.shaper == CarouselConfig::Shaper::HardClip);
    REQUIRE_THAT(c.shaperAmount, WithinAbs(0.8f, 1e-4f));
    REQUIRE(c.crusherBits == 4);
    REQUIRE(c.crusherDownsample == 8);
    REQUIRE(c.filterMode == CarouselConfig::FilterMode::BandPass);
    REQUIRE(c.filterMod == CarouselConfig::FilterMod::Envelope);
    REQUIRE_THAT(c.filterCutoffHz, WithinAbs(800.0f, 1e-3f));
    REQUIRE_THAT(c.filterResonance, WithinAbs(0.7f, 1e-4f));
    REQUIRE_THAT(c.filterEnvAmount, WithinAbs(2000.0f, 1e-3f));
    REQUIRE_THAT(c.chorusRateHz, WithinAbs(5.0f, 1e-4f));
    REQUIRE_THAT(c.reverbWet, WithinAbs(0.2f, 1e-4f));
    REQUIRE_THAT(c.outputTrimDb, WithinAbs(-1.5f, 1e-4f));
}

TEST_CASE("SceneLibrary: missing carousel block leaves disabled default",
          "[scenes][library][carousel]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_tts_text.json"));
    REQUIRE(s.has_value());
    REQUIRE_FALSE(s->carousel.enabled);
    REQUIRE(s->carousel.shaper == CarouselConfig::Shaper::None);
}
