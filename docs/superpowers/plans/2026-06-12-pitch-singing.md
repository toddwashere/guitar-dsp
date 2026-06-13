# Pitch-Tracked Singing Carrier Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a toggleable pitch-tracked sawtooth carrier to the vocoder so the spoken/AI voice sings in the guitar's note, replacing the broadband-noise floor when active.

**Architecture:** New `PitchTrackedCarrier` module (YIN F0 detector + PolyBLEP sawtooth + hold/decay state machine) owned by `AudioGraph`. A new atomic `pitchSinging_` flag selects between today's noise-floor carrier and the pitched carrier per audio block. FCB MIDI CC#80 (latching, value ≥ 64 toggles) flips the flag via a new `SceneCommandType::TogglePitchSinging` routed through the existing `FCB1010Mapping`/`HostMidiSceneRouter` paths. A `NoteReadout` JUCE component polls `AudioGraph` atomics to display detected note + cents + Hz. Toggle is persisted via `PluginState`.

**Tech Stack:** C++20, JUCE, Catch2, existing `guitar_dsp::audio` / `guitar_dsp::midi` / `guitar_dsp::app` modules.

**Spec:** [docs/superpowers/specs/2026-06-12-pitch-singing-design.md](../specs/2026-06-12-pitch-singing-design.md)

---

## Task 1: `PitchTrackedCarrier` scaffold + CMake registration

Create the empty module, get it compiled and a smoke test green. No DSP yet — that comes in Tasks 2-4.

**Files:**
- Create: `src/audio/PitchTrackedCarrier.h`
- Create: `src/audio/PitchTrackedCarrier.cpp`
- Create: `tests/unit/audio/test_pitch_tracked_carrier.cpp`
- Modify: `src/CMakeLists.txt` (add to `guitar_dsp_audio` STATIC sources)
- Modify: `tests/CMakeLists.txt` (add the test file)

- [ ] **Step 1: Write the header**

`src/audio/PitchTrackedCarrier.h`:

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <vector>

namespace guitar_dsp::audio {

// Pitch-tracked sawtooth carrier for the vocoder. Detects the guitar's
// fundamental with YIN, synthesizes a PolyBLEP-anti-aliased sawtooth at
// that pitch, holds the last detected pitch for a configurable window
// when the guitar goes silent, then fades. Allocation-free in process();
// all buffers sized at prepare().
class PitchTrackedCarrier {
public:
    PitchTrackedCarrier();

    void prepare(double sampleRate, int blockSize);
    void reset();

    struct State {
        float freqHz   = 0.0f;   // 0 when unvoiced AND hold expired
        int   midiNote = -1;     // -1 when unvoiced AND hold expired
        float cents    = 0.0f;   // fine offset from midiNote, in [-50, +50]
        bool  voiced   = false;  // current detection (not hold state)
    };

    // Writes pitched carrier samples into `out`. Returns the latest
    // published state. `guitarIn` and `out` must not alias.
    State process(const float* guitarIn, float* out, std::size_t numSamples);

    // Tunable parameters (set once after construction; not message-thread
    // safe to change while audio is running).
    void setHoldMs(float ms)  noexcept;   // default 1000.0
    void setDecayMs(float ms) noexcept;   // default 200.0

    // Frequency range clamp. Detections outside [minHz, maxHz] are
    // treated as unvoiced. Defaults: 40 Hz .. 2000 Hz.
    void setFrequencyRange(float minHz, float maxHz) noexcept;

private:
    // ---- YIN detector parameters ---------------------------------------
    static constexpr int kWindowSize = 2048;   // ~46 ms @ 44.1 kHz
    static constexpr int kHopSize    = 256;    // ~5.8 ms @ 44.1 kHz
    static constexpr float kYinThreshold = 0.15f;

    // ---- Ring buffer of recent input samples ---------------------------
    std::vector<float> ring_;
    int                ringWriteIdx_ = 0;
    int                samplesUntilNextHop_ = kHopSize;

    // ---- YIN scratch ---------------------------------------------------
    std::vector<float> diff_;   // sized kWindowSize/2

    // ---- Saw oscillator ------------------------------------------------
    double sampleRate_ = 48000.0;
    double sawPhase_   = 0.0;   // [0, 1)
    float  currentFreqHz_ = 0.0f;

    // ---- Hold/decay state ----------------------------------------------
    float holdMs_       = 1000.0f;
    float decayMs_      = 200.0f;
    float minHz_        = 40.0f;
    float maxHz_        = 2000.0f;
    float lastVoicedFreqHz_ = 0.0f;
    int   holdSamplesRemaining_  = 0;   // counts down while in hold
    int   decaySamplesRemaining_ = 0;   // counts down during fade
    float decayGain_    = 0.0f;         // updated per sample during decay
    bool  currentlyVoiced_ = false;

    // ---- Cached for State output ---------------------------------------
    int   currentMidiNote_ = -1;
    float currentCents_    = 0.0f;

    // Run one YIN frame on the last kWindowSize samples ending at ringWriteIdx_.
    // Returns frequency in Hz or 0.0f if unvoiced.
    float runYin() noexcept;

