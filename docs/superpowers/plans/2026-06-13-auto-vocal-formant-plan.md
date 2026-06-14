# Auto-Vocal Formant (Scene 4) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Spec:** [docs/superpowers/specs/2026-06-13-auto-vocal-formant-design.md](../specs/2026-06-13-auto-vocal-formant-design.md)

**Goal:** Add a Scene 4 carousel patch where an LFO walks the guitar through a vowel sequence (default "weedly weedly" — EE↔EH at 9 Hz), producing a hands-free automatic vocal-character effect with no clips and no mic.

**Architecture:** Extend `Formant` to take a continuous **position** in `[0, 1)` interpolating across 5 anchors (EE/EH/AH/OH/OO). Existing `setVowel(Vowel)` becomes a thin shim mapping to fixed positions, preserving all current tests. Add a new `FormantModulator` class that drives `Formant`'s position from one of three sources (Static / LFO / per-onset Envelope). `CarouselConfig` gains a `FormantMode` + breakpoints/rate/attack fields. `CarouselMod` wires the modulator into the per-block path. Scene 4 JSON replaces the old 8-bit patch with a "weedly" preset.

**Tech Stack:** C++20, JUCE (juce_dsp), Catch2, CMake + Ninja.

**Key constraint:** The existing `[formant]` and `[carousel]` test cases — especially `"Formant: None vowel is bypass"` which asserts `processSample(0.31f) == 0.31f` exactly — MUST continue to pass without test edits. The shim's AH/OH/EE formant frequencies are copied bit-for-bit from the current `Formant.cpp` `peaksFor()` table (AH: 700/1220/2600, OH: 500/1000/2400, EE: 270/2300/3000). EH and OO are new anchors.

**Build/test commands used throughout:**
```bash
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "<tag>" -s
./build/tests/guitar_dsp_tests                 # full suite (cwd = repo root)
```

---

## Task 1: Extend `CarouselConfig` with `FormantMode`, breakpoints, LFO rate, envelope attack

Smallest possible schema delta — new enum + new fields, defaults keep behavior identical to today.

**Files:**
- Modify: `src/scenes/Scene.h`

- [ ] **Step 1: Add the new fields**

In `src/scenes/Scene.h`, inside `struct CarouselConfig` — find the existing `Vowel formantVowel` and `float formantAmount` lines. Immediately after them, add:

```cpp
    // --- Phase C (Auto-Vocal Formant) -----------------------------------
    // Drives Formant's vowel position over time. Defaults to Static + empty
    // breakpoints, which is back-compat with the existing static-vowel API:
    // CarouselMod calls Formant::setVowel(formantVowel) and the position is
    // pinned. Set `formantMode = Lfo` (or Envelope) and populate
    // `formantBreakpoints` to use the new continuous-vowel-space path.
    enum class FormantMode { Static, Lfo, Envelope };
    FormantMode        formantMode = FormantMode::Static;

    // Vowel position breakpoints in [0,1). Anchors: 0.0=EE, 0.25=EH, 0.5=AH,
    // 0.75=OH, ~1.0=OO (wraps back to EE in the circular vowel space).
    // Empty in Static mode; populated in Lfo/Envelope mode.
    std::vector<float> formantBreakpoints;

    float              formantLfoHz       = 0.0f;    // Lfo mode: cycles/s
    float              formantEnvAttackMs = 30.0f;   // Envelope mode: attack ramp
```

Make sure `#include <vector>` is already in the file (it's pulled in by Phase A's `bank` field). It is — no change needed.

- [ ] **Step 2: Build to confirm**

Run: `cmake --build build --target guitar_dsp_tests`
Expected: clean build.

- [ ] **Step 3: Commit**

```bash
git add src/scenes/Scene.h
git commit -m "feat(scenes): CarouselConfig adds FormantMode + breakpoints/rate/attack fields"
```

---

## Task 2: `SceneLibrary` parses the new formant fields (TDD)

**Files:**
- Create: `tests/fixtures/scenes/with_carousel_formant_lfo.json`
- Create: `tests/unit/scenes/test_scene_library_carousel_formant.cpp`
- Modify: `src/scenes/SceneLibrary.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add the fixture**

Create `tests/fixtures/scenes/with_carousel_formant_lfo.json`:

```json
{
  "id": 99,
  "name": "Test — auto-vocal formant LFO",
  "color": "#ff0000",
  "mixer": { "masterGainDb": -5.0, "dryWet": 0.9, "transitionMs": 20 },
  "carousel": {
    "enabled": true,
    "formant": {
      "amount": 0.85,
      "mode": "lfo",
      "breakpoints": [0.0, 0.18],
      "lfoHz": 9.0
    }
  }
}
```

- [ ] **Step 2: Write the failing tests**

Create `tests/unit/scenes/test_scene_library_carousel_formant.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "scenes/SceneLibrary.h"

#include <filesystem>

using guitar_dsp::scenes::CarouselConfig;
using guitar_dsp::scenes::SceneLibrary;

namespace {
std::string fixturePath(const std::string& rel) {
    auto p = std::filesystem::current_path();
    for (int i = 0; i < 6; ++i) {
        auto c = p / "tests" / "fixtures" / rel;
        if (std::filesystem::exists(c.parent_path())) return c.string();
        p = p.parent_path();
    }
    throw std::runtime_error("fixtures not found");
}
}

TEST_CASE("SceneLibrary: parses carousel.formant.mode=lfo + breakpoints + lfoHz",
          "[scenes][library][carousel][formant]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_carousel_formant_lfo.json"));
    REQUIRE(s.has_value());
    const auto& c = s->carousel;
    REQUIRE(c.formantMode == CarouselConfig::FormantMode::Lfo);
    REQUIRE(c.formantBreakpoints.size() == 2);
    REQUIRE(c.formantBreakpoints[0] == 0.0f);
    REQUIRE(c.formantBreakpoints[1] == 0.18f);
    REQUIRE(c.formantLfoHz == 9.0f);
    REQUIRE(c.formantAmount == 0.85f);
}

