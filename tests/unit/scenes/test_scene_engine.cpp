#include <catch2/catch_test_macros.hpp>
#include "scenes/SceneEngine.h"

using guitar_dsp::scenes::Scene;
using guitar_dsp::scenes::SceneEngine;
using guitar_dsp::scenes::MixerParams;

namespace {
Scene makeScene(int id, float gainDb) {
    Scene s = Scene::defaults(id);
    s.mixer.masterGainDb = gainDb;
    return s;
}
}

TEST_CASE("SceneEngine: starts with no active scene", "[scenes][engine]") {
    SceneEngine eng;
    REQUIRE(eng.getActiveSceneId() == -1);
    REQUIRE(eng.getSceneCount() == 0);
}

TEST_CASE("SceneEngine: loadScenes installs and activates the first by id",
          "[scenes][engine]") {
    SceneEngine eng;
    eng.loadScenes({ makeScene(2, -3.0f), makeScene(0, 0.0f), makeScene(5, -6.0f) });
    REQUIRE(eng.getSceneCount() == 3);
    REQUIRE(eng.getActiveSceneId() == 0);
    REQUIRE(eng.currentMixerParams().masterGainDb == 0.0f);
}

TEST_CASE("SceneEngine: activateScene by id snaps the mixer params",
          "[scenes][engine]") {
    SceneEngine eng;
    eng.loadScenes({ makeScene(0, 0.0f), makeScene(1, -6.0f), makeScene(2, -12.0f) });
    REQUIRE(eng.activateScene(2));
    REQUIRE(eng.getActiveSceneId() == 2);
    REQUIRE(eng.currentMixerParams().masterGainDb == -12.0f);
}

TEST_CASE("SceneEngine: activateScene returns false for unknown id",
          "[scenes][engine]") {
    SceneEngine eng;
    eng.loadScenes({ makeScene(0, 0.0f), makeScene(1, -6.0f) });
    REQUIRE_FALSE(eng.activateScene(99));
    REQUIRE(eng.getActiveSceneId() == 0);
}

TEST_CASE("SceneEngine: getActiveScene returns the live struct",
          "[scenes][engine]") {
    SceneEngine eng;
    eng.loadScenes({ makeScene(0, 0.0f), makeScene(1, -6.0f) });
    eng.activateScene(1);
    REQUIRE(eng.getActiveScene().id == 1);
    REQUIRE(eng.getActiveScene().mixer.masterGainDb == -6.0f);
}

TEST_CASE("SceneEngine: activeTtsKey reflects the active scene's TTS clip",
          "[scenes][engine][tts]") {
    SceneEngine eng;
    Scene s0 = Scene::defaults(0);
    Scene s1 = Scene::defaults(1);
    s1.tts.source = "prebaked";
    s1.tts.clip = "test_clip";
    eng.loadScenes({s0, s1});

    REQUIRE(eng.activeTtsKey().empty());  // scene 0 has no tts

    eng.activateScene(1);
    REQUIRE(eng.activeTtsKey() == "test_clip");

    eng.activateScene(0);
    REQUIRE(eng.activeTtsKey().empty());
}

TEST_CASE("SceneEngine: activeTtsConfig returns full tts block",
          "[scenes][engine][tts]") {
    SceneEngine eng;
    Scene s1 = Scene::defaults(1);
    s1.tts.source = "apple";
    s1.tts.text   = "live text";
    s1.tts.voice  = "com.apple.voice.compact.en-US.Samantha";
    s1.tts.clip   = "";

    eng.loadScenes({Scene::defaults(0), s1});
    eng.activateScene(1);

    const auto cfg = eng.activeTtsConfig();
    REQUIRE(cfg.source == "apple");
    REQUIRE(cfg.text   == "live text");
    REQUIRE(cfg.voice  == "com.apple.voice.compact.en-US.Samantha");
    REQUIRE(cfg.clip.empty());
}
