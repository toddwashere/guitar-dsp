#include <catch2/catch_test_macros.hpp>
#include "scenes/SceneLibrary.h"

#include <filesystem>
#include <stdexcept>

using namespace guitar_dsp::scenes;

namespace {
std::string sceneAssetsDir() {
    auto p = std::filesystem::current_path();
    for (int i = 0; i < 6; ++i) {
        auto c = p / "assets" / "scenes";
        if (std::filesystem::exists(c)) return c.string();
        p = p.parent_path();
    }
    throw std::runtime_error("assets/scenes not found");
}
} // namespace

TEST_CASE("SceneLibrary: parses speech.player=phonemeStepped",
          "[scenes][speechv2]") {
    auto scenes = SceneLibrary::loadDirectory(sceneAssetsDir());
    const Scene* s = nullptr;
    for (const auto& sc : scenes) if (sc.id == 10) { s = &sc; break; }
    REQUIRE(s);
    REQUIRE(s->speech.player == Scene::Speech::Player::PhonemeStepped);
    REQUIRE(s->speech.maxSustainMs == 0.0);   // stutter-fix: grain-loop instantly falls through to Coda
    REQUIRE(s->speech.attackInterrupt == Scene::Speech::AttackInterrupt::Finish);
}

TEST_CASE("SceneLibrary: speech defaults to NoteStepped for scenes without speech block",
          "[scenes][speechv2]") {
    auto scenes = SceneLibrary::loadDirectory(sceneAssetsDir());
    // Scene 0 (intro) has no speech block — should default to NoteStepped
    const Scene* s = nullptr;
    for (const auto& sc : scenes) if (sc.id == 0) { s = &sc; break; }
    REQUIRE(s);
    REQUIRE(s->speech.player == Scene::Speech::Player::NoteStepped);
    REQUIRE(s->speech.maxSustainMs == 1500.0);
    REQUIRE(s->speech.attackInterrupt == Scene::Speech::AttackInterrupt::Finish);
}
