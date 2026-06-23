#include <catch2/catch_test_macros.hpp>
#include "scenes/SceneLibrary.h"

using guitar_dsp::scenes::Scene;
using guitar_dsp::scenes::SceneLibrary;

TEST_CASE("SceneLibrary parses directShift block",
          "[scene-library][direct-shift]") {
    juce::File tmp = juce::File::createTempFile("_test_direct_shift.json");
    tmp.replaceWithText(R"({
        "id": 12,
        "name": "Sung Direct",
        "directShift": {
            "enabled": true,
            "engine": "world",
            "formantPreserve": true,
            "formantTintSemitones": 1.5,
            "portamentoMs": 40.0,
            "scoopInMs": 15.0
        },
        "showSungDirectPanel": true
    })");
    auto s = SceneLibrary::loadOne(tmp.getFullPathName().toStdString());
    REQUIRE(s.has_value());
    CHECK(s->directShift.enabled         == true);
    CHECK(s->directShift.engine          == "world");
    CHECK(s->directShift.formantPreserve == true);
    CHECK(s->directShift.formantTintSemitones == Approx(1.5f));
    CHECK(s->directShift.portamentoMs    == Approx(40.0f));
    CHECK(s->directShift.scoopInMs       == Approx(15.0f));
    CHECK(s->showSungDirectPanel         == true);
    tmp.deleteFile();
}

TEST_CASE("SceneLibrary defaults directShift to disabled",
          "[scene-library][direct-shift][backcompat]") {
    juce::File tmp = juce::File::createTempFile("_test_no_direct_shift.json");
    tmp.replaceWithText(R"({ "id": 0, "name": "Legacy" })");
    auto s = SceneLibrary::loadOne(tmp.getFullPathName().toStdString());
    REQUIRE(s.has_value());
    CHECK(s->directShift.enabled == false);
    CHECK(s->showSungDirectPanel == false);
    tmp.deleteFile();
}
