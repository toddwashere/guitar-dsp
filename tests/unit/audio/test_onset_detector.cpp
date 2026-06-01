#include <catch2/catch_test_macros.hpp>
#include "audio/OnsetDetector.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::OnsetDetector;

namespace {
void pluck(std::vector<float>& buf, int burstLen, int gapLen, float amp, double sr) {
    const int start = static_cast<int>(buf.size());
    for (int i = 0; i < burstLen; ++i) {
        const float env = amp * std::exp(-i / (0.05f * static_cast<float>(sr)));
        buf.push_back(env * std::sin(2.0f*3.14159265f*220.0f*(start+i)/sr));
    }
    for (int i = 0; i < gapLen; ++i) buf.push_back(0.0f);
}
int countOnsets(OnsetDetector& d, const std::vector<float>& buf) {
    int n = 0;
    for (float x : buf) if (d.processSample(x)) ++n;
    return n;
}
}

TEST_CASE("OnsetDetector: one onset per pluck", "[audio][onset]") {
    OnsetDetector d;
    d.prepare(48000.0);
    std::vector<float> buf;
    for (int k = 0; k < 5; ++k) pluck(buf, 4000, 4000, 0.8f, 48000.0);
    REQUIRE(countOnsets(d, buf) == 5);
}

TEST_CASE("OnsetDetector: sustained note fires once", "[audio][onset]") {
    OnsetDetector d;
    d.prepare(48000.0);
    std::vector<float> buf(48000);
    for (size_t i = 0; i < buf.size(); ++i)
        buf[i] = 0.8f * std::sin(2.0f*3.14159265f*220.0f*i/48000.0f);
    REQUIRE(countOnsets(d, buf) == 1);
}

TEST_CASE("OnsetDetector: silence produces no onsets", "[audio][onset]") {
    OnsetDetector d;
    d.prepare(48000.0);
    std::vector<float> buf(48000, 0.0f);
    REQUIRE(countOnsets(d, buf) == 0);
}

TEST_CASE("OnsetDetector: debounce suppresses a rapid double-hit",
          "[audio][onset]") {
    OnsetDetector d;
    d.prepare(48000.0);
    d.setDebounceMs(80.0f);
    std::vector<float> buf;
    pluck(buf, 400, 100, 0.8f, 48000.0);
    pluck(buf, 400, 4000, 0.8f, 48000.0);
    REQUIRE(countOnsets(d, buf) == 1);
}
