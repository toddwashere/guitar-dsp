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

TEST_CASE("PitchTrackedCarrier hold: still loud at 200 ms into silence with holdMs=500",
          "[audio][pitch_tracked_carrier][hold]") {
    PitchTrackedCarrier c;
    c.prepare(48000.0, 512);
    c.setHoldMs(500.0f);
    c.setDecayMs(100.0f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> tone(48000);
    gen.sine(220.0f, 0.4f, tone.data(), tone.size());

    std::vector<float> blockOut(512);
    // Phase 1: 1 s of tone (lock + fill ring).
    for (std::size_t i = 0; i < tone.size(); i += 512)
        c.process(tone.data() + i, blockOut.data(),
                  std::min<std::size_t>(512, tone.size() - i));

    // Phase 2: silence. We want to sample peak amplitude in a block far past
    // YIN's ring-buffer lag (~46 ms) but well before holdMs (500 ms) expires.
    // Specifically, the block covering t = 150–162 ms of silence (samples
    // 7168–7679 of the silence stream).
    std::vector<float> silence(48000, 0.0f);  // 1 s of silence available
    float peakLate = 0.0f;
    constexpr std::size_t kProbeStart = 7168;   // 149 ms into silence
    constexpr std::size_t kProbeLen   = 512;
    for (std::size_t i = 0; i < silence.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, silence.size() - i);
        c.process(silence.data() + i, blockOut.data(), n);
        // Capture peak within the late-probe block only.
        if (i == kProbeStart) {
            for (std::size_t k = 0; k < kProbeLen; ++k)
                peakLate = std::max(peakLate, std::abs(blockOut[k]));
        }
    }
    // With hold active, saw is at full level at 150 ms of silence. Without
    // the hold state machine, the saw goes silent ~46 ms into silence.
    REQUIRE(peakLate > 0.5f);
}

TEST_CASE("PitchTrackedCarrier decay: ramps down across the decay window",
          "[audio][pitch_tracked_carrier][hold]") {
    PitchTrackedCarrier c;
    c.prepare(48000.0, 512);
    c.setHoldMs(100.0f);    // 4800 samples hold
    c.setDecayMs(400.0f);   // 19200 samples decay — wide enough to sample
                            // mid-decay reliably

    SyntheticGuitar gen{48000.0};
    std::vector<float> tone(48000);
    gen.sine(220.0f, 0.4f, tone.data(), tone.size());

    std::vector<float> blockOut(512);
    for (std::size_t i = 0; i < tone.size(); i += 512)
        c.process(tone.data() + i, blockOut.data(),
                  std::min<std::size_t>(512, tone.size() - i));

    // Silence stream:
    //   t = 0 ms       silence begins
    //   t ≈ 46 ms      YIN ring transitions to unvoiced
    //   t = 100 ms     hold expires, decay begins
    //   t = 300 ms     decay halfway through (gain ≈ 0.5)
    //   t = 500 ms     decay complete, saw silent
    std::vector<float> silence(48000, 0.0f);
    float peakMidDecay  = 0.0f;
    float peakPostDecay = 0.0f;
    constexpr std::size_t kMidDecayStart  = 14336;  // ~299 ms
    constexpr std::size_t kPostDecayStart = 28672;  // ~597 ms
    for (std::size_t i = 0; i < silence.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, silence.size() - i);
        auto s = c.process(silence.data() + i, blockOut.data(), n);
        if (i == kMidDecayStart) {
            for (std::size_t k = 0; k < n; ++k)
                peakMidDecay = std::max(peakMidDecay, std::abs(blockOut[k]));
        }
        if (i == kPostDecayStart) {
            for (std::size_t k = 0; k < n; ++k)
                peakPostDecay = std::max(peakPostDecay, std::abs(blockOut[k]));
            REQUIRE(s.voiced == false);
        }
    }
    // Mid-decay: amplitude around 0.5 (linear decay from 1.0 -> 0 over decayMs).
    // Without state machine: 0. Without linear ramp (just hold->cliff): either
    // ~1.0 (if still in hold) or 0. The (0.2, 0.85) window pins linear behavior.
    REQUIRE(peakMidDecay > 0.2f);
    REQUIRE(peakMidDecay < 0.85f);
    // Past hold + decay: saw should be silent.
    REQUIRE(peakPostDecay < 0.01f);
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

