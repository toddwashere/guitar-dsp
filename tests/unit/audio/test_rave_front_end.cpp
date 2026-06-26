#include <catch2/catch_test_macros.hpp>
#include "audio/RaveFrontEnd.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::RaveFrontEnd;

TEST_CASE("RaveFrontEnd: composed chain produces nonzero output for guitar-like tone", "[audio][rave-frontend]") {
    RaveFrontEnd f; f.prepare(48000.0);
    f.setGateDb(-50.0f); f.setPresence(0.5f); f.setDriveDb(0.0f);
    std::vector<float> b(8192);
    for (size_t i = 0; i < b.size(); ++i)
        b[i] = 0.3f * std::sin(2.0f * 3.14159265f * 220.0f * i / 48000.0f);
    f.processBlock(b.data(), b.size());
    float peak = 0.0f;
    for (size_t i = 4000; i < b.size(); ++i) peak = std::max(peak, std::fabs(b[i]));
    REQUIRE(peak > 0.05f);
    REQUIRE(f.postRms() > 0.01f);
}

TEST_CASE("RaveFrontEnd: postRms == 0 when fully gated", "[audio][rave-frontend]") {
    RaveFrontEnd f; f.prepare(48000.0);
    f.setGateDb(-20.0f); f.setPresence(0.0f); f.setDriveDb(0.0f);
    std::vector<float> b(2048, 0.0f);
    f.processBlock(b.data(), b.size());
    REQUIRE(f.postRms() < 1e-4f);
}

TEST_CASE("RaveFrontEnd: setters allocation-free (smoke)", "[audio][rave-frontend]") {
    RaveFrontEnd f; f.prepare(48000.0);
    // Just verify no throw; TSAN catches concurrency issues if run with sanitizer.
    f.setGateDb(-30.0f); f.setPresence(0.7f); f.setDriveDb(3.0f);
    SUCCEED();
}
