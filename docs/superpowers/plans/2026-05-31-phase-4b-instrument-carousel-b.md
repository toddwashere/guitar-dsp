# Instrument Carousel B Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add the two pitch/harmony Carousel patches deferred from Phase 4a — **choir/pad** (scene 1) and **piano-ish** (scene 3) — via bespoke fixed-ratio granular pitch shifting, a harmonizer, a comb filter, and a fixed formant-emphasis filter, all extending the existing `audio::Carousel` chain.

**Architecture:** Four new allocation-free mono DSP stages (`PitchShifter`, `Harmonizer`, `Comb`, `Formant`) slot into the existing fixed-order Carousel chain. `CarouselConfig` gains fixed-size POD sub-blocks (so the audio-thread atomic-swap stays trivially-copyable). The three surviving Phase-4a patches declare none of the new blocks, so their signal path is byte-identical. The existing brick-wall output limiter backstops everything.

**Tech Stack:** C++20, JUCE 8 (`juce::dsp::StateVariableTPTFilter`, `SmoothedValue`, existing `juce_dsp` link), Catch2 v3.

**Reference spec:** [`docs/superpowers/specs/2026-05-31-instrument-carousel-b-design.md`](../specs/2026-05-31-instrument-carousel-b-design.md)

---

## Background for the implementing engineer

Phase 4a built `audio::Carousel` — a fixed-order mono effects chain. Read these first:

- **`src/audio/Carousel.{h,cpp}`** — the chain. `process(const float* in, float* out, size_t n)` runs (per-sample) drive → waveshaper → crusher → resonant filter → (per-block) chorus → reverb → (per-sample) brick-wall limiter. Config arrives via an atomic-bool + pending/active swap (`setConfig` on message thread, `applyConfig` on audio thread at block start). **This is the pattern every new stage plugs into.**
- **`src/audio/Crusher.{h,cpp}`** and **`src/audio/CarouselMod.{h,cpp}`** (EnvelopeFollower, Lfo) — examples of the bespoke, allocation-free, per-sample stage style you will mirror.
- **`src/scenes/Scene.h`** — `CarouselConfig` (the struct you extend). It is trivially-copyable (only bool/int/float/enum + a fixed array would be fine); KEEP IT THAT WAY — no `std::vector`/`std::string` members, because it's copied on the audio thread during the config swap.
- **`src/scenes/SceneLibrary.cpp`** — the defensive JSON parser (`if (obj->hasProperty(...))` + a `getF` lambda). You add `pitch`/`harmonizer`/`comb`/`formant` block parsing in the same style.

**New chain order after this phase** (crusher stays — the 8-bit patch needs it):

```
drive → pitch/harmonizer → waveshaper → crusher → comb → resonant filter
      → formant → chorus → reverb → output trim → brick-wall limiter
```

**Threading / RT rules (non-negotiable, "cannot crash"):**
- `*::process` and `Carousel::process`: no heap allocation, no locks. All buffers sized in `prepare()`.
- Semitone→ratio (`2^(semitones/12)`) and any `pow`/division of config values are computed in `applyConfig` (audio thread, but once per config change), never per sample where avoidable. Per-sample work in the pitch shifter is add/mul/interp only.
- `RealtimeSentinel` tests assert zero allocations.

---

## File structure

```
src/scenes/Scene.h                       (modify, Task 1: pitch/harmonizer/comb/formant fields)
src/scenes/SceneLibrary.cpp              (modify, Task 1: parse the 4 new blocks)
src/audio/PitchShifter.{h,cpp}           (NEW, Task 2)
src/audio/Harmonizer.{h,cpp}             (NEW, Task 3)
src/audio/Comb.{h,cpp}                   (NEW, Task 4)
src/audio/Formant.{h,cpp}                (NEW, Task 5)
src/audio/Carousel.{h,cpp}               (modify, Tasks 6-8: wire stages into the chain)
src/CMakeLists.txt                       (modify, Tasks 2-5: new sources)
assets/scenes/01_carousel_organ.json     (rename→01_carousel_choir.json, Task 10)
assets/scenes/03_carousel_synth.json     (rename→03_carousel_piano.json, Task 10)
tests/CMakeLists.txt                     (modify throughout)
tests/fixtures/scenes/with_carousel_pitch.json (NEW, Task 1)
tests/unit/scenes/test_scene_library_carousel_pitch.cpp (NEW, Task 1)
tests/unit/audio/test_pitch_shifter.cpp  (NEW, Task 2)
tests/unit/audio/test_harmonizer.cpp     (NEW, Task 3)
tests/unit/audio/test_comb.cpp           (NEW, Task 4)
tests/unit/audio/test_formant.cpp        (NEW, Task 5)
tests/unit/audio/test_carousel.cpp       (modify, Tasks 6-9: chain tests)
tests/integration/test_carousel_scene.cpp(modify, Task 11: choir/piano cases)
README.md                                (modify, Task 12)
```

---

## Task 1: CarouselConfig pitch/harmonizer/comb/formant blocks (TDD)

**Files:**
- Modify: `src/scenes/Scene.h`
- Modify: `src/scenes/SceneLibrary.cpp`
- Create: `tests/fixtures/scenes/with_carousel_pitch.json`
- Create: `tests/unit/scenes/test_scene_library_carousel_pitch.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the fixture** `tests/fixtures/scenes/with_carousel_pitch.json`

```json
{
  "id": 32,
  "name": "Carousel pitch fixture",
  "color": "#b48ce3",
  "mixer": { "masterGainDb": -3.0, "dryWet": 1.0, "transitionMs": 40 },
  "carousel": {
    "enabled": true,
    "pitch": { "semitones": 12, "mix": 0.5, "grainMs": 40 },
    "harmonizer": { "intervals": [12, 7, 0], "detuneCents": [0, 0, 6], "mix": 0.7 },
    "comb": { "freqHz": 220, "feedback": 0.6, "mix": 0.5 },
    "formant": { "vowel": "ah", "amount": 0.6 }
  }
}
```

- [ ] **Step 2: Write the failing test** `tests/unit/scenes/test_scene_library_carousel_pitch.cpp`

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "scenes/SceneLibrary.h"

#include <filesystem>

using Catch::Matchers::WithinAbs;
using guitar_dsp::scenes::SceneLibrary;
using guitar_dsp::scenes::CarouselConfig;

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

TEST_CASE("SceneLibrary: parses carousel pitch/harmonizer/comb/formant",
          "[scenes][library][carousel][pitch]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_carousel_pitch.json"));
    REQUIRE(s.has_value());
    const auto& c = s->carousel;

    REQUIRE_THAT(c.pitchSemitones, WithinAbs(12.0f, 1e-4f));
    REQUIRE_THAT(c.pitchMix, WithinAbs(0.5f, 1e-4f));
    REQUIRE_THAT(c.pitchGrainMs, WithinAbs(40.0f, 1e-4f));

    REQUIRE(c.harmVoiceCount == 3);
    REQUIRE(c.harmSemitones[0] == 12);
    REQUIRE(c.harmSemitones[1] == 7);
    REQUIRE(c.harmSemitones[2] == 0);
    REQUIRE(c.harmDetuneCents[2] == 6);
    REQUIRE_THAT(c.harmMix, WithinAbs(0.7f, 1e-4f));

    REQUIRE_THAT(c.combFreqHz, WithinAbs(220.0f, 1e-3f));
    REQUIRE_THAT(c.combFeedback, WithinAbs(0.6f, 1e-4f));
    REQUIRE_THAT(c.combMix, WithinAbs(0.5f, 1e-4f));

    REQUIRE(c.formantVowel == CarouselConfig::Vowel::Ah);
    REQUIRE_THAT(c.formantAmount, WithinAbs(0.6f, 1e-4f));
}

TEST_CASE("SceneLibrary: missing pitch blocks leave bypass defaults",
          "[scenes][library][carousel][pitch]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_carousel.json"));
    REQUIRE(s.has_value());
    REQUIRE(s->carousel.pitchSemitones == 0.0f);
    REQUIRE(s->carousel.harmVoiceCount == 0);
    REQUIRE(s->carousel.combFreqHz == 0.0f);
    REQUIRE(s->carousel.formantVowel == CarouselConfig::Vowel::None);
}
```

