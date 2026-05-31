# Instrument Carousel A Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give carousel scenes 1–5 real pedal-style instrument timbres by processing the live guitar audio through a parametric `Carousel` effects chain (no pitch-shifting, no MIDI notes).

**Architecture:** A new `audio::Carousel` runs a fixed-order chain — drive → waveshaper → crusher → resonant filter (static/envelope/LFO) → chorus → reverb → output trim — built from `juce::dsp` modules plus small bespoke stages. `AudioGraph` gains a wet-source selector so instrument scenes route guitar → Carousel → Mixer while speaking scenes keep guitar → Vocoder → Mixer. Carousel config is pushed from the message thread on scene change via the same atomic-swap pattern `TTSClipPlayer` uses.

**Tech Stack:** C++20, JUCE 8 `juce_dsp` (`WaveShaper`, `StateVariableTPTFilter`, `Chorus`, `Reverb`, `SmoothedValue`), Catch2 v3.

**Reference spec:** [`docs/superpowers/specs/2026-05-31-instrument-carousel-a-design.md`](../specs/2026-05-31-instrument-carousel-a-design.md)

---

## Background for the implementing engineer

Read these first:

- **`src/audio/TTSClipPlayer.{h,cpp}`** — the canonical message-thread→audio-thread handoff: a `std::atomic<bool> newXFlag_`, a `pendingX` (written only on the message thread), and an `activeX` (read only on the audio thread). `process()` does `newXFlag_.exchange(false, acquire)` and, if true, moves pending→active. **Carousel::setConfig/process copies this pattern exactly.**
- **`src/audio/AudioGraph.{h,cpp}`** — the graph. `process(const float* in, float* out, size_t n)` runs InputStage → (TTSClipPlayer → Vocoder) → Mixer. You will add a `Carousel` member and a wet-source selector.
- **`src/scenes/Scene.h`** — plain structs (`MixerParams`, `TtsConfig`, `Scene`). You add `CarouselConfig`.
- **`src/scenes/SceneLibrary.cpp`** — `loadOne()` parses each JSON block (`mixer`, `tts`). You add a `carousel` block parser following the exact same `if (obj->hasProperty(...))` style.
- **`src/scenes/SceneEngine.{h,cpp}`** — `activeTtsConfig()` returns the active scene's TTS block (message thread). You add `activeCarouselConfig()`.
- **`src/app/PluginProcessor.cpp`** — in `processBlock`, a scene-id change triggers `juce::MessageManager::callAsync` that reads `activeTtsConfig()` and configures the graph. You extend that callback to also push the carousel config + select the wet source.

**Threading rules (unchanged, non-negotiable — "cannot crash"):**
- Audio thread (`*::process`): no heap allocation, no locks, no Obj-C. Everything is sized in `prepare()`.
- Message thread: scene loading, config push, JUCE `dsp` `prepare()`.
- `RealtimeSentinel` tests assert zero allocations in `process()`.

**juce::dsp usage notes:**
- Each `juce::dsp` module needs `prepare(const juce::dsp::ProcessSpec&)` once (message thread) and is then RT-safe.
- For a mono `float*` buffer, wrap it as `juce::dsp::AudioBlock<float> block(&ptr, 1, numSamples)` then `juce::dsp::ProcessContextReplacing<float> ctx(block)`. Constructing an `AudioBlock` over existing memory does NOT allocate.
- `juce::dsp::Reverb` exposes `processMono(float* samples, int num)` directly — no block wrapper needed.
- `juce::dsp::StateVariableTPTFilter<float>` exposes `processSample(int channel, float x)` for per-sample use.
- `juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>` ramps continuous params; `setTargetValue()` on the message-thread pickup, `getNextValue()` per sample.

**Build dependency:** `guitar_dsp_audio` currently links only `juce::juce_audio_formats`. Task 5 adds `juce::juce_dsp` (PUBLIC, so tests inherit it).

---

## File structure

```
src/scenes/Scene.h                       (modify, Task 1: CarouselConfig)
src/scenes/SceneLibrary.cpp              (modify, Task 1: parse carousel block)
src/scenes/SceneEngine.{h,cpp}           (modify, Task 2: activeCarouselConfig)
src/audio/Crusher.{h,cpp}                (NEW, Task 3)
src/audio/CarouselMod.{h,cpp}            (NEW, Task 4: EnvelopeFollower + Lfo)
src/audio/Carousel.{h,cpp}               (NEW, Tasks 5-10)
src/audio/AudioGraph.{h,cpp}             (modify, Task 11: wet-source selector)
src/CMakeLists.txt                       (modify, Tasks 3,4,5: new sources + juce_dsp)
src/app/PluginProcessor.cpp              (modify, Task 12: push carousel cfg)
assets/scenes/01_carousel_organ.json     (modify, Task 13)
assets/scenes/02_carousel_distortion.json(rename+modify, Task 13)
assets/scenes/03_carousel_synth.json     (modify, Task 13)
assets/scenes/04_carousel_8bit.json      (modify, Task 13)
assets/scenes/05_carousel_autowah.json   (rename+modify, Task 13)
tests/CMakeLists.txt                     (modify, throughout)
tests/fixtures/scenes/with_carousel.json (NEW, Task 1)
tests/unit/scenes/test_scene_library_carousel.cpp (NEW, Task 1)
tests/unit/scenes/test_scene_engine_carousel.cpp  (NEW, Task 2)
tests/unit/audio/test_crusher.cpp        (NEW, Task 3)
tests/unit/audio/test_carousel_mod.cpp   (NEW, Task 4)
tests/unit/audio/test_carousel.cpp       (NEW, Tasks 5-10)
tests/integration/test_carousel_scene.cpp(NEW, Task 14)
README.md                                (modify, Task 15)
```

---

## Task 1: CarouselConfig data + JSON parser (TDD)

**Files:**
- Modify: `src/scenes/Scene.h`
- Modify: `src/scenes/SceneLibrary.cpp`
- Create: `tests/fixtures/scenes/with_carousel.json`
- Create: `tests/unit/scenes/test_scene_library_carousel.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the fixture** `tests/fixtures/scenes/with_carousel.json`

```json
{
  "id": 31,
  "name": "Carousel fixture",
  "color": "#a64dff",
  "mixer": { "masterGainDb": -3.0, "dryWet": 1.0, "transitionMs": 25 },
  "carousel": {
    "enabled": true,
    "drive": 6.0,
    "waveshaper": { "type": "hardclip", "amount": 0.8 },
    "crusher": { "bits": 4, "downsample": 8 },
    "filter": { "mode": "bandpass", "cutoffHz": 800, "resonance": 0.7,
                "mod": "envelope", "envAmount": 2000, "lfoHz": 0 },
    "chorus": { "rateHz": 5.0, "depth": 0.3, "mix": 0.4 },
    "reverb": { "roomSize": 0.4, "wet": 0.2 },
    "outputTrimDb": -1.5
  }
}
```

- [ ] **Step 2: Write the failing test** `tests/unit/scenes/test_scene_library_carousel.cpp`

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

TEST_CASE("SceneLibrary: parses carousel block", "[scenes][library][carousel]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_carousel.json"));
    REQUIRE(s.has_value());
    const auto& c = s->carousel;
    REQUIRE(c.enabled);
    REQUIRE_THAT(c.drive, WithinAbs(6.0f, 1e-4f));
    REQUIRE(c.shaper == CarouselConfig::Shaper::HardClip);
    REQUIRE_THAT(c.shaperAmount, WithinAbs(0.8f, 1e-4f));
    REQUIRE(c.crusherBits == 4);
    REQUIRE(c.crusherDownsample == 8);
    REQUIRE(c.filterMode == CarouselConfig::FilterMode::BandPass);
    REQUIRE(c.filterMod == CarouselConfig::FilterMod::Envelope);
    REQUIRE_THAT(c.filterCutoffHz, WithinAbs(800.0f, 1e-3f));
    REQUIRE_THAT(c.filterResonance, WithinAbs(0.7f, 1e-4f));
    REQUIRE_THAT(c.filterEnvAmount, WithinAbs(2000.0f, 1e-3f));
    REQUIRE_THAT(c.chorusRateHz, WithinAbs(5.0f, 1e-4f));
    REQUIRE_THAT(c.reverbWet, WithinAbs(0.2f, 1e-4f));
    REQUIRE_THAT(c.outputTrimDb, WithinAbs(-1.5f, 1e-4f));
}

TEST_CASE("SceneLibrary: missing carousel block leaves disabled default",
          "[scenes][library][carousel]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_tts_text.json"));
    REQUIRE(s.has_value());
    REQUIRE_FALSE(s->carousel.enabled);
    REQUIRE(s->carousel.shaper == CarouselConfig::Shaper::None);
}
```

- [ ] **Step 3: Add the test to `tests/CMakeLists.txt`** — append `unit/scenes/test_scene_library_carousel.cpp` to the `add_executable(guitar_dsp_tests ...)` source list.

- [ ] **Step 4: Run to confirm failure**

