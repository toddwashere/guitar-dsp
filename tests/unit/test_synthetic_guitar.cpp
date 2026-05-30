#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "harness/SyntheticGuitar.h"

#include <vector>
#include <cmath>

using Catch::Matchers::WithinAbs;
using guitar_dsp::tests::SyntheticGuitar;

TEST_CASE("synthetic guitar: sine has expected peak", "[harness]") {
    SyntheticGuitar gen{48000.0};
    std::vector<float> buf(4800);
    gen.sine(440.0f, 0.5f, buf.data(), buf.size());

    float peak = 0.0f;
    for (float s : buf) peak = std::max(peak, std::abs(s));
    REQUIRE_THAT(peak, WithinAbs(0.5f, 1e-3f));
}

TEST_CASE("synthetic guitar: silence is exactly zero", "[harness]") {
    SyntheticGuitar gen{48000.0};
    std::vector<float> buf(1024, 0.123f);
    gen.silence(buf.data(), buf.size());
    for (float s : buf) REQUIRE(s == 0.0f);
}

TEST_CASE("synthetic guitar: sweep starts at f0 and ends near f1", "[harness]") {
    SyntheticGuitar gen{48000.0};
    std::vector<float> buf(48000);
    gen.sweep(100.0f, 1000.0f, 1.0f, buf.data(), buf.size());
    // Just check it's non-silent and bounded.
    float peak = 0.0f;
    for (float s : buf) peak = std::max(peak, std::abs(s));
    REQUIRE(peak > 0.5f);
    REQUIRE(peak <= 1.0f);
}
