#include <catch2/catch_test_macros.hpp>
#include "audio/Comb.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::Comb;

TEST_CASE("Comb: impulse produces an echo one period later", "[audio][comb]") {
    Comb c;
    c.prepare(48000.0, 4800);
    c.setFreqHz(480.0f);        // D = 100 samples
    c.setFeedback(0.7f);
    c.setMix(1.0f);

    std::vector<float> out(400, 0.0f);
    out[0] = c.processSample(1.0f);
    for (size_t i = 1; i < out.size(); ++i) out[i] = c.processSample(0.0f);

    REQUIRE(std::fabs(out[100]) > 0.3f);
    REQUIRE(std::fabs(out[100]) < 0.9f);
    REQUIRE(std::fabs(out[200]) > 0.05f);
    REQUIRE(std::fabs(out[200]) < std::fabs(out[100]));
}

TEST_CASE("Comb: feedback clamped, output stays finite", "[audio][comb]") {
    Comb c;
    c.prepare(48000.0, 4800);
    c.setFreqHz(220.0f);
    c.setFeedback(1.5f);
    c.setMix(0.5f);
    for (int i = 0; i < 48000; ++i) {
        const float y = c.processSample(0.3f);
        REQUIRE(std::isfinite(y));
        REQUIRE(std::fabs(y) < 50.0f);
    }
}

TEST_CASE("Comb: zero freq bypasses", "[audio][comb]") {
    Comb c;
    c.prepare(48000.0, 4800);
    c.setFreqHz(0.0f);
    c.setMix(1.0f);
    REQUIRE(c.processSample(0.37f) == 0.37f);
}