TEST_CASE("SceneLibrary: formant block without mode defaults to Static",
          "[scenes][library][carousel][formant]") {
    // The existing with_carousel.json fixture has no formant block, so
    // formantMode should still be the default.
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_carousel.json"));
    REQUIRE(s.has_value());
    REQUIRE(s->carousel.formantMode == CarouselConfig::FormantMode::Static);
    REQUIRE(s->carousel.formantBreakpoints.empty());
}
```

- [ ] **Step 3: Register in CMake**

Edit `tests/CMakeLists.txt` and add `unit/scenes/test_scene_library_carousel_formant.cpp` near `unit/scenes/test_scene_library_carousel.cpp`.

- [ ] **Step 4: Build and run — expect FAIL**

```
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "[carousel][formant]" -s
```
Expected: FAIL on `formantMode == Lfo` (parser doesn't read the new fields yet).

- [ ] **Step 5: Extend the parser**

In `src/scenes/SceneLibrary.cpp`, find the existing `if (auto* fo = c->hasProperty("formant") ? c->getProperty("formant").getDynamicObject() : nullptr) { ... }` block (around line 166-173). Inside that block, AFTER the existing `cc.formantAmount = getF(fo, "amount", cc.formantAmount);` line, add:

```cpp
                if (fo->hasProperty("mode")) {
                    const auto m = fo->getProperty("mode").toString();
                    if (m == "lfo")
                        cc.formantMode = CarouselConfig::FormantMode::Lfo;
                    else if (m == "envelope")
                        cc.formantMode = CarouselConfig::FormantMode::Envelope;
                    else
                        cc.formantMode = CarouselConfig::FormantMode::Static;
                }
                if (fo->hasProperty("breakpoints")) {
                    if (auto* arr = fo->getProperty("breakpoints").getArray()) {
                        cc.formantBreakpoints.clear();
                        cc.formantBreakpoints.reserve(static_cast<std::size_t>(arr->size()));
                        for (int i = 0; i < arr->size(); ++i)
                            cc.formantBreakpoints.push_back(
                                static_cast<float>((double)(*arr)[i]));
                    }
                }
                cc.formantLfoHz       = getF(fo, "lfoHz",       cc.formantLfoHz);
                cc.formantEnvAttackMs = getF(fo, "envAttackMs", cc.formantEnvAttackMs);
```

- [ ] **Step 6: Build and run — expect PASS**

```
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "[carousel][formant]" -s
```
Expected: both new tests PASS.

Also run the existing carousel test set:
```
./build/tests/guitar_dsp_tests "[carousel]" -s
```
Expected: all PASS (backward-compatible parse).

- [ ] **Step 7: Commit**

```bash
git add tests/fixtures/scenes/with_carousel_formant_lfo.json \
        tests/unit/scenes/test_scene_library_carousel_formant.cpp \
        tests/CMakeLists.txt \
        src/scenes/SceneLibrary.cpp
git commit -m "feat(scenes): SceneLibrary parses carousel.formant mode/breakpoints/rate/attack"
```

---

## Task 3: Extend `Formant` with anchor table + `setPosition` (preserve existing tests)

The existing `[formant]` tests must continue to pass without modification. The "None is bypass" test (`processSample(0.31f) == 0.31f` exactly) is the strictest constraint.

**Files:**
- Modify: `src/audio/Formant.h`
- Modify: `src/audio/Formant.cpp`
- Modify: `tests/unit/audio/test_formant.cpp` (add new tests; don't modify existing)

- [ ] **Step 1: Extend the header with anchor table + setPosition**

Edit `src/audio/Formant.h`. Replace the contents (keeping `#pragma once` + include + namespace) with:

```cpp
#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>

#include "scenes/Scene.h"

namespace guitar_dsp::audio {

// Vowel-emphasis filter: 3 parallel resonant bandpass peaks at a vowel's
// formant frequencies, summed and blended with the input by `amount`.
//
// Two control surfaces, both supported simultaneously:
//   1. `setVowel(Vowel)` — back-compat for the original static-vowel enum.
//      None bypasses (output == input exactly). Ee/Ah/Oh map to fixed
//      positions in the continuous vowel space (see kAnchors).
//   2. `setPosition(float)` — continuous position in [0, 1) interpolating
//      between 5 anchors (EE/EH/AH/OH/OO). Out-of-range wraps.
//
// Calling `setPosition` clears the static-bypass state set by
// `setVowel(None)`. Calling `setVowel(None)` re-enables bypass.
class Formant {
public:
    void prepare(double sampleRate);
    void reset();

    // Static-vowel API (back-compat). Vowel::None = bypass.
    void setVowel(scenes::CarouselConfig::Vowel v) noexcept;

    // Continuous-position API.
    void setPosition(float p) noexcept;

    void setAmount(float a) noexcept { amount_ = a; }

    float processSample(float x) noexcept;

private:
    struct Anchor { float f1, f2, f3; };
    // EE / EH / AH / OH / OO — formant frequencies in Hz. The EE/AH/OH
    // values match the previous static-vowel table bit-for-bit so existing
    // [formant] and [carousel] tests keep passing. EH and OO are new.
    static constexpr std::array<Anchor, 5> kAnchors = {{
        {  270.0f, 2300.0f, 3000.0f },  // 0.00  EE  (matches old peaksFor(Ee))
        {  530.0f, 1840.0f, 2480.0f },  // 0.25  EH  (new — Peterson-Barney male-avg)
        {  700.0f, 1220.0f, 2600.0f },  // 0.50  AH  (matches old peaksFor(Ah))
        {  500.0f, 1000.0f, 2400.0f },  // 0.75  OH  (matches old peaksFor(Oh))
        {  300.0f,  870.0f, 2240.0f },  // ~1.00 OO  (new — Peterson-Barney male-avg)
    }};

    static constexpr int kPeaks = 3;

    // Anchor positions are uniformly spaced: 0.00, 0.25, 0.50, 0.75, 1.00.
    // We treat the space as CIRCULAR: positions in [0.75, 1.0) interpolate
    // between OH (0.75) and OO (1.0 == 0.0 wrap); positions in [1.0, 1.25)
    // wrap to [0.0, 0.25) → interpolate between OO (treated as 0.0 boundary)
    // and EE.  For Phase C v1, positions in [0, 1) is the canonical range
    // and the wrap behavior at >= 1.0 is handled by std::fmod inside
    // setPosition. The 5 anchors at uniform 0.25 spacing cover the space.

    void recomputeCoefs(float position) noexcept;

    juce::dsp::StateVariableTPTFilter<float> peaks_[kPeaks];
    double sampleRate_      = 48000.0;
    float  position_        = 0.0f;
    float  lastComputedPos_ = -1.0f;   // forces first recompute
    float  amount_          = 0.0f;
    bool   bypass_          = true;    // setVowel(None) sets this; setVowel(other) or setPosition() clears
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 2: Rewrite Formant.cpp with the new behavior**

Replace `src/audio/Formant.cpp` contents with:

```cpp
#include "Formant.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

