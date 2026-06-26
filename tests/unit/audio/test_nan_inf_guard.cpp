#include <catch2/catch_test_macros.hpp>
#include "audio/NaNInfGuard.h"

#include <cmath>
#include <limits>
#include <vector>

using guitar_dsp::audio::NaNInfGuard;

TEST_CASE("NaNInfGuard: clean block passes through", "[audio][guard]") {
    NaNInfGuard g;
    std::vector<float> b{0.1f, -0.2f, 0.0f, 0.5f};
    REQUIRE(g.processBlock(b.data(), b.size()));
    REQUIRE(b[0] == 0.1f);
    REQUIRE_FALSE(g.stalled());
}

TEST_CASE("NaNInfGuard: NaN block is zeroed and not stalled yet", "[audio][guard]") {
    NaNInfGuard g;
    std::vector<float> b{0.1f, std::numeric_limits<float>::quiet_NaN(), 0.3f, 0.4f};
    REQUIRE_FALSE(g.processBlock(b.data(), b.size()));
    for (float x : b) REQUIRE(x == 0.0f);
    REQUIRE_FALSE(g.stalled());
}

TEST_CASE("NaNInfGuard: Inf block is zeroed", "[audio][guard]") {
    NaNInfGuard g;
    std::vector<float> b{std::numeric_limits<float>::infinity(), 0.0f};
    REQUIRE_FALSE(g.processBlock(b.data(), b.size()));
    REQUIRE(b[0] == 0.0f);
}

TEST_CASE("NaNInfGuard: 3 consecutive bad blocks => stalled", "[audio][guard]") {
    NaNInfGuard g;
    auto bad = [](){ std::vector<float> v{std::nanf("")}; return v; };
    for (int i = 0; i < 2; ++i) { auto v = bad(); g.processBlock(v.data(), v.size()); REQUIRE_FALSE(g.stalled()); }
    auto v3 = bad(); g.processBlock(v3.data(), v3.size());
    REQUIRE(g.stalled());
}

TEST_CASE("NaNInfGuard: clean block clears stall", "[audio][guard]") {
    NaNInfGuard g;
    for (int i = 0; i < 3; ++i) { float x = std::nanf(""); g.processBlock(&x, 1); }
    REQUIRE(g.stalled());
    float ok = 0.0f;
    g.processBlock(&ok, 1);
    REQUIRE_FALSE(g.stalled());
}
