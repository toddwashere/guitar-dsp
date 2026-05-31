#include <catch2/catch_test_macros.hpp>
#include "scenes/SceneLibrary.h"

#include <filesystem>

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

TEST_CASE("SceneLibrary: parses tts block with prebaked source", "[scenes][library][tts]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_tts.json"));
    REQUIRE(s.has_value());
    REQUIRE(s->tts.source == "prebaked");
    REQUIRE(s->tts.clip == "08_gently_weeps");
}

TEST_CASE("SceneLibrary: missing tts block leaves defaults", "[scenes][library][tts]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/valid_minimal.json"));
    REQUIRE(s.has_value());
    REQUIRE(s->tts.source.empty());
    REQUIRE(s->tts.clip.empty());
}
