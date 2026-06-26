#include <catch2/catch_test_macros.hpp>
#include "audio/RaveFrontEnd.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::RaveFrontEnd;

TEST_CASE("RaveFrontEnd::Drive: 0 dB drive is near-bitexact passthrough", "[audio][rave-frontend][drive]") {
    RaveFrontEnd f; f.prepare(48000.0); f.setDriveDb(0.0f);
    std::vector<float> b{0.001f, -0.003f, 0.005f, -0.008f};
    auto in = b;
    f.processBlockDriveOnly(b.data(), b.size());
    for (size_t i = 0; i < b.size(); ++i)
        REQUIRE(std::fabs(b[i] - in[i]) < 1e-3f);
}

TEST_CASE("RaveFrontEnd::Drive: hot drive soft-clips bounded to 1.0", "[audio][rave-frontend][drive]") {
    RaveFrontEnd f; f.prepare(48000.0); f.setDriveDb(12.0f);
    std::vector<float> b(2048, 0.9f);
    f.processBlockDriveOnly(b.data(), b.size());
    for (float x : b) REQUIRE(std::fabs(x) <= 1.0f);
}

TEST_CASE("RaveFrontEnd::Drive: -12 dB attenuates by ~0.25x", "[audio][rave-frontend][drive]") {
    RaveFrontEnd f; f.prepare(48000.0); f.setDriveDb(-12.0f);
    std::vector<float> b{0.4f};
    f.processBlockDriveOnly(b.data(), b.size());
    REQUIRE(std::fabs(b[0] - 0.1f) < 0.01f); // 0.4 * 10^(-12/20) ≈ 0.1
}