    // PolyBLEP-corrected sawtooth: increments sawPhase_ by freq/sampleRate,
    // applies BLEP at the wraparound.
    float nextSawSample(float freqHz) noexcept;
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 2: Write the empty implementation**

`src/audio/PitchTrackedCarrier.cpp`:

```cpp
#include "PitchTrackedCarrier.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

PitchTrackedCarrier::PitchTrackedCarrier() = default;

void PitchTrackedCarrier::prepare(double sampleRate, int blockSize) {
    (void) blockSize;
    sampleRate_ = sampleRate;
    ring_.assign(kWindowSize, 0.0f);
    diff_.assign(kWindowSize / 2, 0.0f);
    reset();
}

void PitchTrackedCarrier::reset() {
    std::fill(ring_.begin(), ring_.end(), 0.0f);
    ringWriteIdx_ = 0;
    samplesUntilNextHop_ = kHopSize;
    sawPhase_ = 0.0;
    currentFreqHz_ = 0.0f;
    lastVoicedFreqHz_ = 0.0f;
    holdSamplesRemaining_ = 0;
    decaySamplesRemaining_ = 0;
    decayGain_ = 0.0f;
    currentlyVoiced_ = false;
    currentMidiNote_ = -1;
    currentCents_ = 0.0f;
}

void PitchTrackedCarrier::setHoldMs(float ms)  noexcept { holdMs_  = ms; }
void PitchTrackedCarrier::setDecayMs(float ms) noexcept { decayMs_ = ms; }
void PitchTrackedCarrier::setFrequencyRange(float minHz, float maxHz) noexcept {
    minHz_ = minHz;
    maxHz_ = maxHz;
}

PitchTrackedCarrier::State PitchTrackedCarrier::process(
        const float* guitarIn, float* out, std::size_t numSamples) {
    // Implementation arrives in Tasks 2-4. For now, emit silence so the
    // module is usable as a no-op in the AudioGraph wiring task.
    for (std::size_t i = 0; i < numSamples; ++i) out[i] = 0.0f;
    return State{};
}

float PitchTrackedCarrier::runYin() noexcept {
    return 0.0f;  // filled in Task 2
}

float PitchTrackedCarrier::nextSawSample(float /*freqHz*/) noexcept {
    return 0.0f;  // filled in Task 3
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 3: Register the source file**

In `src/CMakeLists.txt`, add `audio/PitchTrackedCarrier.cpp` to the `guitar_dsp_audio` STATIC source list (alphabetically near `audio/PitchShifter.cpp`):

```cmake
add_library(guitar_dsp_audio STATIC
    audio/InputStage.cpp
    audio/Mixer.cpp
    audio/AudioGraph.cpp
    audio/ChannelVocoder.cpp
    # ... existing entries ...
    audio/PitchShifter.cpp
    audio/PitchTrackedCarrier.cpp
    # ... rest unchanged ...
)
```

- [ ] **Step 4: Write the smoke test**

`tests/unit/audio/test_pitch_tracked_carrier.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/PitchTrackedCarrier.h"

#include <vector>

using guitar_dsp::audio::PitchTrackedCarrier;

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
```

- [ ] **Step 5: Register the test file**

In `tests/CMakeLists.txt`, add `unit/audio/test_pitch_tracked_carrier.cpp` alphabetically near `test_pitch_shifter.cpp`.

- [ ] **Step 6: Build and run**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "pitch_tracked_carrier"
```

Expected: build green; test PASSES.

- [ ] **Step 7: Commit**

```bash
git add src/audio/PitchTrackedCarrier.h src/audio/PitchTrackedCarrier.cpp \
        src/CMakeLists.txt \
        tests/unit/audio/test_pitch_tracked_carrier.cpp tests/CMakeLists.txt
git commit -m "feat(audio): scaffold PitchTrackedCarrier module (no DSP yet)"
```

---

## Task 2: YIN pitch detector

Implement YIN. Tests drive expected accuracy: ±20 cents on a steady sine across 80–800 Hz, unvoiced on silence.

**Files:**
- Modify: `src/audio/PitchTrackedCarrier.cpp` (`runYin()` + ring-buffer feed in `process()`)
- Modify: `tests/unit/audio/test_pitch_tracked_carrier.cpp`

- [ ] **Step 1: Write the failing tests**

Add to `tests/unit/audio/test_pitch_tracked_carrier.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "audio/PitchTrackedCarrier.h"
#include "harness/SyntheticGuitar.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::PitchTrackedCarrier;
using guitar_dsp::tests::SyntheticGuitar;
using Catch::Matchers::WithinAbs;

namespace {
// Run `seconds` of input through `c` in 512-sample blocks and return the
// final published State.
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

TEST_CASE("PitchTrackedCarrier YIN: low E (82.4 Hz) plucked-string", "[audio][pitch_tracked_carrier][yin]") {
    PitchTrackedCarrier c;
    c.prepare(48000.0, 512);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000);
    gen.pluck(82.4f, 2.0f, 0.6f, in.data(), in.size());

    auto s = runSeconds(c, in.data(), in.size());
    REQUIRE(s.voiced == true);
    // ±50 cents = within a semitone (no octave error). Tighter than that
    // is unreliable on a Karplus-Strong synth; the real test is "no octave
    // error" (82 vs 164 or 41).
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
```

- [ ] **Step 2: Run the tests to confirm they fail**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "pitch_tracked_carrier"
```

Expected: smoke test still PASSES; the 5 new YIN tests FAIL (no detection yet — output `freqHz=0`, `voiced=false`, `midiNote=-1`).

- [ ] **Step 3: Implement YIN + ring-buffer feed + state publish**

Replace `process()` and `runYin()` in `src/audio/PitchTrackedCarrier.cpp`:

```cpp
PitchTrackedCarrier::State PitchTrackedCarrier::process(
        const float* guitarIn, float* out, std::size_t numSamples) {
    for (std::size_t i = 0; i < numSamples; ++i) {
        ring_[ringWriteIdx_] = guitarIn[i];
        ringWriteIdx_ = (ringWriteIdx_ + 1) % kWindowSize;

        if (--samplesUntilNextHop_ == 0) {
            samplesUntilNextHop_ = kHopSize;
            const float f0 = runYin();
            currentlyVoiced_ = (f0 > 0.0f);
            if (currentlyVoiced_) {
                currentFreqHz_ = f0;
                lastVoicedFreqHz_ = f0;
                const float midi = 69.0f + 12.0f * std::log2(f0 / 440.0f);
                currentMidiNote_ = static_cast<int>(std::lround(midi));
                currentCents_ = 100.0f * (midi - currentMidiNote_);
            }
        }

        out[i] = 0.0f;  // Task 3 fills this with the saw
    }

    State s;
    s.voiced   = currentlyVoiced_;
    s.freqHz   = currentlyVoiced_ ? currentFreqHz_ : 0.0f;
    s.midiNote = currentlyVoiced_ ? currentMidiNote_ : -1;
    s.cents    = currentlyVoiced_ ? currentCents_ : 0.0f;
    return s;
}

float PitchTrackedCarrier::runYin() noexcept {
    // YIN steps 2-4 (de Cheveigné & Kawahara, 2002):
    //   1) Difference function: d_t(tau) = sum_{j=0..W-1} (x[j] - x[j+tau])^2
    //      We compute it over the most recent kWindowSize/2 samples for j, and
    //      tau in [1, kWindowSize/2].
    //   2) Cumulative mean normalized difference (CMNDF):
    //      d'_t(0) = 1; d'_t(tau) = d_t(tau) / ((1/tau) * sum_{j=1..tau} d_t(j))
    //   3) Absolute threshold: pick the smallest tau with d'_t(tau) < threshold
    //      such that it's a local minimum; if none below threshold, treat as
    //      unvoiced.
    //   4) Parabolic interpolation around the chosen tau for sub-sample precision.

    const int W = kWindowSize / 2;
    // Read a contiguous window of length W ending at ringWriteIdx_, and a
    // shifted window for each tau.
    auto sampleAt = [this](int idxFromOldest) {
        // idxFromOldest=0 is the oldest sample in the ring, kWindowSize-1 is newest.
        return ring_[(ringWriteIdx_ + idxFromOldest) % kWindowSize];
    };

    // Step 1: difference function
    for (int tau = 0; tau < W; ++tau) {
        float sum = 0.0f;
        for (int j = 0; j < W; ++j) {
            const float a = sampleAt(j);
            const float b = sampleAt(j + tau);
            const float d = a - b;
            sum += d * d;
        }
        diff_[tau] = sum;
    }

    // Step 2: cumulative mean normalized difference
    diff_[0] = 1.0f;
    float runningSum = 0.0f;
    for (int tau = 1; tau < W; ++tau) {
        runningSum += diff_[tau];
        diff_[tau] = diff_[tau] * tau / (runningSum > 0.0f ? runningSum : 1.0f);
    }

    // Step 3: absolute threshold + first local minimum
    int chosenTau = -1;
    const int minTau = std::max(2, static_cast<int>(sampleRate_ / maxHz_));
    const int maxTau = std::min(W - 1, static_cast<int>(sampleRate_ / minHz_));
    for (int tau = minTau; tau <= maxTau; ++tau) {
        if (diff_[tau] < kYinThreshold) {
            // Walk forward while the function is still decreasing.
            while (tau + 1 <= maxTau && diff_[tau + 1] < diff_[tau]) ++tau;
            chosenTau = tau;
            break;
        }
    }
    if (chosenTau < 0) return 0.0f;  // unvoiced

    // Step 4: parabolic interpolation around chosenTau
    float refined = static_cast<float>(chosenTau);
    if (chosenTau > 0 && chosenTau < W - 1) {
        const float y0 = diff_[chosenTau - 1];
        const float y1 = diff_[chosenTau];
        const float y2 = diff_[chosenTau + 1];
        const float denom = 2.0f * (y0 - 2.0f * y1 + y2);
        if (std::abs(denom) > 1e-12f)
            refined += (y0 - y2) / denom;
    }

    const float f0 = static_cast<float>(sampleRate_) / refined;
    if (f0 < minHz_ || f0 > maxHz_) return 0.0f;
    return f0;
}
```

Add includes at the top of the .cpp:

```cpp
#include <cmath>
```

- [ ] **Step 4: Re-run tests to verify they pass**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "pitch_tracked_carrier"
```

Expected: all 6 PitchTrackedCarrier tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/audio/PitchTrackedCarrier.cpp tests/unit/audio/test_pitch_tracked_carrier.cpp
git commit -m "feat(audio): YIN F0 detector in PitchTrackedCarrier"
```

---

## Task 3: PolyBLEP sawtooth oscillator

Replace the silent placeholder output with an anti-aliased sawtooth at the detected pitch.

**Files:**
- Modify: `src/audio/PitchTrackedCarrier.cpp`
- Modify: `tests/unit/audio/test_pitch_tracked_carrier.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/audio/test_pitch_tracked_carrier.cpp`:

```cpp
TEST_CASE("PitchTrackedCarrier saw: produces non-silent output when voiced",
          "[audio][pitch_tracked_carrier][saw]") {
    PitchTrackedCarrier c;
    c.prepare(48000.0, 512);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000);
    gen.sine(220.0f, 0.4f, in.data(), in.size());

