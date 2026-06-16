#include <catch2/catch_test_macros.hpp>
#include "audio/Phoneme.h"
using guitar_dsp::audio::Phoneme;

TEST_CASE("Phoneme type: vowel/consonant/silence classification",
          "[audio][phoneme]") {
    REQUIRE(guitar_dsp::audio::phonemeType("a")  == Phoneme::Type::Vowel);
    REQUIRE(guitar_dsp::audio::phonemeType("aI") == Phoneme::Type::Vowel);
    REQUIRE(guitar_dsp::audio::phonemeType("m")  == Phoneme::Type::Consonant);
    REQUIRE(guitar_dsp::audio::phonemeType("_")  == Phoneme::Type::Silence);
    REQUIRE(guitar_dsp::audio::phonemeType("")   == Phoneme::Type::Silence);
}

TEST_CASE("Phoneme sonority: vowel > liquid > nasal > fric > stop",
          "[audio][phoneme]") {
    using guitar_dsp::audio::phonemeSonority;
    REQUIRE(phonemeSonority("a") > phonemeSonority("r"));
    REQUIRE(phonemeSonority("r") > phonemeSonority("m"));
    REQUIRE(phonemeSonority("m") > phonemeSonority("s"));
    REQUIRE(phonemeSonority("s") > phonemeSonority("t"));
}
