#include <catch2/catch_test_macros.hpp>
#include "scenes/Scene.h"

using guitar_dsp::scenes::Scene;

TEST_CASE("Scene::resolvedGspeakPath falls back to gspeakPath when no voicePacks",
          "[scene][voicePacks]") {
    Scene s;
    s.gspeakPath = "assets/clips/gspeak/scene0.gspeak";
    CHECK(s.resolvedGspeakPath(0) == "assets/clips/gspeak/scene0.gspeak");
    CHECK(s.resolvedGspeakPath(99) == "assets/clips/gspeak/scene0.gspeak");
}

TEST_CASE("Scene::resolvedGspeakPath returns voicePacks[i].path when set",
          "[scene][voicePacks]") {
    Scene s;
    s.gspeakPath = "irrelevant.gspeak";
    s.voicePacks = {
        { "A", "a.gspeak" },
        { "B", "b.gspeak" },
        { "C", "c.gspeak" },
    };
    CHECK(s.resolvedGspeakPath(0) == "a.gspeak");
    CHECK(s.resolvedGspeakPath(1) == "b.gspeak");
    CHECK(s.resolvedGspeakPath(2) == "c.gspeak");
}

TEST_CASE("Scene::resolvedGspeakPath clamps out-of-range to defaultVoiceIndex",
          "[scene][voicePacks]") {
    Scene s;
    s.voicePacks = { { "A", "a.gspeak" }, { "B", "b.gspeak" } };
    s.defaultVoiceIndex = 1;
    CHECK(s.resolvedGspeakPath(-1) == "b.gspeak");
    CHECK(s.resolvedGspeakPath(99) == "b.gspeak");
}
