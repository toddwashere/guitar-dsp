#include <catch2/catch_test_macros.hpp>

#include "audio/PiperTTSSource.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

using guitar_dsp::audio::PiperTTSSource;

namespace {
bool piperTestEnabled() {
    const char* env = std::getenv("GUITAR_DSP_TEST_PIPER");
    return env && std::string(env) == "1";
}
}

TEST_CASE("PiperTTSSource: sourceName is descriptive", "[audio][tts][piper]") {
    PiperTTSSource src{"/nonexistent/binary", "/nonexistent/voice.onnx"};
    REQUIRE(src.sourceName() == "piper");
}

TEST_CASE("PiperTTSSource: isReady returns false for missing binary",
          "[audio][tts][piper]") {
    PiperTTSSource src{"/tmp/definitely_not_a_real_path_xyz",
                       "/tmp/definitely_not_a_real_voice.onnx"};
    src.prepare(48000.0);
    REQUIRE_FALSE(src.isReady());
}

TEST_CASE("PiperTTSSource: synthesize returns nullptr when not ready",
          "[audio][tts][piper]") {
    PiperTTSSource src{"/tmp/definitely_not_a_real_path_xyz",
                       "/tmp/definitely_not_a_real_voice.onnx"};
    src.prepare(48000.0);
    REQUIRE_FALSE(src.synthesize("hello"));
}

TEST_CASE("PiperTTSSource: empty text returns nullptr",
          "[audio][tts][piper]") {
    PiperTTSSource src{"/tmp/definitely_not_a_real_path_xyz",
                       "/tmp/definitely_not_a_real_voice.onnx"};
    src.prepare(48000.0);
    REQUIRE_FALSE(src.synthesize(""));
}

TEST_CASE("PiperTTSSource: live synthesis produces audio [opt-in]",
          "[audio][tts][piper][live]") {
    if (!piperTestEnabled()) {
        SKIP("set GUITAR_DSP_TEST_PIPER=1 and provide GUITAR_DSP_PIPER_BIN/VOICE to run");
    }
    const char* bin   = std::getenv("GUITAR_DSP_PIPER_BIN");
    const char* voice = std::getenv("GUITAR_DSP_PIPER_VOICE");
    if (!bin || !voice) {
        SKIP("set GUITAR_DSP_PIPER_BIN and GUITAR_DSP_PIPER_VOICE to point at "
             "a Piper binary and .onnx voice respectively");
    }

    PiperTTSSource src{bin, voice};
    src.prepare(48000.0);
    REQUIRE(src.isReady());

    auto clip = src.synthesize("hello world");
    REQUIRE(clip);
    REQUIRE(clip->sampleRate == 48000.0);
    REQUIRE(clip->samples.size() > 4800);  // > 0.1 s

    float peak = 0.0f;
    for (float s : clip->samples) peak = std::max(peak, std::abs(s));
    REQUIRE(peak > 0.001f);
}