Run: `cmake --build build --target guitar_dsp_tests 2>&1 | head -15`
Expected: FAIL — `CarouselConfig` is not a member of `guitar_dsp::scenes`.

- [ ] **Step 5: Add `CarouselConfig` to `src/scenes/Scene.h`** — insert after the `TtsConfig` struct (before `struct Scene`):

```cpp
struct CarouselConfig {
    bool  enabled = false;
    float drive   = 0.0f;          // dB pre-gain

    enum class Shaper { None, Tanh, HardClip, Foldback };
    Shaper shaper       = Shaper::None;
    float  shaperAmount = 1.0f;    // pre-shaper gain multiplier

    int crusherBits       = 0;     // 0 = bypass; else 1..16
    int crusherDownsample = 1;     // 1 = bypass; else hold N samples

    enum class FilterMode { Off, LowPass, BandPass, HighPass };
    enum class FilterMod  { Static, Envelope, Lfo };
    FilterMode filterMode     = FilterMode::Off;
    FilterMod  filterMod      = FilterMod::Static;
    float      filterCutoffHz = 1000.0f;
    float      filterResonance= 0.5f;
    float      filterEnvAmount= 0.0f;   // Hz added at full envelope
    float      filterLfoHz    = 0.0f;

    float chorusRateHz = 0.0f;     // 0 = bypass
    float chorusDepth  = 0.0f;
    float chorusMix    = 0.0f;

    float reverbRoomSize = 0.0f;   // 0 = bypass
    float reverbWet      = 0.0f;

    float outputTrimDb = 0.0f;
};
```

Then add a member to `struct Scene` after `TtsConfig tts{};`:

```cpp
    CarouselConfig carousel{};
```

- [ ] **Step 6: Add the parser to `src/scenes/SceneLibrary.cpp`** — insert after the `tts` parsing block (before `return s;`):

```cpp
    if (obj->hasProperty("carousel")) {
        if (auto* c = obj->getProperty("carousel").getDynamicObject()) {
            auto& cc = s.carousel;
            auto getF = [](juce::DynamicObject* o, const char* k, float d) {
                return o->hasProperty(k)
                     ? static_cast<float>(static_cast<double>(o->getProperty(k))) : d;
            };
            if (c->hasProperty("enabled"))
                cc.enabled = static_cast<bool>(c->getProperty("enabled"));
            cc.drive        = getF(c, "drive", cc.drive);
            cc.outputTrimDb = getF(c, "outputTrimDb", cc.outputTrimDb);

            if (auto* w = c->hasProperty("waveshaper")
                            ? c->getProperty("waveshaper").getDynamicObject() : nullptr) {
                const auto t = w->getProperty("type").toString();
                if (t == "tanh")      cc.shaper = CarouselConfig::Shaper::Tanh;
                else if (t == "hardclip") cc.shaper = CarouselConfig::Shaper::HardClip;
                else if (t == "foldback") cc.shaper = CarouselConfig::Shaper::Foldback;
                cc.shaperAmount = getF(w, "amount", cc.shaperAmount);
            }
            if (auto* cr = c->hasProperty("crusher")
                             ? c->getProperty("crusher").getDynamicObject() : nullptr) {
                if (cr->hasProperty("bits"))
                    cc.crusherBits = static_cast<int>(cr->getProperty("bits"));
                if (cr->hasProperty("downsample"))
                    cc.crusherDownsample = static_cast<int>(cr->getProperty("downsample"));
            }
            if (auto* f = c->hasProperty("filter")
                            ? c->getProperty("filter").getDynamicObject() : nullptr) {
                const auto mode = f->getProperty("mode").toString();
                if (mode == "lowpass")  cc.filterMode = CarouselConfig::FilterMode::LowPass;
                else if (mode == "bandpass") cc.filterMode = CarouselConfig::FilterMode::BandPass;
                else if (mode == "highpass") cc.filterMode = CarouselConfig::FilterMode::HighPass;
                const auto mod = f->getProperty("mod").toString();
                if (mod == "envelope") cc.filterMod = CarouselConfig::FilterMod::Envelope;
                else if (mod == "lfo") cc.filterMod = CarouselConfig::FilterMod::Lfo;
                cc.filterCutoffHz  = getF(f, "cutoffHz", cc.filterCutoffHz);
                cc.filterResonance = getF(f, "resonance", cc.filterResonance);
                cc.filterEnvAmount = getF(f, "envAmount", cc.filterEnvAmount);
                cc.filterLfoHz     = getF(f, "lfoHz", cc.filterLfoHz);
            }
            if (auto* ch = c->hasProperty("chorus")
                             ? c->getProperty("chorus").getDynamicObject() : nullptr) {
                cc.chorusRateHz = getF(ch, "rateHz", cc.chorusRateHz);
                cc.chorusDepth  = getF(ch, "depth", cc.chorusDepth);
                cc.chorusMix    = getF(ch, "mix", cc.chorusMix);
            }
            if (auto* rv = c->hasProperty("reverb")
                             ? c->getProperty("reverb").getDynamicObject() : nullptr) {
                cc.reverbRoomSize = getF(rv, "roomSize", cc.reverbRoomSize);
                cc.reverbWet      = getF(rv, "wet", cc.reverbWet);
            }
        }
    }
```

Add `#include "Scene.h"` is already transitively present via `SceneLibrary.h`; `juce::DynamicObject` is already available via the existing `<juce_core/juce_core.h>` include.

- [ ] **Step 7: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "carousel block|carousel block leaves"`
Expected: 2 tests pass. Full suite still green: `ctest --test-dir build 2>&1 | tail -2` → 93 tests (91 + 2).

- [ ] **Step 8: Commit**

```bash
git add src/scenes/Scene.h src/scenes/SceneLibrary.cpp \
        tests/unit/scenes/test_scene_library_carousel.cpp \
        tests/fixtures/scenes/with_carousel.json tests/CMakeLists.txt
git commit -m "feat(scenes): CarouselConfig struct + JSON parser"
```

---

## Task 2: SceneEngine::activeCarouselConfig (TDD)

**Files:**
- Modify: `src/scenes/SceneEngine.h`
- Modify: `src/scenes/SceneEngine.cpp`
- Create: `tests/unit/scenes/test_scene_engine_carousel.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test** `tests/unit/scenes/test_scene_engine_carousel.cpp`

```cpp
#include <catch2/catch_test_macros.hpp>
#include "scenes/SceneEngine.h"

using guitar_dsp::scenes::Scene;
using guitar_dsp::scenes::SceneEngine;
using guitar_dsp::scenes::CarouselConfig;

TEST_CASE("SceneEngine: activeCarouselConfig reflects active scene",
          "[scenes][engine][carousel]") {
    Scene a = Scene::defaults(0);  // no carousel
    Scene b = Scene::defaults(1);
    b.carousel.enabled = true;
    b.carousel.filterMode = CarouselConfig::FilterMode::LowPass;
    b.carousel.filterCutoffHz = 1234.0f;

    SceneEngine engine;
    engine.loadScenes({a, b});

    engine.activateScene(0);
    REQUIRE_FALSE(engine.activeCarouselConfig().enabled);

    engine.activateScene(1);
    const auto c = engine.activeCarouselConfig();
    REQUIRE(c.enabled);
    REQUIRE(c.filterMode == CarouselConfig::FilterMode::LowPass);
    REQUIRE(c.filterCutoffHz == 1234.0f);
}

TEST_CASE("SceneEngine: activeCarouselConfig empty when no active scene",
          "[scenes][engine][carousel]") {
    SceneEngine engine;
    REQUIRE_FALSE(engine.activeCarouselConfig().enabled);
}
```

- [ ] **Step 2: Add the test to `tests/CMakeLists.txt`** — append `unit/scenes/test_scene_engine_carousel.cpp`.

- [ ] **Step 3: Run to confirm failure**

Run: `cmake --build build --target guitar_dsp_tests 2>&1 | head -15`
Expected: FAIL — no member `activeCarouselConfig`.

- [ ] **Step 4: Declare in `src/scenes/SceneEngine.h`** — after the `activeTtsConfig()` declaration:

```cpp
    // Returns the active scene's carousel config (copy). Disabled-default
    // struct if no active scene. Message-thread only.
    CarouselConfig activeCarouselConfig() const;
```

- [ ] **Step 5: Implement in `src/scenes/SceneEngine.cpp`** — add near `activeTtsConfig()`:

```cpp
CarouselConfig SceneEngine::activeCarouselConfig() const {
    if (activeIndex_ < 0 || activeIndex_ >= static_cast<int>(scenes_.size()))
        return CarouselConfig{};
    return scenes_[static_cast<std::size_t>(activeIndex_)].carousel;
}
```

- [ ] **Step 6: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "activeCarouselConfig"`
Expected: 2 tests pass.

- [ ] **Step 7: Commit**

```bash
git add src/scenes/SceneEngine.h src/scenes/SceneEngine.cpp \
        tests/unit/scenes/test_scene_engine_carousel.cpp tests/CMakeLists.txt
