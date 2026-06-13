#include <catch2/catch_test_macros.hpp>
#include "audio/WordAligner.h"
#include "audio/TTSClip.h"

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

TEST_CASE("WordAligner: brief intra-word energy dips do not split a word",
          "[audio][aligner]") {
    // Two-word clip where word 2 has a ~10 ms internal energy dip (the kind
    // of stop-consonant dip a real TTS engine produces inside multi-syllable
    // words like "therefore" or "think"). The boundary must land in the
    // inter-word silence, not in the internal dip — regression check for
    // the multi-syllable splitting bug.
    const int rate = 48000;
    const int burst = 5000;
    const int interGap = 4000;     // ~83 ms inter-word silence
    const int innerDip = 480;      // ~10 ms within-word dip (e.g. a 't' stop)

    auto tone = [&](int n) {
        std::vector<float> v(n);
        for (int i = 0; i < n; ++i)
            v[i] = 0.6f * std::sin(2.0f*3.14159265f*300.0f*i/rate);
        return v;
    };
    auto sil = [](int n) { return std::vector<float>(n, 0.0f); };

    std::vector<float> s;
    auto w = tone(burst);     s.insert(s.end(), w.begin(), w.end());
    auto g = sil(interGap);   s.insert(s.end(), g.begin(), g.end());
    w = tone(burst/2);        s.insert(s.end(), w.begin(), w.end());
    auto d = sil(innerDip);   s.insert(s.end(), d.begin(), d.end());
    w = tone(burst/2);        s.insert(s.end(), w.begin(), w.end());

    auto segs = WordAligner::align(s, {"one","stop"}, rate);
    REQUIRE(segs.size() == 2);
    // Boundary must be inside the inter-word gap [burst..burst+interGap].
    REQUIRE(segs[0].endSample >= static_cast<size_t>(burst));
    REQUIRE(segs[0].endSample <= static_cast<size_t>(burst + interGap));
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

namespace {

// Helper: build samples with two "words" — 200 ms tone + 100 ms silence + 200 ms tone.
std::vector<float> makeTwoWordSamples() {
    constexpr int wordSamples = 48000 / 5;     // 200 ms
    constexpr int gapSamples  = 48000 / 10;    // 100 ms
    std::vector<float> samples(2 * wordSamples + gapSamples, 0.0f);
    for (int i = 0; i < wordSamples; ++i)
        samples[i] = 0.5f * std::sin(2.0 * 3.14159265 * 220.0 * i / 48000.0);
    const int word2Start = wordSamples + gapSamples;
    for (int i = 0; i < wordSamples; ++i)
        samples[word2Start + i] = 0.5f * std::sin(2.0 * 3.14159265 * 330.0 * i / 48000.0);
    return samples;
}

} // namespace

TEST_CASE("WordAligner::alignSyllables: each word's hyphen-count splits its time range",
          "[audio][word_aligner][syllables]") {
    const auto samples = makeTwoWordSamples();
    const std::vector<std::string> words           {"guitar",   "gently"};
    const std::vector<std::string> hyphenatedWords {"gui-tar",  "gent-ly"};
    auto syllables = WordAligner::alignSyllables(samples, words, hyphenatedWords, 48000.0);

    REQUIRE(syllables.size() == 4);
    REQUIRE(syllables[0].word == "gui");
    REQUIRE(syllables[1].word == "tar");
    REQUIRE(syllables[2].word == "gent");
    REQUIRE(syllables[3].word == "ly");

    // Each syllable's range must fit within its parent word's range.
    const auto wordSegs = WordAligner::align(samples, words, 48000.0);
    REQUIRE(syllables[0].startSample == wordSegs[0].startSample);
    REQUIRE(syllables[1].endSample   == wordSegs[0].endSample);
    REQUIRE(syllables[2].startSample == wordSegs[1].startSample);
    REQUIRE(syllables[3].endSample   == wordSegs[1].endSample);
    // Within word 0, syllables are equal halves (within +/- 1 sample).
    const std::size_t midWord0 = (wordSegs[0].startSample + wordSegs[0].endSample) / 2;
    const long delta = static_cast<long>(syllables[0].endSample) - static_cast<long>(midWord0);
    REQUIRE(delta <= 1);
    REQUIRE(delta >= -1);
}

TEST_CASE("WordAligner::alignSyllables: word without hyphen contributes one syllable",
          "[audio][word_aligner][syllables]") {
    const auto samples = makeTwoWordSamples();
    const std::vector<std::string> words           {"guitar", "speaks"};
    const std::vector<std::string> hyphenatedWords {"gui-tar", "speaks"};
    auto syllables = WordAligner::alignSyllables(samples, words, hyphenatedWords, 48000.0);
    REQUIRE(syllables.size() == 3);
    REQUIRE(syllables[2].word == "speaks");
}

TEST_CASE("WordAligner::alignSyllables: shape mismatch returns empty",
          "[audio][word_aligner][syllables]") {
    const auto samples = makeTwoWordSamples();
    const std::vector<std::string> words           {"guitar", "speaks"};
    const std::vector<std::string> hyphenatedWords {"gui-tar"};  // wrong size
    auto syllables = WordAligner::alignSyllables(samples, words, hyphenatedWords, 48000.0);
    REQUIRE(syllables.empty());
}
