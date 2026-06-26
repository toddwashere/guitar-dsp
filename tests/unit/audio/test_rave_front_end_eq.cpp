#include <catch2/catch_test_macros.hpp>
#include "audio/RaveFrontEnd.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::RaveFrontEnd;

namespace {
float rmsAt(const std::vector<float>& b, std::size_t skip) {
    double s = 0.0; for (std::size_t i = skip; i < b.size(); ++i) s += double(b[i]) * b[i];
    return std::sqrt(float(s / double(b.size() - skip)));
}

std::vector<float> tone(float hz, float amp, std::size_t n, double sr) {
    std::vector<float> b(n);
    for (std::size_t i = 0; i < n; ++i)
        b[i] = amp * std::sin(2.0f * 3.14159265f * hz * float(i) / float(sr));
    return b;
}
} // namespace

TEST_CASE("RaveFrontEnd::VoiceEQ: 50 Hz tone attenuated (HPF)", "[audio][rave-frontend][eq]") {
    RaveFrontEnd f; f.prepare(48000.0); f.setPresence(0.5f);
    f.setGateDb(-80.0f); f.setDriveDb(0.0f);
    auto b = tone(50.0f, 0.5f, 4096, 48000.0);
    f.processBlock(b.data(), b.size());
    const float r = rmsAt(b, 2048);
    REQUIRE(r < 0.25f);            // > 6 dB cut at 50 Hz
}

TEST_CASE("RaveFrontEnd::VoiceEQ: 2.5 kHz tone boosted with presence>0", "[audio][rave-frontend][eq]") {
    RaveFrontEnd f0; f0.prepare(48000.0); f0.setPresence(0.0f);
    f0.setGateDb(-80.0f); f0.setDriveDb(0.0f);
    RaveFrontEnd f1; f1.prepare(48000.0); f1.setPresence(1.0f);
    f1.setGateDb(-80.0f); f1.setDriveDb(0.0f);
    auto b0 = tone(2500.0f, 0.5f, 4096, 48000.0);
    auto b1 = tone(2500.0f, 0.5f, 4096, 48000.0);
    f0.processBlock(b0.data(), b0.size());
    f1.processBlock(b1.data(), b1.size());
    REQUIRE(rmsAt(b1, 2048) > rmsAt(b0, 2048) * 1.5f); // +~6 dB
}

TEST_CASE("RaveFrontEnd::VoiceEQ: 16 kHz tone cut with presence>0", "[audio][rave-frontend][eq]") {
    RaveFrontEnd f0; f0.prepare(48000.0); f0.setPresence(0.0f);
    f0.setGateDb(-80.0f); f0.setDriveDb(0.0f);
    RaveFrontEnd f1; f1.prepare(48000.0); f1.setPresence(1.0f);
    f1.setGateDb(-80.0f); f1.setDriveDb(0.0f);
    auto b0 = tone(16000.0f, 0.5f, 4096, 48000.0);
    auto b1 = tone(16000.0f, 0.5f, 4096, 48000.0);
    f0.processBlock(b0.data(), b0.size());
    f1.processBlock(b1.data(), b1.size());
    REQUIRE(rmsAt(b1, 2048) < rmsAt(b0, 2048) * 0.85f); // -~3 dB
}
