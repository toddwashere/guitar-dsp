#include <catch2/catch_test_macros.hpp>
#include "audio/Harmonizer.h"

#include <algorithm>
#include <cmath>
#include <vector>

using guitar_dsp::audio::Harmonizer;

namespace {
std::vector<float> sine(int n, float hz) {
    std::vector<float> b(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) b[static_cast<size_t>(i)] = 0.5f*std::sin(2.0f*3.14159265f*hz*i/48000.0f);
    return b;
}
}

TEST_CASE("Harmonizer: unison voice + full dry ~ passes signal", "[audio][harmonizer]") {
    Harmonizer h;
    h.prepare(48000.0, 4096);
    const int semis[1]  = {0};
    const int cents[1]  = {0};
    h.setVoices(semis, cents, 1, 40.0f);
    h.setMix(0.0f);
    auto in = sine(2048, 330.0f);
    float peak = 0.0f;
    for (float s : in) peak = std::max(peak, std::fabs(h.processSample(s)));
    REQUIRE(peak > 0.4f);
    REQUIRE(peak < 0.6f);
}

TEST_CASE("Harmonizer: three voices stay finite + bounded", "[audio][harmonizer]") {
    Harmonizer h;
    h.prepare(48000.0, 4096);
    const int semis[3] = {12, 7, 0};
    const int cents[3] = {0, 0, 6};
    h.setVoices(semis, cents, 3, 40.0f);
    h.setMix(0.8f);
    auto in = sine(8192, 196.0f);
    for (float s : in) {
        const float y = h.processSample(s);
        REQUIRE(std::isfinite(y));
        REQUIRE(std::fabs(y) < 4.0f);
    }
}

TEST_CASE("Harmonizer: zero voices is pure dry", "[audio][harmonizer]") {
    Harmonizer h;
    h.prepare(48000.0, 4096);
    h.setVoices(nullptr, nullptr, 0, 40.0f);
    h.setMix(1.0f);
    REQUIRE(h.processSample(0.42f) == 0.42f);
}