    std::vector<float> out(512);
    // Process the first 0.5 s so YIN locks, then capture the last block.
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
    // and at 313 Hz (non-harmonic).
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
```

- [ ] **Step 2: Run tests to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "pitch_tracked_carrier"
```

Expected: the 2 new saw tests FAIL (output is still silent).

- [ ] **Step 3: Implement PolyBLEP saw**

In `src/audio/PitchTrackedCarrier.cpp`, replace `nextSawSample` and update the `process()` inner loop:

```cpp
float PitchTrackedCarrier::nextSawSample(float freqHz) noexcept {
    // Phase increment per sample, in [0, 1).
    const double dt = static_cast<double>(freqHz) / sampleRate_;
    // Naive saw: value = 2*phase - 1.
    float v = static_cast<float>(2.0 * sawPhase_ - 1.0);

    // PolyBLEP correction at the phase=0 / phase=1 discontinuity.
    auto polyBlep = [dt](double t) {
        if (t < dt) {
            t /= dt;
            return t + t - t * t - 1.0;
        }
        if (t > 1.0 - dt) {
            t = (t - 1.0) / dt;
            return t * t + t + t + 1.0;
        }
        return 0.0;
    };
    v -= static_cast<float>(polyBlep(sawPhase_));

    sawPhase_ += dt;
    if (sawPhase_ >= 1.0) sawPhase_ -= 1.0;
    return v;
}
```

Now update the `process()` inner loop to drive the saw and apply the simple amplitude rule (full level when voiced, zero otherwise — hold/decay arrives in Task 4):

```cpp
out[i] = currentlyVoiced_ && currentFreqHz_ > 0.0f
       ? nextSawSample(currentFreqHz_)
       : 0.0f;
```

(Replace the existing `out[i] = 0.0f;` line in `process()`.)

- [ ] **Step 4: Re-run tests**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "pitch_tracked_carrier"
```

Expected: all PitchTrackedCarrier tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/audio/PitchTrackedCarrier.cpp tests/unit/audio/test_pitch_tracked_carrier.cpp
git commit -m "feat(audio): PolyBLEP sawtooth oscillator in PitchTrackedCarrier"
```

---

## Task 4: Hold-last-pitch + decay state machine

When the guitar goes silent, the saw should keep singing the last detected pitch for `holdMs`, then fade over `decayMs`. Without this, the carrier dies the moment you release a string mid-TTS-sentence.

**Files:**
- Modify: `src/audio/PitchTrackedCarrier.cpp`
- Modify: `tests/unit/audio/test_pitch_tracked_carrier.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/audio/test_pitch_tracked_carrier.cpp`:

```cpp
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

    // Phase 2: feed 200 ms of silence; saw should still be loud.
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
```

- [ ] **Step 2: Run tests to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "pitch_tracked_carrier"
```

Expected: the 3 new hold/decay tests FAIL.

- [ ] **Step 3: Implement hold/decay in process()**

In `src/audio/PitchTrackedCarrier.cpp`, replace `process()`:

```cpp
PitchTrackedCarrier::State PitchTrackedCarrier::process(
        const float* guitarIn, float* out, std::size_t numSamples) {
    const int holdSamplesTotal  = static_cast<int>(holdMs_  * sampleRate_ / 1000.0f);
    const int decaySamplesTotal = static_cast<int>(decayMs_ * sampleRate_ / 1000.0f);

    for (std::size_t i = 0; i < numSamples; ++i) {
        ring_[ringWriteIdx_] = guitarIn[i];
        ringWriteIdx_ = (ringWriteIdx_ + 1) % kWindowSize;

        if (--samplesUntilNextHop_ == 0) {
            samplesUntilNextHop_ = kHopSize;
            const float f0 = runYin();
            const bool nowVoiced = (f0 > 0.0f);

            if (nowVoiced) {
                if (!currentlyVoiced_) {
                    // Transition unvoiced -> voiced: reset hold/decay,
                    // restart amplitude at full.
                    holdSamplesRemaining_  = 0;
                    decaySamplesRemaining_ = 0;
                    decayGain_ = 1.0f;
                }
                currentFreqHz_     = f0;
                lastVoicedFreqHz_  = f0;
                const float midi   = 69.0f + 12.0f * std::log2(f0 / 440.0f);
                currentMidiNote_   = static_cast<int>(std::lround(midi));
                currentCents_      = 100.0f * (midi - currentMidiNote_);
            } else if (currentlyVoiced_) {
                // Transition voiced -> unvoiced: enter hold.
                holdSamplesRemaining_  = holdSamplesTotal;
                decaySamplesRemaining_ = decaySamplesTotal;
                decayGain_             = 1.0f;
                // keep currentFreqHz_ = lastVoicedFreqHz_
            }
            currentlyVoiced_ = nowVoiced;
        }

        // Determine amplitude envelope for this sample.
        float amp = 0.0f;
        if (currentlyVoiced_) {
            amp = 1.0f;
        } else if (holdSamplesRemaining_ > 0) {
            amp = 1.0f;
            --holdSamplesRemaining_;
        } else if (decaySamplesRemaining_ > 0) {
            // Linear decay from 1.0 -> 0.0 over decaySamplesTotal samples.
            decayGain_ = static_cast<float>(decaySamplesRemaining_)
                       / std::max(1, decaySamplesTotal);
            amp = decayGain_;
            --decaySamplesRemaining_;
        } else {
            // Fully faded — clear cached note so the State reports unvoiced.
            currentMidiNote_ = -1;
            currentCents_    = 0.0f;
            lastVoicedFreqHz_ = 0.0f;
        }

        const float freqForSample = currentlyVoiced_ ? currentFreqHz_
                                                      : lastVoicedFreqHz_;
        out[i] = (amp > 0.0f && freqForSample > 0.0f)
               ? amp * nextSawSample(freqForSample)
               : 0.0f;
    }

    const bool stillSinging = currentlyVoiced_
                              || holdSamplesRemaining_ > 0
                              || decaySamplesRemaining_ > 0;
    State s;
    s.voiced   = currentlyVoiced_;
    s.freqHz   = stillSinging ? (currentlyVoiced_ ? currentFreqHz_ : lastVoicedFreqHz_)
                              : 0.0f;
    s.midiNote = stillSinging ? currentMidiNote_ : -1;
    s.cents    = stillSinging ? currentCents_    : 0.0f;
    return s;
}
```

- [ ] **Step 4: Re-run tests**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "pitch_tracked_carrier"
```

Expected: all PitchTrackedCarrier tests PASS.

- [ ] **Step 5: Commit**

```bash
git add src/audio/PitchTrackedCarrier.cpp tests/unit/audio/test_pitch_tracked_carrier.cpp
git commit -m "feat(audio): hold-last-pitch + linear decay in PitchTrackedCarrier"
```

---

## Task 5: Wire `PitchTrackedCarrier` into `AudioGraph` with the toggle

Add the `pitchSinging_` atomic + the three published atomics, own a `PitchTrackedCarrier`, and route its output into the carrier path when the toggle is on. Toggle off must be a bit-identical regression of today's behavior.

**Files:**
- Modify: `src/audio/AudioGraph.h`
- Modify: `src/audio/AudioGraph.cpp`
- Modify: `tests/unit/audio/test_audio_graph.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/audio/test_audio_graph.cpp`:

```cpp
#include "audio/TTSClip.h"  // already included above; harmless duplicate

TEST_CASE("AudioGraph: pitchSinging toggle defaults to off",
          "[audio][graph][pitch_singing]") {
    AudioGraph graph;
    REQUIRE(graph.pitchSinging() == false);
}

TEST_CASE("AudioGraph: toggle-off output is deterministic across instances",
          "[audio][graph][pitch_singing][regression]") {
    // With pitchSinging off, two AudioGraph instances with identical config
    // and identical inputs must produce bit-identical outputs across blocks.
    // This catches accidental dependence on the new PitchTrackedCarrier's
    // internal state (e.g. if it leaked into a shared buffer). The stronger
    // "unchanged vs pre-change baseline" claim is verified by code review of
    // the carrier-source branch + the broader AudioGraph test suite passing.
    AudioGraph a, b;
    a.prepare(48000.0, 512);
    b.prepare(48000.0, 512);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(512);
    gen.sine(440.0f, 0.3f, in.data(), in.size());

    std::vector<float> outA(512), outB(512);
    for (int blk = 0; blk < 4; ++blk) {
        a.process(in.data(), outA.data(), in.size());
        b.process(in.data(), outB.data(), in.size());
        for (std::size_t i = 0; i < in.size(); ++i)
            REQUIRE(outA[i] == outB[i]);
    }
}

TEST_CASE("AudioGraph: pitchSinging toggle on -> detected note published",
          "[audio][graph][pitch_singing]") {
    AudioGraph graph;
    graph.prepare(48000.0, 512);
    graph.setPitchSinging(true);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000);
    gen.sine(440.0f, 0.4f, in.data(), in.size());

    std::vector<float> out(512);
    for (std::size_t i = 0; i < in.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, in.size() - i);
        graph.process(in.data() + i, out.data(), n);
    }

    REQUIRE(graph.detectedNoteMidi() == 69);  // A4
    REQUIRE(std::abs(graph.detectedCents()) < 20.0f);
    REQUIRE(graph.detectedHz() > 430.0f);
    REQUIRE(graph.detectedHz() < 450.0f);
}

TEST_CASE("AudioGraph: pitchSinging on with TTS modulator -> wet path peak at guitar's F0",
          "[audio][graph][pitch_singing][vocoder]") {
    AudioGraph graph;
    graph.prepare(48000.0, 512);
    graph.setPitchSinging(true);
    graph.mixer().setDryWet(1.0f);
    graph.mixer().setMasterGainDb(0.0f);
    graph.mixer().reset();

    // TTS clip: 1 s of 800 Hz tone (broadband-enough modulator for vocoder
    // to pass a range of carrier bands).
    auto clip = std::make_shared<guitar_dsp::audio::TTSClip>();
    clip->sampleRate = 48000.0;
    clip->samples.resize(48000);
    for (int i = 0; i < 48000; ++i)
        clip->samples[i] = 0.6f * std::sin(2.0 * 3.14159265 * 800.0 * i / 48000.0);
    graph.ttsClipPlayer().setClip(clip);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000);
    gen.sine(220.0f, 0.5f, in.data(), in.size());

    std::vector<float> out(48000), blockOut(512);
    for (std::size_t i = 0; i < in.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, in.size() - i);
        graph.process(in.data() + i, blockOut.data(), n);
        std::copy(blockOut.begin(), blockOut.begin() + n, out.begin() + i);
    }

    // Goertzel @ 220 Hz vs 313 Hz over the last 0.5 s.
    auto goertzel = [&](float hz) {
        const float omega = 2.0f * 3.14159265f * hz / 48000.0f;
        const float coef  = 2.0f * std::cos(omega);
        float s1 = 0.0f, s2 = 0.0f;
        for (std::size_t i = 24000; i < out.size(); ++i) {
            const float s = out[i] + coef * s1 - s2;
            s2 = s1;
            s1 = s;
        }
        return s1 * s1 + s2 * s2 - coef * s1 * s2;
    };
    REQUIRE(goertzel(220.0f) > 2.0f * goertzel(313.0f));
}
```

- [ ] **Step 2: Run tests to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "audio.*graph.*pitch_singing"
```

Expected: the 4 new tests FAIL (missing API + behavior).

- [ ] **Step 3: Extend `AudioGraph.h`**

Add to `src/audio/AudioGraph.h` — `#include "PitchTrackedCarrier.h"` at the top with the others, then inside `class AudioGraph` add the public API just after the existing `setClarity`/`clarity()` block:

```cpp
// Pitch-tracked singing carrier. When ON, AudioGraph composes the carrier
// as `guitar + carrierNoise * pitched_saw` (with ChannelVocoder's internal
// noise floor disabled), so the spoken voice sings the guitar's note.
// Default OFF — preserves today's behavior exactly.
void setPitchSinging(bool on) noexcept {
    pitchSinging_.store(on, std::memory_order_relaxed);
}
bool pitchSinging() const noexcept {
    return pitchSinging_.load(std::memory_order_relaxed);
}

// Latest detected pitch published from the audio thread for the UI. -1
// midiNote / 0 Hz when unvoiced AND hold-decay has expired.
int   detectedNoteMidi() const noexcept { return detectedNoteMidi_.load(std::memory_order_relaxed); }
float detectedCents()    const noexcept { return detectedCents_.load(std::memory_order_relaxed); }
float detectedHz()       const noexcept { return detectedHz_.load(std::memory_order_relaxed); }
```

Add to the private members:

```cpp
PitchTrackedCarrier pitchCarrier_;
std::atomic<bool>   pitchSinging_      {false};
std::atomic<int>    detectedNoteMidi_  {-1};
std::atomic<float>  detectedCents_     {0.0f};
std::atomic<float>  detectedHz_        {0.0f};
std::vector<float>  pitchCarrierBuffer_;  // pitched saw output (read by AudioGraph)
```

- [ ] **Step 4: Wire `prepare`, `reset`, and the carrier swap in `process`**

In `src/audio/AudioGraph.cpp`:

In `prepare()`, after the existing `carrierBuffer_.assign(...)` line:

```cpp
pitchCarrier_.prepare(sampleRate, blockSize);
pitchCarrierBuffer_.assign(static_cast<std::size_t>(blockSize), 0.0f);
```

In `reset()`, after the existing `vocoder_.reset();` line:

```cpp
pitchCarrier_.reset();
std::fill(pitchCarrierBuffer_.begin(), pitchCarrierBuffer_.end(), 0.0f);
```

In `process()`, replace the carrier-construction block (the `const float* carrier = postInputBuffer_.data();` ... `vocoder_.process(...)` region) with this expanded version:

```cpp
// Pitch-tracked carrier always runs so the UI readout is live whether
// the toggle is on or off; its output is only routed when the toggle is on.
const auto pitchState = pitchCarrier_.process(
    postInputBuffer_.data(), pitchCarrierBuffer_.data(), numSamples);
detectedNoteMidi_.store(pitchState.midiNote, std::memory_order_relaxed);
detectedCents_.store(pitchState.cents,       std::memory_order_relaxed);
detectedHz_.store(pitchState.freqHz,         std::memory_order_relaxed);

const bool pitchSinging = pitchSinging_.load(std::memory_order_relaxed);

// Carrier = guitar (default), or (diagnostic) broadband white noise so every
// band has energy to modulate, or (pitch-singing) guitar + cn * pitched saw.
const float* carrier = postInputBuffer_.data();
if (diagNoiseCarrier_.load(std::memory_order_relaxed)) {
    for (std::size_t i = 0; i < numSamples; ++i) {
        diagNoiseState_ ^= diagNoiseState_ << 13;
        diagNoiseState_ ^= diagNoiseState_ >> 17;
        diagNoiseState_ ^= diagNoiseState_ << 5;
        carrierBuffer_[i] =
            0.5f * ((static_cast<float>(diagNoiseState_) / 2147483648.0f) - 1.0f);
    }
    carrier = carrierBuffer_.data();
} else if (pitchSinging) {
    const float cn = vocoder_.carrierNoise();
    for (std::size_t i = 0; i < numSamples; ++i) {
        carrierBuffer_[i] = postInputBuffer_.data()[i]
                          + cn * pitchCarrierBuffer_[i];
    }
    carrier = carrierBuffer_.data();
}

vocoder_.setSibilance(
    diagSibilanceOff_.load(std::memory_order_relaxed)
        ? 0.0f
        : vocoderSibilance_.load(std::memory_order_relaxed));

// When pitch-singing is on, disable the vocoder's internal noise floor so
// the pitched saw is the sole "floor" contribution.
const float savedCarrierNoise = vocoder_.carrierNoise();
if (pitchSinging) vocoder_.setCarrierNoise(0.0f);
vocoder_.process(carrier, wetBuffer_.data(), wetBuffer_.data(), numSamples);
if (pitchSinging) vocoder_.setCarrierNoise(savedCarrierNoise);
```

- [ ] **Step 5: Re-run tests**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "audio.*graph"
```

Expected: all AudioGraph tests PASS, including the 4 new ones and the bit-identical regression.

- [ ] **Step 6: Commit**

```bash
git add src/audio/AudioGraph.h src/audio/AudioGraph.cpp \
        tests/unit/audio/test_audio_graph.cpp
git commit -m "feat(audio): wire PitchTrackedCarrier into AudioGraph (toggle off by default)"
```

---

## Task 6: `TogglePitchSinging` SceneCommand + FCB1010 CC#80

Extend the MIDI mapping to recognize CC#80 (default) with value ≥ 64 as `SceneCommandType::TogglePitchSinging`.

**Files:**
- Modify: `src/midi/SceneCommand.h`
- Modify: `src/midi/FCB1010Mapping.h`
- Modify: `src/midi/FCB1010Mapping.cpp`
- Modify: `tests/unit/midi/test_fcb1010_mapping.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/midi/test_fcb1010_mapping.cpp`:

```cpp
TEST_CASE("FCB1010Mapping: CC 80 value >= 64 -> TogglePitchSinging", "[midi][fcb][pitch_singing]") {
    FCB1010Mapping m = FCB1010Mapping::stockDefaults();
    auto cmd = m.translate(juce::MidiMessage::controllerEvent(1, 80, 127));
    REQUIRE(cmd.has_value());
    REQUIRE(cmd->type == SceneCommandType::TogglePitchSinging);
}

TEST_CASE("FCB1010Mapping: CC 80 value < 64 -> nullopt (latch release)", "[midi][fcb][pitch_singing]") {
    FCB1010Mapping m = FCB1010Mapping::stockDefaults();
    REQUIRE_FALSE(m.translate(juce::MidiMessage::controllerEvent(1, 80, 0)).has_value());
    REQUIRE_FALSE(m.translate(juce::MidiMessage::controllerEvent(1, 80, 63)).has_value());
}

TEST_CASE("FCB1010Mapping: loadFromJson can override pitchSingingToggle CC", "[midi][fcb][pitch_singing]") {
    const auto json = R"({
        "pitchSingingToggleCc": 85
    })";
    auto m = FCB1010Mapping::loadFromJson(json);
    REQUIRE(m.has_value());
    auto cmd = m->translate(juce::MidiMessage::controllerEvent(1, 85, 100));
    REQUIRE(cmd.has_value());
    REQUIRE(cmd->type == SceneCommandType::TogglePitchSinging);
}
```

- [ ] **Step 2: Run tests to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "fcb1010"
```

