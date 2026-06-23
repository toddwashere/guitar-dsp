#include <catch2/catch_test_macros.hpp>
#include "scenes/SceneLibrary.h"

#include <filesystem>
#include <fstream>

using guitar_dsp::scenes::Scene;
using guitar_dsp::scenes::SceneLibrary;

TEST_CASE("SceneLibrary parses voicePacks for scene 11",
          "[scene-library][voicePacks]") {
    // Synthesize the JSON inline so the test is self-contained.
    const auto tmp = std::filesystem::temp_directory_path() / "guitar_dsp_test_voice_packs.json";
    {
        std::ofstream f(tmp);
        f << R"({
        "id": 11,
        "name": "Sung Vowels",
        "voicePacks": [
            { "label": "Male 1",     "path": "assets/clips/gspeak/scene11_sung_m1.gspeak"  },
            { "label": "Mighty Man", "path": "assets/clips/gspeak/scene11_sung_m10.gspeak" },
            { "label": "Female 2",   "path": "assets/clips/gspeak/scene11_sung_f2.gspeak"  },
            { "label": "Female 8",   "path": "assets/clips/gspeak/scene11_sung_f8.gspeak"  }
        ],
        "defaultVoiceIndex": 0,
        "showVoicePackPicker": true,
        "gspeakAutoLoad": true
    })";
    }

    auto s = SceneLibrary::loadOne(tmp.string());
    REQUIRE(s.has_value());
    REQUIRE(s->voicePacks.size() == 4);
    CHECK(s->voicePacks[0].label == "Male 1");
    CHECK(s->voicePacks[1].label == "Mighty Man");
    CHECK(s->voicePacks[2].label == "Female 2");
    CHECK(s->voicePacks[3].label == "Female 8");
    CHECK(s->voicePacks[0].path  == "assets/clips/gspeak/scene11_sung_m1.gspeak");
    CHECK(s->defaultVoiceIndex == 0);
    CHECK(s->showVoicePackPicker == true);
    CHECK(s->gspeakAutoLoad == true);

    std::filesystem::remove(tmp);
}

TEST_CASE("SceneLibrary leaves voicePacks empty when absent",
          "[scene-library][voicePacks][backcompat]") {
    const auto tmp = std::filesystem::temp_directory_path() / "guitar_dsp_test_no_voice_packs.json";
    {
        std::ofstream f(tmp);
        f << R"({
        "id": 0,
        "name": "Legacy",
        "gspeakPath": "assets/clips/gspeak/scene0.gspeak"
    })";
    }
    auto s = SceneLibrary::loadOne(tmp.string());
    REQUIRE(s.has_value());
    CHECK(s->voicePacks.empty());
    CHECK(s->defaultVoiceIndex == 0);
    CHECK(s->showVoicePackPicker == false);
    CHECK(s->gspeakPath == "assets/clips/gspeak/scene0.gspeak");
    std::filesystem::remove(tmp);
}
