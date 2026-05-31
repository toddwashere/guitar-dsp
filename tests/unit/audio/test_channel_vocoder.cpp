#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "audio/ChannelVocoder.h"
#include "harness/SyntheticGuitar.h"
#include "harness/RealtimeSentinel.h"

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

TEST_CASE("ChannelVocoder: silent modulator produces silent output", "[audio][vocoder]") {
    ChannelVocoder voc;
    voc.prepare(48000.0, 512);
    voc.setWetLevel(1.0f);
    voc.setSibilance(0.0f);

    SyntheticGuitar gen{48000.0};
    constexpr int N = 4800;
    std::vector<float> carrier(N), modulator(N, 0.0f), out(N);
    gen.sine(800.0f, 0.6f, carrier.data(), N);

    voc.process(carrier.data(), modulator.data(), out.data(), N);

    float peak = 0.0f;
    for (int i = N - 480; i < N; ++i) peak = std::max(peak, std::abs(out[i]));
    REQUIRE(peak < 1e-3f);
}

TEST_CASE("ChannelVocoder: zero allocations on audio thread", "[audio][vocoder][realtime]") {
    ChannelVocoder voc;
    voc.prepare(48000.0, 512);
    voc.setWetLevel(1.0f);
    voc.setSibilance(0.4f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> carrier(512), modulator(512), out(512);
    gen.sine(800.0f, 0.5f, carrier.data(), 512);
    gen.sine(800.0f, 0.4f, modulator.data(), 512);

    guitar_dsp::tests::RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int i = 0; i < 938; ++i)  // 10 s of audio in 512-sample blocks
        voc.process(carrier.data(), modulator.data(), out.data(), 512);
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}

TEST_CASE("ChannelVocoder: sibilance noise activates with high-band modulator energy",
          "[audio][vocoder]") {
    ChannelVocoder voc;
    voc.prepare(48000.0, 512);
    voc.setWetLevel(1.0f);

    SyntheticGuitar gen{48000.0};
    constexpr int N = 4800;

    // A 5 kHz modulator (sibilant-band) with a 200 Hz carrier (low):
    // without sibilance noise injection, the high modulator band has
    // nothing to scale on the carrier side, so output is near silent.
    // With sibilance enabled, high-band noise should appear in the output.
    std::vector<float> carrier(N), modulator(N), outNoSib(N), outWithSib(N);
    gen.sine(200.0f,  0.6f, carrier.data(),   N);
    gen.sine(5000.0f, 0.6f, modulator.data(), N);

    voc.setSibilance(0.0f);
    voc.process(carrier.data(), modulator.data(), outNoSib.data(), N);

    voc.reset();
    voc.setSibilance(1.0f);
    voc.process(carrier.data(), modulator.data(), outWithSib.data(), N);

    auto rms = [](const float* p, int n) {
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += p[i] * p[i];
        return std::sqrt(s / n);
    };
    INFO("noSib=" << rms(outNoSib.data(),   N)
                  << "  withSib=" << rms(outWithSib.data(), N));
    REQUIRE(rms(outWithSib.data(), N) > rms(outNoSib.data(), N) * 3.0);
}