(`with_carousel.json` already exists from Phase 4a; it has no pitch blocks, so it verifies the defaults.)

- [ ] **Step 3: Add the test to `tests/CMakeLists.txt`** — append `unit/scenes/test_scene_library_carousel_pitch.cpp`.

- [ ] **Step 4: Run to confirm failure**

Run: `cmake --build build --target guitar_dsp_tests 2>&1 | head -15`
Expected: FAIL — `pitchSemitones` / `harmVoiceCount` / `Vowel` not members of `CarouselConfig`.

- [ ] **Step 5: Add fields to `CarouselConfig` in `src/scenes/Scene.h`** — insert before `float outputTrimDb = 0.0f;`:

```cpp
    // --- Phase 4b: pitch / harmony stages (all bypass at the listed default) ---
    static constexpr int kMaxHarmVoices = 4;

    float pitchSemitones = 0.0f;   // 0 = bypass single-voice pitch shift
    float pitchMix       = 0.0f;   // dry/shifted blend (0=dry .. 1=shifted)
    float pitchGrainMs   = 40.0f;  // granular window size

    int   harmVoiceCount = 0;      // 0 = bypass harmonizer
    int   harmSemitones[kMaxHarmVoices]  = {0, 0, 0, 0};
    int   harmDetuneCents[kMaxHarmVoices] = {0, 0, 0, 0};
    float harmMix        = 0.0f;   // dry/harmonized blend

    float combFreqHz   = 0.0f;     // 0 = bypass comb
    float combFeedback = 0.0f;
    float combMix      = 0.0f;

    enum class Vowel { None, Ah, Oh, Ee };
    Vowel formantVowel  = Vowel::None;  // None = bypass formant
    float formantAmount = 0.0f;
```

These are POD/fixed-array fields — `CarouselConfig` stays trivially-copyable.

