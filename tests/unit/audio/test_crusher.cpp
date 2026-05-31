#include <catch2/catch_test_macros.hpp>
#include "audio/Crusher.h"

#include <set>
#include <vector>
#include <cmath>

using guitar_dsp::audio::Crusher;

TEST_CASE("Crusher: bit reduction limits distinct levels", "[audio][crusher]") {
    Crusher c;
    c.setBits(2);            // 2 bits -> at most 4 levels
    c.setDownsample(1);
    std::vector<float> buf(2048);
    for (size_t i = 0; i < buf.size(); ++i)
        buf[i] = std::sin(2.0f * 3.14159265f * 220.0f * i / 48000.0f);
    for (float& x : buf) x = c.processSample(x);

    std::set<int> levels;
    for (float x : buf) levels.insert(static_cast<int>(std::lround(x * 1000.0f)));
    REQUIRE(levels.size() <= 5);   // <=4 quantized + rounding slack
}

TEST_CASE("Crusher: downsample holds samples", "[audio][crusher]") {
    Crusher c;
    c.setBits(0);            // bypass bit reduction
    c.setDownsample(4);
    std::vector<float> in{0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
    std::vector<float> out;
    for (float x : in) out.push_back(c.processSample(x));
    // First sample of each group of 4 is held across the group.
    REQUIRE(out[0] == out[1]);
    REQUIRE(out[1] == out[2]);
    REQUIRE(out[2] == out[3]);
    REQUIRE(out[4] == in[4]);
}

TEST_CASE("Crusher: bypass passes signal unchanged", "[audio][crusher]") {
    Crusher c;
    c.setBits(0);
    c.setDownsample(1);
    REQUIRE(c.processSample(0.37f) == 0.37f);
}
