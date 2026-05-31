#include <catch2/catch_test_macros.hpp>

#include "audio/PiperTTSSource.h"

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
