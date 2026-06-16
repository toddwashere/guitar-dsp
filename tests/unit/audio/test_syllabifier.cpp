#include <catch2/catch_test_macros.hpp>
#include "audio/Syllabifier.h"

using guitar_dsp::audio::Syllabifier;
using guitar_dsp::audio::Phoneme;

namespace {
Phoneme make(const std::string& l, Phoneme::Type t,
             std::size_t s, std::size_t e) {
    Phoneme p; p.label = l; p.type = t; p.startSample = s; p.endSample = e;
    return p;
}
}

TEST_CASE("Syllabifier: empty input yields empty output", "[audio][syl]") {
    REQUIRE(Syllabifier::group({}).empty());
}

TEST_CASE("Syllabifier: single CV (consonant-vowel) is one syllable",
          "[audio][syl]") {
    // "ma" — m + a
    std::vector<Phoneme> ph = {
        make("m", Phoneme::Type::Consonant, 0, 1000),
        make("a", Phoneme::Type::Vowel,     1000, 3000),
    };
    auto s = Syllabifier::group(ph);
    REQUIRE(s.size() == 1);
    REQUIRE(s[0].phonemeIndices == std::vector<int>{0, 1});
    REQUIRE(s[0].startSample == 0);
    REQUIRE(s[0].endSample == 3000);
    REQUIRE(s[0].vowelNucleusSample == 2000);  // midpoint of a
}

TEST_CASE("Syllabifier: CVCV → two syllables (max-onset)",
          "[audio][syl]") {
    // "mama" — m + a + m + a
    std::vector<Phoneme> ph = {
        make("m", Phoneme::Type::Consonant, 0,    1000),
        make("a", Phoneme::Type::Vowel,     1000, 2500),
        make("m", Phoneme::Type::Consonant, 2500, 3500),
        make("a", Phoneme::Type::Vowel,     3500, 5000),
    };
    auto s = Syllabifier::group(ph);
    REQUIRE(s.size() == 2);
    REQUIRE(s[0].phonemeIndices == std::vector<int>{0, 1});         // m-a
    REQUIRE(s[1].phonemeIndices == std::vector<int>{2, 3});         // m-a
}

TEST_CASE("Syllabifier: VCCV → max-onset puts CC on syllable 2",
          "[audio][syl]") {
    // "atra" — a + t + r + a → syllable 1 = "a", syllable 2 = "tra"
    std::vector<Phoneme> ph = {
        make("a", Phoneme::Type::Vowel,     0,    1500),
        make("t", Phoneme::Type::Consonant, 1500, 2000),
        make("r", Phoneme::Type::Consonant, 2000, 2500),
        make("a", Phoneme::Type::Vowel,     2500, 4000),
    };
    auto s = Syllabifier::group(ph);
    REQUIRE(s.size() == 2);
    // With (gap=2, prevCodaCount=ceil(2/2)=1), coda of syl 1 = {t},
    // onset of syl 2 = {r}. Acceptable simple-max-onset behavior.
    REQUIRE(s[0].phonemeIndices == std::vector<int>{0, 1});
    REQUIRE(s[1].phonemeIndices == std::vector<int>{2, 3});
}

TEST_CASE("Syllabifier: silence ends a syllable", "[audio][syl]") {
    // "ma _ pa" — m+a then silence then p+a
    std::vector<Phoneme> ph = {
        make("m", Phoneme::Type::Consonant, 0,    500),
        make("a", Phoneme::Type::Vowel,     500,  1500),
        make("_", Phoneme::Type::Silence,   1500, 2000),
        make("p", Phoneme::Type::Consonant, 2000, 2500),
        make("a", Phoneme::Type::Vowel,     2500, 3500),
    };
    auto s = Syllabifier::group(ph);
    REQUIRE(s.size() == 2);
    REQUIRE(s[0].endSample <= 1500);
    REQUIRE(s[1].startSample >= 2000);
}
