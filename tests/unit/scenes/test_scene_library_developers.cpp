#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "scenes/SceneLibrary.h"

#include <filesystem>

using guitar_dsp::scenes::SceneLibrary;
using Catch::Matchers::WithinAbs;

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

TEST_CASE("SceneLibrary: parses the Developers! scene", "[scenes][library][developers]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_developers.json"));
    REQUIRE(s.has_value());

    REQUIRE(s->id == 1);
    REQUIRE(s->name == "Developers!");
    REQUIRE(s->colorRgb == 0x0078D4u);

    REQUIRE_THAT(s->mixer.masterGainDb, WithinAbs(-10.0f, 1e-4f));
    REQUIRE_THAT(s->mixer.dryWet,       WithinAbs(0.9f,   1e-4f));
    REQUIRE_THAT(s->mixer.transitionMs, WithinAbs(30.0f,  1e-4f));

    REQUIRE(s->tts.source   == "prebaked");
    REQUIRE(s->tts.clip     == "01_developers");
    REQUIRE(s->tts.text     == "DEVELOPERS!");
    REQUIRE(s->tts.trigger  == "note");
    REQUIRE(s->tts.wordSync == "latch");
    REQUIRE_THAT(s->tts.clarity, WithinAbs(0.3f, 1e-4f));
}
