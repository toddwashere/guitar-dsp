#include <catch2/catch_test_macros.hpp>

#include "audio/AppleTTSSource.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

using guitar_dsp::audio::AppleTTSSource;

namespace {
bool ttsTestEnabled() {
    const char* env = std::getenv("GUITAR_DSP_TEST_APPLE_TTS");
    return env && std::string(env) == "1";
}
}

TEST_CASE("AppleTTSSource: sourceName is descriptive", "[audio][tts][apple]") {
    AppleTTSSource src;
    REQUIRE(src.sourceName() == "apple");
}

TEST_CASE("AppleTTSSource: empty text returns nullptr", "[audio][tts][apple]") {
    AppleTTSSource src;
    src.prepare(48000.0);
    REQUIRE_FALSE(src.synthesize(""));
}

TEST_CASE("AppleTTSSource: synthesize produces non-empty clip [opt-in]",
          "[audio][tts][apple][live]") {
    if (!ttsTestEnabled()) {
        SKIP("set GUITAR_DSP_TEST_APPLE_TTS=1 to run live synthesis");
    }
    AppleTTSSource src;
    src.prepare(48000.0);
    auto clip = src.synthesize("hello");
    REQUIRE(clip);
    REQUIRE(clip->sampleRate == 48000.0);
    REQUIRE(clip->samples.size() > 4800);  // at least 0.1 s of audio
    // Peak should be non-trivial (not all zeros).
    float peak = 0.0f;
    for (float s : clip->samples) peak = std::max(peak, std::abs(s));
    REQUIRE(peak > 0.001f);
}

TEST_CASE("AppleTTSSource: setVoice with a known identifier succeeds [opt-in]",
          "[audio][tts][apple][live]") {
    if (!ttsTestEnabled()) {
        SKIP("set GUITAR_DSP_TEST_APPLE_TTS=1 to run live synthesis");
    }
    AppleTTSSource src;
    src.prepare(48000.0);
    // Samantha is a long-standing default macOS voice; should resolve
    // on any modern macOS install.
    src.setVoice("com.apple.voice.compact.en-US.Samantha");
    auto clip = src.synthesize("voice test");
    REQUIRE(clip);
    REQUIRE(clip->samples.size() > 4800);
}
