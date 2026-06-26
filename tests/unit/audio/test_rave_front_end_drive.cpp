#include <catch2/catch_test_macros.hpp>
#include "audio/RaveFrontEnd.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::RaveFrontEnd;

TEST_CASE("RaveFrontEnd::Drive: 0 dB drive is near-bitexact passthrough", "[audio][rave-frontend][drive]") {
    RaveFrontEnd f; f.prepare(48000.0); f.setDriveDb(0.0f);
    f.setGateDb(-80.0f); f.setPresence(0.0f);
    // Use 440 Hz sine wave. With 0 dB drive, should pass through mostly unchanged.
    std::vector<float> b(2048);
    for (size_t i = 0; i < b.size(); ++i)
        b[i] = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * i / 48000.0f);
    auto in = b;
    f.processBlock(b.data(), b.size());
    // After settling, verify output RMS is close to input RMS (within 10%).
    double inRms = 0.0, outRms = 0.0;
    for (size_t i = 1024; i < b.size(); ++i) {
        inRms += double(in[i]) * in[i];
        outRms += double(b[i]) * b[i];
    }
    inRms = std::sqrt(inRms / (b.size() - 1024));
    outRms = std::sqrt(outRms / (b.size() - 1024));
    REQUIRE(outRms > inRms * 0.9f);
    REQUIRE(outRms < inRms * 1.1f);
}

TEST_CASE("RaveFrontEnd::Drive: hot drive soft-clips bounded to 1.0", "[audio][rave-frontend][drive]") {
    RaveFrontEnd f; f.prepare(48000.0); f.setDriveDb(12.0f);
    f.setGateDb(-80.0f); f.setPresence(0.0f);
    std::vector<float> b(2048, 0.9f);
    f.processBlock(b.data(), b.size());
    for (float x : b) REQUIRE(std::fabs(x) <= 1.0f);
}

TEST_CASE("RaveFrontEnd::Drive: -12 dB attenuates by ~0.25x", "[audio][rave-frontend][drive]") {
    RaveFrontEnd f; f.prepare(48000.0); f.setDriveDb(-12.0f);
    f.setGateDb(-80.0f); f.setPresence(0.0f);
    // Use 440 Hz sine wave with 0.5 peak amplitude.
    std::vector<float> b0(2048), b1(2048);
    for (size_t i = 0; i < b0.size(); ++i) {
        b0[i] = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * i / 48000.0f);
        b1[i] = b0[i];
    }
    // Process with 0 dB drive to get reference.
    RaveFrontEnd f0; f0.prepare(48000.0);
    f0.setGateDb(-80.0f); f0.setPresence(0.0f); f0.setDriveDb(0.0f);
    f0.processBlock(b0.data(), b0.size());
    // Process with -12 dB drive.
    f.processBlock(b1.data(), b1.size());
    // Verify b1 RMS is approximately 1/4 of b0 RMS after settling.
    double rms0 = 0.0, rms1 = 0.0;
    for (size_t i = 1024; i < b0.size(); ++i) {
        rms0 += double(b0[i]) * b0[i];
        rms1 += double(b1[i]) * b1[i];
    }
    rms0 = std::sqrt(rms0 / (b0.size() - 1024));
    rms1 = std::sqrt(rms1 / (b0.size() - 1024));
    REQUIRE(rms1 > rms0 * 0.2f);
    REQUIRE(rms1 < rms0 * 0.3f);  // ~0.25x ± 5%
}