- [ ] **Step 6: Add the parser to `src/scenes/SceneLibrary.cpp`** — inside the existing `if (obj->hasProperty("carousel"))` block, after the `reverb` sub-block parse (just before the block's closing braces), add:

```cpp
            if (auto* p = c->hasProperty("pitch")
                            ? c->getProperty("pitch").getDynamicObject() : nullptr) {
                cc.pitchSemitones = getF(p, "semitones", cc.pitchSemitones);
                cc.pitchMix       = getF(p, "mix", cc.pitchMix);
                cc.pitchGrainMs   = getF(p, "grainMs", cc.pitchGrainMs);
            }
            if (auto* h = c->hasProperty("harmonizer")
                            ? c->getProperty("harmonizer").getDynamicObject() : nullptr) {
                cc.harmMix = getF(h, "mix", cc.harmMix);
                if (auto* iv = h->getProperty("intervals").getArray()) {
                    const int n = juce::jmin((int) iv->size(),
                                             CarouselConfig::kMaxHarmVoices);
                    cc.harmVoiceCount = n;
                    for (int i = 0; i < n; ++i)
                        cc.harmSemitones[i] = static_cast<int>((*iv)[i]);
                }
                if (auto* dt = h->getProperty("detuneCents").getArray()) {
                    const int n = juce::jmin((int) dt->size(),
                                             CarouselConfig::kMaxHarmVoices);
                    for (int i = 0; i < n; ++i)
                        cc.harmDetuneCents[i] = static_cast<int>((*dt)[i]);
                }
            }
            if (auto* cb = c->hasProperty("comb")
                             ? c->getProperty("comb").getDynamicObject() : nullptr) {
                cc.combFreqHz   = getF(cb, "freqHz", cc.combFreqHz);
                cc.combFeedback = getF(cb, "feedback", cc.combFeedback);
                cc.combMix      = getF(cb, "mix", cc.combMix);
            }
            if (auto* fo = c->hasProperty("formant")
                             ? c->getProperty("formant").getDynamicObject() : nullptr) {
                const auto v = fo->getProperty("vowel").toString();
                if (v == "ah")      cc.formantVowel = CarouselConfig::Vowel::Ah;
                else if (v == "oh") cc.formantVowel = CarouselConfig::Vowel::Oh;
                else if (v == "ee") cc.formantVowel = CarouselConfig::Vowel::Ee;
                cc.formantAmount = getF(fo, "amount", cc.formantAmount);
            }
```

`juce::var::getArray()` returns `juce::Array<juce::var>*` (or nullptr); `static_cast<int>(juce::var)` calls `operator int`. Both already available via the file's `<juce_core/juce_core.h>` include.

- [ ] **Step 7: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "carousel pitch|pitch blocks leave"`
Expected: 2 tests pass. Full suite: `ctest --test-dir build 2>&1 | tail -2` → 113 tests (111 + 2), 0 failures.

- [ ] **Step 8: Commit**

```bash
git add src/scenes/Scene.h src/scenes/SceneLibrary.cpp \
        tests/unit/scenes/test_scene_library_carousel_pitch.cpp \
        tests/fixtures/scenes/with_carousel_pitch.json tests/CMakeLists.txt
git commit -m "feat(scenes): CarouselConfig pitch/harmonizer/comb/formant blocks"
```

---

## Task 2: PitchShifter stage — granular fixed-ratio (TDD)

**Files:**
- Create: `src/audio/PitchShifter.h`
- Create: `src/audio/PitchShifter.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/unit/audio/test_pitch_shifter.cpp`
- Modify: `tests/CMakeLists.txt`

A two-tap granular (overlap-add) pitch shifter: one circular write buffer, a
read phase advancing at `(1 - ratio)` per sample and wrapping over the grain
window, with two taps half a grain apart crossfaded by triangular windows to
hide the wrap discontinuity. No pitch detection.

- [ ] **Step 1: Write the failing test** `tests/unit/audio/test_pitch_shifter.cpp`

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/PitchShifter.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::PitchShifter;

namespace {
// Estimate dominant frequency by counting positive-going zero crossings.
float estimateHz(const std::vector<float>& x, double sr) {
    int crossings = 0;
    for (size_t i = 1; i < x.size(); ++i)
        if (x[i-1] <= 0.0f && x[i] > 0.0f) ++crossings;
    const double seconds = x.size() / sr;
    return static_cast<float>(crossings / seconds);
}
std::vector<float> sine(int n, float hz, double sr) {
    std::vector<float> b(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        b[static_cast<size_t>(i)] = std::sin(2.0f*3.14159265f*hz*i/static_cast<float>(sr));
    return b;
}
}

TEST_CASE("PitchShifter: octave-up roughly doubles frequency", "[audio][pitch]") {
    PitchShifter ps;
    ps.prepare(48000.0, 4096);   // maxGrainSamples
    ps.setGrainSamples(1920);    // 40 ms
    ps.setRatio(2.0f);           // +12 semitones

    auto in = sine(48000, 220.0f, 48000.0);
    std::vector<float> out(in.size());
    for (size_t i = 0; i < in.size(); ++i) out[i] = ps.processSample(in[i]);

    // Skip the first grain (transient); measure the steady tail.
    std::vector<float> tail(out.begin() + 4096, out.end());
    const float f = estimateHz(tail, 48000.0);
    REQUIRE(f > 380.0f);   // ~440, allow granular slop
    REQUIRE(f < 500.0f);
}

TEST_CASE("PitchShifter: ratio 1.0 passes pitch through", "[audio][pitch]") {
    PitchShifter ps;
    ps.prepare(48000.0, 4096);
    ps.setGrainSamples(1920);
    ps.setRatio(1.0f);

    auto in = sine(48000, 330.0f, 48000.0);
    std::vector<float> out(in.size());
    for (size_t i = 0; i < in.size(); ++i) out[i] = ps.processSample(in[i]);

    std::vector<float> tail(out.begin() + 4096, out.end());
    const float f = estimateHz(tail, 48000.0);
    REQUIRE(f > 300.0f);
    REQUIRE(f < 360.0f);
}

TEST_CASE("PitchShifter: output stays finite and bounded", "[audio][pitch]") {
    PitchShifter ps;
    ps.prepare(48000.0, 4096);
    ps.setGrainSamples(1920);
    ps.setRatio(1.5f);
    auto in = sine(8192, 440.0f, 48000.0);
    for (float s : in) {
        const float y = ps.processSample(s);
        REQUIRE(std::isfinite(y));
        REQUIRE(std::fabs(y) < 4.0f);
    }
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`** — append `unit/audio/test_pitch_shifter.cpp`.

- [ ] **Step 3: Run to confirm failure**

Run: `cmake --build build --target guitar_dsp_tests 2>&1 | head -15`
Expected: FAIL — `audio/PitchShifter.h` not found.

- [ ] **Step 4: Write `src/audio/PitchShifter.h`**

```cpp
#pragma once

#include <vector>

namespace guitar_dsp::audio {

// Two-tap granular (overlap-add) pitch shifter at a fixed ratio. Mono,
// allocation-free after prepare(). No pitch detection: the read phase moves
// at (1 - ratio) per sample over a grain window, with two taps half a grain
// apart crossfaded by triangular windows to hide the wrap discontinuity.
//
// ratio = 2^(semitones/12). ratio 1.0 ≈ identity (with grain latency).
class PitchShifter {
public:
    // maxGrainSamples sizes the internal circular buffer (also caps grain).
    void prepare(double sampleRate, int maxGrainSamples);
    void reset();

    void setRatio(float ratio) noexcept { ratio_ = ratio; }
    void setGrainSamples(int n) noexcept;

    float processSample(float x) noexcept;

private:
    float readInterp(float delaySamples) const noexcept;

    std::vector<float> buffer_;     // circular; sized in prepare
    int   bufSize_     = 0;
    int   writePos_    = 0;
    int   grainSamples_= 1920;
    float ratio_       = 1.0f;
    float phase_       = 0.0f;      // 0 .. grainSamples_
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 5: Write `src/audio/PitchShifter.cpp`**

```cpp
#include "PitchShifter.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

void PitchShifter::prepare(double /*sampleRate*/, int maxGrainSamples) {
    // Buffer must hold a full grain of lookback plus headroom. Round up to a
    // power of two for cheap wrap masking is optional; a plain modulo is fine.
    bufSize_ = std::max(maxGrainSamples * 2, 256);
    buffer_.assign(static_cast<std::size_t>(bufSize_), 0.0f);
    grainSamples_ = std::min(grainSamples_, maxGrainSamples);
    reset();
}

void PitchShifter::reset() {
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writePos_ = 0;
    phase_ = 0.0f;
}

void PitchShifter::setGrainSamples(int n) noexcept {
    grainSamples_ = std::clamp(n, 64, bufSize_ > 0 ? bufSize_ / 2 : n);
}

float PitchShifter::readInterp(float delaySamples) const noexcept {
    float pos = static_cast<float>(writePos_) - delaySamples;
    while (pos < 0.0f) pos += static_cast<float>(bufSize_);
    const int i0 = static_cast<int>(pos);
    const float frac = pos - static_cast<float>(i0);
    const int i1 = (i0 + 1) % bufSize_;
    return (1.0f - frac) * buffer_[static_cast<std::size_t>(i0 % bufSize_)]
         + frac * buffer_[static_cast<std::size_t>(i1)];
}

float PitchShifter::processSample(float x) noexcept {
    // Write incoming sample.
    buffer_[static_cast<std::size_t>(writePos_)] = x;
    writePos_ = (writePos_ + 1) % bufSize_;

    // Advance the grain phase. ratio>1 (pitch up) => phase decreases.
    phase_ += (1.0f - ratio_);
    const float g = static_cast<float>(grainSamples_);
    while (phase_ >= g)   phase_ -= g;
    while (phase_ < 0.0f) phase_ += g;

    const float p1 = phase_;
    float p2 = phase_ + g * 0.5f;
    if (p2 >= g) p2 -= g;

    // Triangular crossfade windows (peak at grain centre, zero at the wrap).
    const float w1 = 1.0f - std::fabs(2.0f * p1 / g - 1.0f);
    const float w2 = 1.0f - std::fabs(2.0f * p2 / g - 1.0f);

    // Delay from write head to each tap (lookback into the past).
    const float s1 = readInterp(g - p1);
    const float s2 = readInterp(g - p2);

    return s1 * w1 + s2 * w2;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 6: Add to `src/CMakeLists.txt`** — append `audio/PitchShifter.cpp` to the `add_library(guitar_dsp_audio STATIC ...)` source list.

- [ ] **Step 7: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "PitchShifter"`
Expected: 3 tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/audio/PitchShifter.h src/audio/PitchShifter.cpp src/CMakeLists.txt \
        tests/unit/audio/test_pitch_shifter.cpp tests/CMakeLists.txt
git commit -m "feat(audio): PitchShifter — granular fixed-ratio pitch shift"
```

---

## Task 3: Harmonizer stage (TDD)

**Files:**
- Create: `src/audio/Harmonizer.h`
- Create: `src/audio/Harmonizer.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/unit/audio/test_harmonizer.cpp`
- Modify: `tests/CMakeLists.txt`

A fixed array of ≤4 `PitchShifter` voices at configured semitone+cents
intervals, summed (normalized) and blended with the dry signal.

- [ ] **Step 1: Write the failing test** `tests/unit/audio/test_harmonizer.cpp`

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/Harmonizer.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::Harmonizer;

namespace {
std::vector<float> sine(int n, float hz) {
    std::vector<float> b(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) b[static_cast<size_t>(i)] = 0.5f*std::sin(2.0f*3.14159265f*hz*i/48000.0f);
    return b;
}
}

TEST_CASE("Harmonizer: unison voice + full dry ~ passes signal", "[audio][harmonizer]") {
    Harmonizer h;
    h.prepare(48000.0, 4096);
    const int semis[1]  = {0};
    const int cents[1]  = {0};
    h.setVoices(semis, cents, 1, 40.0f);
    h.setMix(0.0f);   // fully dry
    auto in = sine(2048, 330.0f);
    float peak = 0.0f;
    for (float s : in) peak = std::max(peak, std::fabs(h.processSample(s)));
    REQUIRE(peak > 0.4f);    // dry signal present
    REQUIRE(peak < 0.6f);
}

TEST_CASE("Harmonizer: three voices stay finite + bounded", "[audio][harmonizer]") {
    Harmonizer h;
    h.prepare(48000.0, 4096);
    const int semis[3] = {12, 7, 0};
    const int cents[3] = {0, 0, 6};
    h.setVoices(semis, cents, 3, 40.0f);
    h.setMix(0.8f);
    auto in = sine(8192, 196.0f);
    for (float s : in) {
        const float y = h.processSample(s);
        REQUIRE(std::isfinite(y));
        REQUIRE(std::fabs(y) < 4.0f);
    }
}

TEST_CASE("Harmonizer: zero voices is pure dry", "[audio][harmonizer]") {
    Harmonizer h;
    h.prepare(48000.0, 4096);
    h.setVoices(nullptr, nullptr, 0, 40.0f);
    h.setMix(1.0f);   // even at full wet, no voices => dry
    REQUIRE(h.processSample(0.42f) == 0.42f);
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`** — append `unit/audio/test_harmonizer.cpp`.

- [ ] **Step 3: Run to confirm failure**

Run: `cmake --build build --target guitar_dsp_tests 2>&1 | head -15`
Expected: FAIL — `audio/Harmonizer.h` not found.

- [ ] **Step 4: Write `src/audio/Harmonizer.h`**

```cpp
#pragma once

#include "PitchShifter.h"

namespace guitar_dsp::audio {

// Up to 4 parallel PitchShifter voices at fixed semitone+cents intervals,
// summed (normalized by voice count) and blended with the dry input.
// mix: 0 = pure dry, 1 = pure harmonized sum.
class Harmonizer {
public:
    static constexpr int kMaxVoices = 4;

    void prepare(double sampleRate, int maxGrainSamples);
    void reset();

    // semitones[]/detuneCents[] may be nullptr when count==0.
    void setVoices(const int* semitones, const int* detuneCents, int count,
                   float grainMs) noexcept;
    void setMix(float mix) noexcept { mix_ = mix; }

    float processSample(float x) noexcept;

private:
    PitchShifter voices_[kMaxVoices];
    int   count_     = 0;
    float voiceGain_ = 0.0f;   // 1 / count
    float mix_       = 0.0f;
    double sampleRate_ = 48000.0;
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 5: Write `src/audio/Harmonizer.cpp`**

```cpp
#include "Harmonizer.h"

#include <cmath>

namespace guitar_dsp::audio {

void Harmonizer::prepare(double sampleRate, int maxGrainSamples) {
    sampleRate_ = sampleRate;
    for (auto& v : voices_) v.prepare(sampleRate, maxGrainSamples);
    reset();
}

void Harmonizer::reset() {
    for (auto& v : voices_) v.reset();
}

void Harmonizer::setVoices(const int* semitones, const int* detuneCents,
                           int count, float grainMs) noexcept {
    count_ = count < 0 ? 0 : (count > kMaxVoices ? kMaxVoices : count);
    voiceGain_ = count_ > 0 ? 1.0f / static_cast<float>(count_) : 0.0f;
    const int grain = static_cast<int>(grainMs * 0.001 * sampleRate_);
    for (int i = 0; i < count_; ++i) {
        const float semis = static_cast<float>(semitones[i])
                          + static_cast<float>(detuneCents[i]) / 100.0f;
        voices_[i].setGrainSamples(grain);
        voices_[i].setRatio(std::pow(2.0f, semis / 12.0f));
    }
}

float Harmonizer::processSample(float x) noexcept {
    if (count_ == 0) return x;
    float wet = 0.0f;
    for (int i = 0; i < count_; ++i) wet += voices_[i].processSample(x);
    wet *= voiceGain_;
    return (1.0f - mix_) * x + mix_ * wet;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 6: Add to `src/CMakeLists.txt`** — append `audio/Harmonizer.cpp`.

- [ ] **Step 7: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "Harmonizer"`
Expected: 3 tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/audio/Harmonizer.h src/audio/Harmonizer.cpp src/CMakeLists.txt \
        tests/unit/audio/test_harmonizer.cpp tests/CMakeLists.txt
git commit -m "feat(audio): Harmonizer — multi-voice fixed-interval pitch"
```

---

## Task 4: Comb filter stage (TDD)

**Files:**
- Create: `src/audio/Comb.h`
- Create: `src/audio/Comb.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/unit/audio/test_comb.cpp`
- Modify: `tests/CMakeLists.txt`

A feedback comb: `y[n] = x[n] + fb * y[n-D]`, `D = sampleRate / freqHz`. The
piano string-resonance / attack tool. Feedback is clamped < 1.0 for stability.

- [ ] **Step 1: Write the failing test** `tests/unit/audio/test_comb.cpp`

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/Comb.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::Comb;

TEST_CASE("Comb: impulse produces an echo one period later", "[audio][comb]") {
    Comb c;
    c.prepare(48000.0, 4800);   // max delay samples
    c.setFreqHz(480.0f);        // D = 100 samples
    c.setFeedback(0.7f);
    c.setMix(1.0f);             // fully wet to see the comb clearly

    std::vector<float> out(400, 0.0f);
    out[0] = c.processSample(1.0f);     // impulse
    for (size_t i = 1; i < out.size(); ++i) out[i] = c.processSample(0.0f);

    // First echo near sample 100 (the delay), attenuated by feedback.
    REQUIRE(std::fabs(out[100]) > 0.3f);
    REQUIRE(std::fabs(out[100]) < 0.9f);
    // Second echo near sample 200, weaker than the first.
    REQUIRE(std::fabs(out[200]) > 0.05f);
    REQUIRE(std::fabs(out[200]) < std::fabs(out[100]));
}

TEST_CASE("Comb: feedback clamped, output stays finite", "[audio][comb]") {
    Comb c;
    c.prepare(48000.0, 4800);
    c.setFreqHz(220.0f);
    c.setFeedback(1.5f);   // out-of-range; must clamp below 1
    c.setMix(0.5f);
    for (int i = 0; i < 48000; ++i) {
        const float y = c.processSample(0.3f);
        REQUIRE(std::isfinite(y));
        REQUIRE(std::fabs(y) < 50.0f);   // bounded (clamp prevents blow-up)
    }
}

TEST_CASE("Comb: zero freq bypasses", "[audio][comb]") {
    Comb c;
    c.prepare(48000.0, 4800);
    c.setFreqHz(0.0f);
    c.setMix(1.0f);
    REQUIRE(c.processSample(0.37f) == 0.37f);
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`** — append `unit/audio/test_comb.cpp`.

- [ ] **Step 3: Run to confirm failure**

Run: `cmake --build build --target guitar_dsp_tests 2>&1 | head -15`
Expected: FAIL — `audio/Comb.h` not found.

- [ ] **Step 4: Write `src/audio/Comb.h`**

```cpp
#pragma once

#include <vector>

namespace guitar_dsp::audio {

// Feedback comb filter: y[n] = x[n] + fb * y[n-D], D = sampleRate / freqHz.
// freqHz == 0 bypasses. Feedback is clamped to [0, 0.95] for stability.
// mix blends dry/comb (0 = dry, 1 = comb).
class Comb {
public:
    void prepare(double sampleRate, int maxDelaySamples);
    void reset();

    void setFreqHz(float hz) noexcept;
    void setFeedback(float fb) noexcept;
    void setMix(float mix) noexcept { mix_ = mix; }

    float processSample(float x) noexcept;

private:
    std::vector<float> buffer_;
    int    bufSize_      = 0;
    int    writePos_     = 0;
    int    delaySamples_ = 0;     // 0 = bypass
    float  feedback_     = 0.0f;
    float  mix_          = 0.0f;
    double sampleRate_   = 48000.0;
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 5: Write `src/audio/Comb.cpp`**

```cpp
#include "Comb.h"

#include <algorithm>

namespace guitar_dsp::audio {

void Comb::prepare(double sampleRate, int maxDelaySamples) {
    sampleRate_ = sampleRate;
    bufSize_ = std::max(maxDelaySamples + 1, 2);
    buffer_.assign(static_cast<std::size_t>(bufSize_), 0.0f);
    reset();
}

void Comb::reset() {
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writePos_ = 0;
}

void Comb::setFreqHz(float hz) noexcept {
    if (hz <= 0.0f) { delaySamples_ = 0; return; }
    const int d = static_cast<int>(sampleRate_ / hz);
    delaySamples_ = std::clamp(d, 1, bufSize_ - 1);
}

void Comb::setFeedback(float fb) noexcept {
    feedback_ = std::clamp(fb, 0.0f, 0.95f);
}

float Comb::processSample(float x) noexcept {
    if (delaySamples_ <= 0) return x;
    int readPos = writePos_ - delaySamples_;
    if (readPos < 0) readPos += bufSize_;
    const float delayed = buffer_[static_cast<std::size_t>(readPos)];
    const float y = x + feedback_ * delayed;
    buffer_[static_cast<std::size_t>(writePos_)] = y;
    writePos_ = (writePos_ + 1) % bufSize_;
    return (1.0f - mix_) * x + mix_ * y;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 6: Add to `src/CMakeLists.txt`** — append `audio/Comb.cpp`.

- [ ] **Step 7: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "Comb"`
Expected: 3 tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/audio/Comb.h src/audio/Comb.cpp src/CMakeLists.txt \
        tests/unit/audio/test_comb.cpp tests/CMakeLists.txt
git commit -m "feat(audio): Comb — feedback comb filter (piano resonance)"
```

---

## Task 5: Formant stage — fixed vowel emphasis (TDD)

**Files:**
- Create: `src/audio/Formant.h`
- Create: `src/audio/Formant.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/unit/audio/test_formant.cpp`
- Modify: `tests/CMakeLists.txt`

Three parallel resonant bandpass peaks at fixed vowel frequencies (via
`juce::dsp::StateVariableTPTFilter` in bandpass mode), summed and blended with
the input by `amount`. NOT pitch-independent formant shifting — a fixed
emphasis filter. Vowel→frequencies table baked in.

- [ ] **Step 1: Write the failing test** `tests/unit/audio/test_formant.cpp`

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/Formant.h"
#include "scenes/Scene.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::Formant;
using guitar_dsp::scenes::CarouselConfig;

namespace {
// Goertzel-ish single-bin magnitude estimate at frequency f.
float bandEnergy(const std::vector<float>& x, float f, double sr) {
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        const double ph = 2.0 * 3.14159265358979 * f * i / sr;
        re += x[i] * std::cos(ph);
        im += x[i] * std::sin(ph);
    }
    return static_cast<float>(std::sqrt(re*re + im*im) / x.size());
}
std::vector<float> noise(int n) {
    std::vector<float> b(static_cast<size_t>(n));
    unsigned s = 0x12345u;
    for (int i = 0; i < n; ++i) { s = s*1664525u + 1013904223u;
        b[static_cast<size_t>(i)] = (static_cast<float>(s >> 9) / 8388608.0f) - 1.0f; }
    return b;
}
}

TEST_CASE("Formant: 'ah' emphasizes its first formant band vs none",
          "[audio][formant]") {
    auto in = noise(48000);

    Formant flat; flat.prepare(48000.0); flat.setVowel(CarouselConfig::Vowel::None);
    flat.setAmount(1.0f);
    std::vector<float> outFlat(in.size());
    for (size_t i = 0; i < in.size(); ++i) outFlat[i] = flat.processSample(in[i]);

    Formant ah; ah.prepare(48000.0); ah.setVowel(CarouselConfig::Vowel::Ah);
    ah.setAmount(1.0f);
    std::vector<float> outAh(in.size());
    for (size_t i = 0; i < in.size(); ++i) outAh[i] = ah.processSample(in[i]);

    // "ah" first formant ~700 Hz: emphasized vs the bypassed (flat) path.
    const float e700flat = bandEnergy(outFlat, 700.0f, 48000.0);
    const float e700ah   = bandEnergy(outAh,   700.0f, 48000.0);
    REQUIRE(e700ah > e700flat * 1.2f);
}

TEST_CASE("Formant: None vowel is bypass", "[audio][formant]") {
    Formant f; f.prepare(48000.0);
    f.setVowel(CarouselConfig::Vowel::None);
    f.setAmount(1.0f);
    REQUIRE(f.processSample(0.31f) == 0.31f);
}

TEST_CASE("Formant: output finite + bounded", "[audio][formant]") {
    Formant f; f.prepare(48000.0);
    f.setVowel(CarouselConfig::Vowel::Ee);
    f.setAmount(0.8f);
    auto in = noise(8192);
    for (float s : in) {
        const float y = f.processSample(s);
        REQUIRE(std::isfinite(y));
        REQUIRE(std::fabs(y) < 8.0f);
    }
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`** — append `unit/audio/test_formant.cpp`.

- [ ] **Step 3: Run to confirm failure**

Run: `cmake --build build --target guitar_dsp_tests 2>&1 | head -15`
Expected: FAIL — `audio/Formant.h` not found.

- [ ] **Step 4: Write `src/audio/Formant.h`**

```cpp
#pragma once

#include <juce_dsp/juce_dsp.h>

#include "scenes/Scene.h"

namespace guitar_dsp::audio {

// Fixed vowel-emphasis filter: 3 parallel resonant bandpass peaks at a
// vowel's formant frequencies, summed and blended with the input by `amount`.
// Vowel::None bypasses. NOT pitch-independent formant shifting.
class Formant {
public:
    void prepare(double sampleRate);
    void reset();

    void setVowel(scenes::CarouselConfig::Vowel v) noexcept;
    void setAmount(float a) noexcept { amount_ = a; }

    float processSample(float x) noexcept;

private:
    static constexpr int kPeaks = 3;
    juce::dsp::StateVariableTPTFilter<float> peaks_[kPeaks];
    scenes::CarouselConfig::Vowel vowel_ = scenes::CarouselConfig::Vowel::None;
    float amount_ = 0.0f;
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 5: Write `src/audio/Formant.cpp`**

```cpp
#include "Formant.h"

namespace guitar_dsp::audio {

namespace {
// First three formant frequencies (Hz) per vowel.
struct VowelPeaks { float f[3]; };
VowelPeaks peaksFor(scenes::CarouselConfig::Vowel v) {
    using V = scenes::CarouselConfig::Vowel;
    switch (v) {
        case V::Ah: return {{ 700.0f, 1220.0f, 2600.0f }};
        case V::Oh: return {{ 500.0f, 1000.0f, 2400.0f }};
        case V::Ee: return {{ 270.0f, 2300.0f, 3000.0f }};
        case V::None:
        default:    return {{ 0.0f, 0.0f, 0.0f }};
    }
}
} // namespace

void Formant::prepare(double sampleRate) {
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
}

void Formant::setVowel(scenes::CarouselConfig::Vowel v) noexcept {
    vowel_ = v;
    const auto vp = peaksFor(v);
    for (int i = 0; i < kPeaks; ++i)
        if (vp.f[i] > 0.0f) peaks_[i].setCutoffFrequency(vp.f[i]);
}

float Formant::processSample(float x) noexcept {
    if (vowel_ == scenes::CarouselConfig::Vowel::None) return x;
    float sum = 0.0f;
    for (auto& p : peaks_) sum += p.processSample(0, x);
    return (1.0f - amount_) * x + amount_ * sum;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 6: Add to `src/CMakeLists.txt`** — append `audio/Formant.cpp`.

- [ ] **Step 7: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "Formant"`
Expected: 3 tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/audio/Formant.h src/audio/Formant.cpp src/CMakeLists.txt \
        tests/unit/audio/test_formant.cpp tests/CMakeLists.txt
git commit -m "feat(audio): Formant — fixed vowel-emphasis filter"
```

---

## Task 6: Wire pitch + harmonizer into the Carousel chain (TDD)

**Files:**
- Modify: `src/audio/Carousel.h`
- Modify: `src/audio/Carousel.cpp`
- Modify: `tests/unit/audio/test_carousel.cpp`

Add `PitchShifter pitch_;` + `Harmonizer harm_;` members, prepare/reset them,
configure in `applyConfig`, and run them **first** in the per-sample loop
(before the existing `drive*shaperAmount`). Harmonizer takes precedence when
`harmVoiceCount > 0`; else single-voice `pitch` when `pitchSemitones != 0`.

- [ ] **Step 1: Add the failing test** — append to `tests/unit/audio/test_carousel.cpp`:

```cpp
TEST_CASE("Carousel: harmonizer raises pitched energy (choir-ish)",
          "[audio][carousel][pitch]") {
    Carousel c;
    c.prepare(48000.0, 512);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.harmVoiceCount = 2;
    cfg.harmSemitones[0] = 12; cfg.harmSemitones[1] = 7;
    cfg.harmMix = 0.9f;
    c.setConfig(cfg);

    auto in = tone(4096, 196.0f);   // low G
    std::vector<float> out(4096, 0.0f);
    for (int blk = 0; blk < 4; ++blk) c.process(in.data(), out.data(), out.size());
    for (float x : out) { REQUIRE(std::isfinite(x)); REQUIRE(std::fabs(x) <= 1.0f); }
    float peak = 0.0f;
    for (float x : out) peak = std::max(peak, std::fabs(x));
    REQUIRE(peak > 0.05f);   // not silent
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target guitar_dsp_tests 2>&1 | head -15`
Expected: FAIL — no `harmVoiceCount` handling yet (test compiles — fields exist from Task 1 — but the harmonizer isn't wired, so the assertion that the chain is engaged may still pass trivially; the real check is the build wiring in Steps 3–5). If it passes pre-wiring, proceed anyway — Steps 3–5 add the behavior and the RT-safety test in Task 8 is the hard gate.

- [ ] **Step 3: Update `src/audio/Carousel.h`** — add includes + members.

Add near the other stage includes:

```cpp
#include "PitchShifter.h"
#include "Harmonizer.h"
#include "Comb.h"
#include "Formant.h"
```

In the `private:` section, alongside `crusher_`, add:

```cpp
    PitchShifter pitch_;
    Harmonizer   harm_;
    Comb         comb_;
    Formant      formant_;
```

(Comb + Formant are wired in Tasks 7–8 but declaring them now keeps the header edits in one place.)

- [ ] **Step 4: Update `src/audio/Carousel.cpp` — prepare/reset + applyConfig + process.**

In `prepare()`, after `crusher_.reset();`, add (sizing pitch/comb buffers from
a 100 ms max):

```cpp
    const int maxGrain = static_cast<int>(sampleRate * 0.1);  // 100 ms
    pitch_.prepare(sampleRate, maxGrain);
    harm_.prepare(sampleRate, maxGrain);
    comb_.prepare(sampleRate, maxGrain);
    formant_.prepare(sampleRate);
```

In `reset()`, after `crusher_.reset();`:

```cpp
    pitch_.reset();
    harm_.reset();
    comb_.reset();
    formant_.reset();
```

In `applyConfig()`, after the crusher setup, add the pitch/harmonizer config:

```cpp
    pitch_.setGrainSamples(static_cast<int>(cfg.pitchGrainMs * 0.001 * sampleRate_));
    pitch_.setRatio(std::pow(2.0f, cfg.pitchSemitones / 12.0f));
    harm_.setVoices(cfg.harmSemitones, cfg.harmDetuneCents,
                    cfg.harmVoiceCount, cfg.pitchGrainMs);
    harm_.setMix(juce::jlimit(0.0f, 1.0f, cfg.harmMix));
```

In `process()`, at the very TOP of the per-sample loop body (before the
`drive` line), insert the pitch/harmonizer voice generation:

```cpp
        float v = in[i];
        if (active_.harmVoiceCount > 0) {
            v = harm_.processSample(v);
        } else if (active_.pitchSemitones != 0.0f) {
            const float shifted = pitch_.processSample(v);
            v = (1.0f - active_.pitchMix) * v + active_.pitchMix * shifted;
        }
        float x = v * driveGain_.getNextValue() * active_.shaperAmount;
```

(Replace the existing first line `float x = in[i] * driveGain_...` with the
block above — `v` feeds `x`.)

Add `#include <cmath>` is already present.

- [ ] **Step 5: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "Carousel"`
Expected: all Carousel tests pass, including the new harmonizer test and the still-bit-exact disabled-passthrough (harmVoiceCount 0 + pitchSemitones 0 → the new block is a no-op).

- [ ] **Step 6: Commit**

```bash
git add src/audio/Carousel.h src/audio/Carousel.cpp tests/unit/audio/test_carousel.cpp
git commit -m "feat(audio): Carousel pitch + harmonizer voice generation"
```

---

## Task 7: Wire comb into the Carousel chain (TDD)

**Files:**
- Modify: `src/audio/Carousel.cpp`
- Modify: `tests/unit/audio/test_carousel.cpp`

Comb runs after the crusher, before the filter.

- [ ] **Step 1: Add the failing test** — append to `tests/unit/audio/test_carousel.cpp`:

```cpp
TEST_CASE("Carousel: comb adds resonant ring (piano-ish)", "[audio][carousel]") {
    Carousel c;
    c.prepare(48000.0, 512);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.combFreqHz = 220.0f;
    cfg.combFeedback = 0.7f;
    cfg.combMix = 0.8f;
    c.setConfig(cfg);

    // One block of tone then silence; the comb should ring into the silence.
    auto in = tone(512, 220.0f);
    std::vector<float> out(512, 0.0f);
    c.process(in.data(), out.data(), out.size());

    std::vector<float> silence(512, 0.0f), tail(512, 0.0f);
    c.process(silence.data(), tail.data(), tail.size());
    float tailPeak = 0.0f;
    for (float x : tail) tailPeak = std::max(tailPeak, std::fabs(x));
    REQUIRE(tailPeak > 1e-3f);
    for (float x : tail) { REQUIRE(std::isfinite(x)); REQUIRE(std::fabs(x) <= 1.0f); }
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "comb adds resonant"`
Expected: FAIL — comb not yet applied; the silent tail stays silent.

- [ ] **Step 3: Configure the comb in `applyConfig()`** — after the pitch/harmonizer config block:

```cpp
    comb_.setFreqHz(cfg.combFreqHz);
    comb_.setFeedback(cfg.combFeedback);
    comb_.setMix(juce::jlimit(0.0f, 1.0f, cfg.combMix));
```

- [ ] **Step 4: Insert the comb into `process()`** — in the per-sample loop, after the crusher line (`x = crusher_.processSample(x);`) and before the filter block:

```cpp
        x = crusher_.processSample(x);
        if (active_.combFreqHz > 0.0f) x = comb_.processSample(x);
```

- [ ] **Step 5: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "Carousel"`
Expected: all pass (disabled passthrough still bit-exact — combFreqHz default 0 skips it).

- [ ] **Step 6: Commit**

```bash
git add src/audio/Carousel.cpp tests/unit/audio/test_carousel.cpp
git commit -m "feat(audio): Carousel comb integration (piano resonance)"
```

---

## Task 8: Wire formant into the chain + RT-safety re-check (TDD)

**Files:**
- Modify: `src/audio/Carousel.cpp`
- Modify: `tests/unit/audio/test_carousel.cpp`

Formant runs after the resonant filter, before chorus.

- [ ] **Step 1: Add the failing test** — append to `tests/unit/audio/test_carousel.cpp`:

```cpp
TEST_CASE("Carousel: full pitch+comb+formant preset is allocation-free",
          "[audio][carousel][rt]") {
    Carousel c;
    c.prepare(48000.0, 512);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.harmVoiceCount = 3;
    cfg.harmSemitones[0] = 12; cfg.harmSemitones[1] = 7; cfg.harmSemitones[2] = 0;
    cfg.harmDetuneCents[2] = 6;
    cfg.harmMix = 0.8f;
    cfg.combFreqHz = 196.0f; cfg.combFeedback = 0.5f; cfg.combMix = 0.4f;
    cfg.formantVowel = CarouselConfig::Vowel::Ah; cfg.formantAmount = 0.6f;
    cfg.reverbRoomSize = 0.6f; cfg.reverbWet = 0.3f;
    c.setConfig(cfg);

    auto in = tone(512, 196.0f);
    std::vector<float> out(512, 0.0f);
    c.process(in.data(), out.data(), out.size());   // pick up config

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int blk = 0; blk < 50; ++blk)
        c.process(in.data(), out.data(), out.size());
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
    for (float x : out) { REQUIRE(std::isfinite(x)); REQUIRE(std::fabs(x) <= 1.0f); }
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "pitch.comb.formant preset is allocation-free"`
Expected: FAIL or pass-but-no-formant — formant not yet wired. (If it passes, the formant still must be wired in Step 3–4; the test mainly guards RT-safety once the formant is engaged.)

- [ ] **Step 3: Configure the formant in `applyConfig()`** — after the comb config:

```cpp
    formant_.setVowel(cfg.formantVowel);
    formant_.setAmount(juce::jlimit(0.0f, 1.0f, cfg.formantAmount));
```

- [ ] **Step 4: Insert the formant into `process()`** — after the filter block (after the closing `}` of `if (filterOn) {...}`) and before `x *= trimGain_...`:

```cpp
            x = filter_.processSample(0, x);
        }

        if (active_.formantVowel != scenes::CarouselConfig::Vowel::None)
            x = formant_.processSample(x);

        x *= trimGain_.getNextValue();
```

- [ ] **Step 5: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "Carousel"`
Expected: all Carousel tests pass, including the new RT-safety test (zero allocations with pitch+comb+formant all active).

- [ ] **Step 6: Commit**

```bash
git add src/audio/Carousel.cpp tests/unit/audio/test_carousel.cpp
git commit -m "feat(audio): Carousel formant integration + full-chain RT-safety"
```

---

## Task 9: Full-chain integrity for choir + piano configs (test only)

**Files:**
- Modify: `tests/unit/audio/test_carousel.cpp`

No new production code — lock in that the two real preset shapes stay finite
and within the limiter ceiling across a sustained pluck.

- [ ] **Step 1: Add the test** — append to `tests/unit/audio/test_carousel.cpp`:

```cpp
TEST_CASE("Carousel: choir + piano presets stay finite and bounded",
          "[audio][carousel][pitch]") {
    auto runPreset = [](const CarouselConfig& cfg) {
        Carousel c;
        c.prepare(48000.0, 512);
        c.setConfig(cfg);
        std::vector<float> in(512), out(512);
        for (int blk = 0; blk < 80; ++blk) {
            const float amp = 0.8f * std::exp(-blk / 30.0f);
            for (int i = 0; i < 512; ++i)
                in[static_cast<size_t>(i)] =
                    amp * std::sin(2.0f*3.14159265f*146.83f*(blk*512+i)/48000.0f);
            c.process(in.data(), out.data(), out.size());
            for (float x : out) {
                REQUIRE(std::isfinite(x));
                REQUIRE(std::fabs(x) <= 1.0f);
            }
        }
    };

    CarouselConfig choir;
    choir.enabled = true;
    choir.harmVoiceCount = 3;
    choir.harmSemitones[0] = 12; choir.harmSemitones[1] = 7; choir.harmSemitones[2] = 0;
    choir.harmDetuneCents[2] = 6; choir.harmMix = 0.85f;
    choir.formantVowel = CarouselConfig::Vowel::Ah; choir.formantAmount = 0.6f;
    choir.reverbRoomSize = 0.7f; choir.reverbWet = 0.4f;
    runPreset(choir);

    CarouselConfig piano;
    piano.enabled = true;
    piano.pitchSemitones = 12.0f; piano.pitchMix = 0.5f; piano.pitchGrainMs = 30.0f;
    piano.combFreqHz = 220.0f; piano.combFeedback = 0.6f; piano.combMix = 0.5f;
    piano.filterMode = CarouselConfig::FilterMode::LowPass;
    piano.filterCutoffHz = 4000.0f; piano.filterResonance = 0.3f;
    piano.reverbRoomSize = 0.3f; piano.reverbWet = 0.15f;
    runPreset(piano);
}
```

- [ ] **Step 2: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "choir . piano presets stay finite"`
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/audio/test_carousel.cpp
git commit -m "test(audio): choir + piano preset stability guard"
```

---

## Task 10: Author the choir + piano presets (rename scenes 1 & 3)

**Files:**
- Rename + rewrite: `assets/scenes/01_carousel_organ.json` → `assets/scenes/01_carousel_choir.json`
- Rename + rewrite: `assets/scenes/03_carousel_synth.json` → `assets/scenes/03_carousel_piano.json`

- [ ] **Step 1: Rename both files**

```bash
git mv assets/scenes/01_carousel_organ.json assets/scenes/01_carousel_choir.json
git mv assets/scenes/03_carousel_synth.json assets/scenes/03_carousel_piano.json
```

- [ ] **Step 2: Write `assets/scenes/01_carousel_choir.json`**

```json
{
  "id": 1,
  "name": "Carousel — choir / pad",
  "color": "#b48ce3",
  "mixer": { "masterGainDb": -4.0, "dryWet": 1.0, "transitionMs": 60 },
  "carousel": {
    "enabled": true,
    "harmonizer": { "intervals": [12, 7, 0], "detuneCents": [0, 0, 6], "mix": 0.85 },
    "formant": { "vowel": "ah", "amount": 0.6 },
    "filter": { "mode": "lowpass", "cutoffHz": 3500, "resonance": 0.2, "mod": "static" },
    "reverb": { "roomSize": 0.7, "wet": 0.4 },
    "outputTrimDb": -2.0
  }
}
```

- [ ] **Step 3: Write `assets/scenes/03_carousel_piano.json`**

```json
{
  "id": 3,
  "name": "Carousel — piano-ish",
  "color": "#7eb8de",
  "mixer": { "masterGainDb": -4.0, "dryWet": 1.0, "transitionMs": 20 },
  "carousel": {
    "enabled": true,
    "pitch": { "semitones": 12, "mix": 0.45, "grainMs": 30 },
    "comb": { "freqHz": 220, "feedback": 0.6, "mix": 0.5 },
    "filter": { "mode": "lowpass", "cutoffHz": 4000, "resonance": 0.3, "mod": "static" },
    "reverb": { "roomSize": 0.3, "wet": 0.15 },
    "outputTrimDb": -2.0
  }
}
```

- [ ] **Step 4: Validate JSON + rebuild (asset copy picks up renames)**

```bash
for f in assets/scenes/0[1-5]_*.json; do python3 -c "import json,sys; json.load(open('$f'))" && echo "ok $f"; done
cmake --build build --target guitar_dsp_app_Standalone 2>&1 | tail -3
```
Expected: 5 "ok" lines (choir, distortion, piano, 8bit, autowah); clean build.

- [ ] **Step 5: Commit**

```bash
git add assets/scenes/01_carousel_choir.json assets/scenes/03_carousel_piano.json
git commit -m "feat(scenes): scenes 1 & 3 become choir/pad + piano-ish"
```

---

## Task 11: Scene-switch integration test for choir + piano (TDD)

**Files:**
- Modify: `tests/integration/test_carousel_scene.cpp`

Extend the existing carousel integration test with the two pitch presets,
asserting they transform the guitar vs clean and stay finite/bounded.

- [ ] **Step 1: Add the test** — append to `tests/integration/test_carousel_scene.cpp`:

```cpp
TEST_CASE("integration: choir + piano presets transform guitar, stay bounded",
          "[integration][carousel][pitch]") {
    AudioGraph g;
    g.prepare(48000.0, 1024);

    std::vector<float> in(1024);
    for (int i = 0; i < 1024; ++i) {
        const float amp = 0.8f * std::exp(-i / 6000.0f);
        in[static_cast<size_t>(i)] = amp * std::sin(2.0f*3.14159265f*146.83f*i/48000.0f);
    }

    g.setWetSource(AudioGraph::WetSource::Vocoder);
    g.mixer().setDryWet(0.0f); g.mixer().setMasterGainDb(0.0f); g.mixer().reset();
    std::vector<float> clean(1024, 0.0f);
    g.process(in.data(), clean.data(), clean.size());
    double cleanE = 0.0; for (float v : clean) cleanE += static_cast<double>(v)*v;

    CarouselConfig choir;
    choir.enabled = true;
    choir.harmVoiceCount = 3;
    choir.harmSemitones[0] = 12; choir.harmSemitones[1] = 7; choir.harmSemitones[2] = 0;
    choir.harmMix = 0.85f;
    choir.formantVowel = CarouselConfig::Vowel::Ah; choir.formantAmount = 0.6f;

    CarouselConfig piano;
    piano.enabled = true;
    piano.pitchSemitones = 12.0f; piano.pitchMix = 0.5f;
    piano.combFreqHz = 220.0f; piano.combFeedback = 0.6f; piano.combMix = 0.5f;

    for (const auto& cfg : { choir, piano }) {
        g.carousel().setConfig(cfg);
        g.setWetSource(AudioGraph::WetSource::Carousel);
        g.mixer().setDryWet(1.0f); g.mixer().reset();
        std::vector<float> out(1024, 0.0f);
        g.process(in.data(), out.data(), out.size());
        g.process(in.data(), out.data(), out.size());
        double e = 0.0;
        for (float x : out) {
            REQUIRE(std::isfinite(x));
            REQUIRE(std::fabs(x) <= 1.0f);
            e += static_cast<double>(x)*x;
        }
        REQUIRE(e != cleanE);   // it changed the signal
    }
}
```

(`AudioGraph`, `CarouselConfig`, `<cmath>`, `<vector>` are already included at
the top of this file from Phase 4a.)

- [ ] **Step 2: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "choir . piano presets transform"`
Expected: PASS. Full suite: `ctest --test-dir build 2>&1 | tail -2` → all green.

- [ ] **Step 3: Commit**

```bash
git add tests/integration/test_carousel_scene.cpp
git commit -m "test(integration): choir + piano scene transforms guitar"
```

---

## Task 12: README update + Phase 4b wrap-up verification

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update the Phase-4 carousel table in `README.md`.** Replace the two changed rows so the table reads:

```markdown
| Key | Scene | Effect |
|-----|-------|--------|
| `2` | 1 | Choir / pad (harmonizer + vowel formant + big reverb) |
| `3` | 2 | Distorted guitar (drive + hard clip + tone filter) |
| `4` | 3 | Piano-ish (octave pitch + comb resonance + reverb) |
| `5` | 4 | 8-bit chiptune (bit crusher + sample-rate reducer) |
| `6` | 5 | Auto-wah (envelope-following resonant bandpass) |
```

Then replace the "Phase 4b" bullet under "Subsequent phases" — since 4b is now
done, change that line to:

```markdown
- **Phase 5**: Full-screen visualization (spectrogram, karaoke text overlay).
- **Phase 6**: Hardening + dress rehearsal (lands queued chips: data race, UAF guards).
```

And update the status sentence near the top of the section from "Phase 4: Instrument Carousel A" to "Phase 4b: Instrument Carousel (full) — all five carousel patches, including the pitch/harmony choir + piano, now process the live guitar." Add one line noting the new stages: "Phase 4b adds bespoke `PitchShifter`, `Harmonizer`, `Comb`, and `Formant` stages (fixed-ratio granular pitch, no MIDI, no pitch tracking)."

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: README documents Phase 4b (choir + piano carousel patches)"
```

- [ ] **Step 3: Clean rebuild from scratch**

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
Expected: clean.

- [ ] **Step 4: Full test pass**

```bash
ctest --test-dir build --output-on-failure
```
Expected: all pass; the 4 pre-existing opt-in tests skipped. New count ≈ 111 + ~17 (2 config + 9 stage + ~6 carousel/integration).

- [ ] **Step 5: Manual smoke** (requires a guitar / input signal)

```bash
open "build/src/app/guitar_dsp_app_artefacts/Release/Standalone/Guitar DSP.app"
```
Verify by ear with a signal at the input:
- Key `2` (scene 1) = choir/pad wash (harmonized, vowel-ish, reverberant).
- Key `4` (scene 3) = piano-ish (octave layer + comb ring; expect "electric-piano-ish," slight latency).
- Keys `3`,`5`,`6` (distortion, 8-bit, auto-wah) unchanged.
- Speaking scenes (`7`–`9`) still vocode TTS; panic (`0`) clean.
- No clicks/crashes on rapid scene switches.

State explicitly whether audio was actually auditioned or only build/tests verified (no input signal in CI).

- [ ] **Step 6: Final commit if any cleanup was made**

```bash
git commit -am "chore: phase 4b wrap-up fixes"   # only if something changed
```

---

## Self-review

**Spec coverage (spec § → task):**
- §2 scene assignment (replace 1 & 3) → Task 10.
- §3 new stages: PitchShifter → Task 2, Harmonizer → Task 3, Comb → Task 4, Formant → Task 5.
- §4 chain placement (pitch/harm early, comb after crusher, formant after filter) → Tasks 6, 7, 8.
- §5 CarouselConfig blocks + parser → Task 1.
- §6 preset sketches (choir/piano) → Task 10.
- §7 testing (per-stage, RT-safety, full-chain integrity, scene-switch) → Tasks 2–5 (unit), Task 8 (RT), Task 9 (integrity), Task 11 (integration).
- §8 out-of-scope (true LPC formant, pitch tracking, banks) → not implemented; Formant is fixed-emphasis only.
- §9 risks (latency, piano realism, comb self-oscillation) → comb feedback clamp (Task 4), limiter backstop (existing), grain tuning in presets (Task 10).

**Placeholder scan:** No "TBD"/"add error handling"/"similar to Task N" — every code step shows complete code.

**Type consistency:**
- `CarouselConfig` fields (`pitchSemitones`, `pitchMix`, `pitchGrainMs`, `harmVoiceCount`, `harmSemitones[4]`, `harmDetuneCents[4]`, `harmMix`, `combFreqHz`, `combFeedback`, `combMix`, `Vowel{None,Ah,Oh,Ee}`, `formantVowel`, `formantAmount`, `kMaxHarmVoices`) — defined Task 1, used identically in Tasks 5–11.
- `PitchShifter` (`prepare(sr,maxGrain)`, `setRatio`, `setGrainSamples`, `processSample`, `reset`) — Task 2, used by Harmonizer (Task 3) + Carousel (Task 6).
- `Harmonizer` (`prepare`, `setVoices(semis,cents,count,grainMs)`, `setMix`, `processSample`, `reset`) — Task 3, used Task 6.
- `Comb` (`prepare`, `setFreqHz`, `setFeedback`, `setMix`, `processSample`, `reset`) — Task 4, used Task 7.
- `Formant` (`prepare(sr)`, `setVowel(Vowel)`, `setAmount`, `processSample`, `reset`) — Task 5, used Task 8.
- Chain order (drive→pitch/harm→shaper→crusher→comb→filter→formant→chorus→reverb→trim→limiter) consistent across Tasks 6–8.

---

## Execution handoff

Plan complete. Per project convention this executes **subagent-driven on a git worktree** — each implementer subagent's first instruction is to `cd` into the worktree's absolute path before any other action (prevents the stray-to-main bug).

1. **Subagent-Driven (recommended)** — fresh subagent per task, two-stage review between tasks.
2. **Inline Execution** — batch execution with checkpoints.