Expected: the 3 new tests FAIL.

- [ ] **Step 3: Extend `SceneCommand.h`**

Replace `src/midi/SceneCommand.h`:

```cpp
#pragma once

namespace guitar_dsp::midi {

enum class SceneCommandType {
    ActivateScene,        // payload = scene id
    SetWetDry,            // payload = 0..127 (CC value, caller normalizes)
    SetMasterGain,        // payload = 0..127 (CC value, caller normalizes)
    TogglePitchSinging,   // payload unused; emit once per CC press
};

struct SceneCommand {
    SceneCommandType type;
    int              payload;
};

} // namespace guitar_dsp::midi
```

- [ ] **Step 4: Extend `FCB1010Mapping`**

In `src/midi/FCB1010Mapping.h`, add the field at the bottom of the private section:

```cpp
int pitchSingingToggleCc_ = -1;
```

In `src/midi/FCB1010Mapping.cpp`:

In `stockDefaults()`, after `m.masterGainCc_ = 7;`:

```cpp
m.pitchSingingToggleCc_ = 80;
```

In `loadFromJson()`, after the `expressionPedalCcs` block:

```cpp
if (obj->hasProperty("pitchSingingToggleCc"))
    m.pitchSingingToggleCc_ =
        static_cast<int>(obj->getProperty("pitchSingingToggleCc"));
```