git commit -m "feat(scenes): SceneEngine::activeCarouselConfig accessor"
```

---

## Task 3: Crusher stage (bit-depth + downsample) (TDD)

**Files:**
- Create: `src/audio/Crusher.h`
- Create: `src/audio/Crusher.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/unit/audio/test_crusher.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test** `tests/unit/audio/test_crusher.cpp`

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/Crusher.h"

#include <set>
#include <vector>
#include <cmath>

using guitar_dsp::audio::Crusher;

TEST_CASE("Crusher: bit reduction limits distinct levels", "[audio][crusher]") {
    Crusher c;
    c.setBits(2);            // 2 bits -> at most 4 levels
    c.setDownsample(1);
    std::vector<float> buf(2048);
    for (size_t i = 0; i < buf.size(); ++i)
        buf[i] = std::sin(2.0f * 3.14159265f * 220.0f * i / 48000.0f);
    for (float& x : buf) x = c.processSample(x);

    std::set<int> levels;
    for (float x : buf) levels.insert(static_cast<int>(std::lround(x * 1000.0f)));
    REQUIRE(levels.size() <= 5);   // <=4 quantized + rounding slack
}

TEST_CASE("Crusher: downsample holds samples", "[audio][crusher]") {
    Crusher c;
    c.setBits(0);            // bypass bit reduction
    c.setDownsample(4);
    std::vector<float> in{0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f};
    std::vector<float> out;
    for (float x : in) out.push_back(c.processSample(x));
    // First sample of each group of 4 is held across the group.
    REQUIRE(out[0] == out[1]);
    REQUIRE(out[1] == out[2]);
    REQUIRE(out[2] == out[3]);
    REQUIRE(out[4] == in[4]);
}

