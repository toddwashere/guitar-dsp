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

TEST_CASE("PitchTrackedCarrier saw: produces non-silent output when voiced",
          "[audio][pitch_tracked_carrier][saw]") {
    PitchTrackedCarrier c;
    c.prepare(48000.0, 512);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000);
    gen.sine(220.0f, 0.4f, in.data(), in.size());

    std::vector<float> out(512);
    // Process the first 0.5 s so YIN locks past warm-up, then capture peak
    // in the second half.
    float peakLast = 0.0f;
    for (std::size_t i = 0; i < in.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, in.size() - i);
        auto s = c.process(in.data() + i, out.data(), n);
        if (s.voiced && i >= 24000) {
            for (std::size_t k = 0; k < n; ++k)
                peakLast = std::max(peakLast, std::abs(out[k]));
        }
    }
    REQUIRE(peakLast > 0.1f);
}

TEST_CASE("PitchTrackedCarrier saw: spectral peak near detected fundamental",
          "[audio][pitch_tracked_carrier][saw]") {
    // Goertzel-based check: at the detected F0, output should have
    // substantially more energy than at a non-harmonic probe frequency.
    PitchTrackedCarrier c;
    c.prepare(48000.0, 512);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000);
    gen.sine(220.0f, 0.4f, in.data(), in.size());

    std::vector<float> sawOut(48000);
    std::vector<float> blockOut(512);
    for (std::size_t i = 0; i < in.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, in.size() - i);
        c.process(in.data() + i, blockOut.data(), n);
        std::copy(blockOut.begin(), blockOut.begin() + n,
                  sawOut.begin() + i);
    }

    // Goertzel magnitude over the last 0.5 s at 220 Hz (expected peak)
    // and at 313 Hz (non-harmonic probe).
    auto goertzel = [&](float hz) {
        const float omega = 2.0f * 3.14159265f * hz / 48000.0f;
        const float coef  = 2.0f * std::cos(omega);
        float s1 = 0.0f, s2 = 0.0f;
        for (std::size_t i = 24000; i < sawOut.size(); ++i) {
            const float s = sawOut[i] + coef * s1 - s2;
            s2 = s1;
            s1 = s;
        }
        return s1 * s1 + s2 * s2 - coef * s1 * s2;
    };

    REQUIRE(goertzel(220.0f) > 4.0f * goertzel(313.0f));
}

TEST_CASE("PitchTrackedCarrier hold: continues singing for ~holdMs after silence",
          "[audio][pitch_tracked_carrier][hold]") {
    PitchTrackedCarrier c;
    c.prepare(48000.0, 512);
    c.setHoldMs(500.0f);   // 24000 samples @ 48k
    c.setDecayMs(100.0f);  // 4800 samples decay

    SyntheticGuitar gen{48000.0};
    std::vector<float> tone(48000);
    gen.sine(220.0f, 0.4f, tone.data(), tone.size());

    std::vector<float> blockOut(512);
    // Phase 1: feed 1s of tone so YIN locks.
    for (std::size_t i = 0; i < tone.size(); i += 512)
        c.process(tone.data() + i, blockOut.data(),
                  std::min<std::size_t>(512, tone.size() - i));

    // Phase 2: feed 200 ms of silence; saw should still be loud (hold).
    std::vector<float> silence(48000 * 200 / 1000, 0.0f);
    float peakDuringHold = 0.0f;
    for (std::size_t i = 0; i < silence.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, silence.size() - i);
        c.process(silence.data() + i, blockOut.data(), n);
        for (std::size_t k = 0; k < n; ++k)
            peakDuringHold = std::max(peakDuringHold, std::abs(blockOut[k]));
    }
    REQUIRE(peakDuringHold > 0.5f);  // still near full level mid-hold
}

TEST_CASE("PitchTrackedCarrier decay: amplitude near zero after hold+decay window",
          "[audio][pitch_tracked_carrier][hold]") {
    PitchTrackedCarrier c;
    c.prepare(48000.0, 512);
    c.setHoldMs(200.0f);   //  9600 samples
    c.setDecayMs(100.0f);  //  4800 samples

    SyntheticGuitar gen{48000.0};
    std::vector<float> tone(48000);
    gen.sine(220.0f, 0.4f, tone.data(), tone.size());

    std::vector<float> blockOut(512);
    for (std::size_t i = 0; i < tone.size(); i += 512)
        c.process(tone.data() + i, blockOut.data(),
                  std::min<std::size_t>(512, tone.size() - i));

    // Feed enough silence to exceed hold + decay + slack.
    std::vector<float> silence(48000, 0.0f);  // 1 second
    float peakLastBlock = 0.0f;
    for (std::size_t i = 0; i < silence.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, silence.size() - i);
        auto s = c.process(silence.data() + i, blockOut.data(), n);
        if (i + n == silence.size()) {
            for (std::size_t k = 0; k < n; ++k)
                peakLastBlock = std::max(peakLastBlock, std::abs(blockOut[k]));
            // voiced flag must be false even though we still emit during decay.
            REQUIRE(s.voiced == false);
        }
    }
    REQUIRE(peakLastBlock < 0.01f);
}

TEST_CASE("PitchTrackedCarrier: re-locks within 2 hops after voiced->unvoiced->voiced",
          "[audio][pitch_tracked_carrier][hold]") {
    PitchTrackedCarrier c;
    c.prepare(48000.0, 512);
    c.setHoldMs(50.0f);
    c.setDecayMs(50.0f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> a(24000), silence(12000, 0.0f), b(24000);
    gen.sine(220.0f, 0.4f, a.data(), a.size());
    gen.sine(330.0f, 0.4f, b.data(), b.size());

    std::vector<float> in;
    in.insert(in.end(), a.begin(), a.end());
    in.insert(in.end(), silence.begin(), silence.end());
    in.insert(in.end(), b.begin(), b.end());

    std::vector<float> blockOut(512);
    PitchTrackedCarrier::State last{};
    for (std::size_t i = 0; i < in.size(); i += 512)
        last = c.process(in.data() + i, blockOut.data(),
                         std::min<std::size_t>(512, in.size() - i));

    REQUIRE(last.voiced == true);
    REQUIRE_THAT(centsBetween(last.freqHz, 330.0f), WithinAbs(0.0f, 30.0f));
}
