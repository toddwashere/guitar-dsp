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
