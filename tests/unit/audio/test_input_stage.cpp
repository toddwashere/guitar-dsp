#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "audio/InputStage.h"
#include "harness/SyntheticGuitar.h"

#include <vector>

using Catch::Matchers::WithinAbs;
using guitar_dsp::audio::InputStage;
using guitar_dsp::tests::SyntheticGuitar;

TEST_CASE("InputStage: DC offset is removed", "[audio][input_stage]") {
    InputStage stage;
    stage.prepare(48000.0, 1024);
    stage.setNoiseGateThreshold(-200.0f);  // disable gate
    stage.setInputGainDb(0.0f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> buf(48000);
    gen.dc(0.5f, buf.data(), buf.size());

    stage.process(buf.data(), buf.data(), buf.size());

    // After settling, the DC offset should be near zero.
    float meanLast = 0.0f;
    for (size_t i = buf.size() - 1024; i < buf.size(); ++i) meanLast += buf[i];
    meanLast /= 1024.0f;
    REQUIRE_THAT(meanLast, WithinAbs(0.0f, 1e-3f));
}

TEST_CASE("InputStage: AC signal preserved through DC blocker", "[audio][input_stage]") {
    InputStage stage;
    stage.prepare(48000.0, 1024);
    stage.setNoiseGateThreshold(-200.0f);
    stage.setInputGainDb(0.0f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000), out(48000);
    gen.sine(440.0f, 0.5f, in.data(), in.size());

    std::copy(in.begin(), in.end(), out.begin());
    stage.process(out.data(), out.data(), out.size());

    // Peak after settling should still be approximately 0.5.
    float peak = 0.0f;
    for (size_t i = out.size() / 2; i < out.size(); ++i) peak = std::max(peak, std::abs(out[i]));
    REQUIRE_THAT(peak, WithinAbs(0.5f, 0.05f));
}