In `translate()`, inside the `msg.isController()` branch, before the final `return std::nullopt;`:

```cpp
if (cc == pitchSingingToggleCc_ && pitchSingingToggleCc_ >= 0 && val >= 64)
    return SceneCommand{SceneCommandType::TogglePitchSinging, 0};
```

- [ ] **Step 5: Re-run tests**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "fcb1010"
```

Expected: all FCB1010Mapping tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/midi/SceneCommand.h src/midi/FCB1010Mapping.h src/midi/FCB1010Mapping.cpp \
        tests/unit/midi/test_fcb1010_mapping.cpp
git commit -m "feat(midi): TogglePitchSinging on CC#80 (latch >=64) in FCB1010Mapping"
```

---

## Task 7: `pitchSingingToggleFromMidiBuffer` helper

Mirror `sceneFromMidiBuffer`: scan a `MidiBuffer` for any pitch-singing-toggle command and return a bool. Used by `PluginProcessor::processBlock` (plugin path).

**Files:**
- Modify: `src/midi/HostMidiSceneRouter.h`
- Modify: `src/midi/HostMidiSceneRouter.cpp`
- Modify: `tests/unit/midi/test_host_midi_scene_router.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/midi/test_host_midi_scene_router.cpp`:

```cpp
TEST_CASE("pitchSingingToggleFromMidiBuffer: returns true when CC#80 >=64 present",
          "[midi][host_router][pitch_singing]") {
    using namespace guitar_dsp::midi;
    FCB1010Mapping mapping = FCB1010Mapping::stockDefaults();

    juce::MidiBuffer buf;
    buf.addEvent(juce::MidiMessage::controllerEvent(1, 80, 100), 0);

    REQUIRE(pitchSingingToggleFromMidiBuffer(buf, mapping) == true);
}

TEST_CASE("pitchSingingToggleFromMidiBuffer: returns false on empty / unrelated msgs",
          "[midi][host_router][pitch_singing]") {
    using namespace guitar_dsp::midi;
    FCB1010Mapping mapping = FCB1010Mapping::stockDefaults();

    juce::MidiBuffer empty;
    REQUIRE(pitchSingingToggleFromMidiBuffer(empty, mapping) == false);

    juce::MidiBuffer pcOnly;
    pcOnly.addEvent(juce::MidiMessage::programChange(1, 3), 0);
    REQUIRE(pitchSingingToggleFromMidiBuffer(pcOnly, mapping) == false);
}
```

- [ ] **Step 2: Run test to confirm failure**

Expected: compile error (function does not exist).

- [ ] **Step 3: Declare + implement**

In `src/midi/HostMidiSceneRouter.h`:

