# Pitch-Singing Polish + Sing Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Tame the scratchy pitched-sawtooth carrier and add a Sing-mode toggle (vibrato + chromatic pitch quantize) so the speech feels noticeably sung rather than spoken-at-a-pitch.

**Architecture:** Three small tuning changes to existing modules (saw LPF in `PitchTrackedCarrier`, hold-time default, envelope time constant in `ChannelVocoder`) plus a new `singing_` atomic bool on `PitchTrackedCarrier` that gates per-sample vibrato and per-frame pitch-quantize. Wired through `AudioGraph` → `PluginProcessor` → `PluginState` and surfaced via a 5th `DiagToggleBar` pill labeled "M Sing."

**Tech Stack:** C++20, JUCE, Catch2.

**Spec:** [docs/superpowers/specs/2026-06-13-singing-polish-design.md](../specs/2026-06-13-singing-polish-design.md)

---

## File-touch summary

**Edited:**
- `src/audio/PitchTrackedCarrier.{h,cpp}` — saw LPF state + setters, `holdMs_` default, `singing_`/vibrato/quantize state + setters, modified `process()` and `nextSawSample()` glue
- `src/audio/ChannelVocoder.cpp` — envelope time constant 0.015f → 0.025f
- `src/audio/AudioGraph.{h,cpp}` — forward `singing` setter/getter
- `src/app/PluginProcessor.{h,cpp}` — forward `singing` + toggle + persistence
- `src/app/PluginState.{h,cpp}` — `bool singing` field + JSON
- `src/app/DiagToggleBar.{h,cpp}` — 5-pill layout, new "M Sing" pill
- `tests/unit/audio/test_pitch_tracked_carrier.cpp` — saw LPF, vibrato, quantize tests
- `tests/unit/audio/test_channel_vocoder.cpp` — envelope time-constant test (extend or create)
- `tests/unit/app/test_plugin_state.cpp` — singing round-trip

**New:** none.

---

## Task 1: Saw lowpass filter

Roll off the PolyBLEP saw above ~2 kHz with a 1-pole IIR LPF so the carrier doesn't pass buzz straight into the vocoder's high bands.

**Files:**
- Modify: `src/audio/PitchTrackedCarrier.h`
- Modify: `src/audio/PitchTrackedCarrier.cpp`
- Modify: `tests/unit/audio/test_pitch_tracked_carrier.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/audio/test_pitch_tracked_carrier.cpp`:

```cpp
TEST_CASE("PitchTrackedCarrier saw: LPF attenuates >4 kHz energy at 1 kHz fundamental",
          "[audio][pitch_tracked_carrier][saw][lpf]") {
    PitchTrackedCarrier c;
    c.prepare(48000.0, 512);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000);
    gen.sine(1000.0f, 0.4f, in.data(), in.size());  // F0=1 kHz, well-detected

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
    // The LPF cutoff is 2 kHz; energy at 5 kHz should be at least 12 dB below
    // energy at the 1 kHz fundamental.
    const float lowE  = goertzel(1000.0f);
    const float highE = goertzel(5000.0f);
    REQUIRE(highE * 16.0f < lowE);  // 12 dB = 4x amplitude = 16x power
}
```

- [ ] **Step 2: Run the test to confirm it fails**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "PitchTrackedCarrier saw: LPF"
```

Expected: FAIL (no LPF yet — naked sawtooth has flat harmonic content out to Nyquist).

- [ ] **Step 3: Add LPF state to the header**

In `src/audio/PitchTrackedCarrier.h`, add to the private section near the saw state:

```cpp
// Saw post-filter: 1-pole IIR LPF on the sawtooth output. Tames the
// bright top end so vocoder bands don't pass buzz through.
float  sawLpfState_   = 0.0f;
float  sawLpfHz_      = 2000.0f;
float  sawLpfAlpha_   = 0.0f;  // recomputed in prepare() from sawLpfHz_
```

Add a public setter near the other tunables (`setHoldMs`, `setDecayMs`):

```cpp
// Saw lowpass cutoff in Hz. Default 2000. Recompute happens in prepare();
// call this BEFORE prepare(), or update sampleRate-dependent alpha
// manually if you change it later.
void setSawLowpassHz(float hz) noexcept;
```

- [ ] **Step 4: Wire up the LPF in the implementation**

In `src/audio/PitchTrackedCarrier.cpp`:

After the existing setter definitions for hold/decay/range, add:

```cpp
void PitchTrackedCarrier::setSawLowpassHz(float hz) noexcept {
    sawLpfHz_ = hz;
    sawLpfAlpha_ = 1.0f - std::exp(
        -2.0f * 3.14159265358979323846f * sawLpfHz_ / static_cast<float>(sampleRate_));
}
```

In `prepare()`, after `sampleRate_ = sampleRate;` and before `ring_.assign(...)`, recompute alpha so it tracks the current sample rate:

```cpp
sawLpfAlpha_ = 1.0f - std::exp(
    -2.0f * 3.14159265358979323846f * sawLpfHz_ / static_cast<float>(sampleRate_));
