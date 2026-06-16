#include <catch2/catch_test_macros.hpp>
#include "audio/PhonemeAlignedClipBuilder.h"
#include "audio/PiperTTSSource.h"
#include "audio/PhonemeExtractor.h"

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