```cpp
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace guitar_dsp::midi {

class FCB1010Mapping;

int  sceneFromMidiBuffer(const juce::MidiBuffer& midi, const FCB1010Mapping& mapping);

// Returns true if the buffer contains at least one CC matching the
// FCB1010Mapping's pitchSinging toggle (CC#80 by default, value >= 64).
// Each such message represents one user "press"; the caller flips the
// app's toggle once per call where this returns true (debounce is on the
// FCB side — single press = single CC>=64 message).
bool pitchSingingToggleFromMidiBuffer(const juce::MidiBuffer& midi,
                                      const FCB1010Mapping& mapping);

} // namespace guitar_dsp::midi
```

In `src/midi/HostMidiSceneRouter.cpp`, append:

```cpp
bool pitchSingingToggleFromMidiBuffer(const juce::MidiBuffer& midi,
                                      const FCB1010Mapping& mapping) {
    for (const auto metadata : midi) {
        const auto msg = metadata.getMessage();
        if (auto cmd = mapping.translate(msg)) {
            if (cmd->type == SceneCommandType::TogglePitchSinging)
                return true;
        }
    }
    return false;
}
```

- [ ] **Step 4: Re-run tests**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "host_midi"
```

Expected: all PASS.

- [ ] **Step 5: Commit**

```bash
git add src/midi/HostMidiSceneRouter.h src/midi/HostMidiSceneRouter.cpp \
        tests/unit/midi/test_host_midi_scene_router.cpp
git commit -m "feat(midi): pitchSingingToggleFromMidiBuffer helper"
```

---

## Task 8: `PluginProcessor` wiring — toggle dispatch + pitch accessors

Add the toggle setter/getter (forwards to `AudioGraph`), forward the three pitch atomics, dispatch `TogglePitchSinging` from both the host-MIDI path (plugin) and the `MidiRouter` callback (standalone).

**Files:**
- Modify: `src/app/PluginProcessor.h`
- Modify: `src/app/PluginProcessor.cpp`

- [ ] **Step 1: Add accessors to `PluginProcessor.h`**

In `src/app/PluginProcessor.h`, after the existing `setVocoderClarity` / `vocoderClarity()` block, add:

```cpp
// Pitch-singing toggle (message thread).
void setPitchSinging(bool on) noexcept  { graph_.setPitchSinging(on); }
void togglePitchSinging() noexcept      { graph_.setPitchSinging(!graph_.pitchSinging()); }
bool pitchSinging() const noexcept      { return graph_.pitchSinging(); }

// Live pitch readout published by AudioGraph (audio thread -> UI).
int   detectedNoteMidi() const noexcept { return graph_.detectedNoteMidi(); }
float detectedCents()    const noexcept { return graph_.detectedCents(); }
float detectedHz()       const noexcept { return graph_.detectedHz(); }
```

In the private members section, near `pendingHostScene_`, add:

```cpp
std::atomic<bool> pendingPitchSingingToggle_ {false};
```

- [ ] **Step 2: Dispatch in `MidiRouter` callback (standalone path)**

In `src/app/PluginProcessor.cpp`, inside the `MidiRouter` lambda, after the existing scene-change `if`:

```cpp
if (auto cmd = midiMapping_.translate(msg)) {
    if (cmd->type == midi::SceneCommandType::ActivateScene) {
        sceneEngine_.activateScene(cmd->payload);
    } else if (cmd->type == midi::SceneCommandType::TogglePitchSinging) {
        graph_.setPitchSinging(!graph_.pitchSinging());
    }
    // SetWetDry / SetMasterGain unchanged
}
```

(Replace the existing single-branch `if (cmd->type == ActivateScene)` with the version above.)

- [ ] **Step 3: Dispatch in `processBlock` (plugin host path)**

In `processBlock`, immediately after the existing `pendingHostScene_.store(...)` block:

```cpp
if (wrapperType != wrapperType_Standalone) {
    if (midi::pitchSingingToggleFromMidiBuffer(midiMessages, midiMapping_))
        pendingPitchSingingToggle_.store(true, std::memory_order_release);
}
```

- [ ] **Step 4: Apply pending toggle in `HostMidiPoller`**

Extend `HostMidiPoller::timerCallback()`:

```cpp
void timerCallback() override {
    const int s = p_.pendingHostScene_.exchange(-1, std::memory_order_acquire);
    if (s >= 0) p_.sceneEngine_.activateScene(s);

    if (p_.pendingPitchSingingToggle_.exchange(false, std::memory_order_acquire))
        p_.togglePitchSinging();
}
```

- [ ] **Step 5: Add `#include "midi/HostMidiSceneRouter.h"` if not already present**

Verify the `#include` is at the top of `PluginProcessor.cpp`. (It should already be — search to confirm.)

- [ ] **Step 6: Build + run all tests**

```bash
cmake --build build -j 8
ctest --test-dir build --output-on-failure
```

Expected: full test suite still green (no new tests in this task; later tasks add UI/state tests).

- [ ] **Step 7: Commit**

```bash
git add src/app/PluginProcessor.h src/app/PluginProcessor.cpp
git commit -m "feat(app): wire TogglePitchSinging dispatch + pitch accessors in PluginProcessor"
```

---

## Task 9: Persist `pitchSinging` in `PluginState`

**Files:**
- Modify: `src/app/PluginState.h`
- Modify: `src/app/PluginState.cpp`
- Modify: `src/app/PluginProcessor.cpp` (get/setStateInformation)
- Modify: `tests/unit/app/test_plugin_state.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/app/test_plugin_state.cpp`:

```cpp
TEST_CASE("PluginState: pitchSinging round-trips through JSON", "[app][state][pitch_singing]") {
    guitar_dsp::app::PluginStateData d;
    d.pitchSinging = true;
    const auto json = guitar_dsp::app::PluginState::toJson(d);
    const auto out  = guitar_dsp::app::PluginState::fromJson(json);
    REQUIRE(out.pitchSinging == true);
}

TEST_CASE("PluginState: pitchSinging defaults to false when absent",
          "[app][state][pitch_singing]") {
    const juce::String json = R"({ "sceneId": 0 })";
    const auto out = guitar_dsp::app::PluginState::fromJson(json);
    REQUIRE(out.pitchSinging == false);
}
```

- [ ] **Step 2: Run to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "plugin_state"
```

Expected: compile error (`pitchSinging` not a member of `PluginStateData`).

- [ ] **Step 3: Add the field**

In `src/app/PluginState.h`, in `PluginStateData`, after `gateThresholdDb`:

```cpp
bool pitchSinging = false;
```

In `src/app/PluginState.cpp`:

In `toJson()`, after `o->setProperty("gateThresholdDb", d.gateThresholdDb);`:

```cpp
o->setProperty("pitchSinging", d.pitchSinging);
```

In `fromJson()`, after the `gateThresholdDb` line:

```cpp
if (o->hasProperty("pitchSinging"))
    d.pitchSinging = (bool) o->getProperty("pitchSinging");
```

- [ ] **Step 4: Wire into PluginProcessor save/restore**

In `src/app/PluginProcessor.cpp::getStateInformation`, after `d.gateThresholdDb = graph_.noiseGateThresholdDb();`:

```cpp
d.pitchSinging = graph_.pitchSinging();
```

In `setStateInformation`, after `graph_.setNoiseGateThresholdDb(d.gateThresholdDb);`:

```cpp
graph_.setPitchSinging(d.pitchSinging);
```

- [ ] **Step 5: Re-run tests**

```bash
cmake --build build -j 8
ctest --test-dir build --output-on-failure -R "plugin_state"
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/app/PluginState.h src/app/PluginState.cpp src/app/PluginProcessor.cpp \
        tests/unit/app/test_plugin_state.cpp