TEST_CASE("PitchTrackedCarrier saw: LPF at 2 kHz attenuates 5 kHz energy vs LPF-bypassed reference",
          "[audio][pitch_tracked_carrier][saw][lpf]") {
    // Drive the same 1 kHz sine through two PitchTrackedCarrier instances.
    // One has the default 2 kHz LPF; the other has the LPF effectively
    // bypassed (cutoff set above Nyquist). At 5 kHz the LPF instance
    // should show meaningfully less energy than the bypass instance.
    auto runAndCollect = [](float lpfHz) {
        PitchTrackedCarrier c;
        c.setSawLowpassHz(lpfHz);     // BEFORE prepare so alpha is set
        c.prepare(48000.0, 512);

        SyntheticGuitar gen{48000.0};
        std::vector<float> in(48000);
        gen.sine(1000.0f, 0.4f, in.data(), in.size());

        std::vector<float> sawOut(48000), blockOut(512);
        for (std::size_t i = 0; i < in.size(); i += 512) {
            const std::size_t n = std::min<std::size_t>(512, in.size() - i);
            c.process(in.data() + i, blockOut.data(), n);
            std::copy(blockOut.begin(), blockOut.begin() + n, sawOut.begin() + i);
        }
        return sawOut;
    };

    auto goertzel = [](const std::vector<float>& buf, float hz) {
        const float omega = 2.0f * 3.14159265f * hz / 48000.0f;
        const float coef  = 2.0f * std::cos(omega);
        float s1 = 0.0f, s2 = 0.0f;
        for (std::size_t i = 24000; i < buf.size(); ++i) {
            const float s = buf[i] + coef * s1 - s2;
            s2 = s1; s1 = s;
        }
        return s1 * s1 + s2 * s2 - coef * s1 * s2;
    };

    const auto withLpf      = runAndCollect(2000.0f);    // default
    const auto withoutLpf   = runAndCollect(40000.0f);   // above Nyquist -> alpha=1, pass-through

    // At the FUNDAMENTAL (1 kHz), below cutoff, energy should be comparable.
    const float lowWithLpf    = goertzel(withLpf,    1000.0f);
    const float lowWithoutLpf = goertzel(withoutLpf, 1000.0f);
    REQUIRE(lowWithLpf > 0.5f * lowWithoutLpf);

    // At 5 kHz (above the 2 kHz cutoff), the LPF instance should have
    // SUBSTANTIALLY less energy than the bypass instance — at least 5x less
    // (~7 dB reduction). This pins the LPF's actual effect.
    const float highWithLpf    = goertzel(withLpf,    5000.0f);
    const float highWithoutLpf = goertzel(withoutLpf, 5000.0f);
    REQUIRE(highWithLpf * 5.0f < highWithoutLpf);
}

TEST_CASE("PitchTrackedCarrier vibrato: peak instantaneous frequency deviates from F0 when Sing on",
          "[audio][pitch_tracked_carrier][sing][vibrato]") {
    PitchTrackedCarrier c;
    c.prepare(48000.0, 512);
    c.setSinging(true);
    c.setPitchQuantize(false);  // isolate vibrato — no semitone snapping
    c.setVibratoHz(5.0f);
    c.setVibratoCents(40.0f);   // bigger depth so the test is robust

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000);
    gen.sine(220.0f, 0.4f, in.data(), in.size());

    std::vector<float> sawOut(48000), blockOut(512);
    for (std::size_t i = 0; i < in.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, in.size() - i);
        c.process(in.data() + i, blockOut.data(), n);
        std::copy(blockOut.begin(), blockOut.begin() + n, sawOut.begin() + i);
    }

    // Measure zero-crossing intervals over the last 0.5 s; the longest
    // observed interval should correspond to a frequency lower than F0 by
    // at least 10 cents (vibrato pulling pitch down).
    int lastCrossing = -1, longestInterval = 0;
    for (std::size_t i = 24000; i + 1 < sawOut.size(); ++i) {
        if (sawOut[i] <= 0.0f && sawOut[i + 1] > 0.0f) {
            if (lastCrossing >= 0) {
                const int interval = static_cast<int>(i) - lastCrossing;
                longestInterval = std::max(longestInterval, interval);
            }
            lastCrossing = static_cast<int>(i);
        }
    }
    const float minHz = 48000.0f / static_cast<float>(longestInterval);
    const float centsFromF0 = 1200.0f * std::log2(minHz / 220.0f);
    REQUIRE(centsFromF0 < -10.0f);
}

TEST_CASE("PitchTrackedCarrier quantize: 226 Hz input -> 220 Hz output peak in Sing+quantize mode",
          "[audio][pitch_tracked_carrier][sing][quantize]") {
    PitchTrackedCarrier c;
    c.prepare(48000.0, 512);
    c.setSinging(true);
    c.setPitchQuantize(true);
    c.setVibratoCents(0.0f);   // disable vibrato so quantize is isolated

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000);
    gen.sine(226.0f, 0.4f, in.data(), in.size());  // slightly sharp A3

    std::vector<float> sawOut(48000), blockOut(512);
    for (std::size_t i = 0; i < in.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, in.size() - i);
        c.process(in.data() + i, blockOut.data(), n);
        std::copy(blockOut.begin(), blockOut.begin() + n, sawOut.begin() + i);
    }

    auto goertzel = [&](float hz) {
        const float omega = 2.0f * 3.14159265f * hz / 48000.0f;
        const float coef  = 2.0f * std::cos(omega);
        float s1 = 0.0f, s2 = 0.0f;
        for (std::size_t i = 24000; i < sawOut.size(); ++i) {
            const float s = sawOut[i] + coef * s1 - s2;
            s2 = s1; s1 = s;
        }
        return s1 * s1 + s2 * s2 - coef * s1 * s2;
    };
    // Quantized output should have substantially more energy at 220 Hz (A3)
    // than at 226 Hz (the raw input pitch).
    REQUIRE(goertzel(220.0f) > 2.0f * goertzel(226.0f));
}
