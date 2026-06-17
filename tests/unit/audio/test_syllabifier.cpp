#include <catch2/catch_test_macros.hpp>
#include "audio/Syllabifier.h"

using guitar_dsp::audio::Syllabifier;
using guitar_dsp::audio::SyllableSpan;
using guitar_dsp::audio::Phoneme;
using guitar_dsp::audio::moveBoundary;
using guitar_dsp::audio::addBoundary;
using guitar_dsp::audio::removeBoundary;

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

// ---------------------------------------------------------------------------
// Slice-editing free functions
// ---------------------------------------------------------------------------

TEST_CASE("moveBoundary: clamps within neighbor bounds + min width",
          "[audio][slice]") {
    std::vector<SyllableSpan> syls = {
        { /*phonemeIndices*/ {}, 0,    1000, 500,  300,  700,  false },
        { {},                   1000, 2000, 1500, 1300, 1700, false },
    };
    std::vector<float> audio(2000, 0.5f);  // flat — anchor refinement is moot

    const auto newPos = moveBoundary(syls, 1, 1200, audio, 48000.0, 100);
    REQUIRE(newPos == 1200);
    REQUIRE(syls[0].endSample   == 1200);
    REQUIRE(syls[1].startSample == 1200);

    // Too close to syls[1].endSample (min width 100); should clamp to 1900.
    const auto blockedHigh = moveBoundary(syls, 1, 1950, audio, 48000.0, 100);
    REQUIRE(blockedHigh == 1900);  // clamped to 2000 - 100
    REQUIRE(syls[0].endSample   == 1900);
    REQUIRE(syls[1].startSample == 1900);
}

TEST_CASE("moveBoundary: boundary 0 and last are non-editable",
          "[audio][slice]") {
    std::vector<SyllableSpan> syls = {
        { {}, 0,    1000, 500,  300,  700,  false },
        { {}, 1000, 2000, 1500, 1300, 1700, false },
    };
    std::vector<float> audio(2000, 0.5f);

    // Boundary 0 — should return clip start (0), not crash.
    const auto r0 = moveBoundary(syls, 0, 500, audio, 48000.0, 100);
    REQUIRE(r0 == 0);
    REQUIRE(syls[0].startSample == 0);  // unchanged

    // Boundary syls.size() — clip end, unchanged.
    const auto rN = moveBoundary(syls, syls.size(), 1500, audio, 48000.0, 100);
    REQUIRE(rN == 2000);
    REQUIRE(syls.back().endSample == 2000);  // unchanged
}

TEST_CASE("addBoundary: splits a syllable into two", "[audio][slice]") {
    std::vector<SyllableSpan> syls = {
        { {}, 0, 2000, 1000, 500, 1500, false },
    };
    std::vector<float> audio(2000, 0.5f);

    REQUIRE(addBoundary(syls, 1000, audio, 48000.0, 100));
    REQUIRE(syls.size() == 2);
    REQUIRE(syls[0].startSample == 0);
    REQUIRE(syls[0].endSample   == 1000);
    REQUIRE(syls[1].startSample == 1000);
    REQUIRE(syls[1].endSample   == 2000);
}

TEST_CASE("addBoundary: rejects insertion too close to existing boundary",
          "[audio][slice]") {
    std::vector<SyllableSpan> syls = {
        { {}, 0,    1000, 500,  300,  700,  false },
        { {}, 1000, 2000, 1500, 1300, 1700, false },
    };
    std::vector<float> audio(2000, 0.5f);

    // 1050 is only 50 samples from the boundary at 1000 — below minWidth 100.
    REQUIRE_FALSE(addBoundary(syls, 1050, audio, 48000.0, 100));
    REQUIRE(syls.size() == 2);  // unchanged
}

TEST_CASE("removeBoundary: merges adjacent syllables", "[audio][slice]") {
    std::vector<SyllableSpan> syls = {
        { {}, 0,    1000, 500,  300,  700,  false },
        { {}, 1000, 2000, 1500, 1300, 1700, false },
        { {}, 2000, 3000, 2500, 2300, 2700, false },
    };
    std::vector<float> audio(3000, 0.5f);

    REQUIRE(removeBoundary(syls, 1, audio, 48000.0));
    REQUIRE(syls.size() == 2);
    REQUIRE(syls[0].startSample == 0);
    REQUIRE(syls[0].endSample   == 2000);   // merged
    REQUIRE(syls[1].startSample == 2000);
    REQUIRE(syls[1].endSample   == 3000);   // unchanged
}

TEST_CASE("removeBoundary: rejects boundary 0 or last", "[audio][slice]") {
    std::vector<SyllableSpan> syls = {
        { {}, 0,    1000, 500,  300,  700,  false },
        { {}, 1000, 2000, 1500, 1300, 1700, false },
    };
    std::vector<float> audio(2000, 0.5f);

    REQUIRE_FALSE(removeBoundary(syls, 0,          audio, 48000.0));
    REQUIRE_FALSE(removeBoundary(syls, syls.size(), audio, 48000.0));
    REQUIRE(syls.size() == 2);  // unchanged
}