git commit -m "feat(app): persist pitchSinging in PluginState"
```

---

## Task 10: 4-pill `DiagToggleBar` with "Pitch Sing" button

**Files:**
- Modify: `src/app/DiagToggleBar.h` (comment header update)
- Modify: `src/app/DiagToggleBar.cpp` (4 pills instead of 3)

- [ ] **Step 1: Update the header comment**

In `src/app/DiagToggleBar.h`, replace the 3-pill comment with:

```cpp
// A thin row of toggle pills used to isolate vocoder behavior and select
// the singing carrier:
//   V — Bypass vocoder (hear the raw TTS modulator)
//   N — Noise carrier  (swap the guitar carrier for white noise)
//   S — Sibilance off  (mute the vocoder's noise/sibilance path)
//   P — Pitch sing     (use a pitch-tracked sawtooth as the carrier floor)
// Click a pill or press V / N / S / P to toggle. Active toggles are highlighted.
```

Update `pillBounds` doc-comment:

```cpp
juce::Rectangle<int> pillBounds(int index) const;  // index 0..3
```

- [ ] **Step 2: Update the implementation**

Replace the `pillBounds` and `paint` and `mouseDown` methods in `src/app/DiagToggleBar.cpp`:

```cpp
juce::Rectangle<int> DiagToggleBar::pillBounds(int index) const {
    auto area = getLocalBounds().reduced(6, 5);
    constexpr int gap = 6;
    const int w = (area.getWidth() - 3 * gap) / 4;
    return juce::Rectangle<int>(area.getX() + index * (w + gap),
                                area.getY(), w, area.getHeight());
}

void DiagToggleBar::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(14, 16, 22));

    struct Pill { const char* label; bool active; juce::Colour on; };
    const Pill pills[4] = {
        { "V  Bypass vocoder", processor_.diagBypassVocoder(), juce::Colour::fromRGB(230, 170,  70) },
        { "N  Noise carrier",  processor_.diagNoiseCarrier(),  juce::Colour::fromRGB( 90, 200, 120) },
        { "S  Sibilance off",  processor_.diagSibilanceOff(),  juce::Colour::fromRGB(110, 170, 230) },
        { "P  Pitch sing",     processor_.pitchSinging(),      juce::Colour::fromRGB(220, 120, 220) },
    };

    for (int i = 0; i < 4; ++i) {
        const auto b = pillBounds(i);
        const bool on = pills[i].active;
        g.setColour(on ? pills[i].on : juce::Colour::fromRGB(34, 38, 46));
        g.fillRoundedRectangle(b.toFloat(), 4.0f);
        g.setColour(on ? juce::Colour::fromRGB(18, 20, 26)
                       : juce::Colour::fromRGB(150, 160, 175));
        g.setFont(juce::Font{juce::FontOptions{}.withHeight(12.0f)
                                 .withStyle(on ? "Bold" : "Regular")});
        g.drawText(pills[i].label, b, juce::Justification::centred);
    }
}

void DiagToggleBar::mouseDown(const juce::MouseEvent& e) {
    for (int i = 0; i < 4; ++i) {
        if (!pillBounds(i).contains(e.getPosition())) continue;
        if      (i == 0) processor_.toggleDiagBypassVocoder();
        else if (i == 1) processor_.toggleDiagNoiseCarrier();
        else if (i == 2) processor_.toggleDiagSibilanceOff();
        else             processor_.togglePitchSinging();
        repaint();
        return;
    }
}
```

- [ ] **Step 3: Build (no test for paint code) and visually smoke-test**

```bash
cmake --build build -j 8
ctest --test-dir build --output-on-failure
```

Expected: all tests still PASS; manual UI check deferred to Task 13's gates.

- [ ] **Step 4: Commit**

```bash
git add src/app/DiagToggleBar.h src/app/DiagToggleBar.cpp
git commit -m "feat(ui): add Pitch Sing pill to DiagToggleBar"
```

---

## Task 11: `NoteReadout` component

A small JUCE component showing the detected note, cents, and Hz.

**Files:**
- Create: `src/app/NoteReadout.h`
- Create: `src/app/NoteReadout.cpp`
- Modify: `src/app/CMakeLists.txt`

- [ ] **Step 1: Look up the existing src/app CMakeLists**

```bash
ls src/app/CMakeLists.txt
```

Read it to find the `juce_add_plugin` SOURCES entry (or equivalent) to extend.

- [ ] **Step 2: Write the header**

`src/app/NoteReadout.h`:

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class PluginProcessor;

// Live readout of the pitch detected by AudioGraph: note name + octave (big),
// cents offset (small), Hz (small). Dims when unvoiced + hold expired. Runs
// at 30 Hz off a juce::Timer; reads three atomics on the processor.
class NoteReadout : public juce::Component, private juce::Timer {
public:
    explicit NoteReadout(PluginProcessor& p);
    ~NoteReadout() override;

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    PluginProcessor& processor_;
    int   midiNote_ = -1;
    float cents_    = 0.0f;
    float hz_       = 0.0f;
};

} // namespace guitar_dsp
```

- [ ] **Step 3: Write the implementation**

`src/app/NoteReadout.cpp`:

```cpp
#include "NoteReadout.h"

#include "PluginProcessor.h"

#include <cmath>

namespace guitar_dsp {

namespace {
juce::String noteName(int midi) {
    static const char* kNames[12] = {
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
    };
    if (midi < 0) return "—";
    const int n = ((midi % 12) + 12) % 12;
    const int oct = (midi / 12) - 1;  // MIDI 0 = C-1; A4 = MIDI 69
    return juce::String(kNames[n]) + juce::String(oct);
}
}

NoteReadout::NoteReadout(PluginProcessor& p) : processor_(p) {
    setOpaque(true);
    startTimerHz(30);
}

NoteReadout::~NoteReadout() { stopTimer(); }

void NoteReadout::timerCallback() {
    const int   m = processor_.detectedNoteMidi();
    const float c = processor_.detectedCents();
    const float h = processor_.detectedHz();
    if (m != midiNote_ || std::abs(c - cents_) > 0.5f
                       || std::abs(h - hz_)    > 0.1f) {
        midiNote_ = m;
        cents_    = c;
        hz_       = h;
        repaint();
    }
}

void NoteReadout::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(16, 18, 24));

    const bool active = midiNote_ >= 0;
    g.setColour(active ? juce::Colour::fromRGB(220, 220, 230)
                       : juce::Colour::fromRGB(70, 75, 85));

    auto area = getLocalBounds().reduced(8, 4);

    // Note name + octave (big)
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(28.0f).withStyle("Bold")});
    g.drawText(noteName(midiNote_),
               area.removeFromTop(area.getHeight() * 2 / 3),
               juce::Justification::centred);

    // Cents and Hz (small, side by side)
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
    if (active) {
        const auto centsStr = (cents_ >= 0.0f ? juce::String("+") : juce::String("-"))
                              + juce::String(std::abs(cents_), 0) + juce::String::charToString(0x00A2);  // ¢
        const auto hzStr    = juce::String(hz_, 1) + " Hz";
        const auto half = area.removeFromTop(area.getHeight());
        const auto left  = half.withWidth(half.getWidth() / 2);
        const auto right = half.withTrimmedLeft(half.getWidth() / 2);
        g.drawText(centsStr, left, juce::Justification::centred);
        g.drawText(hzStr,    right, juce::Justification::centred);
    } else {
        g.drawText("(no pitch)", area, juce::Justification::centred);
    }
}

} // namespace guitar_dsp
```

- [ ] **Step 4: Register in `src/app/CMakeLists.txt`**

Add `NoteReadout.cpp` to whichever source list the existing `VocoderPanel.cpp` / `DiagToggleBar.cpp` are in. (Open the file; the pattern will be obvious — append alphabetically near `NoteReadout`.)

- [ ] **Step 5: Build to confirm it compiles**

```bash
cmake --build build -j 8
```

Expected: clean build (no test yet — this is a UI component).

- [ ] **Step 6: Commit**

