#include <catch2/catch_test_macros.hpp>
#include "audio/BranchLimiter.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::BranchLimiter;

TEST_CASE("BranchLimiter: signal under ceiling passes through", "[audio][limiter]") {
    BranchLimiter lim; lim.prepare(48000.0); lim.setCeilingDb(-3.0f);
    std::vector<float> b{0.1f, -0.2f, 0.3f};
    lim.processBlock(b.data(), b.size());
    REQUIRE(b[0] == 0.1f);
    REQUIRE(b[1] == -0.2f);
}

TEST_CASE("BranchLimiter: hot signal is clamped to ceiling", "[audio][limiter]") {
    BranchLimiter lim; lim.prepare(48000.0); lim.setCeilingDb(-3.0f);
    const float ceiling = std::pow(10.0f, -3.0f / 20.0f); // ~0.7079
    std::vector<float> b(64, 2.0f); // way over ceiling
    lim.processBlock(b.data(), b.size());
    for (float x : b) REQUIRE(std::fabs(x) <= ceiling + 1e-4f);
}

TEST_CASE("BranchLimiter: negative spike is clamped", "[audio][limiter]") {
    BranchLimiter lim; lim.prepare(48000.0); lim.setCeilingDb(-3.0f);
    const float ceiling = std::pow(10.0f, -3.0f / 20.0f);
    std::vector<float> b(64, -1.5f);
    lim.processBlock(b.data(), b.size());
    for (float x : b) REQUIRE(std::fabs(x) <= ceiling + 1e-4f);
}

TEST_CASE("BranchLimiter: release lets gain recover", "[audio][limiter]") {
    BranchLimiter lim; lim.prepare(48000.0); lim.setCeilingDb(-3.0f); lim.setReleaseMs(10.0f);
    std::vector<float> hot(48, 2.0f);   // 1 ms of hot
    lim.processBlock(hot.data(), hot.size());
    std::vector<float> quiet(48000, 0.1f); // 1 s of quiet
    lim.processBlock(quiet.data(), quiet.size());
    REQUIRE(std::fabs(quiet[quiet.size() - 1] - 0.1f) < 1e-4f); // gain fully recovered
}