```

In `reset()`, after the other state clears, add:

```cpp
sawLpfState_ = 0.0f;
```

In `nextSawSample()`, BEFORE the final `return v;`, apply the LPF:

```cpp
sawLpfState_ += sawLpfAlpha_ * (v - sawLpfState_);
return sawLpfState_;
```

(Remove the old `return v;` — the new return is the filtered value.)

- [ ] **Step 5: Re-run tests**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "PitchTrackedCarrier"
```

Expected: 13/13 pass (12 prior + the new LPF test).

- [ ] **Step 6: Commit**

```bash
git add src/audio/PitchTrackedCarrier.h src/audio/PitchTrackedCarrier.cpp \
        tests/unit/audio/test_pitch_tracked_carrier.cpp
git commit -m "feat(audio): 1-pole LPF on PitchTrackedCarrier saw to tame buzz"
```

---

## Task 2: Drop default hold time 1000 ms → 250 ms

The 1-second hold causes the saw to drone past word boundaries. 250 ms preserves "AI keeps singing the chord" without the drone.

**Files:**
- Modify: `src/audio/PitchTrackedCarrier.h`

- [ ] **Step 1: Edit the default**

In `src/audio/PitchTrackedCarrier.h`, find the private member:

```cpp
float holdMs_       = 1000.0f;
```

Replace with:

```cpp
float holdMs_       = 250.0f;
```

- [ ] **Step 2: Verify all existing tests still pass**

The existing hold/decay tests use `setHoldMs(N)` explicitly with specific values, so they're insulated from the default change.

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "PitchTrackedCarrier"
```

Expected: 13/13 pass (same as Task 1's end state).

- [ ] **Step 3: Commit**

```bash
git add src/audio/PitchTrackedCarrier.h
git commit -m "fix(audio): drop PitchTrackedCarrier default hold 1000 -> 250 ms"
```

---

## Task 3: Slow down ChannelVocoder envelope follower 15 → 25 ms

Softens consonant edges + sustains vowel energy slightly — sounds less robotic with the pitched carrier.

**Files:**
- Modify: `src/audio/ChannelVocoder.cpp`
- Modify: `tests/unit/audio/test_channel_vocoder.cpp`

- [ ] **Step 1: Add a step-response test (pinning the new time constant)**

Append to `tests/unit/audio/test_channel_vocoder.cpp`:

```cpp
TEST_CASE("ChannelVocoder: envelope follower decays with ~25 ms time constant",
          "[audio][channel_vocoder][envelope]") {
    using namespace guitar_dsp::audio;
    ChannelVocoder voc;
    voc.prepare(48000.0, 1024);
    voc.setWetLevel(1.0f);
    voc.setSibilance(0.0f);
    voc.setOutputGain(1.0f);
    voc.setCarrierNoise(0.0f);  // no extra excitation

    // Modulator: 100 ms of 1 kHz tone followed by 100 ms of silence.
    std::vector<float> mod(48000 / 5);  // 0.2 s
    for (std::size_t i = 0; i < mod.size() / 2; ++i)
        mod[i] = 0.6f * std::sin(2.0 * 3.14159265 * 1000.0 * i / 48000.0);
    // tail (second half) already zero from default

    // Constant carrier so output amplitude tracks the modulator envelope.
    std::vector<float> car(mod.size(), 0.3f);
    std::vector<float> out(mod.size());

    voc.process(car.data(), mod.data(), out.data(), mod.size());

    // Sample peak just after modulator stops (sample 4800 = 100 ms boundary)
    // and ~25 ms later (sample 4800 + 1200 = 6000).
    auto peakOver = [&](std::size_t from, std::size_t len) {
        float p = 0.0f;
        for (std::size_t i = from; i < from + len && i < out.size(); ++i)
            p = std::max(p, std::abs(out[i]));
        return p;
    };
    const float atStop  = peakOver(4700, 100);    // peak right at the stop
    const float at25ms  = peakOver(6000, 100);    // 25 ms later
    // After one time constant (~25 ms), envelope should be ~1/e (≈37%) of its
    // pre-stop value. Allow generous bounds (20%..55%) to be robust to phase
    // and to the fact that the test reads sample peaks not the envelope state
    // directly.
    REQUIRE(at25ms < atStop * 0.55f);
    REQUIRE(at25ms > atStop * 0.20f);
}
```

- [ ] **Step 2: Run the test (it FAILS with the current 15 ms constant)**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "envelope follower decays"
```

