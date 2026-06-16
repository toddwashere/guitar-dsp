#include <catch2/catch_test_macros.hpp>
#include "audio/PhonemeExtractor.h"

using guitar_dsp::audio::PhonemeExtractor;
using guitar_dsp::audio::Phoneme;

TEST_CASE("PhonemeExtractor: extracts phonemes from a 2-word phrase",
          "[audio][phex]") {
    PhonemeExtractor pe("assets/piper/espeak-ng");
    if (!pe.isReady()) {
        WARN("espeak-ng not present: " + pe.statusDetail());
        return;
    }
    auto ph = pe.extract("hello world", 48000.0);
    REQUIRE(ph.size() >= 6);            // h-e-l-o w-o-r-l-d roughly
    // First phoneme should start at sample 0.
    REQUIRE(ph.front().startSample == 0);
    // Phonemes should be contiguous in time.
    for (std::size_t i = 1; i < ph.size(); ++i)
        REQUIRE(ph[i].startSample == ph[i-1].endSample);
    // Should contain at least one vowel.
    bool hasVowel = false;
    for (const auto& p : ph)
        if (p.type == Phoneme::Type::Vowel) { hasVowel = true; break; }
    REQUIRE(hasVowel);
}

TEST_CASE("PhonemeExtractor: empty text returns empty", "[audio][phex]") {
    PhonemeExtractor pe("assets/piper/espeak-ng");
    REQUIRE(pe.extract("", 48000.0).empty());
}

TEST_CASE("PhonemeExtractor: bad binary path is detected", "[audio][phex]") {
    PhonemeExtractor pe("/nonexistent/espeak-ng");
    REQUIRE_FALSE(pe.isReady());
    REQUIRE(pe.extract("hello", 48000.0).empty());
}
