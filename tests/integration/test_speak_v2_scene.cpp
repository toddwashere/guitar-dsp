#include <catch2/catch_test_macros.hpp>
#include "audio/PhonemeAlignedClipBuilder.h"
#include "audio/PhonemeExtractor.h"
#include "audio/PhonemeSteppedTTSPlayer.h"
#include "audio/PiperTTSSource.h"

#include <vector>

using namespace guitar_dsp::audio;

TEST_CASE("integration: Say-textbox text → phoneme-aligned clip → player",
          "[integration][speakv2]") {
    PiperTTSSource piper("assets/piper/piper",
                         "assets/piper/voices/en_US-amy-medium.onnx");
    piper.prepare(48000.0);
    PhonemeExtractor phex("assets/piper/espeak-ng");
    if (!piper.isReady() || !phex.isReady()) {
        WARN("piper/espeak-ng not ready — skipping");
        return;
    }
    PhonemeAlignedClipBuilder b(&piper, &phex);
    auto clip = b.build("automatically learn");
    REQUIRE(clip);
    REQUIRE(clip->sylsV2.size() >= 4);   // 5-syl + 2-syl = 7-ish

    PhonemeSteppedTTSPlayer p;
    p.prepare(48000.0, 256);
    p.setMaxSustainMs(200.0);   // short sustain so 1-s silence is enough to re-arm
    p.setClip(clip);

    auto pluck = [&](int n) {
        std::vector<float> onset(n, 0.0f), mod(n, 0.0f);
        for (int i = 0; i < 480 && i < n; ++i) onset[i] = 0.8f;
        p.process(onset.data(), mod.data(), mod.size());
    };

    pluck(1000);
    REQUIRE(p.currentSyllableIndex() == 0);
    std::vector<float> silence(48000, 0.0f), mod(48000, 0.0f);
    p.process(silence.data(), mod.data(), mod.size());
    pluck(1000);
    REQUIRE(p.currentSyllableIndex() == 1);
}