Expected: FAIL (current 15 ms time constant decays too fast — `at25ms` is well below the 20% lower bound).

- [ ] **Step 3: Change the time constant**

In `src/audio/ChannelVocoder.cpp::recomputeCoefficients()`, find:

```cpp
constexpr float envT = 0.015f;
```

Replace with:

```cpp
constexpr float envT = 0.025f;  // softer release, sounds less robotic
```

- [ ] **Step 4: Re-run tests**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure
```

Expected: the new test passes; ALL OTHER vocoder tests should pass too (the change is small, no algorithmic break). Total: 294/297 (the same 3 pre-existing AI HTTP-mock failures unchanged).

If a vocoder regression test fails because it pinned the 15 ms decay shape, mark `DONE_WITH_CONCERNS` and flag.

- [ ] **Step 5: Commit**

```bash
git add src/audio/ChannelVocoder.cpp tests/unit/audio/test_channel_vocoder.cpp
git commit -m "feat(audio): slower vocoder envelope follower 15 -> 25 ms"
```

---

## Task 4: Sing-mode toggle infrastructure

Add the `singing_` atomic bool, member storage for vibrato + quantize, and the setters/getters — no behavior change yet. Tasks 5 and 6 fill in the behavior.

**Files:**
- Modify: `src/audio/PitchTrackedCarrier.h`
- Modify: `src/audio/PitchTrackedCarrier.cpp`

- [ ] **Step 1: Add fields to the header**

In `src/audio/PitchTrackedCarrier.h`:

Add `<atomic>` to the includes at the top.

In the public section, after `setSawLowpassHz`:

```cpp
// Sing mode: when ON, the saw output is modulated by vibrato (a sine
// LFO on the F0) and the F0 is quantized to the nearest chromatic
// semitone. Independent of the pitchSinging toggle in AudioGraph.
void setSinging(bool on) noexcept;
bool singing() const noexcept;