void Formant::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = 1;
    spec.numChannels = 1;
    for (auto& p : peaks_) {
        p.prepare(spec);
        p.setType(juce::dsp::StateVariableTPTFilterType::bandpass);
        p.setResonance(0.7f);
    }
    reset();
}

void Formant::reset() {
    for (auto& p : peaks_) p.reset();
    lastComputedPos_ = -1.0f;
}

void Formant::setVowel(scenes::CarouselConfig::Vowel v) noexcept {
    using V = scenes::CarouselConfig::Vowel;
    if (v == V::None) {
        bypass_ = true;
        return;
    }
    bypass_ = false;
    float p = 0.0f;
    switch (v) {
        case V::Ee: p = 0.0f;  break;   // anchor 0
        case V::Ah: p = 0.5f;  break;   // anchor 2
        case V::Oh: p = 0.75f; break;   // anchor 3
        default:    p = 0.0f;  break;
    }
    setPosition(p);
}

void Formant::setPosition(float p) noexcept {
    bypass_ = false;
    // Wrap into [0, 1).
    float wrapped = p - std::floor(p);
    position_ = wrapped;
    // Defer the coef recompute until processSample — keeps setPosition
    // cheap when called per-sample from the modulator.
}

void Formant::recomputeCoefs(float position) noexcept {
    // Map position in [0,1) to a [lo, hi] anchor pair and a fractional
    // index. 4 segments between 5 anchors → index = floor(p * 4), frac =
    // (p * 4) - index. Wrap at index == 4 → use anchor 0 as the upper.
    const float scaled = position * 4.0f;
    const int   lo     = static_cast<int>(scaled);
    const int   hi     = (lo + 1) % static_cast<int>(kAnchors.size());
    const float frac   = scaled - static_cast<float>(lo);

    const auto& A = kAnchors[static_cast<std::size_t>(lo)];
    const auto& B = kAnchors[static_cast<std::size_t>(hi)];

    const float f1 = (1.0f - frac) * A.f1 + frac * B.f1;
    const float f2 = (1.0f - frac) * A.f2 + frac * B.f2;
    const float f3 = (1.0f - frac) * A.f3 + frac * B.f3;

    peaks_[0].setCutoffFrequency(f1);
    peaks_[1].setCutoffFrequency(f2);
    peaks_[2].setCutoffFrequency(f3);
    lastComputedPos_ = position;
}

float Formant::processSample(float x) noexcept {
    if (bypass_) return x;
    // Lazy recompute when position drifts more than ~0.005 (1/200 of the
    // space). Bounds CPU during slow LFO sweeps. With per-sample calls and
    // a 9 Hz LFO at 48 kHz that's a recompute roughly every 25 samples.
    if (std::fabs(position_ - lastComputedPos_) > 0.005f) {
        recomputeCoefs(position_);
    }
    float sum = 0.0f;
    for (auto& p : peaks_) sum += p.processSample(0, x);
    return (1.0f - amount_) * x + amount_ * sum;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 3: Run existing `[formant]` tests — they MUST PASS**

```
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "[formant]" -s
```
Expected: all three existing `[formant]` tests PASS:
- `"Formant: 'ah' emphasizes its first formant band vs none"` — passes because setVowel(Ah) → setPosition(0.5) → AH anchor F1=700 Hz.
- `"Formant: None vowel is bypass"` — passes because setVowel(None) sets bypass_, and processSample returns x untouched.
- `"Formant: output finite + bounded"` — passes because the math is well-behaved.

If any existing test fails, STOP. Re-read the assertion and adjust the shim — don't modify the test. The most likely culprits: bypass_ default value (must be true so setVowel(None) bypass behavior persists from prepare/reset until a non-None call), and the AH/OH/EE anchor values (must match the old table exactly).

- [ ] **Step 4: Add new tests for setPosition**

Append to `tests/unit/audio/test_formant.cpp`:

```cpp
TEST_CASE("Formant: setPosition(0.0) matches setVowel(Ee) at first formant",
          "[audio][formant][position]") {
    auto in = noise(48000);

    Formant ee; ee.prepare(48000.0); ee.setVowel(CarouselConfig::Vowel::Ee);
    ee.setAmount(1.0f);
    Formant pos; pos.prepare(48000.0); pos.setPosition(0.0f); pos.setAmount(1.0f);

    float energyEe  = 0.0f, energyPos = 0.0f;
    for (float s : in) {
        const float a = ee.processSample(s);
        const float b = pos.processSample(s);
        energyEe  += a * a;
        energyPos += b * b;
    }
    // The two pipelines should produce nearly identical output — the
    // shim is exactly setPosition(0.0).
    REQUIRE(std::fabs(energyEe - energyPos) / energyEe < 0.01f);
}

TEST_CASE("Formant: setPosition midway between EE and EH interpolates F1",
          "[audio][formant][position]") {
    // Position 0.125 = halfway between EE (F1=270) and EH (F1=530). Expect
    // peak band energy around (270+530)/2 = 400 Hz to be elevated relative
    // to a flat baseline.
    auto in = noise(48000);

    Formant pos; pos.prepare(48000.0); pos.setPosition(0.125f); pos.setAmount(1.0f);
    std::vector<float> outPos(in.size());
    for (size_t i = 0; i < in.size(); ++i) outPos[i] = pos.processSample(in[i]);

    Formant flat; flat.prepare(48000.0); flat.setVowel(CarouselConfig::Vowel::None);
    flat.setAmount(1.0f);
    std::vector<float> outFlat(in.size());
    for (size_t i = 0; i < in.size(); ++i) outFlat[i] = flat.processSample(in[i]);

    const float e400flat = bandEnergy(outFlat, 400.0f, 48000.0);
    const float e400pos  = bandEnergy(outPos,  400.0f, 48000.0);
    REQUIRE(e400pos > e400flat * 1.2f);
}

TEST_CASE("Formant: setPosition then setVowel(None) restores bypass",
          "[audio][formant][position]") {
    Formant f; f.prepare(48000.0);
    f.setPosition(0.5f);
    f.setAmount(1.0f);
    f.setVowel(CarouselConfig::Vowel::None);
    REQUIRE(f.processSample(0.31f) == 0.31f);
}
```

- [ ] **Step 5: Build and run new tests — expect PASS**

```
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "[formant]" -s
```
Expected: all six `[formant]` tests PASS.

- [ ] **Step 6: Run carousel tests for regression**

```
./build/tests/guitar_dsp_tests "[carousel]" -s
```
Expected: all PASS (Carousel still calls setVowel via the old API).

- [ ] **Step 7: Commit**

```bash
git add src/audio/Formant.h src/audio/Formant.cpp \
        tests/unit/audio/test_formant.cpp
git commit -m "feat(audio): Formant adds continuous vowel-space setPosition; setVowel shim preserved"
```

---

## Task 4: `FormantModulator` skeleton (silent stub)

**Files:**
- Create: `src/audio/FormantModulator.h`
- Create: `src/audio/FormantModulator.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/audio/FormantModulator.h`:

```cpp
#pragma once

#include <cstddef>
#include <vector>

#include "OnsetDetector.h"

namespace guitar_dsp::audio {

// Drives a Formant's vowel position from a sequence of breakpoints + a
// timebase. Three modes:
//   - Static:   position is a single constant (setStaticPosition).
//   - Lfo:      position is a triangle wave through breakpoints at rateHz,
//               wrapping after the last.
//   - Envelope: position advances one breakpoint per detected onset, with
//               a linear attack ramp into the new position.
class FormantModulator {
public:
    enum class Mode { Static, Lfo, Envelope };

    void prepare(double sampleRate);
    void reset();

    void setMode(Mode m) noexcept;
    void setStaticPosition(float p) noexcept;
    void setBreakpoints(std::vector<float> bp) noexcept;
    void setLfoRateHz(float hz) noexcept;
    void setEnvelopeAttackMs(float ms) noexcept;

    // Audio thread.
    //   onsetSrc — required only in Envelope mode (ignored in Static/Lfo).
    //   posOut   — filled with one position value per sample.
    void process(const float* onsetSrc, float* posOut, std::size_t numSamples) noexcept;

private:
    Mode  mode_              = Mode::Static;
    float staticPos_         = 0.0f;
    std::vector<float> breakpoints_;
    double sampleRate_       = 48000.0;
    float  lfoPhase_         = 0.0f;          // [0, 1)
    float  lfoIncrPerSample_ = 0.0f;
    int    envIndex_         = -1;            // -1 = no onset yet
    float  envCurrentPos_    = 0.0f;
    float  envTargetPos_     = 0.0f;
    float  envRampPerSample_ = 0.0f;
    float  envAttackMs_      = 30.0f;
    OnsetDetector onset_;
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 2: Write the stub implementation**

Create `src/audio/FormantModulator.cpp`:

```cpp
#include "FormantModulator.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

void FormantModulator::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    onset_.prepare(sampleRate);
    reset();
}

