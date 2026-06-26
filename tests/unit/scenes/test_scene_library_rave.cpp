#include <catch2/catch_test_macros.hpp>
#include "scenes/SceneLibrary.h"

#include <filesystem>
#include <fstream>

using guitar_dsp::scenes::SceneLibrary;

namespace {
std::string writeTempJson(const std::string& contents) {
    auto path = std::filesystem::temp_directory_path() / "rave_scene_test.json";
    std::ofstream(path) << contents;
    return path.string();
}
} // namespace

TEST_CASE("SceneLibrary: missing raveConfig defaults to disabled", "[scenes][rave]") {
    const auto p = writeTempJson(R"({ "id": 99, "name": "Bare", "mixer": { "dryWet": 0.5 } })");
    SceneLibrary lib;
    auto scene = lib.loadOne(p);
    REQUIRE(scene.has_value());
    REQUIRE(scene->raveConfig.enabled == false);
    REQUIRE(scene->showRave == false);
}

TEST_CASE("SceneLibrary: raveConfig present is parsed", "[scenes][rave]") {
    const auto p = writeTempJson(R"({
        "id": 5,
        "name": "Neural Voice",
        "showRave": true,
        "rave": {
            "enabled": true,
            "gateDb": -35.0,
            "presence": 0.7,
            "driveDb": 3.0,
            "dryWet": 0.9
        }
    })");
    SceneLibrary lib;
    auto scene = lib.loadOne(p);
    REQUIRE(scene.has_value());
    REQUIRE(scene->raveConfig.enabled);
    REQUIRE(scene->raveConfig.gateDb == -35.0f);
    REQUIRE(scene->raveConfig.presence == 0.7f);
    REQUIRE(scene->raveConfig.driveDb == 3.0f);
    REQUIRE(scene->raveConfig.dryWet == 0.9f);
    REQUIRE(scene->showRave);
}

TEST_CASE("SceneLibrary: out-of-range raveConfig is clamped", "[scenes][rave]") {
    const auto p = writeTempJson(R"({
        "id": 5,
        "name": "Bad",
        "rave": { "enabled": true, "gateDb": -200.0, "presence": 9.0, "driveDb": 999.0, "dryWet": -1.0 }
    })");
    SceneLibrary lib;
    auto scene = lib.loadOne(p);
    REQUIRE(scene.has_value());
    REQUIRE(scene->raveConfig.gateDb == -80.0f);
    REQUIRE(scene->raveConfig.presence == 1.0f);
    REQUIRE(scene->raveConfig.driveDb == 12.0f);
    REQUIRE(scene->raveConfig.dryWet == 0.0f);
}
