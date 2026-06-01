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

TEST_CASE("SceneLibrary: parses tts.trigger", "[scenes][library][tts]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_tts_trigger.json"));
    REQUIRE(s.has_value());
    REQUIRE(s->tts.trigger == "note");
}

TEST_CASE("SceneLibrary: missing tts.trigger leaves empty", "[scenes][library][tts]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_tts_text.json"));
    REQUIRE(s.has_value());
    REQUIRE(s->tts.trigger.empty());
}