void setVibratoHz(float hz)        noexcept;  // default 5.0
void setVibratoCents(float depth)  noexcept;  // default 20.0
void setPitchQuantize(bool on)     noexcept;  // default true
```

In the private section, near the other state:

```cpp
std::atomic<bool>  singing_         {false};
std::atomic<bool>  pitchQuantize_   {true};
std::atomic<float> vibratoHz_       {5.0f};
std::atomic<float> vibratoCents_    {20.0f};
double             vibratoPhase_    = 0.0;  // [0, 2π)
```

- [ ] **Step 2: Implement the setters/getters**

In `src/audio/PitchTrackedCarrier.cpp`, after the existing setters (saw LPF / hold / decay / range), add:

```cpp
void PitchTrackedCarrier::setSinging(bool on) noexcept {
    singing_.store(on, std::memory_order_relaxed);
}
bool PitchTrackedCarrier::singing() const noexcept {
    return singing_.load(std::memory_order_relaxed);
}
void PitchTrackedCarrier::setVibratoHz(float hz) noexcept {
    vibratoHz_.store(hz, std::memory_order_relaxed);
}
void PitchTrackedCarrier::setVibratoCents(float depth) noexcept {
    vibratoCents_.store(depth, std::memory_order_relaxed);
}
void PitchTrackedCarrier::setPitchQuantize(bool on) noexcept {
    pitchQuantize_.store(on, std::memory_order_relaxed);
}
```

In `reset()`, after the other state clears, add:

```cpp
vibratoPhase_ = 0.0;
```

- [ ] **Step 3: Build to confirm compiles**

```bash
cmake --build build --target guitar_dsp_tests -j 8
```

Expected: clean build (no behavior change, no new tests yet).

- [ ] **Step 4: Commit**

```bash
git add src/audio/PitchTrackedCarrier.h src/audio/PitchTrackedCarrier.cpp
git commit -m "feat(audio): Sing-mode toggle + vibrato/quantize state in PitchTrackedCarrier"
```

---

## Task 5: Vibrato implementation

Add the per-sample vibrato modulation: a sine LFO that modulates the saw's effective F0 by ±vibratoCents around the current pitch when Sing mode is on.

**Files:**
- Modify: `src/audio/PitchTrackedCarrier.cpp`
- Modify: `tests/unit/audio/test_pitch_tracked_carrier.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/audio/test_pitch_tracked_carrier.cpp`:

```cpp
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
```

- [ ] **Step 2: Run to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "PitchTrackedCarrier vibrato"
```

Expected: FAIL (no vibrato; instantaneous frequency stays at ~220 Hz; `centsFromF0 ≈ 0`).

- [ ] **Step 3: Implement vibrato in process()**

In `src/audio/PitchTrackedCarrier.cpp::process()`, find the line that emits the saw:

```cpp
out[i] = (amp > 0.0f && freqForSample > 0.0f)
       ? amp * nextSawSample(freqForSample)
       : 0.0f;
```

Replace with:

```cpp
float effectiveFreq = freqForSample;
if (singing_.load(std::memory_order_relaxed) && effectiveFreq > 0.0f) {
    const float vHz    = vibratoHz_.load(std::memory_order_relaxed);
    const float vCents = vibratoCents_.load(std::memory_order_relaxed);
    vibratoPhase_ += 2.0 * 3.14159265358979323846 * vHz / sampleRate_;
    if (vibratoPhase_ >= 2.0 * 3.14159265358979323846) vibratoPhase_ -= 2.0 * 3.14159265358979323846;
    const float vibratoOffsetCents =
        vCents * static_cast<float>(std::sin(vibratoPhase_));
    effectiveFreq *= std::pow(2.0f, vibratoOffsetCents / 1200.0f);
}
out[i] = (amp > 0.0f && effectiveFreq > 0.0f)
       ? amp * nextSawSample(effectiveFreq)
       : 0.0f;
```

- [ ] **Step 4: Re-run tests**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "PitchTrackedCarrier"
```

Expected: 14/14 pass (Task 1's 13 + new vibrato test). Singing-off tests must still pass (regression).

- [ ] **Step 5: Commit**

```bash
git add src/audio/PitchTrackedCarrier.cpp tests/unit/audio/test_pitch_tracked_carrier.cpp
git commit -m "feat(audio): vibrato LFO on PitchTrackedCarrier saw in Sing mode"
```

---

## Task 6: Pitch quantize to nearest chromatic semitone

When Sing mode is on AND `pitchQuantize_` is true, snap the detected F0 to the nearest semitone before driving the saw.

**Files:**
- Modify: `src/audio/PitchTrackedCarrier.cpp`
- Modify: `tests/unit/audio/test_pitch_tracked_carrier.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/audio/test_pitch_tracked_carrier.cpp`:

```cpp
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
```

- [ ] **Step 2: Run to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "PitchTrackedCarrier quantize"
```

Expected: FAIL (no quantize — the saw tracks the raw 226 Hz; energy at 220 Hz is comparable to or lower than at 226 Hz).

- [ ] **Step 3: Implement quantize in process()**

In `src/audio/PitchTrackedCarrier.cpp::process()`, find the hop-boundary block that handles a voiced detection:

```cpp
if (nowVoiced) {
    if (!currentlyVoiced_) {
        ...
    }
    currentFreqHz_     = f0;
    lastVoicedFreqHz_  = f0;
    const float midi   = 69.0f + 12.0f * std::log2(f0 / 440.0f);
    currentMidiNote_   = static_cast<int>(std::lround(midi));
    currentCents_      = 100.0f * (midi - currentMidiNote_);
}
```

Replace with the version that applies quantize WHEN Sing mode + quantize are both on:

```cpp
if (nowVoiced) {
    if (!currentlyVoiced_) {
        holdSamplesRemaining_  = 0;
        decaySamplesRemaining_ = 0;
        decayGain_ = 1.0f;
    }
    float effectiveF0 = f0;
    if (singing_.load(std::memory_order_relaxed)
            && pitchQuantize_.load(std::memory_order_relaxed)) {
        const float midiFloat   = 69.0f + 12.0f * std::log2(f0 / 440.0f);
        const float midiRounded = std::round(midiFloat);
        effectiveF0 = 440.0f * std::pow(2.0f, (midiRounded - 69.0f) / 12.0f);
    }
    currentFreqHz_     = effectiveF0;
    lastVoicedFreqHz_  = effectiveF0;
    const float midi   = 69.0f + 12.0f * std::log2(effectiveF0 / 440.0f);
    currentMidiNote_   = static_cast<int>(std::lround(midi));
    currentCents_      = 100.0f * (midi - currentMidiNote_);
}
```

The published `midiNote` / `cents` now reflect the quantized pitch — which is what the listener hears and what the UI readout should show.

- [ ] **Step 4: Re-run tests**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "PitchTrackedCarrier"
```

Expected: 15/15 pass. The existing "midiNote + cents fields agree with freqHz" YIN test still passes because it uses 440 Hz (already exactly A4) with default Sing=off.

- [ ] **Step 5: Commit**

```bash
git add src/audio/PitchTrackedCarrier.cpp tests/unit/audio/test_pitch_tracked_carrier.cpp
git commit -m "feat(audio): chromatic semitone pitch quantize in Sing mode"
```

---

## Task 7: Wire Sing toggle through AudioGraph + PluginProcessor

**Files:**
- Modify: `src/audio/AudioGraph.h`
- Modify: `src/app/PluginProcessor.h`

- [ ] **Step 1: Add forwarders to `AudioGraph.h`**

In `src/audio/AudioGraph.h`, in the public section near the other `setPitchSinging` / pitch detection methods, add:

```cpp
// Forwarders to PitchTrackedCarrier's Sing-mode toggle.
void setSinging(bool on)  noexcept { pitchCarrier_.setSinging(on); }
bool singing()      const noexcept { return pitchCarrier_.singing(); }
```

- [ ] **Step 2: Add forwarders to `PluginProcessor.h`**

In `src/app/PluginProcessor.h`, after the existing `togglePitchSinging` block, add:

```cpp
// Sing-mode toggle (vibrato + pitch quantize when on).
void setSinging(bool on)    noexcept { graph_.setSinging(on); }
void toggleSinging()        noexcept { graph_.setSinging(!graph_.singing()); }
bool singing()        const noexcept { return graph_.singing(); }
```

- [ ] **Step 3: Build to confirm**

```bash
cmake --build build -j 8
ctest --test-dir build --output-on-failure 2>&1 | tail -5
```

Expected: all tests pass (no new tests here; this is plumbing).

- [ ] **Step 4: Commit**

```bash
git add src/audio/AudioGraph.h src/app/PluginProcessor.h
git commit -m "feat(app): wire Sing-mode toggle through AudioGraph + PluginProcessor"
```

---

## Task 8: Persist `singing` in `PluginState`

**Files:**
- Modify: `src/app/PluginState.h`
- Modify: `src/app/PluginState.cpp`
- Modify: `src/app/PluginProcessor.cpp`
- Modify: `tests/unit/app/test_plugin_state.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/app/test_plugin_state.cpp`:

```cpp
TEST_CASE("PluginState: singing round-trips through JSON",
          "[app][state][singing]") {
    guitar_dsp::app::PluginStateData d;
    d.singing = true;
    const auto json = guitar_dsp::app::PluginState::toJson(d);
    const auto out  = guitar_dsp::app::PluginState::fromJson(json);
    REQUIRE(out.singing == true);
}