TEST_CASE("Crusher: bypass passes signal unchanged", "[audio][crusher]") {
    Crusher c;
    c.setBits(0);
    c.setDownsample(1);
    REQUIRE(c.processSample(0.37f) == 0.37f);
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`** — append `unit/audio/test_crusher.cpp`.

- [ ] **Step 3: Run to confirm failure**

Run: `cmake --build build --target guitar_dsp_tests 2>&1 | head -15`
Expected: FAIL — `audio/Crusher.h` not found.

- [ ] **Step 4: Write `src/audio/Crusher.h`**

```cpp
#pragma once

namespace guitar_dsp::audio {

// Bit-depth quantizer + sample-and-hold downsampler ("8-bit" character).
// Per-sample, allocation-free. bits==0 bypasses quantization;
// downsample<=1 bypasses the hold.
class Crusher {
public:
    void setBits(int bits) noexcept { bits_ = bits; }
    void setDownsample(int factor) noexcept { downsample_ = factor < 1 ? 1 : factor; }
    void reset() noexcept { holdCounter_ = 0; held_ = 0.0f; }

    float processSample(float x) noexcept;

private:
    int   bits_       = 0;
    int   downsample_ = 1;
    int   holdCounter_= 0;
    float held_       = 0.0f;
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 5: Write `src/audio/Crusher.cpp`**

```cpp
#include "Crusher.h"

#include <cmath>

namespace guitar_dsp::audio {

float Crusher::processSample(float x) noexcept {
    // Sample-and-hold downsampling.
    if (downsample_ > 1) {
        if (holdCounter_ == 0) held_ = x;
        x = held_;
        if (++holdCounter_ >= downsample_) holdCounter_ = 0;
    }
    // Bit-depth quantization.
    if (bits_ > 0 && bits_ < 16) {
        const float levels = static_cast<float>(1 << bits_);
        x = std::round(x * levels) / levels;
    }
    return x;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 6: Add to `src/CMakeLists.txt`** — append `audio/Crusher.cpp` to `add_library(guitar_dsp_audio STATIC ...)`.

- [ ] **Step 7: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "Crusher"`
Expected: 3 tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/audio/Crusher.h src/audio/Crusher.cpp src/CMakeLists.txt \
        tests/unit/audio/test_crusher.cpp tests/CMakeLists.txt
git commit -m "feat(audio): Crusher stage (bit-depth + downsample)"
```

---

## Task 4: Modulation sources — EnvelopeFollower + Lfo (TDD)

**Files:**
- Create: `src/audio/CarouselMod.h`
- Create: `src/audio/CarouselMod.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/unit/audio/test_carousel_mod.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test** `tests/unit/audio/test_carousel_mod.cpp`

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "audio/CarouselMod.h"

#include <cmath>

using Catch::Matchers::WithinAbs;
using guitar_dsp::audio::EnvelopeFollower;
using guitar_dsp::audio::Lfo;

TEST_CASE("EnvelopeFollower: rises on loud input, decays on silence",
          "[audio][mod]") {
    EnvelopeFollower env;
    env.prepare(48000.0);
    // Drive with full-scale for 4800 samples (0.1 s).
    float e = 0.0f;
    for (int i = 0; i < 4800; ++i) e = env.processSample(1.0f);
    REQUIRE(e > 0.5f);
    // Now feed silence; envelope should decay below 0.1 within 0.5 s.
    for (int i = 0; i < 24000; ++i) e = env.processSample(0.0f);
    REQUIRE(e < 0.1f);
}

TEST_CASE("Lfo: produces bounded sine at requested rate", "[audio][mod]") {
    Lfo lfo;
    lfo.prepare(48000.0);
    lfo.setRateHz(2.0f);
    float minV = 1.0f, maxV = -1.0f;
    int zeroCrossings = 0;
    float prev = lfo.processSample();
    for (int i = 0; i < 48000; ++i) {  // 1 second
        const float v = lfo.processSample();
        minV = std::min(minV, v);
        maxV = std::max(maxV, v);
        if ((prev <= 0.0f && v > 0.0f)) ++zeroCrossings;
        prev = v;
    }
    REQUIRE(minV >= -1.01f);
    REQUIRE(maxV <= 1.01f);
    // 2 Hz over 1 s -> ~2 rising zero-crossings.
    REQUIRE(zeroCrossings >= 1);
    REQUIRE(zeroCrossings <= 3);
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`** — append `unit/audio/test_carousel_mod.cpp`.

- [ ] **Step 3: Run to confirm failure**

Run: `cmake --build build --target guitar_dsp_tests 2>&1 | head -15`
Expected: FAIL — `audio/CarouselMod.h` not found.

- [ ] **Step 4: Write `src/audio/CarouselMod.h`**

```cpp
#pragma once

namespace guitar_dsp::audio {

// One-pole peak envelope follower (fixed ~5 ms attack, ~120 ms release).
// Output is a smoothed absolute-amplitude estimate in [0, ~1].
class EnvelopeFollower {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept { env_ = 0.0f; }
    float processSample(float x) noexcept;

private:
    float env_       = 0.0f;
    float attackCoef_= 0.0f;
    float releaseCoef_=0.0f;
};

// Sine LFO phasor in [-1, 1].
class Lfo {
public:
    void prepare(double sampleRate) noexcept { sampleRate_ = sampleRate; }
    void setRateHz(float hz) noexcept { rateHz_ = hz; }
    void reset() noexcept { phase_ = 0.0f; }
    float processSample() noexcept;

private:
    double sampleRate_ = 48000.0;
    float  rateHz_     = 0.0f;
    float  phase_      = 0.0f;  // 0..1
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 5: Write `src/audio/CarouselMod.cpp`**

```cpp
#include "CarouselMod.h"

#include <cmath>

namespace guitar_dsp::audio {

void EnvelopeFollower::prepare(double sampleRate) noexcept {
    const double atkMs = 5.0, relMs = 120.0;
    attackCoef_  = static_cast<float>(std::exp(-1.0 / (sampleRate * atkMs  / 1000.0)));
    releaseCoef_ = static_cast<float>(std::exp(-1.0 / (sampleRate * relMs  / 1000.0)));
    env_ = 0.0f;
}

float EnvelopeFollower::processSample(float x) noexcept {
    const float a = std::fabs(x);
    const float coef = (a > env_) ? attackCoef_ : releaseCoef_;
    env_ = a + coef * (env_ - a);
    return env_;
}

float Lfo::processSample() noexcept {
    if (rateHz_ <= 0.0f) return 0.0f;
    const float v = std::sin(2.0f * 3.14159265358979f * phase_);
    phase_ += static_cast<float>(rateHz_ / sampleRate_);
    if (phase_ >= 1.0f) phase_ -= 1.0f;
    return v;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 6: Add to `src/CMakeLists.txt`** — append `audio/CarouselMod.cpp`.

- [ ] **Step 7: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "EnvelopeFollower|Lfo"`
Expected: 2 tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/audio/CarouselMod.h src/audio/CarouselMod.cpp src/CMakeLists.txt \
        tests/unit/audio/test_carousel_mod.cpp tests/CMakeLists.txt
git commit -m "feat(audio): CarouselMod — envelope follower + LFO"
```

---

## Task 5: Carousel skeleton — config swap + passthrough + juce_dsp link (TDD)

**Files:**
- Create: `src/audio/Carousel.h`
- Create: `src/audio/Carousel.cpp`
- Modify: `src/CMakeLists.txt` (add source + `juce::juce_dsp` to `guitar_dsp_audio`)
- Create: `tests/unit/audio/test_carousel.cpp`
- Modify: `tests/CMakeLists.txt`

This task establishes the class shape: the `TTSClipPlayer`-style atomic config swap, `prepare/reset/process`, and a guarantee that a **disabled** config passes audio through unchanged. The DSP stages are added in Tasks 6–10.

- [ ] **Step 1: Write the failing test** `tests/unit/audio/test_carousel.cpp`

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "audio/Carousel.h"
#include "scenes/Scene.h"
#include "harness/RealtimeSentinel.h"

#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;
using guitar_dsp::audio::Carousel;
using guitar_dsp::scenes::CarouselConfig;
using guitar_dsp::tests::RealtimeSentinel;

namespace {
std::vector<float> tone(int n, float hz) {
    std::vector<float> b(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        b[static_cast<size_t>(i)] = 0.5f * std::sin(2.0f*3.14159265f*hz*i/48000.0f);
    return b;
}
}

TEST_CASE("Carousel: disabled config is passthrough", "[audio][carousel]") {
    Carousel c;
    c.prepare(48000.0, 512);
    CarouselConfig cfg;  // enabled == false
    c.setConfig(cfg);

    auto in = tone(512, 220.0f);
    std::vector<float> out(512, 0.0f);
    c.process(in.data(), out.data(), out.size());
    for (size_t i = 0; i < out.size(); ++i)
        REQUIRE_THAT(out[i], WithinAbs(in[i], 1e-6f));
}

TEST_CASE("Carousel: process is allocation-free", "[audio][carousel][rt]") {
    Carousel c;
    c.prepare(48000.0, 512);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.drive = 6.0f;
    cfg.shaper = CarouselConfig::Shaper::Tanh;
    cfg.filterMode = CarouselConfig::FilterMode::LowPass;
    cfg.filterMod = CarouselConfig::FilterMod::Envelope;
    cfg.chorusRateHz = 5.0f; cfg.chorusMix = 0.4f;
    cfg.reverbRoomSize = 0.4f; cfg.reverbWet = 0.2f;
    c.setConfig(cfg);

    auto in = tone(512, 220.0f);
    std::vector<float> out(512, 0.0f);
    // Pick up config once (outside the measured region).
    c.process(in.data(), out.data(), out.size());

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int blk = 0; blk < 50; ++blk)
        c.process(in.data(), out.data(), out.size());
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`** — append `unit/audio/test_carousel.cpp`.

- [ ] **Step 3: Run to confirm failure**

Run: `cmake --build build --target guitar_dsp_tests 2>&1 | head -15`
Expected: FAIL — `audio/Carousel.h` not found.

- [ ] **Step 4: Write `src/audio/Carousel.h`**

```cpp
#pragma once

#include <atomic>
#include <cstddef>

#include <juce_dsp/juce_dsp.h>

#include "Crusher.h"
#include "CarouselMod.h"
#include "scenes/Scene.h"

namespace guitar_dsp::audio {

// Pedal-style mono effects chain for the instrument-carousel scenes.
// Fixed order: drive -> waveshaper -> crusher -> resonant filter
// (static/envelope/LFO) -> chorus -> reverb -> output trim. Each stage
// bypasses when its config is inert. Built from juce::dsp modules plus
// the bespoke Crusher / EnvelopeFollower / Lfo.
//
// Threading mirrors TTSClipPlayer: setConfig() (message thread) stashes a
// pending config + flag; process() (audio thread) picks it up at block
// start. process() never allocates.
class Carousel {
public:
    Carousel();

    void prepare(double sampleRate, int blockSize);
    void reset();

    // Message-thread API.
    void setConfig(const scenes::CarouselConfig& cfg);

    // Audio-thread API. in and out may alias.
    void process(const float* in, float* out, std::size_t numSamples) noexcept;

private:
    void applyConfig(const scenes::CarouselConfig& cfg) noexcept;  // audio thread

    double sampleRate_ = 48000.0;

    std::atomic<bool>       newConfigFlag_ {false};
    scenes::CarouselConfig  pendingConfig_;   // message thread
    scenes::CarouselConfig  active_;          // audio thread

    // Stages (added across Tasks 6-10).
    juce::dsp::StateVariableTPTFilter<float> filter_;
    juce::dsp::Chorus<float>                 chorus_;
    juce::dsp::Reverb                        reverb_;
    Crusher                                  crusher_;
    EnvelopeFollower                         env_;
    Lfo                                      lfo_;

    // Smoothed continuous params.
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> driveGain_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> trimGain_;
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 5: Write `src/audio/Carousel.cpp`** (skeleton — stages filled in Tasks 6–10)

```cpp
#include "Carousel.h"

#include <algorithm>
#include <cstring>

namespace guitar_dsp::audio {

Carousel::Carousel() = default;

void Carousel::prepare(double sampleRate, int blockSize) {
    sampleRate_ = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(blockSize);
    spec.numChannels = 1;

    filter_.prepare(spec);
    chorus_.prepare(spec);
    reverb_.prepare(spec);
    crusher_.reset();
    env_.prepare(sampleRate);
    lfo_.prepare(sampleRate);

    driveGain_.reset(sampleRate, 0.02);  // 20 ms ramp
    trimGain_.reset(sampleRate, 0.02);
    driveGain_.setCurrentAndTargetValue(1.0f);
    trimGain_.setCurrentAndTargetValue(1.0f);

    reset();
}

void Carousel::reset() {
    filter_.reset();
    chorus_.reset();
    reverb_.reset();
    crusher_.reset();
    env_.reset();
    lfo_.reset();
}

void Carousel::setConfig(const scenes::CarouselConfig& cfg) {
    pendingConfig_ = cfg;
    newConfigFlag_.store(true, std::memory_order_release);
}

void Carousel::applyConfig(const scenes::CarouselConfig& cfg) noexcept {
    active_ = cfg;
    // Stage configuration is filled in across Tasks 6-10.
    crusher_.setBits(cfg.crusherBits);
    crusher_.setDownsample(cfg.crusherDownsample);
    driveGain_.setTargetValue(juce::Decibels::decibelsToGain(cfg.drive));
    trimGain_.setTargetValue(juce::Decibels::decibelsToGain(cfg.outputTrimDb));
}

void Carousel::process(const float* in, float* out, std::size_t numSamples) noexcept {
    if (newConfigFlag_.exchange(false, std::memory_order_acquire))
        applyConfig(pendingConfig_);

    if (!active_.enabled) {
        if (in != out) std::memcpy(out, in, numSamples * sizeof(float));
        return;
    }

    // Stage processing is filled in across Tasks 6-10. For now, passthrough
    // with the smoothed drive/trim gains so the skeleton is exercisable.
    for (std::size_t i = 0; i < numSamples; ++i) {
        float x = in[i] * driveGain_.getNextValue();
        x *= trimGain_.getNextValue();
        out[i] = x;
    }
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 6: Update `src/CMakeLists.txt`** — append `audio/Carousel.cpp` to the library, AND add `juce::juce_dsp` to the PUBLIC link list of `guitar_dsp_audio`:

```cmake
target_link_libraries(guitar_dsp_audio
    PUBLIC
        juce::juce_audio_formats
        juce::juce_dsp
    PRIVATE
        "-framework AVFoundation"
        "-framework Foundation"
)
```

- [ ] **Step 7: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "Carousel:"`
Expected: 2 tests pass (disabled passthrough + allocation-free). The drive/trim default to unity so the disabled test stays bit-exact.

- [ ] **Step 8: Commit**

```bash
git add src/audio/Carousel.h src/audio/Carousel.cpp src/CMakeLists.txt \
        tests/unit/audio/test_carousel.cpp tests/CMakeLists.txt
git commit -m "feat(audio): Carousel skeleton (config swap + juce_dsp link)"
```

---

## Task 6: Carousel — drive + waveshaper stage (TDD)

**Files:**
- Modify: `src/audio/Carousel.cpp`
- Modify: `tests/unit/audio/test_carousel.cpp`

- [ ] **Step 1: Add the failing test** — append to `tests/unit/audio/test_carousel.cpp`:

```cpp
TEST_CASE("Carousel: hardclip waveshaper bounds output", "[audio][carousel]") {
    Carousel c;
    c.prepare(48000.0, 512);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.drive = 24.0f;          // push hard into the clipper
    cfg.shaper = CarouselConfig::Shaper::HardClip;
    cfg.shaperAmount = 1.0f;
    c.setConfig(cfg);

    auto in = tone(2048, 110.0f);
    std::vector<float> out(2048, 0.0f);
    c.process(in.data(), out.data(), out.size());   // pick up config
    c.process(in.data(), out.data(), out.size());   // steady state

    float peak = 0.0f;
    for (float x : out) peak = std::max(peak, std::fabs(x));
    REQUIRE(peak <= 1.001f);
    REQUIRE(peak > 0.5f);       // it IS clipping, not silent
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "hardclip waveshaper"`
Expected: FAIL — output not bounded (skeleton has no clipper yet; peak exceeds 1.0 because drive=24 dB ≈ 16× gain).

- [ ] **Step 3: Add a shaper helper + wire it into `process()`** in `src/audio/Carousel.cpp`. Add an anonymous-namespace helper near the top:

```cpp
namespace {
inline float shape(float x, guitar_dsp::scenes::CarouselConfig::Shaper s) noexcept {
    using S = guitar_dsp::scenes::CarouselConfig::Shaper;
    switch (s) {
        case S::Tanh:     return std::tanh(x);
        case S::HardClip: return std::clamp(x, -1.0f, 1.0f);
        case S::Foldback:
            while (x > 1.0f || x < -1.0f) {
                if (x > 1.0f)  x = 2.0f - x;
                if (x < -1.0f) x = -2.0f - x;
            }
            return x;
        case S::None:
        default:          return x;
    }
}
} // namespace
```

Add `#include <cmath>` and `#include <algorithm>` (already present). Then replace the placeholder loop in `process()` with the drive + shaper portion:

```cpp
    for (std::size_t i = 0; i < numSamples; ++i) {
        float x = in[i] * driveGain_.getNextValue() * active_.shaperAmount;
        x = shape(x, active_.shaper);
        x *= trimGain_.getNextValue();
        out[i] = x;
    }
```

- [ ] **Step 4: Run to confirm pass**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "Carousel"`
Expected: all Carousel tests pass (disabled passthrough still bit-exact because `shaperAmount` default is 1.0 and `shaper` default is `None` → `shape()` returns `x`).

- [ ] **Step 5: Commit**

```bash
git add src/audio/Carousel.cpp tests/unit/audio/test_carousel.cpp
git commit -m "feat(audio): Carousel drive + waveshaper stage"
```

---

## Task 7: Carousel — crusher integration (TDD)

**Files:**
- Modify: `src/audio/Carousel.cpp`
- Modify: `tests/unit/audio/test_carousel.cpp`

- [ ] **Step 1: Add the failing test** — append to `tests/unit/audio/test_carousel.cpp`:

```cpp
TEST_CASE("Carousel: crusher reduces distinct output levels", "[audio][carousel]") {
    Carousel c;
    c.prepare(48000.0, 1024);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.crusherBits = 3;        // <= 8 levels
    cfg.crusherDownsample = 1;
    c.setConfig(cfg);

    auto in = tone(1024, 220.0f);
    std::vector<float> out(1024, 0.0f);
    c.process(in.data(), out.data(), out.size());

    std::set<int> levels;
    for (float x : out) levels.insert(static_cast<int>(std::lround(x * 100.0f)));
    REQUIRE(levels.size() <= 12);   // 8 quantized levels + rounding slack
}
```

Add `#include <set>` to the test file's includes if not already present.

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "crusher reduces"`
Expected: FAIL — crusher not yet applied in `process()`; too many distinct levels.

- [ ] **Step 3: Insert crusher into `process()`** in `src/audio/Carousel.cpp`, after the shaper line and before the trim:

```cpp
    for (std::size_t i = 0; i < numSamples; ++i) {
        float x = in[i] * driveGain_.getNextValue() * active_.shaperAmount;
        x = shape(x, active_.shaper);
        x = crusher_.processSample(x);
        x *= trimGain_.getNextValue();
        out[i] = x;
    }
```

(The crusher already bypasses when `bits==0 && downsample<=1`, so the disabled-passthrough test stays bit-exact.)

- [ ] **Step 4: Run to confirm pass**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "Carousel"`
Expected: all Carousel tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/audio/Carousel.cpp tests/unit/audio/test_carousel.cpp
git commit -m "feat(audio): Carousel crusher integration"
```

---

## Task 8: Carousel — resonant filter (static/envelope/LFO) (TDD)

**Files:**
- Modify: `src/audio/Carousel.cpp`
- Modify: `tests/unit/audio/test_carousel.cpp`

The filter uses `juce::dsp::StateVariableTPTFilter<float>` per-sample via
`processSample(0, x)`. Its cutoff is recomputed per sample when modulation is
active (envelope follower or LFO); resonance and type come from the config.

- [ ] **Step 1: Add the failing test** — append to `tests/unit/audio/test_carousel.cpp`:

```cpp
static float rmsAbove(const std::vector<float>& x) {  // crude HF energy proxy
    double acc = 0.0;
    for (size_t i = 1; i < x.size(); ++i) {
        const float d = x[i] - x[i-1];      // first difference emphasizes HF
        acc += static_cast<double>(d) * d;
    }
    return static_cast<float>(std::sqrt(acc / x.size()));
}

TEST_CASE("Carousel: lowpass attenuates a high tone", "[audio][carousel]") {
    Carousel c;
    c.prepare(48000.0, 4096);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.filterMode = CarouselConfig::FilterMode::LowPass;
    cfg.filterMod  = CarouselConfig::FilterMod::Static;
    cfg.filterCutoffHz = 400.0f;
    cfg.filterResonance = 0.3f;
    c.setConfig(cfg);

    auto hi = tone(4096, 5000.0f);
    std::vector<float> out(4096, 0.0f);
    c.process(hi.data(), out.data(), out.size());   // pick up + settle
    c.process(hi.data(), out.data(), out.size());

    float peak = 0.0f;
    for (float x : out) peak = std::max(peak, std::fabs(x));
    REQUIRE(peak < 0.25f);   // 5 kHz well above a 400 Hz LP is strongly cut
}

TEST_CASE("Carousel: envelope-modulated bandpass produces finite output (auto-wah)",
          "[audio][carousel]") {
    Carousel c;
    c.prepare(48000.0, 4096);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.filterMode = CarouselConfig::FilterMode::BandPass;
    cfg.filterMod  = CarouselConfig::FilterMod::Envelope;
    cfg.filterCutoffHz = 300.0f;
    cfg.filterResonance = 0.7f;
    cfg.filterEnvAmount = 2000.0f;
    c.setConfig(cfg);

    auto in = tone(4096, 440.0f);
    std::vector<float> out(4096, 0.0f);
    c.process(in.data(), out.data(), out.size());
    c.process(in.data(), out.data(), out.size());
    for (float x : out) { REQUIRE(std::isfinite(x)); REQUIRE(std::fabs(x) < 4.0f); }
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "lowpass attenuates"`
Expected: FAIL — filter not applied; 5 kHz tone passes through near full amplitude.

- [ ] **Step 3: Wire the filter into `applyConfig()` + `process()`** in `src/audio/Carousel.cpp`.

In `applyConfig()`, after the crusher setup, configure the filter type +
static cutoff/resonance:

```cpp
    using FM = scenes::CarouselConfig::FilterMode;
    switch (cfg.filterMode) {
        case FM::LowPass:  filter_.setType(juce::dsp::StateVariableTPTFilterType::lowpass); break;
        case FM::BandPass: filter_.setType(juce::dsp::StateVariableTPTFilterType::bandpass); break;
        case FM::HighPass: filter_.setType(juce::dsp::StateVariableTPTFilterType::highpass); break;
        case FM::Off: default: break;
    }
    filter_.setResonance(juce::jlimit(0.05f, 0.95f, cfg.filterResonance));
    filter_.setCutoffFrequency(juce::jlimit(20.0f, 18000.0f, cfg.filterCutoffHz));
    lfo_.setRateHz(cfg.filterLfoHz);
```

In `process()`, insert the filter between the crusher and the trim. Replace the loop body so it computes a per-sample cutoff when modulated:

```cpp
    const bool filterOn = active_.filterMode != scenes::CarouselConfig::FilterMode::Off;
    using FMod = scenes::CarouselConfig::FilterMod;
    for (std::size_t i = 0; i < numSamples; ++i) {
        float x = in[i] * driveGain_.getNextValue() * active_.shaperAmount;
        x = shape(x, active_.shaper);
        x = crusher_.processSample(x);

        if (filterOn) {
            if (active_.filterMod == FMod::Envelope) {
                const float e = env_.processSample(x);
                const float fc = juce::jlimit(20.0f, 18000.0f,
                    active_.filterCutoffHz + active_.filterEnvAmount * e);
                filter_.setCutoffFrequency(fc);
            } else if (active_.filterMod == FMod::Lfo) {
                const float l = lfo_.processSample();       // -1..1
                const float fc = juce::jlimit(20.0f, 18000.0f,
                    active_.filterCutoffHz * std::pow(2.0f, l));  // ±1 octave
                filter_.setCutoffFrequency(fc);
            }
            x = filter_.processSample(0, x);
        }

        x *= trimGain_.getNextValue();
        out[i] = x;
    }
```

Note: calling `setCutoffFrequency` per sample is allocation-free for
`StateVariableTPTFilter` (it just recomputes coefficients with cheap math).

- [ ] **Step 4: Run to confirm pass**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "Carousel"`
Expected: all Carousel tests pass, including the new filter tests and the still-bit-exact disabled passthrough (filterMode default `Off` skips the filter).

- [ ] **Step 5: Commit**

```bash
git add src/audio/Carousel.cpp tests/unit/audio/test_carousel.cpp
git commit -m "feat(audio): Carousel resonant filter (static/envelope/LFO)"
```

---

## Task 9: Carousel — chorus + reverb (TDD)

**Files:**
- Modify: `src/audio/Carousel.cpp`
- Modify: `tests/unit/audio/test_carousel.cpp`

These are block-based `juce::dsp` modules. Process them over the whole buffer
*after* the per-sample loop, gated on whether they're active.

- [ ] **Step 1: Add the failing test** — append to `tests/unit/audio/test_carousel.cpp`:

```cpp
TEST_CASE("Carousel: reverb adds a decaying tail after input stops",
          "[audio][carousel]") {
    Carousel c;
    c.prepare(48000.0, 4096);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.reverbRoomSize = 0.7f;
    cfg.reverbWet = 0.6f;
    c.setConfig(cfg);

    // One block of tone, then a block of silence: the silent block should
    // still carry reverb energy.
    auto in = tone(4096, 440.0f);
    std::vector<float> out(4096, 0.0f);
    c.process(in.data(), out.data(), out.size());     // pick up + excite

    std::vector<float> silence(4096, 0.0f), tail(4096, 0.0f);
    c.process(silence.data(), tail.data(), tail.size());
    float tailPeak = 0.0f;
    for (float x : tail) tailPeak = std::max(tailPeak, std::fabs(x));
    REQUIRE(tailPeak > 1e-3f);   // reverb tail present
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "reverb adds"`
Expected: FAIL — no reverb yet; the silent block stays silent.

- [ ] **Step 3: Wire chorus + reverb** in `src/audio/Carousel.cpp`.

In `applyConfig()`, after the filter setup:

```cpp
    chorus_.setRate(cfg.chorusRateHz);
    chorus_.setDepth(juce::jlimit(0.0f, 1.0f, cfg.chorusDepth));
    chorus_.setMix(juce::jlimit(0.0f, 1.0f, cfg.chorusMix));
    chorus_.setCentreDelay(7.0f);
    chorus_.setFeedback(0.0f);

    juce::dsp::Reverb::Parameters rp;
    rp.roomSize = juce::jlimit(0.0f, 1.0f, cfg.reverbRoomSize);
    rp.wetLevel = juce::jlimit(0.0f, 1.0f, cfg.reverbWet);
    rp.dryLevel = 1.0f - rp.wetLevel * 0.5f;
    rp.width = 1.0f;
    reverb_.setParameters(rp);
```

In `process()`, after the per-sample loop, add block-based chorus + reverb on
`out` (wrapping the mono buffer as an AudioBlock — no allocation):

```cpp
    if (active_.chorusMix > 0.0f && active_.chorusRateHz > 0.0f) {
        juce::dsp::AudioBlock<float> block(&out, 1, numSamples);
        juce::dsp::ProcessContextReplacing<float> ctx(block);
        chorus_.process(ctx);
    }
    if (active_.reverbWet > 0.0f) {
        reverb_.processMono(out, static_cast<int>(numSamples));
    }
```

Note `juce::dsp::AudioBlock<float> block(&out, 1, numSamples)` takes the
address of the `out` pointer (a `float* const*` of length 1). This views the
existing buffer; it does not allocate.

- [ ] **Step 4: Run to confirm pass**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "Carousel"`
Expected: all Carousel tests pass. Disabled passthrough still bit-exact (chorusMix and reverbWet default to 0 → both skipped). Re-run the RT-safety test specifically to confirm chorus+reverb didn't introduce allocations: `ctest --test-dir build --output-on-failure -R "allocation-free"`.

- [ ] **Step 5: Commit**

```bash
git add src/audio/Carousel.cpp tests/unit/audio/test_carousel.cpp
git commit -m "feat(audio): Carousel chorus + reverb stages"
```

---

## Task 10: Carousel — full-chain integrity test (no-NaN, bounded)

**Files:**
- Modify: `tests/unit/audio/test_carousel.cpp`

No new production code — this task locks in a guard that the full chain (all
stages enabled, like a real preset) never produces NaN/Inf or runaway output.

- [ ] **Step 1: Add the test** — append to `tests/unit/audio/test_carousel.cpp`:

```cpp
TEST_CASE("Carousel: full preset stays finite and bounded on a pluck",
          "[audio][carousel]") {
    Carousel c;
    c.prepare(48000.0, 512);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.drive = 9.0f;
    cfg.shaper = CarouselConfig::Shaper::Tanh;
    cfg.shaperAmount = 1.2f;
    cfg.crusherBits = 6; cfg.crusherDownsample = 2;
    cfg.filterMode = CarouselConfig::FilterMode::BandPass;
    cfg.filterMod  = CarouselConfig::FilterMod::Envelope;
    cfg.filterCutoffHz = 500.0f; cfg.filterResonance = 0.85f;
    cfg.filterEnvAmount = 2500.0f;
    cfg.chorusRateHz = 4.0f; cfg.chorusDepth = 0.4f; cfg.chorusMix = 0.5f;
    cfg.reverbRoomSize = 0.5f; cfg.reverbWet = 0.3f;
    cfg.outputTrimDb = -2.0f;
    c.setConfig(cfg);

    std::vector<float> in(512), out(512);
    // 100 blocks of a decaying tone burst.
    for (int blk = 0; blk < 100; ++blk) {
        const float amp = 0.8f * std::exp(-blk / 30.0f);
        for (int i = 0; i < 512; ++i)
            in[static_cast<size_t>(i)] =
                amp * std::sin(2.0f*3.14159265f*196.0f*(blk*512+i)/48000.0f);
        c.process(in.data(), out.data(), out.size());
        for (float x : out) {
            REQUIRE(std::isfinite(x));
            REQUIRE(std::fabs(x) < 8.0f);   // generous ceiling; no runaway
        }
    }
}
```

- [ ] **Step 2: Run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "full preset stays finite"`
Expected: PASS. If it fails on the bound, the resonance clamp in Task 8
(`jlimit(0.05, 0.95)`) plus the output trim should keep it stable — investigate
before loosening the bound.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/audio/test_carousel.cpp
git commit -m "test(audio): Carousel full-preset stability guard"
```

---

## Task 11: AudioGraph — wet-source selector + Carousel branch (TDD)

**Files:**
- Modify: `src/audio/AudioGraph.h`
- Modify: `src/audio/AudioGraph.cpp`
- Modify: `tests/unit/audio/test_audio_graph.cpp`

`AudioGraph` gains a `Carousel` member and an atomic wet-source selector set
from the message thread. When the selector is `Carousel`, the graph routes
`InputStage → Carousel → Mixer.wet`; otherwise it keeps the existing
`InputStage → Vocoder → Mixer.wet`.

- [ ] **Step 1: Add the failing test** — append to `tests/unit/audio/test_audio_graph.cpp`:

```cpp
TEST_CASE("AudioGraph: carousel wet-source transforms the guitar",
          "[audio][graph][carousel]") {
    using guitar_dsp::audio::AudioGraph;
    using guitar_dsp::scenes::CarouselConfig;

    AudioGraph g;
    g.prepare(48000.0, 512);
    g.mixer().setDryWet(1.0f);   // hear the wet path only
    g.mixer().setMasterGainDb(0.0f);
    g.mixer().reset();

    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.shaper = CarouselConfig::Shaper::HardClip;
    cfg.drive = 24.0f;
    g.carousel().setConfig(cfg);
    g.setWetSource(AudioGraph::WetSource::Carousel);

    std::vector<float> in(512), out(512);
    for (int i = 0; i < 512; ++i)
        in[static_cast<size_t>(i)] = 0.9f * std::sin(2.0f*3.14159265f*110.0f*i/48000.0f);

    g.process(in.data(), out.data(), out.size());  // pick up config
    g.process(in.data(), out.data(), out.size());

    float peak = 0.0f;
    for (float x : out) peak = std::max(peak, std::fabs(x));
    REQUIRE(peak <= 1.05f);     // hard-clipped
    REQUIRE(peak > 0.3f);       // and present
}
```

Ensure the test file includes `"scenes/Scene.h"`, `<cmath>`, `<algorithm>`, and `<vector>`.

- [ ] **Step 2: Run to confirm failure**

Run: `cmake --build build --target guitar_dsp_tests 2>&1 | head -15`
Expected: FAIL — no `carousel()`, `setWetSource`, or `WetSource` on `AudioGraph`.

- [ ] **Step 3: Update `src/audio/AudioGraph.h`** — add include, member, accessor, selector:

```cpp
#include "Carousel.h"
```

In the `public:` section, after `ChannelVocoder& vocoder()`:

```cpp
    Carousel& carousel() { return carousel_; }

    enum class WetSource { Vocoder, Carousel };
    // Message-thread: choose which branch feeds the Mixer's wet input.
    void setWetSource(WetSource s) noexcept {
        wetSource_.store(static_cast<int>(s), std::memory_order_relaxed);
    }
```

In the `private:` section, add:

```cpp
    Carousel carousel_;
    std::atomic<int> wetSource_ {static_cast<int>(WetSource::Vocoder)};
```

Add `#include <atomic>` at the top.

- [ ] **Step 4: Update `src/audio/AudioGraph.cpp`**

In `prepare()`, after `vocoder_.setSibilance(0.5f);`:

```cpp
    carousel_.prepare(sampleRate, blockSize);
```

In `reset()`, after `vocoder_.reset();`:

```cpp
    carousel_.reset();
```

In `process()`, replace the vocoder/wet section so the wet buffer comes from
the selected branch:

```cpp
    if (wetSource_.load(std::memory_order_relaxed)
            == static_cast<int>(WetSource::Carousel)) {
        // Carousel branch: transform the guitar directly into the wet buffer.
        carousel_.process(postInputBuffer_.data(), wetBuffer_.data(), numSamples);
    } else {
        // Vocoder branch (unchanged): modulator = TTS playback.
        ttsClipPlayer_.process(wetBuffer_.data(), numSamples);
        vocoder_.process(postInputBuffer_.data(), wetBuffer_.data(),
                         wetBuffer_.data(), numSamples);
    }

    mixer_.process(postInputBuffer_.data(), wetBuffer_.data(), out, numSamples);
```

- [ ] **Step 5: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "carousel wet-source"`
Expected: PASS. Full suite green: `ctest --test-dir build 2>&1 | tail -2`.

- [ ] **Step 6: Commit**

```bash
git add src/audio/AudioGraph.h src/audio/AudioGraph.cpp tests/unit/audio/test_audio_graph.cpp
git commit -m "feat(audio): AudioGraph wet-source selector + Carousel branch"
```

---

## Task 12: PluginProcessor — push carousel config on scene change

**Files:**
- Modify: `src/app/PluginProcessor.cpp`

The scene-change `callAsync` (in `processBlock`, currently ~lines 156–188)
reads `activeTtsConfig()` and configures the vocoder/TTS branch. Extend it to
also read `activeCarouselConfig()`, push it to the graph's carousel, and
select the wet source. No new test here — Task 14's integration test covers
the end-to-end path; this task is wiring that the existing suite must not
regress.

- [ ] **Step 1: Add the carousel wiring inside the callAsync lambda.** Open
`src/app/PluginProcessor.cpp` and find the scene-change `callAsync` block. At
the **top of the lambda body**, right after the
`if (sceneEngine_.getActiveSceneId() != activeSceneId) return;` guard, insert:

```cpp
            // Carousel branch selection + config push (message thread).
            const auto carouselCfg = sceneEngine_.activeCarouselConfig();
            graph_.carousel().setConfig(carouselCfg);
            graph_.setWetSource(carouselCfg.enabled
                ? audio::AudioGraph::WetSource::Carousel
                : audio::AudioGraph::WetSource::Vocoder);
            if (carouselCfg.enabled) {
                // Instrument scene: no TTS clip should play under it.
                currentTtsClipKey_.clear();
                graph_.ttsClipPlayer().setClip(nullptr);
                return;  // skip the TTS/vocoder routing below
            }
```

This early-returns for instrument scenes so the existing TTS routing only runs
for speaking scenes. The `audio::AudioGraph` type is already visible via the
`#include "audio/AudioGraph.h"` in `PluginProcessor.h`.

- [ ] **Step 2: Build + verify no regressions**

Run: `cmake --build build --target guitar_dsp_app_Standalone guitar_dsp_tests && ctest --test-dir build --output-on-failure 2>&1 | tail -3`
Expected: builds; full suite green (speaking scenes 6–8 still route through the vocoder; clean/panic unaffected because their `carousel.enabled` is false and they have no TTS).

- [ ] **Step 3: Commit**

```bash
git add src/app/PluginProcessor.cpp
git commit -m "feat(app): push carousel config + select wet source on scene change"
```

---

## Task 13: Author the 5 carousel preset JSON files

**Files:**
- Modify: `assets/scenes/01_carousel_organ.json`
- Rename + modify: `assets/scenes/02_carousel_piano.json` → `assets/scenes/02_carousel_distortion.json`
- Modify: `assets/scenes/03_carousel_synth.json`
- Modify: `assets/scenes/04_carousel_8bit.json`
- Rename + modify: `assets/scenes/05_carousel_choir.json` → `assets/scenes/05_carousel_autowah.json`

- [ ] **Step 1: Rename the two repurposed files**

```bash
git mv assets/scenes/02_carousel_piano.json assets/scenes/02_carousel_distortion.json
git mv assets/scenes/05_carousel_choir.json assets/scenes/05_carousel_autowah.json
```

- [ ] **Step 2: Write `assets/scenes/01_carousel_organ.json`** (Hammond/Leslie)

```json
{
  "id": 1,
  "name": "Carousel — organ / Leslie",
  "color": "#e69e3c",
  "mixer": { "masterGainDb": -3.0, "dryWet": 1.0, "transitionMs": 25 },
  "carousel": {
    "enabled": true,
    "drive": 8.0,
    "waveshaper": { "type": "tanh", "amount": 1.4 },
    "filter": { "mode": "lowpass", "cutoffHz": 3200, "resonance": 0.2, "mod": "static" },
    "chorus": { "rateHz": 6.0, "depth": 0.6, "mix": 0.6 },
    "reverb": { "roomSize": 0.3, "wet": 0.15 },
    "outputTrimDb": -2.0
  }
}
```

- [ ] **Step 3: Write `assets/scenes/02_carousel_distortion.json`** (distorted guitar)

```json
{
  "id": 2,
  "name": "Carousel — distorted guitar",
  "color": "#d6453c",
  "mixer": { "masterGainDb": -5.0, "dryWet": 1.0, "transitionMs": 20 },
  "carousel": {
    "enabled": true,
    "drive": 18.0,
    "waveshaper": { "type": "hardclip", "amount": 1.0 },
    "filter": { "mode": "lowpass", "cutoffHz": 3000, "resonance": 0.2, "mod": "static" },
    "reverb": { "roomSize": 0.2, "wet": 0.08 },
    "outputTrimDb": -4.0
  }
}
```

- [ ] **Step 4: Write `assets/scenes/03_carousel_synth.json`** (synth lead)

```json
{
  "id": 3,
  "name": "Carousel — synth lead",
  "color": "#5cd1c4",
  "mixer": { "masterGainDb": -5.0, "dryWet": 1.0, "transitionMs": 25 },
  "carousel": {
    "enabled": true,
    "drive": 10.0,
    "waveshaper": { "type": "tanh", "amount": 1.6 },
    "filter": { "mode": "lowpass", "cutoffHz": 700, "resonance": 0.75,
                "mod": "lfo", "lfoHz": 0.5 },
    "chorus": { "rateHz": 3.0, "depth": 0.4, "mix": 0.4 },
    "reverb": { "roomSize": 0.5, "wet": 0.25 },
    "outputTrimDb": -3.0
  }
}
```

- [ ] **Step 5: Write `assets/scenes/04_carousel_8bit.json`** (8-bit chiptune)

```json
{
  "id": 4,
  "name": "Carousel — 8-bit",
  "color": "#f25e8e",
  "mixer": { "masterGainDb": -6.0, "dryWet": 1.0, "transitionMs": 15 },
  "carousel": {
    "enabled": true,
    "drive": 4.0,
    "waveshaper": { "type": "hardclip", "amount": 0.9 },
    "crusher": { "bits": 4, "downsample": 8 },
    "outputTrimDb": -3.0
  }
}
```

- [ ] **Step 6: Write `assets/scenes/05_carousel_autowah.json`** (auto-wah)

```json
{
  "id": 5,
  "name": "Carousel — auto-wah",
  "color": "#9bd14a",
  "mixer": { "masterGainDb": -4.0, "dryWet": 1.0, "transitionMs": 20 },
  "carousel": {
    "enabled": true,
    "drive": 6.0,
    "waveshaper": { "type": "tanh", "amount": 1.2 },
    "filter": { "mode": "bandpass", "cutoffHz": 350, "resonance": 0.8,
                "mod": "envelope", "envAmount": 2200 },
    "outputTrimDb": -2.0
  }
}
```

- [ ] **Step 7: Validate JSON + rebuild (asset copy picks up renames/edits)**

```bash
for f in assets/scenes/0[1-5]_*.json; do python3 -c "import json,sys; json.load(open('$f'))" && echo "ok $f"; done
cmake --build build --target guitar_dsp_app_Standalone
```
Expected: 5 "ok" lines; clean build (the CMake asset-copy step re-copies the renamed/edited scenes into the .app bundle).

- [ ] **Step 8: Commit**

```bash
git add assets/scenes/01_carousel_organ.json assets/scenes/02_carousel_distortion.json \
        assets/scenes/03_carousel_synth.json assets/scenes/04_carousel_8bit.json \
        assets/scenes/05_carousel_autowah.json
git commit -m "feat(scenes): author 5 carousel presets (organ, distortion, synth, 8-bit, auto-wah)"
```

---

## Task 14: Scene-switch integration test (TDD)

**Files:**
- Create: `tests/integration/test_carousel_scene.cpp`
- Modify: `tests/CMakeLists.txt`

Drives `SceneEngine` + `AudioGraph` together: load the real carousel scenes,
activate each, and assert the output differs from clean and stays finite. This
mirrors `tests/integration/test_scene_switch.cpp` (read it for the wiring
pattern — how it builds scenes and pumps `AudioGraph::process`).

- [ ] **Step 1: Write the test** `tests/integration/test_carousel_scene.cpp`

```cpp
#include <catch2/catch_test_macros.hpp>

#include "audio/AudioGraph.h"
#include "scenes/Scene.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::AudioGraph;
using guitar_dsp::scenes::CarouselConfig;

namespace {
std::vector<float> pluckish(int n) {
    std::vector<float> b(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float amp = 0.8f * std::exp(-i / 6000.0f);
        b[static_cast<size_t>(i)] = amp * std::sin(2.0f*3.14159265f*146.83f*i/48000.0f);
    }
    return b;
}
float energy(const std::vector<float>& x) {
    double a = 0.0; for (float v : x) a += static_cast<double>(v)*v;
    return static_cast<float>(a);
}
}

TEST_CASE("integration: each carousel preset transforms the guitar, stays finite",
          "[integration][carousel]") {
    AudioGraph g;
    g.prepare(48000.0, 1024);

    auto in = pluckish(1024);

    // Baseline: clean (dry) output.
    g.setWetSource(AudioGraph::WetSource::Vocoder);
    g.mixer().setDryWet(0.0f); g.mixer().setMasterGainDb(0.0f); g.mixer().reset();
    std::vector<float> clean(1024, 0.0f);
    g.process(in.data(), clean.data(), clean.size());

    struct Preset { CarouselConfig cfg; const char* name; };
    std::vector<Preset> presets;
    {
        CarouselConfig c; c.enabled = true; c.crusherBits = 4; c.crusherDownsample = 8;
        c.shaper = CarouselConfig::Shaper::HardClip; presets.push_back({c, "8bit"});
    }
    {
        CarouselConfig c; c.enabled = true; c.drive = 18.0f;
        c.shaper = CarouselConfig::Shaper::HardClip; presets.push_back({c, "distortion"});
    }
    {
        CarouselConfig c; c.enabled = true; c.filterMode = CarouselConfig::FilterMode::BandPass;
        c.filterMod = CarouselConfig::FilterMod::Envelope; c.filterCutoffHz = 350.0f;
        c.filterResonance = 0.8f; c.filterEnvAmount = 2200.0f; presets.push_back({c, "autowah"});
    }

    for (auto& p : presets) {
        g.carousel().setConfig(p.cfg);
        g.setWetSource(AudioGraph::WetSource::Carousel);
        g.mixer().setDryWet(1.0f); g.mixer().reset();

        std::vector<float> out(1024, 0.0f);
        g.process(in.data(), out.data(), out.size());   // pick up config
        g.process(in.data(), out.data(), out.size());

        for (float x : out) REQUIRE(std::isfinite(x));
        INFO("preset = " << p.name);
        REQUIRE(energy(out) != energy(clean));   // it changed the signal
    }
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`** — append `integration/test_carousel_scene.cpp`.

- [ ] **Step 3: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "each carousel preset"`
Expected: PASS.

Full suite check: `ctest --test-dir build --output-on-failure 2>&1 | tail -3` → all green.

- [ ] **Step 4: Commit**

```bash
git add tests/integration/test_carousel_scene.cpp tests/CMakeLists.txt
git commit -m "test(integration): carousel presets transform guitar + stay finite"
```

---

## Task 15: README — document the Instrument Carousel

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Replace the "Project status" section** so it describes Phase 4. Keep the existing build/test/Piper sections above it untouched; replace the `## Project status` block with:

```markdown
## Project status

This branch implements **Phase 4: Instrument Carousel A**. Carousel scenes 1–5
now process the live guitar through a pedal-style effects chain (`audio::Carousel`)
— no MIDI notes, no pitch-shifting; the effect only reshapes timbre:

| Key | Scene | Effect |
|-----|-------|--------|
| `2` | 1 | Organ / Leslie (tanh warmth + chorus + light reverb) |
| `3` | 2 | Distorted guitar (drive + hard clip + tone filter) |
| `4` | 3 | Synth lead (LFO-swept resonant filter + chorus + reverb) |
| `5` | 4 | 8-bit chiptune (bit crusher + sample-rate reducer) |
| `6` | 5 | Auto-wah (envelope-following resonant bandpass) |

The chain is built from `juce::dsp` modules (`WaveShaper`,
`StateVariableTPTFilter`, `Chorus`, `Reverb`) plus bespoke `Crusher` and
modulation stages. Each scene's `carousel` JSON block parameterizes the chain;
absent sub-blocks bypass that stage. `AudioGraph` routes instrument scenes
through the carousel and speaking scenes through the vocoder via a wet-source
selector.

Piano and choir/pad (which need pitch-shifting/harmonization) are deferred to
**Phase 4b**.

### Subsequent phases (see plans directory)

- **Phase 4b**: pitch/harmony instruments — pitch shifter, harmonizer, formant
  shifter (piano, choir/pad).
- **Phase 5**: Full-screen visualization (spectrogram, karaoke text overlay).
- **Phase 6**: Hardening + dress rehearsal.
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: README documents Phase 4 Instrument Carousel"
```

---

## Task 16: Phase 4 wrap-up verification

- [ ] **Step 1: Clean build from scratch**

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
Expected: clean.

- [ ] **Step 2: Full test pass**

```bash
ctest --test-dir build --output-on-failure
```
Expected: all pass; the 4 pre-existing opt-in tests skipped. New count ≈ 91 + ~14 carousel tests.

- [ ] **Step 3: Manual smoke** (UI / audio — requires a guitar or input signal)

```bash
open "build/src/app/guitar_dsp_app_artefacts/Release/Standalone/Guitar DSP.app"
```
Verify by ear / metering with a signal at the input:
- Keys `2`–`6` (scenes 1–5) each clearly change the timbre (8-bit grit, distortion, filter sweep, auto-wah, organ shimmer).
- Key `1` (scene 0) = clean. Keys `7`–`9` (speaking scenes) still vocode TTS. Key `0` (panic) = clean.
- No clicks/pops on scene switches; no crashes when switching rapidly.

State explicitly in the report whether audio was actually verified or only the build/tests (no input signal available in CI).

- [ ] **Step 4: Final commit if any cleanup was needed**

```bash
git commit -am "chore: phase 4 wrap-up fixes"   # only if something changed
```

---

## Self-review

**Spec coverage (spec § → task):**
- §2 five patches → Task 13 (presets) + Tasks 6–9 (the stages each needs).
- §3 DSP stage chain → Task 3 (crusher), Task 4 (mod), Tasks 5–10 (Carousel chain).
- §3 juce::dsp + juce_dsp link → Task 5.
- §4 signal routing / wet-source selector → Task 11 (AudioGraph) + Task 12 (PluginProcessor).
- §5 `carousel` JSON block → Task 1 (parser) + Task 13 (files).
- §6 testing (per-stage, RT-safety, integrity, scene-switch) → Tasks 3,4,6–10 (unit), Task 5/10 (RT + integrity), Task 14 (integration).
- §7 out-of-scope (pitch/harmony, UI knobs, viz) → not implemented; deferred notes in README (Task 15).

**Placeholder scan:** No "TBD"/"add error handling"/"similar to Task N" — every code step shows complete code.

**Type consistency:**
- `scenes::CarouselConfig` with nested enums `Shaper{None,Tanh,HardClip,Foldback}`, `FilterMode{Off,LowPass,BandPass,HighPass}`, `FilterMod{Static,Envelope,Lfo}` — defined Task 1, used identically in Tasks 2, 5–11, 13, 14.
- `audio::Crusher` (`setBits`, `setDownsample`, `processSample`, `reset`) — Task 3, used Task 5/7.
- `audio::EnvelopeFollower` (`prepare`, `processSample`, `reset`), `audio::Lfo` (`prepare`, `setRateHz`, `processSample`, `reset`) — Task 4, used Task 8.
- `audio::Carousel` (`prepare`, `reset`, `setConfig`, `process`) — Task 5, used Tasks 11, 12, 14.
- `AudioGraph::carousel()`, `AudioGraph::WetSource{Vocoder,Carousel}`, `AudioGraph::setWetSource()` — Task 11, used Tasks 12, 14.
- `SceneEngine::activeCarouselConfig()` — Task 2, used Task 12.

---

## Execution handoff

Plan complete. Two execution options:

1. **Subagent-Driven (recommended)** — fresh subagent per task, two-stage review between tasks.
2. **Inline Execution** — batch execution with checkpoints.
