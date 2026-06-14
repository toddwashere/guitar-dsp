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

TEST_CASE("SceneLibrary: parses tts source clipBank and bank array",
          "[scenes][library][tts][bank]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_tts_clip_bank.json"));
    REQUIRE(s.has_value());
    REQUIRE(s->tts.source == "clipBank");
    REQUIRE(s->tts.bank.size() == 3);
    REQUIRE(s->tts.bank[0] == "a");
    REQUIRE(s->tts.bank[1] == "b");
    REQUIRE(s->tts.bank[2] == "c");
    REQUIRE(s->tts.trigger == "note");
}

TEST_CASE("SceneLibrary: tts without bank leaves bank empty",
          "[scenes][library][tts][bank]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_tts.json"));
    REQUIRE(s.has_value());
    REQUIRE(s->tts.bank.empty());
}