TEST_CASE("PluginState: singing defaults to false when absent",
          "[app][state][singing]") {
    const juce::String json = R"({ "sceneId": 0 })";
    const auto out = guitar_dsp::app::PluginState::fromJson(json);
    REQUIRE(out.singing == false);
}
```

- [ ] **Step 2: Run to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests -j 8
```

Expected: compile error — `singing` not a member.

- [ ] **Step 3: Add the field**

In `src/app/PluginState.h`, inside `PluginStateData`, after the `pitchSinging` line, add:

```cpp
bool singing = false;
```

- [ ] **Step 4: Update JSON in PluginState.cpp**

In `src/app/PluginState.cpp::toJson()`, after the existing `pitchSinging` setProperty:

```cpp
o->setProperty("singing", d.singing);
```

In `fromJson()`, after the existing `pitchSinging` if-block:

```cpp
if (o->hasProperty("singing"))
    d.singing = (bool) o->getProperty("singing");
```

- [ ] **Step 5: Wire into PluginProcessor save/restore**

In `src/app/PluginProcessor.cpp::getStateInformation`, after `d.pitchSinging = graph_.pitchSinging();`:

```cpp
d.singing = graph_.singing();
```

In `setStateInformation`, after `graph_.setPitchSinging(d.pitchSinging);`:

```cpp
graph_.setSinging(d.singing);
```

- [ ] **Step 6: Re-run tests**

```bash
cmake --build build -j 8
ctest --test-dir build --output-on-failure -R "plugin_state"
```

Expected: pass.

Full suite:

```bash
ctest --test-dir build 2>&1 | tail -5
```

Expected: previous baseline + 2 new tests passing. Same 3 pre-existing AI failures.

- [ ] **Step 7: Commit**

```bash
git add src/app/PluginState.h src/app/PluginState.cpp src/app/PluginProcessor.cpp \
        tests/unit/app/test_plugin_state.cpp
git commit -m "feat(app): persist singing toggle in PluginState"
```

---

## Task 9: Add 5th "M Sing" pill to DiagToggleBar

**Files:**
- Modify: `src/app/DiagToggleBar.h`
- Modify: `src/app/DiagToggleBar.cpp`

- [ ] **Step 1: Update header comment**

In `src/app/DiagToggleBar.h`, replace the class comment block with:

```cpp
// A thin row of toggle pills used to isolate vocoder behavior and select
// pitch-singing/sing modes:
//   V — Bypass vocoder (hear the raw TTS modulator)
//   N — Noise carrier  (swap the guitar carrier for white noise)
//   S — Sibilance off  (mute the vocoder's noise/sibilance path)
//   P — Pitch sing     (use a pitch-tracked sawtooth as the carrier floor)
//   M — Modulate sing  (vibrato + chromatic semitone quantize on the saw)
// Click a pill or press V / N / S / P / M to toggle. Active toggles are highlighted.
```

Update the `pillBounds` doc-comment:

```cpp
juce::Rectangle<int> pillBounds(int index) const;  // index 0..4
```

- [ ] **Step 2: Update the implementation**

In `src/app/DiagToggleBar.cpp`, replace `pillBounds`, `paint`, and `mouseDown`:

```cpp
juce::Rectangle<int> DiagToggleBar::pillBounds(int index) const {
    auto area = getLocalBounds().reduced(6, 5);
    constexpr int gap = 6;
    const int w = (area.getWidth() - 4 * gap) / 5;
    return juce::Rectangle<int>(area.getX() + index * (w + gap),
                                area.getY(), w, area.getHeight());
}

void DiagToggleBar::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(14, 16, 22));

    struct Pill { const char* label; bool active; juce::Colour on; };
    const Pill pills[5] = {
        { "V  Bypass vocoder", processor_.diagBypassVocoder(), juce::Colour::fromRGB(230, 170,  70) },
        { "N  Noise carrier",  processor_.diagNoiseCarrier(),  juce::Colour::fromRGB( 90, 200, 120) },
        { "S  Sibilance off",  processor_.diagSibilanceOff(),  juce::Colour::fromRGB(110, 170, 230) },
        { "P  Pitch sing",     processor_.pitchSinging(),      juce::Colour::fromRGB(220, 120, 220) },
        { "M  Sing",           processor_.singing(),           juce::Colour::fromRGB(120, 220, 200) },
    };

    for (int i = 0; i < 5; ++i) {
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
    for (int i = 0; i < 5; ++i) {
        if (!pillBounds(i).contains(e.getPosition())) continue;
        if      (i == 0) processor_.toggleDiagBypassVocoder();
        else if (i == 1) processor_.toggleDiagNoiseCarrier();
        else if (i == 2) processor_.toggleDiagSibilanceOff();
        else if (i == 3) processor_.togglePitchSinging();
        else             processor_.toggleSinging();
        repaint();
        return;
    }
}
```

