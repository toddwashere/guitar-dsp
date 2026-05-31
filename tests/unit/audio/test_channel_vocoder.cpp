#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "audio/ChannelVocoder.h"
#include "harness/SyntheticGuitar.h"

#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using guitar_dsp::audio::ChannelVocoder;
using guitar_dsp::tests::SyntheticGuitar;

TEST_CASE("ChannelVocoder: modulator amplitude shapes carrier", "[audio][vocoder]") {
    ChannelVocoder voc;
    voc.prepare(48000.0, 512);
    voc.setWetLevel(1.0f);
    voc.setSibilance(0.0f);

    SyntheticGuitar gen{48000.0};

    // 1 second total, in two halves: loud modulator, then near-silent.
    constexpr int N = 48000;
    std::vector<float> carrier(N), modLoud(N / 2), modQuiet(N / 2), out(N);
    gen.sine(800.0f, 0.6f, carrier.data(), N);
    gen.sine(800.0f, 0.6f, modLoud.data(), N / 2);
    gen.sine(800.0f, 0.01f, modQuiet.data(), N / 2);

    std::vector<float> modulator(N);
    std::copy(modLoud.begin(),  modLoud.end(),  modulator.begin());
    std::copy(modQuiet.begin(), modQuiet.end(), modulator.begin() + N / 2);

    voc.process(carrier.data(), modulator.data(), out.data(), N);

    // RMS in the second half (modulator quiet) should be lower than first.
    auto rms = [](const float* p, int n) {
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += p[i] * p[i];
        return std::sqrt(s / n);
    };
    const double rmsFirst  = rms(out.data(),         N / 2);
    const double rmsSecond = rms(out.data() + N / 2, N / 2);

    INFO("rmsFirst=" << rmsFirst << " rmsSecond=" << rmsSecond);
    REQUIRE(rmsSecond < rmsFirst * 0.5);  // strongly attenuated
}
