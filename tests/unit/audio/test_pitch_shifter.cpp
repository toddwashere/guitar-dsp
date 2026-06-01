#include <catch2/catch_test_macros.hpp>
#include "audio/PitchShifter.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::PitchShifter;

namespace {
float estimateHz(const std::vector<float>& x, double sr) {
    int crossings = 0;
    for (size_t i = 1; i < x.size(); ++i)
        if (x[i-1] <= 0.0f && x[i] > 0.0f) ++crossings;
    const double seconds = x.size() / sr;
    return static_cast<float>(crossings / seconds);
}
std::vector<float> sine(int n, float hz, double sr) {
    std::vector<float> b(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        b[static_cast<size_t>(i)] = std::sin(2.0f*3.14159265f*hz*i/static_cast<float>(sr));
    return b;
}
}

TEST_CASE("PitchShifter: octave-up roughly doubles frequency", "[audio][pitch]") {
    PitchShifter ps;
    ps.prepare(48000.0, 4096);
    ps.setGrainSamples(1920);
    ps.setRatio(2.0f);

    auto in = sine(48000, 220.0f, 48000.0);
    std::vector<float> out(in.size());
    for (size_t i = 0; i < in.size(); ++i) out[i] = ps.processSample(in[i]);

    std::vector<float> tail(out.begin() + 4096, out.end());
    const float f = estimateHz(tail, 48000.0);
    REQUIRE(f > 380.0f);
    REQUIRE(f < 500.0f);
}

TEST_CASE("PitchShifter: ratio 1.0 passes pitch through", "[audio][pitch]") {
    PitchShifter ps;
    ps.prepare(48000.0, 4096);
    ps.setGrainSamples(1920);
    ps.setRatio(1.0f);

    auto in = sine(48000, 330.0f, 48000.0);
    std::vector<float> out(in.size());
    for (size_t i = 0; i < in.size(); ++i) out[i] = ps.processSample(in[i]);

    std::vector<float> tail(out.begin() + 4096, out.end());
    const float f = estimateHz(tail, 48000.0);
    REQUIRE(f > 300.0f);
    REQUIRE(f < 360.0f);
}

TEST_CASE("PitchShifter: output stays finite and bounded", "[audio][pitch]") {
    PitchShifter ps;
    ps.prepare(48000.0, 4096);
    ps.setGrainSamples(1920);
    ps.setRatio(1.5f);
    auto in = sine(8192, 440.0f, 48000.0);
    for (float s : in) {
        const float y = ps.processSample(s);
        REQUIRE(std::isfinite(y));
        REQUIRE(std::fabs(y) < 4.0f);
    }
}
