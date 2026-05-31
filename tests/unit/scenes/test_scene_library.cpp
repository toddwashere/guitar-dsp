#include <catch2/catch_test_macros.hpp>
#include "scenes/SceneLibrary.h"

#include <filesystem>
#include <fstream>

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

TEST_CASE("SceneLibrary: loads a single valid scene file", "[scenes][library]") {
    auto result = SceneLibrary::loadOne(fixturePath("scenes/valid_minimal.json"));
    REQUIRE(result.has_value());
    const auto& s = *result;
    REQUIRE(s.id == 7);
    REQUIRE(s.name == "Test scene");
    REQUIRE(s.colorRgb == 0xABCDEFu);
    REQUIRE(s.mixer.masterGainDb == -3.5f);
    REQUIRE(s.mixer.dryWet == 0.25f);
    REQUIRE(s.mixer.transitionMs == 15.0f);
}

TEST_CASE("SceneLibrary: malformed JSON returns nullopt with logged error",
          "[scenes][library]") {
    auto result = SceneLibrary::loadOne(fixturePath("scenes/malformed.json"));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("SceneLibrary: missing required field returns nullopt",
          "[scenes][library]") {
    auto result = SceneLibrary::loadOne(fixturePath("scenes/missing_required.json"));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("SceneLibrary: loadDirectory returns sorted by id, skipping invalid",
          "[scenes][library]") {
    const auto tmp = std::filesystem::temp_directory_path() / "guitar_dsp_test_scenes";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);

    auto writeFile = [&tmp](const std::string& name, const std::string& body) {
        std::ofstream f(tmp / name);
        f << body;
    };

    writeFile("a.json",
        R"({ "id": 5, "name": "Five", "color": "#000000",
             "mixer": { "masterGainDb": 0, "dryWet": 0, "transitionMs": 20 } })");
    writeFile("b.json",
        R"({ "id": 2, "name": "Two", "color": "#000000",
             "mixer": { "masterGainDb": 0, "dryWet": 0, "transitionMs": 20 } })");
    writeFile("c.json", "not json");

    auto scenes = SceneLibrary::loadDirectory(tmp.string());
    REQUIRE(scenes.size() == 2);
    REQUIRE(scenes[0].id == 2);
    REQUIRE(scenes[1].id == 5);
}
