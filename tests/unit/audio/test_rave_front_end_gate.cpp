#include <catch2/catch_test_macros.hpp>
#include "audio/RaveFrontEnd.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::RaveFrontEnd;

TEST_CASE("RaveFrontEnd::Gate: silence stays silent", "[audio][rave-frontend][gate]") {
    RaveFrontEnd f; f.prepare(48000.0); f.setGateDb(-40.0f);
    f.setPresence(0.0f); f.setDriveDb(0.0f);
    std::vector<float> b(1024, 0.0f);
    f.processBlock(b.data(), b.size());
    for (float x : b) REQUIRE(x == 0.0f);
}

TEST_CASE("RaveFrontEnd::Gate: signal well above threshold passes through after attack", "[audio][rave-frontend][gate]") {
    RaveFrontEnd f; f.prepare(48000.0); f.setGateDb(-40.0f);
    f.setPresence(0.0f); f.setDriveDb(0.0f);
    std::vector<float> b(4096);
    for (size_t i = 0; i < b.size(); ++i)
        b[i] = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * i / 48000.0f);
    f.processBlock(b.data(), b.size());
    // After ~20 ms attack, the tail should approximate the input amplitude.
    float peak = 0.0f;
    for (size_t i = 2000; i < b.size(); ++i) peak = std::max(peak, std::fabs(b[i]));
    REQUIRE(peak > 0.4f);
}

TEST_CASE("RaveFrontEnd::Gate: signal well below threshold is suppressed", "[audio][rave-frontend][gate]") {
    RaveFrontEnd f; f.prepare(48000.0); f.setGateDb(-40.0f);
    f.setPresence(0.0f); f.setDriveDb(0.0f);
    std::vector<float> b(4096);
    for (size_t i = 0; i < b.size(); ++i)
        b[i] = 0.001f * std::sin(2.0f * 3.14159265f * 440.0f * i / 48000.0f); // ~-60 dBFS
    f.processBlock(b.data(), b.size());
    float peak = 0.0f;
    for (size_t i = 2000; i < b.size(); ++i) peak = std::max(peak, std::fabs(b[i]));
    REQUIRE(peak < 1e-4f);
}
