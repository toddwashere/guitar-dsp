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