```bash
git add src/app/NoteReadout.h src/app/NoteReadout.cpp src/app/CMakeLists.txt
git commit -m "feat(ui): NoteReadout component (detected note + cents + Hz)"
```

---

## Task 12: Host `NoteReadout` in `VocoderPanel` + dynamic carrier-noise label

**Files:**
- Modify: `src/app/VocoderPanel.h`
- Modify: `src/app/VocoderPanel.cpp`

- [ ] **Step 1: Add a NoteReadout child to `VocoderPanel`**

In `src/app/VocoderPanel.h`:

Add `#include "NoteReadout.h"` near the other includes.

In the private members:

```cpp
NoteReadout noteReadout_;
juce::String lastCarrierNoiseLabel_;
```

Update constructor signature already passes `PluginProcessor&` — no change needed there.

- [ ] **Step 2: Initialize it in the constructor**

In `src/app/VocoderPanel.cpp`, change the constructor initializer list:

```cpp
VocoderPanel::VocoderPanel(PluginProcessor& p)
    : processor_(p), noteReadout_(p) {
    setOpaque(true);
    addAndMakeVisible(noteReadout_);
    // ... existing body unchanged ...
}
```

- [ ] **Step 3: Lay it out in `resized()`**

Reserve a slim strip at the bottom of the panel for the readout:

```cpp
void VocoderPanel::resized() {
    auto area = getLocalBounds().reduced(6, 4);
    area.removeFromTop(12);  // header band

    // Bottom strip for the note readout (~36 px tall).
    constexpr int readoutH = 36;
    noteReadout_.setBounds(area.removeFromBottom(readoutH));

    const int rowH = area.getHeight() / 5;
    auto row = [&](juce::Slider& s, juce::Label& l, int labelW) {
        auto r = area.removeFromTop(rowH);
        l.setBounds(r.removeFromLeft(labelW));
        s.setBounds(r);
    };
    row(makeup_,         makeupLabel_,         86);
    row(carrierNoise_,   carrierNoiseLabel_,   86);
    row(sibilance_,      sibilanceLabel_,      86);
    row(clarity_,        clarityLabel_,       140);
    row(gateThreshold_,  gateThresholdLabel_,  86);
}
```

- [ ] **Step 4: Make the carrier-noise label track the pitch-singing toggle**

In `VocoderPanel::timerCallback()`, after the existing clarity block, add:

```cpp
const juce::String desired = processor_.pitchSinging()
    ? juce::String("Pitched floor")
    : juce::String("Noise floor");
if (desired != lastCarrierNoiseLabel_) {
    lastCarrierNoiseLabel_ = desired;
    carrierNoiseLabel_.setText(desired, juce::dontSendNotification);
}
```

(The slider's initial label "Carrier noise" will be overwritten within ~250 ms of opening the UI — the timer runs at 4 Hz.)

- [ ] **Step 5: Build + run all tests**

```bash
cmake --build build -j 8
ctest --test-dir build --output-on-failure
```

Expected: all PASS.

- [ ] **Step 6: Commit**

```bash
git add src/app/VocoderPanel.h src/app/VocoderPanel.cpp
git commit -m "feat(ui): VocoderPanel hosts NoteReadout + carrier-noise label tracks toggle"
```

---

## Task 13: README + final gates (build, auval, pluginval, full test suite)

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Document the CC#80 default**

Open `README.md`. Find the section that mentions FCB1010 setup (search for `FCB1010` or `Program Change`). Append a subsection:

```markdown
### Pitch-tracked singing carrier (toggle)

The vocoder can switch its carrier-floor source from broadband noise to a
pitched sawtooth that tracks the guitar's note ("singing" voice). The toggle
is bound to **MIDI CC#80**, value ≥ 64. The FCB1010 doesn't send CC#80 in
its stock programming — reprogram one switch (any free pedal) to send
`Controller 80, value 127` on press. The toggle persists in the plugin state.

You can also click the **P  Pitch sing** pill in the diagnostic toggle bar.
The pitch readout below the vocoder sliders shows the detected note + cents
+ Hz whether the toggle is on or off, so you can see what the algorithm has
locked to.

To pick a different CC, set `pitchSingingToggleCc` in your FCB mapping JSON.
```

- [ ] **Step 2: Clean build**

```bash
rm -rf build
cmake -S . -B build -G Ninja
cmake --build build -j 8
```

Expected: clean compile; no warnings beyond pre-existing baseline.

- [ ] **Step 3: Full test suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests PASS (count should be ≥ pre-change count + roughly 15 from the new modules).

- [ ] **Step 4: AU validation**

```bash
killall -9 AudioComponentRegistrar 2>/dev/null
auval -v aumf GtSp TdBF
```

Expected: `AU VALIDATION SUCCEEDED`. (If FAIL, see [au_plugin_status memory](../../../../../../.claude/projects/-Users-user-GIT-guitar-dsp/memory/au_plugin_status.md) note about registration caching.)

- [ ] **Step 5: pluginval (strictness 10)**

```bash
# Adjust path if pluginval is installed elsewhere.
pluginval --strictness-level 10 --validate-in-process \
    "$HOME/Library/Audio/Plug-Ins/Components/Guitar Speak.component"
```

Expected: in-process tests all PASS. (Embedded `auval` sub-test may time out on Apple-TTS slowness — known issue, ignore per the AU plan.)

- [ ] **Step 6: Manual smoke (Logic Pro)**

Open Logic, instantiate Guitar Speak on a track with the guitar input. Trigger any speech scene. Verify:

1. Toggle OFF: speech rides on the existing noise-floor carrier (current behavior).
2. Click the **P** pill in the diag bar (or send CC#80 = 127): label lights, "Carrier noise" slider's label changes to "Pitched floor", `NoteReadout` shows the detected note.
3. Play a note. Speech now sings that note. Release while TTS still talking: speech continues singing the held note for ~1 s, then fades.
4. Save the Logic project, close, reopen: pitchSinging state restored.

- [ ] **Step 7: Commit + push**

```bash
git add README.md
git commit -m "docs(readme): pitch-singing toggle + CC#80 mapping note"
git push origin main
```

---

## Spec → Plan coverage map

| Spec section | Covered by |
|---|---|
| §3 Architecture — PitchTrackedCarrier | Tasks 1-4 |
| §3 Architecture — AudioGraph atomics + wiring | Task 5 |
| §3 Architecture — SceneCommand + FCB1010Mapping | Task 6 |
| §3 Architecture — PluginProcessor pending toggle + dispatch | Tasks 7, 8 |
| §3 Architecture — PluginState | Task 9 |
| §3 Architecture — DiagToggleBar | Task 10 |
| §3 Architecture — NoteReadout | Task 11 |
| §3 Architecture — VocoderPanel hosts readout + label swap | Task 12 |
| §4.1 YIN parameters + State struct | Task 1 (header), Task 2 (algo) |
| §4.2 Mixing model (guitar + cn × saw, vocoder.setCarrierNoise(0)) | Task 5 |
| §4.3 Toggle plumbing pattern | Task 5 (audio), Tasks 7-8 (MIDI/UI) |
| §4.4 FCB CC#80 latch + JSON override | Task 6 |
| §4.5 DiagToggleBar 4-pill + NoteReadout + label swap | Tasks 10, 11, 12 |
| §4.6 PluginState save/restore | Task 9 |
| §5.1 Unit tests (sine sweep, low E, silence, re-lock, hold, decay, PolyBLEP spectral) | Tasks 2, 3, 4 |
| §5.1 AudioGraph regression + spectral tracking | Task 5 |
| §5.2 auval + pluginval + manual Logic | Task 13 |
| §6 Performance budget | implicit — RealtimeSentinel suite passes in Task 13 |
| §7 Failure modes (clamp, hold, toggle mid-block, state restore) | Task 4 (hold/clamp), Task 9 (restore), Task 13 (manual) |
| §8 Out of scope | not built — by definition |
