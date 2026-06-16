#include <catch2/catch_test_macros.hpp>
#include "audio/PhonemeAlignedClipBuilder.h"
#include "audio/PiperTTSSource.h"
#include "audio/PhonemeExtractor.h"

#include <cmath>

using namespace guitar_dsp::audio;

TEST_CASE("PhonemeAlignedClipBuilder: end-to-end produces sylsV2",
          "[audio][builder][e2e]") {
    PiperTTSSource piper("assets/piper/piper",
                         "assets/piper/voices/en_US-amy-medium.onnx");
    piper.prepare(48000.0);
    PhonemeExtractor phex("assets/piper/espeak-ng");
    if (!piper.isReady() || !phex.isReady()) {
        WARN("piper/espeak-ng not ready — skipping");
        return;
    }
    PhonemeAlignedClipBuilder b(&piper, &phex);
    auto clip = b.build("hello world");
    REQUIRE(clip);
    REQUIRE(clip->samples.size() > 0);
    REQUIRE(!clip->phonemes.empty());
    REQUIRE(!clip->sylsV2.empty());
    // Syllables should cover audio start-to-end approximately.
    REQUIRE(clip->sylsV2.front().startSample == 0);
    REQUIRE(clip->sylsV2.back().endSample <= clip->samples.size());
    REQUIRE(clip->sylsV2.back().endSample
            >= clip->samples.size() * 9 / 10);  // within last 10 %
}

TEST_CASE("PhonemeAlignedClipBuilder: refines vowel anchor to energy peak",
          "[audio][builder][refine]") {
    using namespace guitar_dsp::audio;
    PiperTTSSource piper("assets/piper/piper",
                         "assets/piper/voices/en_US-amy-medium.onnx");
    piper.prepare(48000.0);
    PhonemeExtractor phex("assets/piper/espeak-ng");
    if (!piper.isReady() || !phex.isReady()) {
        WARN("piper/espeak-ng not ready — skipping");
        return;
    }
    PhonemeAlignedClipBuilder b(&piper, &phex);
    auto clip = b.build("hello world");
    REQUIRE(clip);
    REQUIRE(!clip->sylsV2.empty());

    // For each syllable, the RMS at vowelNucleusSample should be >= RMS at
    // startSample and >= RMS at endSample (a true peak in the energy curve).
    const auto rmsAt = [&](std::size_t center) {
        const std::size_t win = 240;  // 5 ms @ 48 kHz
        const std::size_t lo = center > win/2 ? center - win/2 : 0;
        const std::size_t hi = std::min(center + win/2, clip->samples.size());
        if (hi <= lo) return 0.0;
        double s = 0.0;
        for (std::size_t i = lo; i < hi; ++i) s += double(clip->samples[i]) * clip->samples[i];
        return std::sqrt(s / double(hi - lo));
    };
    for (const auto& syl : clip->sylsV2) {
        const auto e_nuc   = rmsAt(syl.vowelNucleusSample);
        const auto e_start = rmsAt(syl.startSample);
        const auto e_end   = syl.endSample > 0 ? rmsAt(syl.endSample - 1) : 0.0;
        INFO("syl: start=" << syl.startSample << " nuc=" << syl.vowelNucleusSample
             << " end=" << syl.endSample
             << " | rms start=" << e_start << " nuc=" << e_nuc << " end=" << e_end);
        REQUIRE(e_nuc >= e_start * 0.9);   // small margin for tied-peak ties
        REQUIRE(e_nuc >= e_end   * 0.9);
    }
}
