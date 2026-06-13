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

TEST_CASE("ChannelVocoder: makeup gain raises output level", "[audio][vocoder]") {
    SyntheticGuitar gen{48000.0};
    constexpr int N = 4800;
    // Small-signal amplitudes so we measure in the tanh limiter's linear
    // region (loud matched tones would already saturate at gain 1).
    std::vector<float> carrier(N), modulator(N), out(N);
    gen.sine(800.0f, 0.02f, carrier.data(),   N);
    gen.sine(800.0f, 0.02f, modulator.data(), N);

    auto rms = [](const float* p, int n) {
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += p[i] * p[i];
        return std::sqrt(s / n);
    };
    auto run = [&](float gain) {
        ChannelVocoder voc;
        voc.prepare(48000.0, 512);
        voc.setWetLevel(1.0f); voc.setSibilance(0.0f); voc.setCarrierNoise(0.0f);
        voc.setOutputGain(gain);
        voc.process(carrier.data(), modulator.data(), out.data(), N);
        return rms(out.data() + N / 2, N / 2);  // skip the filter warm-up
    };

    const double low  = run(1.0f);
    const double high = run(4.0f);
    INFO("low=" << low << " high=" << high);
    REQUIRE(high > low * 2.0);   // louder (sub-4x due to the tanh limiter)
}

TEST_CASE("ChannelVocoder: carrier noise floor lets a silent carrier vocode",
          "[audio][vocoder]") {
    SyntheticGuitar gen{48000.0};
    constexpr int N = 4800;
    std::vector<float> carrier(N, 0.0f), modulator(N), out(N);  // silent carrier
    gen.sine(800.0f, 0.5f, modulator.data(), N);

    auto rms = [](const float* p, int n) {
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += p[i] * p[i];
        return std::sqrt(s / n);
    };
    auto run = [&](float carrierNoise) {
        ChannelVocoder voc;
        voc.prepare(48000.0, 512);
        voc.setWetLevel(1.0f); voc.setSibilance(0.0f); voc.setOutputGain(1.0f);
        voc.setCarrierNoise(carrierNoise);
        voc.process(carrier.data(), modulator.data(), out.data(), N);
        return rms(out.data() + N / 2, N / 2);
    };

    const double noFloor   = run(0.0f);
    const double withFloor = run(0.5f);
    INFO("noFloor=" << noFloor << " withFloor=" << withFloor);
    REQUIRE(noFloor   < 1e-3);          // silent carrier, no floor -> ~silence
    REQUIRE(withFloor > noFloor * 10);  // floor excites the bands -> audible
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

TEST_CASE("ChannelVocoder: envelope follower decays with ~25 ms time constant",
          "[audio][channel_vocoder][envelope]") {
    using namespace guitar_dsp::audio;
    ChannelVocoder voc;
    voc.prepare(48000.0, 1024);
    voc.setWetLevel(1.0f);
    voc.setSibilance(0.0f);
    // Keep gain low so the output stays in the tanh linear region; otherwise
    // saturation masks envelope decay.
    voc.setOutputGain(0.05f);
    voc.setCarrierNoise(0.1f);  // small broadband floor so all bands have carrier

    // Modulator: 100 ms of 1 kHz tone followed by 100 ms of silence.
    std::vector<float> mod(48000 / 5);  // 0.2 s
    for (std::size_t i = 0; i < mod.size() / 2; ++i)
        mod[i] = 0.6f * std::sin(2.0 * 3.14159265 * 1000.0 * i / 48000.0);
    // tail (second half) already zero from default

    // Constant carrier — band-filter rejects DC, so carrier excitation comes
    // from the noise floor above.
    std::vector<float> car(mod.size(), 0.0f);
    std::vector<float> out(mod.size());

    voc.process(car.data(), mod.data(), out.data(), mod.size());

    // RMS just before modulator stops (sample 3800-4799, last 20 ms of tone)
    // and ~25 ms after stop (sample 5800-6199, ~21..29 ms post-stop).
    // RMS is more stable than sample peak for the noise-carrier output.
    auto rmsOver = [&](std::size_t from, std::size_t len) {
        double s = 0.0;
        std::size_t n = 0;
        for (std::size_t i = from; i < from + len && i < out.size(); ++i) {
            s += static_cast<double>(out[i]) * out[i];
            ++n;
        }
        return n ? std::sqrt(s / n) : 0.0;
    };
    const double atStop  = rmsOver(3800, 1000);
    const double at25ms  = rmsOver(5800, 400);
    INFO("atStop=" << atStop << "  at25ms=" << at25ms
                   << "  ratio=" << (at25ms / atStop));
    // The output amplitude tracks the modulator-band envelope (LP of |modulator|).
    // After one time constant (~25 ms), envelope should be ~1/e (~37%) of its
    // pre-stop value. With envT=15 ms it would fall to ~19% in the same window
    // (exp(-25/15)). Bounds 25%..50% distinguish 25 ms from 15 ms while
    // tolerating noise-carrier statistical jitter.
    REQUIRE(at25ms < atStop * 0.50);
    REQUIRE(at25ms > atStop * 0.25);
}
