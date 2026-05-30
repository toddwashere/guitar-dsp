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

TEST_CASE("InputStage: gate closes on silence", "[audio][input_stage]") {
    InputStage stage;
    stage.prepare(48000.0, 1024);
    stage.setNoiseGateThreshold(-40.0f);  // close on quiet
    stage.setInputGainDb(0.0f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> buf(48000 * 2);
    // First half: loud sine; second half: very quiet sine (below threshold).
    gen.sine(440.0f, 0.5f, buf.data(), 48000);
    gen.sine(440.0f, 0.001f, buf.data() + 48000, 48000);

    stage.process(buf.data(), buf.data(), buf.size());

    // RMS of the gated region (last 1024 samples after 100 ms release).
    float sumSq = 0.0f;
    for (std::size_t i = buf.size() - 1024; i < buf.size(); ++i) sumSq += buf[i] * buf[i];
    const float rms = std::sqrt(sumSq / 1024.0f);
    REQUIRE(rms < 1e-3f);
}

TEST_CASE("InputStage: gate opens on loud signal", "[audio][input_stage]") {
    InputStage stage;
    stage.prepare(48000.0, 1024);
    stage.setNoiseGateThreshold(-40.0f);
    stage.setInputGainDb(0.0f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> buf(48000);
    gen.sine(440.0f, 0.5f, buf.data(), buf.size());

    stage.process(buf.data(), buf.data(), buf.size());

    // After settling, peak should be near 0.5 — gate is fully open.
    float peak = 0.0f;
    for (std::size_t i = buf.size() - 4800; i < buf.size(); ++i) peak = std::max(peak, std::abs(buf[i]));
    REQUIRE(peak > 0.4f);
}

TEST_CASE("InputStage: input gain scales output", "[audio][input_stage]") {
    InputStage stage;
    stage.prepare(48000.0, 1024);
    stage.setNoiseGateThreshold(-200.0f);
    stage.setInputGainDb(6.0f);  // ~2x

    SyntheticGuitar gen{48000.0};
    std::vector<float> buf(4800);
    gen.sine(440.0f, 0.25f, buf.data(), buf.size());

    stage.process(buf.data(), buf.data(), buf.size());

    float peak = 0.0f;
    for (std::size_t i = buf.size() / 2; i < buf.size(); ++i) peak = std::max(peak, std::abs(buf[i]));
    // 0.25 * 10^(6/20) ≈ 0.498
    REQUIRE_THAT(peak, WithinAbs(0.498f, 0.02f));
}

#include "harness/RealtimeSentinel.h"
using guitar_dsp::tests::RealtimeSentinel;

TEST_CASE("InputStage: zero allocations on audio thread", "[audio][input_stage][realtime]") {
    InputStage stage;
    stage.prepare(48000.0, 512);
    stage.setNoiseGateThreshold(-40.0f);
    stage.setInputGainDb(3.0f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> buf(512);

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();

    // Process 10 seconds of audio in 512-sample blocks.
    for (int i = 0; i < 938; ++i) {
        gen.sine(440.0f, 0.5f, buf.data(), buf.size());
        stage.process(buf.data(), buf.data(), buf.size());
    }

    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
