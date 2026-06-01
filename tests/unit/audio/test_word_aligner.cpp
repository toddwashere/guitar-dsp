#include <catch2/catch_test_macros.hpp>
#include "audio/WordAligner.h"

#include <cmath>
#include <string>
#include <vector>

using guitar_dsp::audio::WordAligner;
using guitar_dsp::audio::WordSegment;

namespace {
std::vector<float> bursts(int nWords, int burstLen, int gapLen) {
    std::vector<float> b;
    for (int w = 0; w < nWords; ++w) {
        for (int i = 0; i < burstLen; ++i)
            b.push_back(0.6f * std::sin(2.0f*3.14159265f*300.0f*i/48000.0f));
        if (w < nWords - 1) for (int i = 0; i < gapLen; ++i) b.push_back(0.0f);
    }
    return b;
}
}

TEST_CASE("WordAligner: segments a 3-word clip on its gaps", "[audio][aligner]") {
    const int burst = 8000, gap = 4000;
    auto samples = bursts(3, burst, gap);
    std::vector<std::string> words{"one", "two", "three"};

    auto segs = WordAligner::align(samples, words, 48000.0);
    REQUIRE(segs.size() == 3);
    REQUIRE(segs[0].word == "one");
    REQUIRE(segs[2].word == "three");
    REQUIRE(segs[0].startSample == 0);
    REQUIRE(segs[2].endSample == samples.size());
    for (size_t i = 1; i < segs.size(); ++i)
        REQUIRE(segs[i].startSample == segs[i-1].endSample);
    REQUIRE(segs[0].endSample > static_cast<size_t>(burst));
    REQUIRE(segs[0].endSample < static_cast<size_t>(burst + gap));
}

TEST_CASE("WordAligner: single word spans the whole clip", "[audio][aligner]") {
    std::vector<float> samples(10000, 0.5f);
    auto segs = WordAligner::align(samples, {"solo"}, 48000.0);
    REQUIRE(segs.size() == 1);
    REQUIRE(segs[0].startSample == 0);
    REQUIRE(segs[0].endSample == 10000);
}

TEST_CASE("WordAligner: empty clip or no words yields no segments",
          "[audio][aligner]") {
    REQUIRE(WordAligner::align({}, {"a","b"}, 48000.0).empty());
    std::vector<float> s(100, 0.1f);
    REQUIRE(WordAligner::align(s, {}, 48000.0).empty());
}

TEST_CASE("WordAligner: gapless clip still returns N even segments",
          "[audio][aligner]") {
    std::vector<float> samples(9000, 0.5f);
    auto segs = WordAligner::align(samples, {"a","b","c"}, 48000.0);
    REQUIRE(segs.size() == 3);
    REQUIRE(segs[0].startSample == 0);
    REQUIRE(segs[2].endSample == 9000);
    for (size_t i = 1; i < segs.size(); ++i)
        REQUIRE(segs[i].startSample == segs[i-1].endSample);
}
