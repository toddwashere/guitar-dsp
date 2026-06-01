#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "scenes/SceneLibrary.h"

#include <filesystem>

using Catch::Matchers::WithinAbs;
using guitar_dsp::scenes::SceneLibrary;
using guitar_dsp::scenes::CarouselConfig;

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

TEST_CASE("SceneLibrary: parses carousel pitch/harmonizer/comb/formant",
          "[scenes][library][carousel][pitch]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_carousel_pitch.json"));
    REQUIRE(s.has_value());
    const auto& c = s->carousel;

    REQUIRE_THAT(c.pitchSemitones, WithinAbs(12.0f, 1e-4f));
    REQUIRE_THAT(c.pitchMix, WithinAbs(0.5f, 1e-4f));
    REQUIRE_THAT(c.pitchGrainMs, WithinAbs(40.0f, 1e-4f));

    REQUIRE(c.harmVoiceCount == 3);
    REQUIRE(c.harmSemitones[0] == 12);
    REQUIRE(c.harmSemitones[1] == 7);
    REQUIRE(c.harmSemitones[2] == 0);
    REQUIRE(c.harmDetuneCents[2] == 6);
    REQUIRE_THAT(c.harmMix, WithinAbs(0.7f, 1e-4f));

    REQUIRE_THAT(c.combFreqHz, WithinAbs(220.0f, 1e-3f));
    REQUIRE_THAT(c.combFeedback, WithinAbs(0.6f, 1e-4f));
    REQUIRE_THAT(c.combMix, WithinAbs(0.5f, 1e-4f));

    REQUIRE(c.formantVowel == CarouselConfig::Vowel::Ah);
    REQUIRE_THAT(c.formantAmount, WithinAbs(0.6f, 1e-4f));
}

TEST_CASE("SceneLibrary: missing pitch blocks leave bypass defaults",
          "[scenes][library][carousel][pitch]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_carousel.json"));
    REQUIRE(s.has_value());
    REQUIRE(s->carousel.pitchSemitones == 0.0f);
    REQUIRE(s->carousel.harmVoiceCount == 0);
    REQUIRE(s->carousel.combFreqHz == 0.0f);
    REQUIRE(s->carousel.formantVowel == CarouselConfig::Vowel::None);
}
