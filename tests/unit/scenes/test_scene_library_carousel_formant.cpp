#include <catch2/catch_test_macros.hpp>
#include "scenes/SceneLibrary.h"

#include <filesystem>

using guitar_dsp::scenes::CarouselConfig;
using guitar_dsp::scenes::SceneLibrary;

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

TEST_CASE("SceneLibrary: parses carousel.formant.mode=lfo + breakpoints + lfoHz",
          "[scenes][library][carousel][formant]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_carousel_formant_lfo.json"));
    REQUIRE(s.has_value());
    const auto& c = s->carousel;
    REQUIRE(c.formantMode == CarouselConfig::FormantMode::Lfo);
    REQUIRE(c.formantBreakpoints.size() == 2);
    REQUIRE(c.formantBreakpoints[0] == 0.0f);
    REQUIRE(c.formantBreakpoints[1] == 0.18f);
    REQUIRE(c.formantLfoHz == 9.0f);
    REQUIRE(c.formantAmount == 0.85f);
}

TEST_CASE("SceneLibrary: formant block without mode defaults to Static",
          "[scenes][library][carousel][formant]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_carousel.json"));
    REQUIRE(s.has_value());
    REQUIRE(s->carousel.formantMode == CarouselConfig::FormantMode::Static);
    REQUIRE(s->carousel.formantBreakpoints.empty());
}
