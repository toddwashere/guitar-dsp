#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "audio/CarouselMod.h"

#include <algorithm>
#include <cmath>

using Catch::Matchers::WithinAbs;
using guitar_dsp::audio::EnvelopeFollower;
using guitar_dsp::audio::Lfo;

TEST_CASE("EnvelopeFollower: rises on loud input, decays on silence",
          "[audio][mod]") {
    EnvelopeFollower env;
    env.prepare(48000.0);
    float e = 0.0f;
    for (int i = 0; i < 4800; ++i) e = env.processSample(1.0f);
    REQUIRE(e > 0.5f);
    for (int i = 0; i < 24000; ++i) e = env.processSample(0.0f);
    REQUIRE(e < 0.1f);
}

TEST_CASE("Lfo: produces bounded sine at requested rate", "[audio][mod]") {
    Lfo lfo;
    lfo.prepare(48000.0);
    lfo.setRateHz(2.0f);
    float minV = 1.0f, maxV = -1.0f;
    int zeroCrossings = 0;
    float prev = lfo.processSample();
    for (int i = 0; i < 48000; ++i) {
        const float v = lfo.processSample();
        minV = std::min(minV, v);
        maxV = std::max(maxV, v);
        if (prev <= 0.0f && v > 0.0f) ++zeroCrossings;
        prev = v;
    }
    REQUIRE(minV >= -1.01f);
    REQUIRE(maxV <= 1.01f);
    REQUIRE(zeroCrossings >= 1);
    REQUIRE(zeroCrossings <= 3);
}