- [ ] **Step 3: Build + test**

```bash
cmake --build build -j 8
ctest --test-dir build --output-on-failure 2>&1 | tail -5
```

Expected: tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/app/DiagToggleBar.h src/app/DiagToggleBar.cpp
git commit -m "feat(ui): add M Sing pill to DiagToggleBar"
```

---

## Task 10: README + final gates

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Document Sing mode in README**

Open `README.md`, find the "Pitch-tracked singing carrier (toggle)" subsection added in the previous feature. Append immediately after it:

```markdown
### Sing mode

A complementary toggle layered on top of pitch-singing that nudges the
output from "spoken-at-a-pitch" toward "sung":

- **Vibrato**: 5 Hz sine LFO, +/- 20 cents — adds expressive wobble.
- **Pitch quantize**: snaps the detected F0 to the nearest chromatic
  semitone, so slightly sharp/flat plucks still sing in tune.

Click the **M  Sing** pill in the diagnostic toggle bar to enable.
Persisted in plugin state. Works whether pitch-singing is on or off
(but only audible while pitch-singing is on, since that's what routes
the pitched saw into the carrier).
```

- [ ] **Step 2: Clean build**

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 8
```

Expected: clean compile.

- [ ] **Step 3: Full test suite**

```bash
ctest --test-dir build --output-on-failure 2>&1 | tail -30
```

Expected: previous-baseline + ~7 new tests pass; 3 pre-existing AI HTTP-mock failures unchanged.

- [ ] **Step 4: AU validation**

```bash
killall -9 AudioComponentRegistrar 2>/dev/null || true
auval -v aumf GtSp TdBF 2>&1 | tail -10
```

Expected: `AU VALIDATION SUCCEEDED`.

- [ ] **Step 5: pluginval (if installed)**

```bash
pluginval --strictness-level 10 --validate-in-process \
    "$HOME/Library/Audio/Plug-Ins/Components/Guitar Speak.component" 2>&1 | tail -20
```

Expected: in-process tests pass.

- [ ] **Step 6: Manual smoke (you)**

Open Logic, instantiate Guitar Speak, scene 8:
1. Toggle P (pitch sing) on; verify the existing carrier feel.
2. Toggle M (sing) on; play a slightly-out-of-tune plucked note — listen for vibrato wobble + the saw snapping to a clean semitone (NoteReadout should show the quantized note).
3. Toggle M off; verify the raw pitch-singing feel comes back.

- [ ] **Step 7: Commit**

```bash
git add README.md
git commit -m "docs(readme): document Sing mode (vibrato + quantize)"
```

---

## Spec → plan coverage map

| Spec section | Covered by |
|---|---|
| §1.1 Saw LPF | Task 1 |
| §1.2 holdMs default 250 | Task 2 |
| §1.3 Vocoder envelope 25 ms | Task 3 |
| §2.1 Sing toggle API | Tasks 4, 7 |
| §2.2 Vibrato | Task 5 |
| §2.3 Pitch quantize | Task 6 |
| §2.4 UI pill ("M Sing") | Task 9 |
| §3 NoteReadout shows quantized note | Task 6 (currentMidiNote_ derived from effectiveF0) |
| §4.1 PitchTrackedCarrier unit tests | Tasks 1, 5, 6 |
| §4.2 PluginState round-trip | Task 8 |
| §4.3 Vocoder envelope test | Task 3 |
| §4.4 Manual ear check | Task 10 |
| §5 File-touch summary | All tasks |
| §6 Out of scope | Not built — by definition |
| §7 Verification | Task 10 |
