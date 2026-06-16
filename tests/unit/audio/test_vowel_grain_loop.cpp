#include <catch2/catch_test_macros.hpp>
#include "audio/VowelGrainLoop.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::VowelGrainLoop;

TEST_CASE("VowelGrainLoop: returns 0 before beginLoop", "[audio][grain]") {
    VowelGrainLoop g;
    g.prepare(48000.0);
    REQUIRE(g.next() == 0.0f);
}

TEST_CASE("VowelGrainLoop: produces non-zero output after beginLoop on a sine",
          "[audio][grain]") {
    std::vector<float> samples(48000, 0.0f);
    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i] = 0.5f * std::sin(2 * 3.14159265f * 440.0f * i / 48000.0f);
    VowelGrainLoop g;
    g.prepare(48000.0);
    g.beginLoop(samples.data(), 24000);  // anchor mid-buffer

    float maxAbs = 0.0f;
    for (int i = 0; i < 2400; ++i) {  // 50 ms of output
        const float v = g.next();
        if (std::abs(v) > maxAbs) maxAbs = std::abs(v);
    }
    REQUIRE(maxAbs > 0.1f);
}

TEST_CASE("VowelGrainLoop: wraps without reading past clip end",
          "[audio][grain]") {
    std::vector<float> samples(2000, 0.123f);
    VowelGrainLoop g;
    g.prepare(48000.0);
    g.beginLoop(samples.data(), 1000);
    for (int i = 0; i < 100'000; ++i) {
        const float v = g.next();
        REQUIRE(std::isfinite(v));  // no NaN/inf from OOB read
    }
}
