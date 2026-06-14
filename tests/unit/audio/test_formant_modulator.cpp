#include <catch2/catch_test_macros.hpp>
#include "audio/FormantModulator.h"
#include "harness/RealtimeSentinel.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::FormantModulator;
using guitar_dsp::tests::RealtimeSentinel;

TEST_CASE("FormantModulator Static: posOut is constant staticPos",
          "[audio][formant_modulator][static]") {
    FormantModulator m;
    m.prepare(48000.0);
    m.setMode(FormantModulator::Mode::Static);
    m.setStaticPosition(0.42f);

    std::vector<float> onset(2048, 0.0f), pos(2048, 0.0f);
    m.process(onset.data(), pos.data(), pos.size());

    for (float p : pos) REQUIRE(p == 0.42f);
}

TEST_CASE("FormantModulator Lfo: triangle walks between breakpoint extremes",
          "[audio][formant_modulator][lfo]") {
    FormantModulator m;
    m.prepare(48000.0);
    m.setMode(FormantModulator::Mode::Lfo);
    m.setBreakpoints({ 0.0f, 0.2f });
    m.setLfoRateHz(1.0f);

    constexpr std::size_t N = 48000;
    std::vector<float> onset(N, 0.0f), pos(N, 0.0f);
    m.process(onset.data(), pos.data(), N);

    float pmin = 1.0f, pmax = 0.0f;
    for (float p : pos) {
        pmin = std::min(pmin, p);
        pmax = std::max(pmax, p);
    }
    REQUIRE(pmin <= 0.02f);
    REQUIRE(pmax >= 0.18f);
    REQUIRE(pmax <= 0.21f);
}

TEST_CASE("FormantModulator Lfo: empty breakpoints -> constant 0",
          "[audio][formant_modulator][lfo]") {
    FormantModulator m;
    m.prepare(48000.0);
    m.setMode(FormantModulator::Mode::Lfo);
    m.setBreakpoints({});

    std::vector<float> onset(512, 0.0f), pos(512, 1.0f);
    m.process(onset.data(), pos.data(), pos.size());

    for (float p : pos) REQUIRE(p == 0.0f);
}

TEST_CASE("FormantModulator Envelope: advances one breakpoint per onset",
          "[audio][formant_modulator][envelope]") {
    FormantModulator m;
    m.prepare(48000.0);
    m.setMode(FormantModulator::Mode::Envelope);
    m.setBreakpoints({ 0.0f, 0.5f, 0.75f });
    m.setEnvelopeAttackMs(5.0f);

    constexpr std::size_t N = 48000 * 2;
    std::vector<float> onset(N, 0.0f), pos(N, 0.0f);
    for (std::size_t i = 0; i < 64; ++i) onset[100         + i] = 0.9f * std::exp(-(int)i * 0.05f);
    for (std::size_t i = 0; i < 64; ++i) onset[100 + 24000 + i] = 0.9f * std::exp(-(int)i * 0.05f);
    for (std::size_t i = 0; i < 64; ++i) onset[100 + 48000 + i] = 0.9f * std::exp(-(int)i * 0.05f);

    constexpr std::size_t blockSize = 512;
    for (std::size_t i = 0; i < N; i += blockSize) {
        const std::size_t n = std::min<std::size_t>(blockSize, N - i);
        m.process(onset.data() + i, pos.data() + i, n);
    }

    REQUIRE(std::fabs(pos[12000]                 - 0.0f)  < 0.05f);
    REQUIRE(std::fabs(pos[100 + 24000 + 12000]   - 0.5f)  < 0.05f);
    REQUIRE(std::fabs(pos[N - 100]               - 0.75f) < 0.05f);
}

TEST_CASE("FormantModulator: process is allocation-free",
          "[audio][formant_modulator][rt]") {
    FormantModulator m;
    m.prepare(48000.0);
    m.setMode(FormantModulator::Mode::Lfo);
    m.setBreakpoints({ 0.0f, 0.5f, 1.0f });
    m.setLfoRateHz(2.0f);

    std::vector<float> onset(512), pos(512);
    for (int i = 0; i < 512; ++i)
        onset[static_cast<std::size_t>(i)] =
            0.5f * std::sin(2.0f * 3.14159265f * 110.0f * i / 48000.0f);
    // Warm-up call.
    m.process(onset.data(), pos.data(), 512);

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int blk = 0; blk < 50; ++blk)
        m.process(onset.data(), pos.data(), 512);
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
