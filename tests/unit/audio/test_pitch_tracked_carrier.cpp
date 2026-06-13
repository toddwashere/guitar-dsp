#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "audio/PitchTrackedCarrier.h"
#include "harness/SyntheticGuitar.h"

#include <algorithm>
#include <cmath>
#include <vector>

using guitar_dsp::audio::PitchTrackedCarrier;
using guitar_dsp::tests::SyntheticGuitar;
using Catch::Matchers::WithinAbs;

TEST_CASE("PitchTrackedCarrier: prepare + process writes numSamples without crashing",
          "[audio][pitch_tracked_carrier][smoke]") {
    PitchTrackedCarrier c;
    c.prepare(48000.0, 512);

    std::vector<float> in(512, 0.0f), out(512, 1.0f);  // out pre-filled to detect writes
    auto s = c.process(in.data(), out.data(), out.size());

    // Default state: not voiced, no note.
    REQUIRE(s.voiced == false);
    REQUIRE(s.midiNote == -1);

    // Output must be silence for silent input pre-Task-4 (no hold history).
    for (float v : out) REQUIRE(v == 0.0f);
}

namespace {
// Run input through `c` in 512-sample blocks and return the final published State.
PitchTrackedCarrier::State runSeconds(PitchTrackedCarrier& c,
                                       const float* in,
                                       std::size_t totalSamples) {
    std::vector<float> out(512);
    PitchTrackedCarrier::State last{};
    for (std::size_t i = 0; i < totalSamples; i += 512) {
        const std::size_t n = std::min<std::size_t>(512, totalSamples - i);
        last = c.process(in + i, out.data(), n);
    }
    return last;
}

constexpr float centsBetween(float f, float ref) {
    return 1200.0f * std::log2(f / ref);
}
}

TEST_CASE("PitchTrackedCarrier YIN: detects 110 Hz sine within ±20 cents",
          "[audio][pitch_tracked_carrier][yin]") {
    PitchTrackedCarrier c;
    c.prepare(48000.0, 512);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000);  // 1 second
    gen.sine(110.0f, 0.4f, in.data(), in.size());

    auto s = runSeconds(c, in.data(), in.size());

    REQUIRE(s.voiced == true);
    REQUIRE_THAT(centsBetween(s.freqHz, 110.0f), WithinAbs(0.0f, 20.0f));
}

TEST_CASE("PitchTrackedCarrier YIN: detects 440 Hz sine within ±20 cents",
          "[audio][pitch_tracked_carrier][yin]") {
    PitchTrackedCarrier c;
    c.prepare(48000.0, 512);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000);
    gen.sine(440.0f, 0.4f, in.data(), in.size());

    auto s = runSeconds(c, in.data(), in.size());
    REQUIRE(s.voiced == true);
    REQUIRE_THAT(centsBetween(s.freqHz, 440.0f), WithinAbs(0.0f, 20.0f));
}

TEST_CASE("PitchTrackedCarrier YIN: low E (82.4 Hz) plucked-string",
          "[audio][pitch_tracked_carrier][yin]") {
    PitchTrackedCarrier c;
    c.prepare(48000.0, 512);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000);
    gen.pluck(82.4f, 2.0f, 0.6f, in.data(), in.size());

    auto s = runSeconds(c, in.data(), in.size());
    REQUIRE(s.voiced == true);
    // ±50 cents = within a semitone (no octave error). Tighter than that is
    // unreliable on a Karplus-Strong synth; the real test is "no octave error"
    // (82 vs 164 or 41).
    REQUIRE_THAT(centsBetween(s.freqHz, 82.4f), WithinAbs(0.0f, 50.0f));
}

TEST_CASE("PitchTrackedCarrier YIN: silence -> unvoiced",
          "[audio][pitch_tracked_carrier][yin]") {
    PitchTrackedCarrier c;
    c.prepare(48000.0, 512);

    std::vector<float> in(48000, 0.0f);
    auto s = runSeconds(c, in.data(), in.size());
    REQUIRE(s.voiced == false);
}

TEST_CASE("PitchTrackedCarrier YIN: midiNote + cents fields agree with freqHz",
          "[audio][pitch_tracked_carrier][yin]") {
    PitchTrackedCarrier c;
    c.prepare(48000.0, 512);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000);
    gen.sine(440.0f, 0.4f, in.data(), in.size());  // A4 = MIDI 69

    auto s = runSeconds(c, in.data(), in.size());
    REQUIRE(s.midiNote == 69);
    REQUIRE_THAT(s.cents, WithinAbs(0.0f, 20.0f));
}

TEST_CASE("PitchTrackedCarrier YIN: warm-up suppresses voiced detection until ring is full",
          "[audio][pitch_tracked_carrier][yin]") {
    PitchTrackedCarrier c;
    c.prepare(48000.0, 256);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(2048);          // < kWindowSize, so warm-up window
    gen.sine(220.0f, 0.4f, in.data(), in.size());

    std::vector<float> out(256);
    PitchTrackedCarrier::State s{};
    // Drive only enough samples to fire the first hop but not enough to fill
    // the ring (kWindowSize = 2048; first hop fires at sample 256 / 512 / ...
    // up through 1792, all before the ring is "full" of real samples).
    for (std::size_t i = 0; i < 1792; i += 256) {
        s = c.process(in.data() + i, out.data(), 256);
    }
    REQUIRE(s.voiced == false);
}