void FormantModulator::reset() {
    onset_.reset();
    lfoPhase_      = 0.0f;
    envIndex_      = -1;
    envCurrentPos_ = 0.0f;
    envTargetPos_  = 0.0f;
}

void FormantModulator::setMode(Mode m) noexcept { mode_ = m; }
void FormantModulator::setStaticPosition(float p) noexcept { staticPos_ = p; }
void FormantModulator::setBreakpoints(std::vector<float> bp) noexcept {
    breakpoints_ = std::move(bp);
}
void FormantModulator::setLfoRateHz(float hz) noexcept {
    lfoIncrPerSample_ = (sampleRate_ > 0.0) ? static_cast<float>(hz / sampleRate_) : 0.0f;
}
void FormantModulator::setEnvelopeAttackMs(float ms) noexcept {
    envAttackMs_ = ms;
    const float samples = static_cast<float>(sampleRate_ * ms / 1000.0);
    envRampPerSample_ = (samples > 0.0f) ? (1.0f / samples) : 1.0f;
}

void FormantModulator::process(const float* /*onsetSrc*/, float* posOut,
                                std::size_t numSamples) noexcept {
    // Stub: fill with the static position so Static mode "works" already.
    std::fill(posOut, posOut + numSamples, staticPos_);
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 3: Add to CMake**

Edit `src/CMakeLists.txt` and add `audio/FormantModulator.cpp` to `guitar_dsp_audio` (alphabetically near `audio/Formant.cpp`).

- [ ] **Step 4: Build**

Run: `cmake --build build --target guitar_dsp_tests`
Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add src/audio/FormantModulator.h src/audio/FormantModulator.cpp src/CMakeLists.txt
git commit -m "feat(audio): FormantModulator skeleton (static-mode stub)"
```

---

## Task 5: `FormantModulator` Static mode (TDD)

**Files:**
- Create: `tests/unit/audio/test_formant_modulator.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the test**

Create `tests/unit/audio/test_formant_modulator.cpp`:

```cpp
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
```

- [ ] **Step 2: Register in CMake + build + run — expect PASS**

Edit `tests/CMakeLists.txt` and add `unit/audio/test_formant_modulator.cpp` (place near `unit/audio/test_formant.cpp`).

```
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "[formant_modulator]" -s
```
Expected: PASS (the stub already returns staticPos_).

- [ ] **Step 3: Commit**

```bash
git add tests/unit/audio/test_formant_modulator.cpp tests/CMakeLists.txt
git commit -m "test(audio): FormantModulator Static mode constant output"
```

---

## Task 6: `FormantModulator` LFO mode (TDD)

**Files:**
- Modify: `tests/unit/audio/test_formant_modulator.cpp`
- Modify: `src/audio/FormantModulator.cpp`

- [ ] **Step 1: Append the failing test**

Append to `tests/unit/audio/test_formant_modulator.cpp`:

```cpp
TEST_CASE("FormantModulator Lfo: triangle walks between breakpoint extremes",
          "[audio][formant_modulator][lfo]") {
    FormantModulator m;
    m.prepare(48000.0);
    m.setMode(FormantModulator::Mode::Lfo);
    m.setBreakpoints({ 0.0f, 0.2f });   // EE ↔ a bit toward EH
    m.setLfoRateHz(1.0f);                // one full cycle per second

    constexpr std::size_t N = 48000;     // 1 second
    std::vector<float> onset(N, 0.0f), pos(N, 0.0f);
    m.process(onset.data(), pos.data(), N);

    // Over one full cycle, posOut should range across [0.0, 0.2] (or close
    // to it — the triangle starts at the first breakpoint and walks).
    float pmin = 1.0f, pmax = 0.0f;
    for (float p : pos) {
        pmin = std::min(pmin, p);
        pmax = std::max(pmax, p);
    }
    REQUIRE(pmin <= 0.02f);   // started low and visited the bottom
    REQUIRE(pmax >= 0.18f);   // climbed near the top
    REQUIRE(pmax <= 0.21f);   // didn't overshoot
}

TEST_CASE("FormantModulator Lfo: empty breakpoints → constant 0",
          "[audio][formant_modulator][lfo]") {
    FormantModulator m;
    m.prepare(48000.0);
    m.setMode(FormantModulator::Mode::Lfo);
    m.setBreakpoints({});

    std::vector<float> onset(512, 0.0f), pos(512, 1.0f);
    m.process(onset.data(), pos.data(), pos.size());

    for (float p : pos) REQUIRE(p == 0.0f);
}
```

- [ ] **Step 2: Build and run — expect FAIL**

The current stub returns `staticPos_` in all modes. The Lfo tests must fail.

```
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "[formant_modulator][lfo]" -s
```
Expected: FAIL.

- [ ] **Step 3: Implement Lfo mode in process()**

Replace `FormantModulator::process` in `src/audio/FormantModulator.cpp`:

```cpp
void FormantModulator::process(const float* /*onsetSrc*/, float* posOut,
                                std::size_t numSamples) noexcept {
    switch (mode_) {
        case Mode::Static: {
            std::fill(posOut, posOut + numSamples, staticPos_);
            break;
        }
        case Mode::Lfo: {
            if (breakpoints_.empty()) {
                std::fill(posOut, posOut + numSamples, 0.0f);
                break;
            }
            const int n = static_cast<int>(breakpoints_.size());
            for (std::size_t i = 0; i < numSamples; ++i) {
                // Triangle through breakpoints: walk forward through the
                // list and back to form a symmetric cycle. lfoPhase_ in
                // [0, 1) maps to a position on this triangle.
                //   first half  [0.0, 0.5]: linear interp through forward order
                //   second half [0.5, 1.0]: linear interp through reverse order
                float phase = lfoPhase_;
                const float halfFold = (phase < 0.5f) ? (phase * 2.0f)
                                                       : ((1.0f - phase) * 2.0f);
                // halfFold in [0, 1] maps across n breakpoints linearly.
                const float scaled = halfFold * static_cast<float>(n - 1);
                const int lo = std::min(static_cast<int>(scaled), n - 1);
                const int hi = std::min(lo + 1, n - 1);
                const float frac = scaled - static_cast<float>(lo);
                posOut[i] = (1.0f - frac) * breakpoints_[static_cast<std::size_t>(lo)]
                          + frac        * breakpoints_[static_cast<std::size_t>(hi)];

                lfoPhase_ += lfoIncrPerSample_;
                if (lfoPhase_ >= 1.0f) lfoPhase_ -= 1.0f;
            }
            break;
        }
        case Mode::Envelope: {
            // Implemented in Task 7.
            std::fill(posOut, posOut + numSamples, staticPos_);
            break;
        }
    }
}
```

- [ ] **Step 4: Build and run — expect PASS**

```
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "[formant_modulator]" -s
```
Expected: all three `[formant_modulator]` tests PASS.

- [ ] **Step 5: Commit**

```bash
git add tests/unit/audio/test_formant_modulator.cpp \
        src/audio/FormantModulator.cpp
git commit -m "feat(audio): FormantModulator Lfo mode triangle-walks breakpoints"
```

---

## Task 7: `FormantModulator` Envelope mode (TDD)

**Files:**
- Modify: `tests/unit/audio/test_formant_modulator.cpp`
- Modify: `src/audio/FormantModulator.cpp`

- [ ] **Step 1: Append the failing test**

Append to `tests/unit/audio/test_formant_modulator.cpp`:

```cpp
TEST_CASE("FormantModulator Envelope: advances one breakpoint per onset",
          "[audio][formant_modulator][envelope]") {
    FormantModulator m;
    m.prepare(48000.0);
    m.setMode(FormantModulator::Mode::Envelope);
    m.setBreakpoints({ 0.0f, 0.5f, 0.75f });
    m.setEnvelopeAttackMs(5.0f);   // fast attack so the test settles quickly

    // Three onset pulses spaced ~500 ms apart (well past the OnsetDetector's
    // ~80 ms debounce). Position should settle near each breakpoint in turn.
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

    // Sample positions well after each onset settles. After onset 1 (clip 0)
    // position ≈ 0.0; after onset 2 ≈ 0.5; after onset 3 ≈ 0.75.
    REQUIRE(std::fabs(pos[12000]                 - 0.0f)  < 0.05f);  // mid-block-2
    REQUIRE(std::fabs(pos[100 + 24000 + 12000]   - 0.5f)  < 0.05f);  // mid-block-after-2
    REQUIRE(std::fabs(pos[N - 100]               - 0.75f) < 0.05f);
}
```

- [ ] **Step 2: Build and run — expect FAIL**

```
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "[formant_modulator][envelope]" -s
```
Expected: FAIL (current Envelope branch is a stub).

- [ ] **Step 3: Implement Envelope mode**

Replace the `case Mode::Envelope:` block inside `FormantModulator::process` with:

```cpp
        case Mode::Envelope: {
            if (breakpoints_.empty()) {
                std::fill(posOut, posOut + numSamples, 0.0f);
                break;
            }
            for (std::size_t i = 0; i < numSamples; ++i) {
                if (onset_.processSample(onsetSrc[i])) {
                    const int n = static_cast<int>(breakpoints_.size());
                    envIndex_ = (envIndex_ + 1) % n;
                    envTargetPos_ = breakpoints_[static_cast<std::size_t>(envIndex_)];
                }
                // Linear ramp envCurrentPos_ toward envTargetPos_.
                if (envCurrentPos_ < envTargetPos_) {
                    envCurrentPos_ += envRampPerSample_;
                    if (envCurrentPos_ > envTargetPos_) envCurrentPos_ = envTargetPos_;
                } else if (envCurrentPos_ > envTargetPos_) {
                    envCurrentPos_ -= envRampPerSample_;
                    if (envCurrentPos_ < envTargetPos_) envCurrentPos_ = envTargetPos_;
                }
                posOut[i] = envCurrentPos_;
            }
            break;
        }
```

- [ ] **Step 4: Build and run — expect PASS**

```
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "[formant_modulator]" -s
```
Expected: all four `[formant_modulator]` tests PASS.

- [ ] **Step 5: Commit**

```bash
git add tests/unit/audio/test_formant_modulator.cpp \
        src/audio/FormantModulator.cpp
git commit -m "feat(audio): FormantModulator Envelope mode advances per onset"
```

---

## Task 8: `FormantModulator` RT allocation-free

**Files:**
- Modify: `tests/unit/audio/test_formant_modulator.cpp`

- [ ] **Step 1: Append the RT test**

Append to `tests/unit/audio/test_formant_modulator.cpp`:

```cpp
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
    // Warm-up call so any first-call lazy init lands before the sentinel.
    m.process(onset.data(), pos.data(), 512);

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int blk = 0; blk < 50; ++blk)
        m.process(onset.data(), pos.data(), 512);
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
```

- [ ] **Step 2: Build and run — expect PASS**

```
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "[formant_modulator][rt]" -s
```
Expected: PASS (the implementation only does per-sample arithmetic; no allocations).

- [ ] **Step 3: Commit**

```bash
git add tests/unit/audio/test_formant_modulator.cpp
git commit -m "test(audio): FormantModulator process is RT allocation-free"
```

---

## Task 9: `CarouselMod` / `Carousel` wires `FormantModulator` into the chain

The existing `Carousel.cpp` reads `cfg.formantVowel` and calls `formant_.setVowel(cfg.formantVowel)` once on scene activation, then per-sample calls `formant_.processSample(x)` gated on `formantVowel != None`. After Phase C, the chain reads `cfg.formantMode` + breakpoints/rate/attack and drives a per-block `FormantModulator` that updates `formant_.setPosition` per-sample.

**Files:**
- Modify: `src/audio/Carousel.h`
- Modify: `src/audio/Carousel.cpp`
- Modify: `tests/unit/audio/test_carousel.cpp` (add 1 new test; do not edit existing)

- [ ] **Step 1: Add a FormantModulator member + scratch buffer to Carousel.h**

In `src/audio/Carousel.h`, locate the existing `Formant formant_;` member. Above or below it, add:

```cpp
    FormantModulator    formantMod_;
    std::vector<float>  formantPosBuffer_;   // per-sample position output for current block
```

Also add `#include "FormantModulator.h"` near the other audio includes at the top.

- [ ] **Step 2: Prepare/reset `formantMod_`**

In `src/audio/Carousel.cpp::prepare`, after the existing `formant_.prepare(sampleRate);`, add:
```cpp
    formantMod_.prepare(sampleRate);
    formantPosBuffer_.assign(static_cast<std::size_t>(blockSize), 0.0f);
```

In `Carousel::reset()`, after `formant_.reset();`, add:
```cpp
    formantMod_.reset();
    std::fill(formantPosBuffer_.begin(), formantPosBuffer_.end(), 0.0f);
```

- [ ] **Step 3: Update `setConfig` to translate CarouselConfig → FormantModulator**

Find the existing `formant_.setVowel(cfg.formantVowel);` and `formant_.setAmount(juce::jlimit(0.0f, 1.0f, cfg.formantAmount));` lines (around line 87-88). Just AFTER them, add:

```cpp
    using FM = FormantModulator::Mode;
    switch (cfg.formantMode) {
        case scenes::CarouselConfig::FormantMode::Static:
            formantMod_.setMode(FM::Static);
            // setVowel(cfg.formantVowel) already set the static position above
            // via the shim; mirror it on the modulator for consistency.
            formantMod_.setStaticPosition(
                cfg.formantVowel == scenes::CarouselConfig::Vowel::Ah ? 0.5f :
                cfg.formantVowel == scenes::CarouselConfig::Vowel::Oh ? 0.75f :
                /* Ee or None */                                         0.0f);
            break;
        case scenes::CarouselConfig::FormantMode::Lfo:
            formantMod_.setMode(FM::Lfo);
            formantMod_.setBreakpoints(cfg.formantBreakpoints);
            formantMod_.setLfoRateHz(cfg.formantLfoHz);
            break;
        case scenes::CarouselConfig::FormantMode::Envelope:
            formantMod_.setMode(FM::Envelope);
            formantMod_.setBreakpoints(cfg.formantBreakpoints);
            formantMod_.setEnvelopeAttackMs(cfg.formantEnvAttackMs);
            break;
    }
```

- [ ] **Step 4: Update `process()` to drive the formant position per sample**

Find the existing per-sample formant block (around lines 154-155):
```cpp
        if (active_.formantVowel != scenes::CarouselConfig::Vowel::None)
            x = formant_.processSample(x);
```

This gate stays as the bypass (Static + None case). REPLACE it with a slightly-richer routine that runs the modulator per block AND wires per-sample positions only when the modulator mode is non-Static:

Look at the structure of `Carousel::process()` — it iterates a per-sample loop. Identify (a) the buffer holding the per-block carrier samples, (b) the loop index, (c) how `formantPosBuffer_` should be filled BEFORE the per-sample loop. The high-level plan:

- BEFORE the per-sample loop (but inside `process()`):

  ```cpp
      const bool formantActive =
          (active_.formantMode != scenes::CarouselConfig::FormantMode::Static)
          || (active_.formantVowel != scenes::CarouselConfig::Vowel::None);

      if (formantActive
              && active_.formantMode != scenes::CarouselConfig::FormantMode::Static) {
          // Generate per-sample positions for this block. The onset source for
          // Envelope mode is the input (pre-shaper) signal.
          formantMod_.process(input, formantPosBuffer_.data(), numSamples);
      }
  ```

- Inside the per-sample loop, REPLACE the existing two-line formant block with:

  ```cpp
          if (formantActive) {
              if (active_.formantMode != scenes::CarouselConfig::FormantMode::Static)
                  formant_.setPosition(formantPosBuffer_[i]);
              x = formant_.processSample(x);
          }
  ```

If the actual `Carousel::process()` uses different variable names (`input`, `numSamples`, `x`, `i`), substitute the canonical ones in the file. Read the existing function end-to-end before editing.

- [ ] **Step 5: Build to confirm**

Run: `cmake --build build --target guitar_dsp_tests`
Expected: clean build.

- [ ] **Step 6: Run existing `[carousel]` tests for regression**

```
./build/tests/guitar_dsp_tests "[carousel]" -s
```
Expected: all PASS. The existing tests use static-vowel mode only (formantMode defaults to Static); the new code branches keep that path identical.

- [ ] **Step 7: Add a regression test for LFO-mode carousel**

Append to `tests/unit/audio/test_carousel.cpp`:

```cpp
TEST_CASE("Carousel: LFO-mode formant produces time-varying vowel character",
          "[audio][carousel][formant_lfo]") {
    using guitar_dsp::audio::Carousel;
    using guitar_dsp::scenes::CarouselConfig;

    CarouselConfig cfg;
    cfg.enabled        = true;
    cfg.formantAmount  = 1.0f;
    cfg.formantMode    = CarouselConfig::FormantMode::Lfo;
    cfg.formantBreakpoints = { 0.0f, 0.5f };  // EE ↔ AH
    cfg.formantLfoHz   = 2.0f;                 // 2 Hz so a 1 sec capture sees 2 cycles

    Carousel c;
    c.prepare(48000.0, 512);
    c.setConfig(cfg);

    // Sustained pink-noise-ish input.
    constexpr std::size_t N = 48000;
    std::vector<float> in(N), out(N);
    unsigned s = 0xabcd1234u;
    for (std::size_t i = 0; i < N; ++i) {
        s = s * 1664525u + 1013904223u;
        in[i] = (static_cast<float>(s >> 9) / 8388608.0f) - 1.0f;
    }
    for (std::size_t i = 0; i < N; i += 512) {
        const std::size_t n = std::min<std::size_t>(512, N - i);
        c.process(in.data() + i, out.data() + i, n);
    }

    // Energy in the first 250 ms (when LFO is near EE → high F1 energy ≈
    // 270 Hz) vs the next 250 ms (when LFO swings toward AH → high F1
    // energy ≈ 700 Hz) should differ noticeably at one of those bands.
    auto energy = [](const float* p, std::size_t n, double f, double sr) {
        double re = 0.0, im = 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            const double ph = 2.0 * 3.14159265 * f * i / sr;
            re += p[i] * std::cos(ph);
            im += p[i] * std::sin(ph);
        }
        return std::sqrt(re * re + im * im) / static_cast<double>(n);
    };

    const double eEa = energy(out.data() +    0, 12000, 270.0, 48000.0);
    const double eEb = energy(out.data() + 12000, 12000, 270.0, 48000.0);
    const double eAa = energy(out.data() +    0, 12000, 700.0, 48000.0);
    const double eAb = energy(out.data() + 12000, 12000, 700.0, 48000.0);

    // The LFO sweep means at least one band changes meaningfully across
    // the two windows. (Both windows together should not be flat in both
    // bands.)
    const bool changedEE = std::fabs(eEa - eEb) / std::max(eEa, 1e-9) > 0.1;
    const bool changedAH = std::fabs(eAa - eAb) / std::max(eAa, 1e-9) > 0.1;
    REQUIRE((changedEE || changedAH));
}
```

- [ ] **Step 8: Build and run new test — expect PASS**

```
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "[carousel][formant_lfo]" -s
```
Expected: PASS.

- [ ] **Step 9: Commit**

```bash
git add src/audio/Carousel.h src/audio/Carousel.cpp \
        tests/unit/audio/test_carousel.cpp
git commit -m "feat(audio): Carousel drives Formant position via FormantModulator per block"
```

---

## Task 10: Replace Scene 4 JSON (archive old)

**Files:**
- Move: `assets/scenes/04_carousel_8bit.json` → `assets/scenes/archive/04_carousel_8bit.json`
- Create: `assets/scenes/04_auto_vocal.json`

- [ ] **Step 1: Archive the old scene**

```bash
git mv assets/scenes/04_carousel_8bit.json assets/scenes/archive/04_carousel_8bit.json
```

- [ ] **Step 2: Write the new Scene 4**

Create `assets/scenes/04_auto_vocal.json`:

```json
{
  "id": 4,
  "name": "Auto-Vocal — weedly",
  "color": "#ff7f24",
  "mixer": { "masterGainDb": -5.0, "dryWet": 0.95, "transitionMs": 20 },
  "carousel": {
    "enabled": true,
    "drive": 12.0,
    "waveshaper": { "type": "tanh", "amount": 1.5 },
    "filter": { "mode": "lowpass", "cutoffHz": 6000, "resonance": 0.3, "mod": "static" },
    "formant": {
      "amount": 0.85,
      "mode": "lfo",
      "breakpoints": [0.0, 0.18],
      "lfoHz": 9.0
    },
    "reverb": { "roomSize": 0.2, "wet": 0.08 },
    "outputTrimDb": -3.0
  }
}
```

- [ ] **Step 3: Build + full regression**

```
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests
```
Expected: no regressions. Test count rises by the number of Phase C tests added.

- [ ] **Step 4: Commit**

```bash
git add assets/scenes/04_auto_vocal.json assets/scenes/archive/04_carousel_8bit.json
git commit -m "feat(scenes): Scene 4 is Auto-Vocal weedly; archive old 8-bit patch"
```

---

## Task 11: Integration test — Auto-Vocal scene end-to-end

**Files:**
- Create: `tests/integration/test_auto_vocal_formant_scene.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the integration test**

Create `tests/integration/test_auto_vocal_formant_scene.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "audio/Carousel.h"
#include "scenes/Scene.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::Carousel;
using guitar_dsp::scenes::CarouselConfig;

TEST_CASE("Auto-Vocal Scene 4: LFO formant + drive produces audible 'weedly' character",
          "[integration][formant][scene]") {
    // Mirror the JSON for Scene 4 in code.
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.drive   = 12.0f;
    cfg.shaper  = CarouselConfig::Shaper::Tanh;
    cfg.shaperAmount = 1.5f;
    cfg.filterMode      = CarouselConfig::FilterMode::LowPass;
    cfg.filterCutoffHz  = 6000.0f;
    cfg.filterResonance = 0.3f;
    cfg.formantAmount = 0.85f;
    cfg.formantMode   = CarouselConfig::FormantMode::Lfo;
    cfg.formantBreakpoints = { 0.0f, 0.18f };
    cfg.formantLfoHz = 9.0f;
    cfg.reverbRoomSize = 0.2f;
    cfg.reverbWet      = 0.08f;
    cfg.outputTrimDb   = -3.0f;

    Carousel c;
    c.prepare(48000.0, 512);
    c.setConfig(cfg);

    // 1 second of guitar-like input (220 Hz sawtooth surrogate via sin + harmonics).
    constexpr std::size_t N = 48000;
    std::vector<float> in(N), out(N);
    for (std::size_t i = 0; i < N; ++i) {
        const float t = static_cast<float>(i) / 48000.0f;
        in[i] = 0.4f * (std::sin(2.0f * 3.14159265f * 220.0f * t)
                       + 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * t)
                       + 0.25f * std::sin(2.0f * 3.14159265f * 660.0f * t));
    }
    for (std::size_t i = 0; i < N; i += 512) {
        const std::size_t n = std::min<std::size_t>(512, N - i);
        c.process(in.data() + i, out.data() + i, n);
    }

    // A "weedly" preset should produce nontrivial output.
    float peak = 0.0f;
    for (float v : out) peak = std::max(peak, std::fabs(v));
    REQUIRE(peak > 0.01f);
    REQUIRE(peak < 5.0f);   // not blowing up
}
```

- [ ] **Step 2: Register in CMake + build + run**

Edit `tests/CMakeLists.txt` and add `integration/test_auto_vocal_formant_scene.cpp` near the other `integration/` tests.

```
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "[integration][formant]" -s
```
Expected: PASS.

- [ ] **Step 3: Full regression**

```
./build/tests/guitar_dsp_tests
```
Expected: all earlier counts preserved, with the new Phase C tests added on top.

- [ ] **Step 4: Commit**

```bash
git add tests/integration/test_auto_vocal_formant_scene.cpp tests/CMakeLists.txt
git commit -m "test(integration): Auto-Vocal Scene 4 LFO formant + drive end-to-end"
```

---

## Task 12: Manual verification

**Files:** (none — manual)

- [ ] **Step 1: Build the standalone**

Run: `cmake --build build --target guitar_dsp_app_Standalone`

- [ ] **Step 2: Launch + switch to Scene 4**

```
open "build/src/app/guitar_dsp_app_artefacts/Release/Standalone/Guitar Speak.app"
```

In the app, switch to Scene 4 ("Auto-Vocal — weedly").

Expected:
- Picking any note: hear unmistakable "weedly weedly" vowel oscillation overlaid on the guitar tone, even with no rhythm.
- Holding a sustained note: the weedly continues at ~9 Hz (the LFO rate), independent of pick attacks — proving the Lfo mode is running.
- The output level meter shows life; no audio explosion.

- [ ] **Step 3: Scene-switching smoke**

Switch Scene 4 → Scene 0 → Scene 4 → Scene 2 → Scene 4.

Expected:
- No audio glitch on each transition.
- Scene 2 (clipBank) and Scene 4 (formant) are completely independent — no parameter bleed.

- [ ] **Step 4: Older-scene backwards-compat smoke**

If you have any scene JSON that uses the old `formant: { "vowel": "ah", "amount": 0.8 }` style (none in the current main, but you can hand-edit one for the test), confirm it still produces a static vowel (formantMode defaults to Static, the shim runs setVowel via the back-compat path).

- [ ] **Step 5: No commit needed (validation only)**

---

## Self-Review

Spec sections vs. tasks:

- "Approach: rewrite Formant for continuous position" → Task 3 (preserves all existing tests; the spec's word was "rewrite" but in practice it's an extension because `setVowel` becomes a shim).
- "`Formant.{h,cpp}` extend" → Task 3.
- "`FormantModulator.{h,cpp}` (NEW)" → Tasks 4–8.
- "`CarouselMod.{cpp,h}` — wire FormantModulator" → Task 9 (in this codebase the wiring lives in `Carousel.{h,cpp}` directly; `CarouselMod` is an effects-rack class not the integration point).
- "`Scene.h` — CarouselConfig extensions" → Task 1.
- "`SceneLibrary.cpp` formant block extensions" → Task 2.
- "Scene 4 JSON" → Task 10.
- "Testing: `Formant_PositionInterpolatesBetweenAnchors`" → Task 3 (new tests at the bottom of test_formant.cpp).
- "Testing: `FormantModulator_LfoCyclesBreakpoints`" → Task 6.
- "Testing: `FormantModulator_EnvelopeAdvancesOnOnset`" → Task 7.
- "Testing: `SceneLibrary_ParsesFormantMode`" → Task 2.
- "Testing: `SceneLibrary_BackwardCompatVowelStillWorks`" → Task 2 second test case.
- "Testing: manual / demo verification" → Task 12.
- "Risk: formant tables are rough" → Task 3 uses spec's male-average values for EH/OO; AH/OH/EE copied from existing Formant.cpp; future retuning is polish.
- "Risk: filter bandwidth tradeoff" → Task 3 keeps the existing `setResonance(0.7f)` constant; revisit if listening reveals harshness.
- "Risk: per-sample vs per-block position updates" → Task 9 uses per-sample positions; the lazy-recompute threshold in Task 3 bounds CPU.
- "Risk: schema versioning" → Task 1 keeps `formantVowel` enum; Task 3 honors it via shim.
- "Replaces: 04_carousel_8bit.json → archive" → Task 10.
- "Replaces: `Crusher` stays" → No deletion task. Crusher remains in the engine code untouched.

No spec section unaddressed. No "TBD"/"TODO" placeholders. Type names consistent: `FormantMode`, `formantMode`, `formantBreakpoints`, `formantLfoHz`, `formantEnvAttackMs`, `FormantModulator`, `setPosition`.

---

Plan complete and saved to `docs/superpowers/plans/2026-06-13-auto-vocal-formant-plan.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
