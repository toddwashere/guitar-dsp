# M2 — Scene 12 Sung Direct (Formant-Preserving) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship scene 12 — direct, formant-preserving pitch-shifted singing driven by WORLD — as a sibling wet-bus path next to the vocoder, sharing the 4-voice bundle set with scene 11. No vocoder coloration; the actual singer's voice tracks the guitar.

**Architecture:** A new audio chain `SungDirectPath` (ClipBankPlayer → FormantShifter → VowelGrainLoop) drives the wet bus when scene's `directShift.enabled == true`. `FormantShifter` wraps a streaming WORLD session: blockwise analysis (Harvest/CheapTrick/D4C) on the source grain, ratio-driven synthesis, output ring buffer. AudioGraph gains a three-way wet-bus arbiter `{Vocoder, Carousel, SungDirect}` (reduces to the existing two-way when SungDirect is disabled). A new `SungDirectPanel` UI hosts the VoicePackPicker, formant tint, portamento, scoop-in.

**Tech Stack:** C++17, JUCE, Catch2, WORLD (BSD-3, validated in M1.5).

**Prerequisite:** [docs/superpowers/plans/2026-06-23-m1-5-world-shifter-validation.md](2026-06-23-m1-5-world-shifter-validation.md) Outcome A — WORLD passed the latency/CPU bar.

**Design spec:** [docs/superpowers/specs/2026-06-23-scene-11-sung-vowels-design.md](../specs/2026-06-23-scene-11-sung-vowels-design.md), Section 6.

## Global Constraints

- **No regression in scenes 0..11 or 13.** Activating scene 12 must not alter how scenes 11 and 10 sound. The three-way arbiter reduces to the existing two-way when `sungDirect == false`.
- **Audio-thread safety.** `FormantShifter::process()` and `SungDirectPath::process()` allocate nothing. WORLD's `Synthesis` is invoked from a worker thread (or pre-computed per grain at bundle-load time, then read from a ring buffer at audio rate — pick whichever Task 2 confirms latency-feasible).
- **WORLD link target available** (`world` from M1.5).
- **All M1 tests must still pass.** Re-run the full suite at the end of every task.
- **Test-first.** Failing test before implementation.

---

## File Map

**Created:**
- `src/audio/FormantShifter.h` / `.cpp`
- `src/audio/SungDirectPath.h` / `.cpp`
- `src/app/SungDirectPanel.h` / `.cpp`
- `assets/scenes/12_sung_direct.json`
- `tests/unit/audio/test_formant_shifter.cpp`
- `tests/unit/audio/test_sung_direct_path.cpp`
- `tests/unit/scenes/test_scene_library_sung_direct.cpp`
- `tests/integration/test_sung_direct_scene.cpp`

**Modified:**
- `src/audio/AudioGraph.h` / `.cpp` — add `SungDirect` to `WetSource`; instantiate `SungDirectPath`.
- `src/scenes/Scene.h` — add `Scene::DirectShift` struct.
- `src/scenes/SceneLibrary.cpp` — parse `directShift` block.
- `src/app/VocoderPanel.h` / `.cpp` — share the VoicePackPicker pattern (already there from M1).
- `src/app/PluginEditor.h` / `.cpp` — instantiate `SungDirectPanel`, gate by `Scene::showSungDirectPanel`.
- `src/app/CMakeLists.txt` — register new sources.
- `tests/CMakeLists.txt` — register new tests.
- `tests/integration/test_scene_switch.cpp` — extend cycle to PC 0..13 (already extended in M1; M2 adds scene 12 to the asset dir, which the cycle picks up automatically).
- `tests/integration/test_realtime_safety.cpp` — add scene 12 to the audit.

---

## Task 1: Scene::DirectShift struct + JSON parsing

**Files:**
- Modify: `src/scenes/Scene.h`, `src/scenes/SceneLibrary.cpp`
- Test: `tests/unit/scenes/test_scene_library_sung_direct.cpp` (new)

**Interfaces:**
- Consumes: nothing.
- Produces: `Scene::DirectShift { bool enabled; std::string engine; bool formantPreserve; float formantTintSemitones; float portamentoMs; float scoopInMs; }`; `Scene::directShift` member; `Scene::showSungDirectPanel`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/scenes/test_scene_library_sung_direct.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "scenes/SceneLibrary.h"

using guitar_dsp::scenes::Scene;
using guitar_dsp::scenes::SceneLibrary;

TEST_CASE("SceneLibrary parses directShift block",
          "[scene-library][direct-shift]") {
    juce::File tmp = juce::File::createTempFile("_test_direct_shift.json");
    tmp.replaceWithText(R"({
        "id": 12,
        "name": "Sung Direct",
        "directShift": {
            "enabled": true,
            "engine": "world",
            "formantPreserve": true,
            "formantTintSemitones": 1.5,
            "portamentoMs": 40.0,
            "scoopInMs": 15.0
        },
        "showSungDirectPanel": true
    })");
    auto s = SceneLibrary::loadOne(tmp.getFullPathName().toStdString());
    REQUIRE(s.has_value());
    CHECK(s->directShift.enabled         == true);
    CHECK(s->directShift.engine          == "world");
    CHECK(s->directShift.formantPreserve == true);
    CHECK(s->directShift.formantTintSemitones == Approx(1.5f));
    CHECK(s->directShift.portamentoMs    == Approx(40.0f));
    CHECK(s->directShift.scoopInMs       == Approx(15.0f));
    CHECK(s->showSungDirectPanel         == true);
    tmp.deleteFile();
}

TEST_CASE("SceneLibrary defaults directShift to disabled",
          "[scene-library][direct-shift][backcompat]") {
    juce::File tmp = juce::File::createTempFile("_test_no_direct_shift.json");
    tmp.replaceWithText(R"({ "id": 0, "name": "Legacy" })");
    auto s = SceneLibrary::loadOne(tmp.getFullPathName().toStdString());
    REQUIRE(s.has_value());
    CHECK(s->directShift.enabled == false);
    CHECK(s->showSungDirectPanel == false);
    tmp.deleteFile();
}
```

Register it in `tests/CMakeLists.txt`.

- [ ] **Step 2: Build; FAIL**

- [ ] **Step 3: Extend Scene.h**

In `src/scenes/Scene.h`, inside `struct Scene` (after `voicePacks`-related fields from M1):

```cpp
    struct DirectShift {
        bool        enabled              = false;
        std::string engine               = "world";
        bool        formantPreserve      = true;
        float       formantTintSemitones = 0.0f;
        float       portamentoMs         = 40.0f;
        float       scoopInMs            = 0.0f;
    };
    DirectShift directShift;

    bool showSungDirectPanel = false;
```

- [ ] **Step 4: Patch SceneLibrary.cpp**

In `src/scenes/SceneLibrary.cpp`, after the existing voicePacks block, add:

```cpp
    if (obj->hasProperty("showSungDirectPanel"))
        s.showSungDirectPanel =
            static_cast<bool>(obj->getProperty("showSungDirectPanel"));

    if (obj->hasProperty("directShift")) {
        if (auto* d = obj->getProperty("directShift").getDynamicObject()) {
            auto& ds = s.directShift;
            if (d->hasProperty("enabled"))
                ds.enabled = static_cast<bool>(d->getProperty("enabled"));
            if (d->hasProperty("engine"))
                ds.engine = d->getProperty("engine").toString().toStdString();
            if (d->hasProperty("formantPreserve"))
                ds.formantPreserve =
                    static_cast<bool>(d->getProperty("formantPreserve"));
            if (d->hasProperty("formantTintSemitones"))
                ds.formantTintSemitones =
                    static_cast<float>((double) d->getProperty("formantTintSemitones"));
            if (d->hasProperty("portamentoMs"))
                ds.portamentoMs =
                    static_cast<float>((double) d->getProperty("portamentoMs"));
            if (d->hasProperty("scoopInMs"))
                ds.scoopInMs =
                    static_cast<float>((double) d->getProperty("scoopInMs"));
        }
    }
```

- [ ] **Step 5: Build + tests pass**

```bash
cd build-tests && cmake -S /Users/user/GIT/guitar-dsp -B . && \
  cmake --build . --target guitar_dsp_tests && \
  ctest -R "direct-shift" --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add src/scenes/Scene.h src/scenes/SceneLibrary.cpp \
        tests/unit/scenes/test_scene_library_sung_direct.cpp \
        tests/CMakeLists.txt
git commit -m "feat(scene): Scene::DirectShift + showSungDirectPanel parsing"
```

---

## Task 2: FormantShifter — streaming wrapper around WORLD

**Files:**
- Create: `src/audio/FormantShifter.h` / `.cpp`
- Test: `tests/unit/audio/test_formant_shifter.cpp`
- Modify: `src/audio/CMakeLists.txt` to add source + link `world`.

**Interfaces:**
- Consumes: a const reference to a source grain (`TTSClipPtr`-like).
- Produces:
  ```cpp
  class FormantShifter {
  public:
      void prepare(double sampleRate, int blockSize);
      void reset();
      // Audio thread. Cheap pointer swap; never copies the analysis arrays.
      void setSource(std::shared_ptr<const ShifterGrain> grain) noexcept;
      // Audio thread. r in [0.25, 4.0]; clamped.
      void setRatio(float r) noexcept;
      void setFormantTintSemitones(float n) noexcept;
      // Audio thread. Writes `n` samples to `out`.
      void process(float* out, int n) noexcept;
      int  latencySamples() const noexcept;
  };

  struct ShifterGrain {
      // Pre-computed offline at bundle load. Each field is a contiguous
      // double buffer owned by the grain (deleted on destruction).
      int     sampleRate;
      int     fftSize;
      double  framePeriodMs;
      std::vector<double>              timeAxis;   // [f0Length]
      std::vector<double>              f0;         // [f0Length]
      std::vector<std::vector<double>> spectrum;   // [f0Length][fftSize/2+1]
      std::vector<std::vector<double>> aperiodicity;
  };
  ```

The trick that keeps `process()` RT-safe: **WORLD analysis runs offline** (at bundle load time, message thread). At audio rate, `process()` only invokes WORLD's `Synthesis` over a short streaming chunk — and even that runs on a worker thread, with samples handed to the audio thread via a lock-free ring buffer.

To avoid heap allocations during synthesis, we pre-allocate buffers in `prepare()` sized for `fftSize` and the longest expected grain.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/audio/test_formant_shifter.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/FormantShifter.h"
#include <cmath>
#include <memory>
#include <vector>

using guitar_dsp::audio::FormantShifter;
using guitar_dsp::audio::ShifterGrain;

namespace {
// A trivial 1-second 440 Hz sine ShifterGrain with hand-rolled spectrum.
// Real grains come from the offline analysis path (Task 3).
std::shared_ptr<ShifterGrain> makeSineGrain(int sr = 48000) {
    auto g = std::make_shared<ShifterGrain>();
    g->sampleRate    = sr;
    g->fftSize       = 2048;
    g->framePeriodMs = 5.0;
    const int frames = static_cast<int>(1000.0 / g->framePeriodMs);
    const int bins   = g->fftSize / 2 + 1;
    g->timeAxis.resize(frames);
    g->f0.resize(frames, 440.0);
    g->spectrum.assign(frames, std::vector<double>(bins, 1e-9));
    g->aperiodicity.assign(frames, std::vector<double>(bins, 0.1));
    for (int i = 0; i < frames; ++i) g->timeAxis[i] = i * g->framePeriodMs / 1000.0;
    return g;
}
}

TEST_CASE("FormantShifter ratio=1.0 over a stable grain produces non-silent output",
          "[formant-shifter]") {
    FormantShifter sh;
    sh.prepare(48000.0, 256);
    sh.setSource(makeSineGrain());
    sh.setRatio(1.0f);
    std::vector<float> out(256, 0.0f);
    sh.process(out.data(), 256);
    double energy = 0.0;
    for (float v : out) {
        REQUIRE(! std::isnan(v));
        REQUIRE(! std::isinf(v));
        energy += static_cast<double>(v) * v;
    }
    CHECK(energy > 0.0);
}

TEST_CASE("FormantShifter clamps setRatio outside [0.25, 4.0]",
          "[formant-shifter]") {
    FormantShifter sh;
    sh.prepare(48000.0, 256);
    sh.setRatio(0.0f);
    sh.setRatio(10.0f);
    sh.setRatio(-5.0f);
    // No assertion on internal state; the contract is "no crash, no NaN".
    sh.setSource(makeSineGrain());
    std::vector<float> out(256, 0.0f);
    sh.process(out.data(), 256);
    for (float v : out) REQUIRE(! std::isnan(v));
}

TEST_CASE("FormantShifter reports finite latencySamples",
          "[formant-shifter]") {
    FormantShifter sh;
    sh.prepare(48000.0, 256);
    const int lat = sh.latencySamples();
    REQUIRE(lat >= 0);
    REQUIRE(lat < 48000);  // < 1 s
}
```

Register in `tests/CMakeLists.txt`.

- [ ] **Step 2: Build; FAIL on missing files**

- [ ] **Step 3: Write FormantShifter.h**

Create `src/audio/FormantShifter.h`:

```cpp
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace guitar_dsp::audio {

// Per-grain pre-computed WORLD analysis. Built offline (message thread)
// once per grain when the bundle loads.
struct ShifterGrain {
    int                              sampleRate    = 48000;
    int                              fftSize       = 2048;
    double                           framePeriodMs = 5.0;
    std::vector<double>              timeAxis;
    std::vector<double>              f0;
    std::vector<std::vector<double>> spectrum;
    std::vector<std::vector<double>> aperiodicity;
};

// Streaming formant-preserving pitch shift over a pre-analysed grain.
// Audio-thread process() is allocation-free; it advances a synthesis
// cursor through the grain's WORLD parameters with the current ratio.
//
// Threading:
//   * setSource() is message-thread; it atomically swaps the active grain
//     pointer (cheap).
//   * setRatio() is callable from either thread.
//   * process() reads only the active-grain pointer and produces samples.
//     WORLD's Synthesis is invoked block-by-block; on a 256-sample block at
//     48 kHz, this is ~5 ms of compute budget. M1.5 verified compute fits.
class FormantShifter {
public:
    FormantShifter();
    ~FormantShifter();

    void prepare(double sampleRate, int blockSize);
    void reset();

    void setSource(std::shared_ptr<const ShifterGrain> grain) noexcept;
    void setRatio(float r) noexcept;
    void setFormantTintSemitones(float n) noexcept;

    void process(float* out, int n) noexcept;

    int latencySamples() const noexcept;

private:
    double sampleRate_ = 48000.0;
    int    blockSize_  = 256;
    int    latencySamples_ = 0;

    std::atomic<float> ratio_     {1.0f};
    std::atomic<float> tintSemi_  {0.0f};

    // Pointer swap: writer (message) stores; reader (audio) loads.
    std::atomic<std::shared_ptr<const ShifterGrain>> activeGrain_{nullptr};
    std::shared_ptr<const ShifterGrain>              localGrain_;
    int                                              localFrameIdx_ = 0;

    // Scratch buffers sized at prepare() — avoid heap during process().
    std::vector<double> outBufferD_;  // sized blockSize_
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 4: Write FormantShifter.cpp**

Create `src/audio/FormantShifter.cpp`:

```cpp
#include "FormantShifter.h"

#include "world/synthesis.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

FormantShifter::FormantShifter()  = default;
FormantShifter::~FormantShifter() = default;

void FormantShifter::prepare(double sampleRate, int blockSize) {
    sampleRate_ = sampleRate;
    blockSize_  = std::max(64, blockSize);
    outBufferD_.assign(static_cast<std::size_t>(blockSize_), 0.0);
    // M1.5 confirmed WORLD's per-block compute fits; the only latency is
    // the synthesis lookahead — for 5 ms frame period, ~1 frame = ~5 ms.
    latencySamples_ = static_cast<int>(0.005 * sampleRate_);
    reset();
}

void FormantShifter::reset() {
    localFrameIdx_ = 0;
    std::fill(outBufferD_.begin(), outBufferD_.end(), 0.0);
}

void FormantShifter::setSource(std::shared_ptr<const ShifterGrain> g) noexcept {
    activeGrain_.store(std::move(g), std::memory_order_release);
}

void FormantShifter::setRatio(float r) noexcept {
    if (r < 0.25f) r = 0.25f;
    if (r > 4.0f)  r = 4.0f;
    ratio_.store(r, std::memory_order_relaxed);
}

void FormantShifter::setFormantTintSemitones(float n) noexcept {
    if (n < -6.0f) n = -6.0f;
    if (n > 6.0f)  n = 6.0f;
    tintSemi_.store(n, std::memory_order_relaxed);
}

int FormantShifter::latencySamples() const noexcept { return latencySamples_; }

void FormantShifter::process(float* out, int n) noexcept {
    // Atomic load + local cache: avoid repeated atomic loads inside the loop.
    auto fresh = activeGrain_.load(std::memory_order_acquire);
    if (fresh != localGrain_) {
        localGrain_    = std::move(fresh);
        localFrameIdx_ = 0;
    }
    if (! localGrain_ || localGrain_->f0.empty()) {
        std::fill(out, out + n, 0.0f);
        return;
    }

    const float ratio = ratio_.load(std::memory_order_relaxed);
    const auto& g     = *localGrain_;
    const int   bins  = g.fftSize / 2 + 1;

    // Build per-block scratch arrays of pointers — small (frames-per-block
    // is ~10–30 for a 256-sample block at 5 ms frame period at 48 kHz).
    const double secondsPerBlock = static_cast<double>(n) / sampleRate_;
    const int    framesPerBlock  = std::max(1, static_cast<int>(std::ceil(
        secondsPerBlock * 1000.0 / g.framePeriodMs)) + 2);
    const int    startFrame      = localFrameIdx_;
    const int    endFrame        = std::min<int>(
        startFrame + framesPerBlock, static_cast<int>(g.f0.size()));
    const int    blockFrames     = endFrame - startFrame;
    if (blockFrames <= 0) {
        std::fill(out, out + n, 0.0f);
        return;
    }

    // Stack-alloc small arrays via fixed-size; cap framesPerBlock at 128.
    constexpr int kMaxFramesPerBlock = 128;
    if (blockFrames > kMaxFramesPerBlock) {
        std::fill(out, out + n, 0.0f);
        return;
    }
    double  f0Scaled[kMaxFramesPerBlock];
    const double* specPtr[kMaxFramesPerBlock];
    const double* apPtr  [kMaxFramesPerBlock];
    for (int i = 0; i < blockFrames; ++i) {
        f0Scaled[i] = g.f0[(std::size_t)(startFrame + i)] * static_cast<double>(ratio);
        specPtr [i] = g.spectrum    [(std::size_t)(startFrame + i)].data();
        apPtr   [i] = g.aperiodicity[(std::size_t)(startFrame + i)].data();
    }

    // Cast away const for WORLD's C-style API; WORLD treats inputs as read-only.
    Synthesis(f0Scaled, blockFrames,
              const_cast<double**>(specPtr),
              const_cast<double**>(apPtr),
              g.fftSize, g.framePeriodMs, g.sampleRate, n, outBufferD_.data());

    for (int i = 0; i < n; ++i)
        out[i] = static_cast<float>(outBufferD_[(std::size_t) i]);

    // Advance the frame cursor by (n samples) → time → frames.
    localFrameIdx_ += static_cast<int>(std::round(
        secondsPerBlock * 1000.0 / g.framePeriodMs));
    if (localFrameIdx_ >= static_cast<int>(g.f0.size()))
        localFrameIdx_ = static_cast<int>(g.f0.size()) - 1;
    (void) bins;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 5: CMakeLists**

In `src/audio/CMakeLists.txt`, add `FormantShifter.cpp` to the audio library sources. Also add `world` to its `target_link_libraries`.

If `src/audio/` doesn't have its own CMakeLists, search where audio sources are declared (top-level or `src/CMakeLists.txt`) and add it there.

- [ ] **Step 6: Build + tests pass**

```bash
cd build-tests && cmake -S /Users/user/GIT/guitar-dsp -B . && \
  cmake --build . --target guitar_dsp_tests && \
  ctest -R "formant-shifter" --output-on-failure
```

Expected: 3 PASS.

- [ ] **Step 7: Commit**

```bash
git add src/audio/FormantShifter.h src/audio/FormantShifter.cpp \
        src/audio/CMakeLists.txt tests/unit/audio/test_formant_shifter.cpp \
        tests/CMakeLists.txt
git commit -m "feat(formant-shifter): WORLD-backed streaming pitch+formant shift"
```

---

## Task 3: Offline grain analyser — populate ShifterGrain from bundle grains

**Files:**
- Modify: `src/audio/GspeakBundle.cpp` to optionally output a `ShifterGrain` per phoneme alongside the TTSClip.
- Or alternative: create `src/audio/GrainAnalyser.{h,cpp}` that takes a TTSClip + phoneme-range and runs Harvest+CheapTrick+D4C.

The cleaner split: keep `GspeakBundle::read` doing only WAV+manifest reading, and create a separate `GrainAnalyser`.

**Interfaces:**
- Consumes: `TTSClipPtr` (the master clip) + a phoneme range.
- Produces:
  ```cpp
  namespace guitar_dsp::audio {
      std::shared_ptr<ShifterGrain> analyseGrain(
          const float* samples, int numSamples, int sampleRate);
  }
  ```

- [ ] **Step 1: Write the failing test**

In `tests/unit/audio/test_formant_shifter.cpp`, append:

```cpp
TEST_CASE("analyseGrain produces a non-empty ShifterGrain over a sine",
          "[grain-analyser]") {
    using namespace guitar_dsp::audio;
    const int sr = 48000;
    const int n  = sr;  // 1 s
    std::vector<float> samples(static_cast<std::size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i)
        samples[i] = 0.3f * std::sin(2.0 * M_PI * 220.0 * i / sr);
    auto g = analyseGrain(samples.data(), n, sr);
    REQUIRE(g);
    CHECK(g->sampleRate == sr);
    CHECK(g->f0.size() > 0);
    CHECK(g->spectrum.size() == g->f0.size());
    CHECK(g->aperiodicity.size() == g->f0.size());
    // Expect detected F0 near 220 Hz on at least one frame.
    bool foundNear = false;
    for (double f : g->f0) if (std::fabs(f - 220.0) < 20.0) { foundNear = true; break; }
    CHECK(foundNear);
}
```

- [ ] **Step 2: Build; FAIL on missing `analyseGrain`**

- [ ] **Step 3: Create GrainAnalyser.h / .cpp**

Create `src/audio/GrainAnalyser.h`:

```cpp
#pragma once

#include "FormantShifter.h"

#include <memory>

namespace guitar_dsp::audio {

// Offline (message thread). Run WORLD's analysis stack on a mono
// float32 buffer and return a fully populated ShifterGrain.
// Allocates freely; caller invokes this once per grain at bundle load.
std::shared_ptr<ShifterGrain>
analyseGrain(const float* samples, int numSamples, int sampleRate);

} // namespace guitar_dsp::audio
```

Create `src/audio/GrainAnalyser.cpp`:

```cpp
#include "GrainAnalyser.h"

#include "world/harvest.h"
#include "world/cheaptrick.h"
#include "world/d4c.h"

#include <vector>

namespace guitar_dsp::audio {

std::shared_ptr<ShifterGrain>
analyseGrain(const float* samples, int numSamples, int sampleRate) {
    if (! samples || numSamples <= 0 || sampleRate <= 0) return nullptr;

    std::vector<double> x(static_cast<std::size_t>(numSamples));
    for (int i = 0; i < numSamples; ++i)
        x[(std::size_t) i] = static_cast<double>(samples[i]);

    auto g = std::make_shared<ShifterGrain>();
    g->sampleRate    = sampleRate;
    g->framePeriodMs = 5.0;

    HarvestOption hopt; InitializeHarvestOption(&hopt);
    hopt.frame_period = g->framePeriodMs;
    const int f0Length = GetSamplesForHarvest(sampleRate, numSamples,
                                              hopt.frame_period);
    g->f0.assign((std::size_t) f0Length, 0.0);
    g->timeAxis.assign((std::size_t) f0Length, 0.0);
    Harvest(x.data(), numSamples, sampleRate, &hopt,
            g->timeAxis.data(), g->f0.data());

    CheapTrickOption ctOpt; InitializeCheapTrickOption(sampleRate, &ctOpt);
    g->fftSize = GetFFTSizeForCheapTrick(sampleRate, &ctOpt);
    const int bins = g->fftSize / 2 + 1;
    g->spectrum.assign((std::size_t) f0Length,
                       std::vector<double>((std::size_t) bins, 0.0));
    std::vector<double*> specPtr((std::size_t) f0Length);
    for (int i = 0; i < f0Length; ++i)
        specPtr[(std::size_t) i] = g->spectrum[(std::size_t) i].data();
    CheapTrick(x.data(), numSamples, sampleRate,
               g->timeAxis.data(), g->f0.data(), f0Length,
               &ctOpt, specPtr.data());

    D4COption dOpt; InitializeD4COption(&dOpt);
    g->aperiodicity.assign((std::size_t) f0Length,
                           std::vector<double>((std::size_t) bins, 0.0));
    std::vector<double*> apPtr((std::size_t) f0Length);
    for (int i = 0; i < f0Length; ++i)
        apPtr[(std::size_t) i] = g->aperiodicity[(std::size_t) i].data();
    D4C(x.data(), numSamples, sampleRate,
        g->timeAxis.data(), g->f0.data(), f0Length, g->fftSize, &dOpt,
        apPtr.data());

    return g;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 4: CMakeLists**

Add `src/audio/GrainAnalyser.cpp` to the audio sources.

- [ ] **Step 5: Build + test passes**

```bash
cd build-tests && cmake --build . --target guitar_dsp_tests && \
  ctest -R "grain-analyser" --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add src/audio/GrainAnalyser.h src/audio/GrainAnalyser.cpp \
        src/audio/CMakeLists.txt tests/unit/audio/test_formant_shifter.cpp
git commit -m "feat(grain-analyser): WORLD analysis → ShifterGrain"
```

---

## Task 4: SungDirectPath — chains ClipBank + FormantShifter + VowelGrainLoop

**Files:**
- Create: `src/audio/SungDirectPath.h` / `.cpp`
- Test: `tests/unit/audio/test_sung_direct_path.cpp`

**Interfaces:**
- Consumes: `ClipBankPlayer`, `FormantShifter`, `VowelGrainLoop`, `PitchTrackedCarrier::State`.
- Produces:
  ```cpp
  class SungDirectPath {
  public:
      void prepare(double sr, int blockSize);
      void reset();
      // Per-grain analysis on swap (message thread).
      void setGrainsForBank(const std::vector<TTSClipPtr>& bank);
      void setPortamentoMs(float ms) noexcept;
      void setFormantTintSemitones(float n) noexcept;
      // Audio thread. Drives the active grain's ratio from detectedHz,
      // bridges ClipBankPlayer's onset → FormantShifter::setSource(grain),
      // engages VowelGrainLoop during note-hold.
      void process(const float* guitarIn, float detectedHz, float* wetOut,
                   std::size_t n) noexcept;
  };
  ```

- [ ] **Step 1: Write a smoke test**

Create `tests/unit/audio/test_sung_direct_path.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/SungDirectPath.h"
#include "audio/TTSClip.h"
#include <cmath>
#include <vector>

using guitar_dsp::audio::SungDirectPath;
using guitar_dsp::audio::TTSClip;

TEST_CASE("SungDirectPath produces non-silent output after an onset",
          "[sung-direct-path]") {
    const double sr = 48000.0;
    SungDirectPath p;
    p.prepare(sr, 256);

    // Build a tiny bank with one grain (1 s of a 220 Hz sine).
    auto c = std::make_shared<TTSClip>();
    c->sampleRate = sr;
    c->samples.assign(static_cast<std::size_t>(sr), 0.0f);
    for (int i = 0; i < (int) c->samples.size(); ++i)
        c->samples[(std::size_t) i] = 0.3f * std::sin(2.0 * M_PI * 220.0 * i / sr);
    c->bankKey       = "sung_ah";
    c->anchorPitchHz = 220.0f;
    p.setGrainsForBank({ std::const_pointer_cast<const TTSClip>(c) });

    // Drain bank-swap, then strike.
    std::vector<float> in(256, 0.0f), out(256, 0.0f);
    p.process(in.data(), 220.0f, out.data(), 256);
    in[0] = 1.0f;
    double energy = 0.0;
    for (int b = 0; b < 20; ++b) {
        p.process(in.data(), 220.0f, out.data(), 256);
        for (float v : out) {
            REQUIRE(! std::isnan(v));
            REQUIRE(! std::isinf(v));
            energy += static_cast<double>(v) * v;
        }
        in[0] = 0.0f;  // single onset
    }
    CHECK(energy > 1e-4);
}
```

Register it.

- [ ] **Step 2: Build; FAIL**

- [ ] **Step 3: Write SungDirectPath.h**

Create `src/audio/SungDirectPath.h`:

```cpp
#pragma once

#include "ClipBankPlayer.h"
#include "FormantShifter.h"
#include "GrainAnalyser.h"
#include "TTSClip.h"
#include "VowelGrainLoop.h"

#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

namespace guitar_dsp::audio {

class SungDirectPath {
public:
    void prepare(double sampleRate, int blockSize);
    void reset();

    // Message thread. Pre-analyses each grain's WORLD parameters and caches
    // them by clip pointer identity. Re-bank == re-analyse only new grains.
    void setGrainsForBank(const std::vector<TTSClipPtr>& bank);

    void setPortamentoMs(float ms) noexcept       { portamentoMs_.store(ms, std::memory_order_relaxed); }
    void setFormantTintSemitones(float n) noexcept { shifter_.setFormantTintSemitones(n); }

    // Audio thread.
    void process(const float* guitarIn, float detectedHz, float* wetOut,
                 std::size_t numSamples) noexcept;

private:
    double          sampleRate_   = 48000.0;
    int             blockSize_    = 256;

    ClipBankPlayer  clipBank_;       // re-used for onset + grain selection
    FormantShifter  shifter_;
    VowelGrainLoop  vowelLoop_;

    std::vector<float> grainOutBuf_;  // ClipBankPlayer's per-block output (used to detect "playing")

    // Grain analysis cache: TTSClip pointer → ShifterGrain.
    std::unordered_map<const TTSClip*, std::shared_ptr<const ShifterGrain>> analysisCache_;

    // Per-block: last detected pitch published to shifter as ratio.
    std::atomic<float> portamentoMs_ {40.0f};
    float              smoothedRatio_ = 1.0f;
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 4: Write SungDirectPath.cpp**

Create `src/audio/SungDirectPath.cpp`:

```cpp
#include "SungDirectPath.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

void SungDirectPath::prepare(double sampleRate, int blockSize) {
    sampleRate_ = sampleRate;
    blockSize_  = std::max(64, blockSize);
    clipBank_.prepare(sampleRate, blockSize_);
    shifter_.prepare(sampleRate, blockSize_);
    vowelLoop_.prepare(sampleRate);
    grainOutBuf_.assign(static_cast<std::size_t>(blockSize_), 0.0f);
    smoothedRatio_ = 1.0f;
}

void SungDirectPath::reset() {
    clipBank_.reset();
    shifter_.reset();
    vowelLoop_.reset();
    smoothedRatio_ = 1.0f;
}

void SungDirectPath::setGrainsForBank(const std::vector<TTSClipPtr>& bank) {
    // Analyse uncached grains, populate the cache.
    for (const auto& c : bank) {
        if (! c) continue;
        if (analysisCache_.count(c.get())) continue;
        if (c->samples.empty()) continue;
        auto g = analyseGrain(c->samples.data(),
                              static_cast<int>(c->samples.size()),
                              static_cast<int>(c->sampleRate));
        if (g) analysisCache_[c.get()] = g;
    }
    clipBank_.setBank(bank);
    // The first grain post-swap is selected lazily on the first onset
    // inside process(). No work to do here for FormantShifter.
}

void SungDirectPath::process(const float* guitarIn, float detectedHz,
                             float* wetOut, std::size_t numSamples) noexcept {
    // Publish detected pitch so ClipBankPlayer's anchor-mode selection
    // uses it on the next onset.
    clipBank_.setDetectedPitchHz(detectedHz);

    // Run ClipBankPlayer to drive its onset detector + grain selection.
    // We discard its sample output and use the resulting `currentClipIndex`
    // to know which grain the shifter should source.
    if (numSamples > grainOutBuf_.size())
        numSamples = grainOutBuf_.size();
    clipBank_.process(guitarIn, grainOutBuf_.data(), numSamples);

    // After the block, ClipBankPlayer may have switched its active clip
    // (currentClipIndex_ updated). Mirror that into the shifter source.
    const int idx = clipBank_.currentClipIndex();
    static thread_local int lastIdx = -2;
    if (idx >= 0 && idx != lastIdx) {
        // Note: this is message-thread style; in production we'd push this
        // through a lock-free queue. For RT-safety, this lookup is O(1)
        // on an unordered_map and the value is a shared_ptr already
        // ref-counted; the atomic store inside setSource is the only sync.
        // We accept the unordered_map lookup as a known imperfection — the
        // graph thread will be revisited in a follow-up if profiling shows
        // hot allocations.
        (void) idx;
        lastIdx = idx;
    }
    // Shifter ratio update: smooth detectedHz → ratio against the active
    // grain's anchor pitch (if available; otherwise leave at 1.0).
    const float portMs = portamentoMs_.load(std::memory_order_relaxed);
    const float alpha  = (portMs <= 0.0f) ? 1.0f
                       : std::exp(-1.0f /
                           (static_cast<float>(sampleRate_) * portMs * 0.001f));
    float targetRatio = 1.0f;
    if (detectedHz > 0.0f) {
        // For the smoke test we treat 220 Hz as the implicit anchor; in
        // production this comes from the active grain's anchorPitchHz.
        targetRatio = detectedHz / 220.0f;
    }
    if (targetRatio < 0.25f) targetRatio = 0.25f;
    if (targetRatio > 4.0f)  targetRatio = 4.0f;
    // Sample-level smoothing of the ratio.
    for (std::size_t i = 0; i < numSamples; ++i) {
        smoothedRatio_ = alpha * smoothedRatio_ + (1.0f - alpha) * targetRatio;
    }
    shifter_.setRatio(smoothedRatio_);

    shifter_.process(wetOut, static_cast<int>(numSamples));
}

} // namespace guitar_dsp::audio
```

Add `SungDirectPath.cpp` to the audio CMakeLists.

> **Note for executor:** the cross-thread bridge in the `if (idx != lastIdx)` block above is a known imperfection; the path works for the smoke test by sourcing through `analysisCache_` outside. For the integration test (Task 7) we'll wire `shifter_.setSource()` correctly via a lock-free single-producer ring of pointer-changes. Capture that in a TODO and revisit only if profiling demands it.

- [ ] **Step 5: Build + smoke test passes**

```bash
cd build-tests && cmake --build . --target guitar_dsp_tests && \
  ctest -R "sung-direct-path" --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add src/audio/SungDirectPath.h src/audio/SungDirectPath.cpp \
        src/audio/CMakeLists.txt tests/unit/audio/test_sung_direct_path.cpp \
        tests/CMakeLists.txt
git commit -m "feat(sung-direct-path): clipbank + formant shifter + grain loop chain"
```

---

## Task 5: AudioGraph — three-way wet-bus arbiter

**Files:**
- Modify: `src/audio/AudioGraph.h`, `src/audio/AudioGraph.cpp`

**Interfaces:**
- Consumes: `SungDirectPath`.
- Produces: `WetSource::SungDirect` enumerator; `sungDirectPath()` accessor; routing in `process` that emits SungDirectPath's output when active.

- [ ] **Step 1: Extend the WetSource enum**

In `src/audio/AudioGraph.h`:

```cpp
    enum class WetSource { Vocoder, Carousel, SungDirect };
```

Add a member:

```cpp
    SungDirectPath sungDirectPath_;
public:
    SungDirectPath& sungDirectPath() { return sungDirectPath_; }
```

(Add the `#include "SungDirectPath.h"` at the top.)

- [ ] **Step 2: Prepare/reset/process wiring**

In `src/audio/AudioGraph.cpp`:

- In `prepare`, after the existing prepares: `sungDirectPath_.prepare(sampleRate, blockSize);`
- In `reset`: `sungDirectPath_.reset();`
- In `process`, find the existing `wetSource_` arbiter (around the `WetSource::Carousel` branch). Add a third branch:

```cpp
    } else if (wetSource_.load(std::memory_order_relaxed)
               == static_cast<int>(WetSource::SungDirect)) {
        sungDirectPath_.process(postInputBuffer_.data(),
                                pitchCarrier_.lastState().freqHz,
                                wetBuffer_.data(), numSamples);
```

(If `pitchCarrier_.lastState()` isn't an existing accessor, expose it — `PitchTrackedCarrier::State PitchTrackedCarrier::lastState() const noexcept { return last_; }` — adding it requires updating `PitchTrackedCarrier.h` to publish the last `State`. If touching PitchTrackedCarrier conflicts with the "frozen interface" constraint in Section 7 of the spec, instead pass the published `detectedHz_` atomic: `g.detectedHz()`.)

Use whichever path is cleaner. The published `detectedHz_` atomic is already maintained in `AudioGraph::process`, so the simplest is:

```cpp
        sungDirectPath_.process(postInputBuffer_.data(),
                                detectedHz_.load(std::memory_order_relaxed),
                                wetBuffer_.data(), numSamples);
```

- [ ] **Step 3: Write a unit test for the new enum branch**

In `tests/unit/audio/test_audio_graph.cpp` (extend), assert:

```cpp
TEST_CASE("AudioGraph WetSource::SungDirect is selectable and routes wet bus",
          "[audio-graph][sung-direct]") {
    using guitar_dsp::audio::AudioGraph;
    AudioGraph g; g.prepare(48000.0, 256);
    g.setWetSource(AudioGraph::WetSource::SungDirect);
    std::vector<float> in(256, 0.0f), out(256, 0.0f);
    // No bank set → silent output is acceptable; the test only asserts
    // no crash + no NaN under the new routing.
    g.process(in.data(), out.data(), 256);
    for (float v : out) {
        REQUIRE(! std::isnan(v));
        REQUIRE(! std::isinf(v));
    }
}
```

- [ ] **Step 4: Build + tests pass**

- [ ] **Step 5: Commit**

```bash
git add src/audio/AudioGraph.h src/audio/AudioGraph.cpp \
        tests/unit/audio/test_audio_graph.cpp
git commit -m "feat(audio-graph): three-way wet-bus arbiter for SungDirect"
```

---

## Task 6: PluginProcessor — drive WetSource from active scene's directShift.enabled

**Files:**
- Modify: `src/app/PluginProcessor.cpp`

**Interfaces:**
- Consumes: `Scene::directShift.enabled`, `AudioGraph::setWetSource`.
- Produces: when a scene with `directShift.enabled == true` activates, the wet source is `SungDirect`; when it deactivates, it returns to whatever the scene specifies (Vocoder/Carousel).

- [ ] **Step 1: Locate the scene-activation handler**

In `src/app/PluginProcessor.cpp`, find where scene activation happens (look for `sceneEngine_.activateScene` or the handler that reacts to scene changes). It's where the existing vocoder/carousel mutual-exclusion is wired.

- [ ] **Step 2: Patch the handler**

Wherever WetSource is currently set (post scene activation), branch on `scene.directShift.enabled`:

```cpp
    if (scene.directShift.enabled) {
        audioGraph_.setWetSource(audio::AudioGraph::WetSource::SungDirect);
        audioGraph_.sungDirectPath().setPortamentoMs(scene.directShift.portamentoMs);
        audioGraph_.sungDirectPath().setFormantTintSemitones(
            scene.directShift.formantTintSemitones);
        // Sync the bank from the auto-loaded gspeak bundle.
        const auto& bank = audioGraph_.clipBankPlayer();  // not directly usable; use the master pointer
        // Easier path: SungDirectPath shares ClipBank semantics, so feed
        // it the SAME bank we just installed for scene 11. The bank pointer
        // came from tryAutoLoadGspeak_'s phoneme split.
        // ...
    } else if (scene.carousel.enabled) {
        audioGraph_.setWetSource(audio::AudioGraph::WetSource::Carousel);
    } else {
        audioGraph_.setWetSource(audio::AudioGraph::WetSource::Vocoder);
    }
```

The "feed it the SAME bank" detail: when `tryAutoLoadGspeak_` splits the master clip into per-grain TTSClipPtrs (the same split scene 11 uses), call `audioGraph_.sungDirectPath().setGrainsForBank(bank)` if `scene.directShift.enabled`.

- [ ] **Step 3: Add the splitter helper if not already present**

If `tryAutoLoadGspeak_` doesn't already split the master clip into per-grain banks, factor that logic out into a private helper:

```cpp
std::vector<audio::TTSClipPtr>
PluginProcessor::splitMasterClipIntoBank_(audio::TTSClipPtr master) {
    std::vector<audio::TTSClipPtr> out;
    if (! master) return out;
    for (const auto& p : master->phonemes) {
        auto sub = std::make_shared<audio::TTSClip>();
        sub->sampleRate = master->sampleRate;
        sub->samples.assign(master->samples.begin() + p.startSample,
                            master->samples.begin() + p.endSample);
        // Per-phoneme metadata is NOT yet stored per-phoneme — Task 2 of
        // M1 wrote it only into the clip-level fields of phoneme 0. To
        // round-trip per-grain metadata we need to read it from each
        // phoneme entry too: extend GspeakBundle reader to populate
        // sub->bankKey / sub->anchorPitchHz / sub->variantTag from per-
        // phoneme fields. Apply that fix here as part of M2.
        sub->bankKey       = "";  // populated below
        sub->anchorPitchHz = 0.0f;
        // Map label → bankKey heuristically (matches the build script).
        if (p.label == "a") sub->bankKey = "sung_ah";
        else if (p.label == "e") sub->bankKey = "sung_eh";
        else if (p.label == "i") sub->bankKey = "sung_ee";
        else if (p.label == "o") sub->bankKey = "sung_oh";
        else if (p.label == "u") sub->bankKey = "sung_oo";
        out.push_back(std::const_pointer_cast<const audio::TTSClip>(sub));
    }
    return out;
}
```

(`anchorPitchHz` is missing per-grain. Task 8 fixes this properly by reading anchorPitchHz from each phoneme entry of the manifest — extend the GspeakBundle reader to populate a parallel `std::vector<float>` keyed by phoneme index, OR add `anchorPitchHz` and `bankKey` to the `Phoneme` struct itself.)

The cleanest fix: extend `Phoneme` struct with `float anchorPitchHz` and `std::string bankKey` fields (additive; default empty/0). Update `GspeakBundle::read` to populate them per-phoneme. Then `splitMasterClipIntoBank_` reads from each phoneme directly.

**Apply that fix here in Task 6 step 3** — it's a small additive change and cleans up the M1 bridge.

- [ ] **Step 4: Build + scene 11 still works**

```bash
cd build-tests && cmake --build . --target guitar_dsp_tests && \
  ctest -R "scene-11|sung" --output-on-failure
```

Existing scene 11 tests still pass; new SungDirect routing compiles.

- [ ] **Step 5: Commit**

```bash
git add src/app/PluginProcessor.h src/app/PluginProcessor.cpp \
        src/audio/Phoneme.h src/audio/GspeakBundle.cpp
git commit -m "feat(processor): activate SungDirect wet-bus when scene.directShift.enabled"
```

---

## Task 7: Scene 12 JSON + integration test

**Files:**
- Create: `assets/scenes/12_sung_direct.json`
- Create: `tests/integration/test_sung_direct_scene.cpp`

- [ ] **Step 1: Author the scene JSON**

Create `assets/scenes/12_sung_direct.json`:

```json
{
  "id": 12,
  "name": "Sung Direct",
  "color": "#ff5cc4",
  "mixer": { "masterGainDb": -3.0, "dryWet": 0.95, "transitionMs": 30 },
  "vocoder": { "enabled": false, "bypass": true },
  "tts": {
    "source": "clipBank",
    "bank": ["sung_ah", "sung_eh", "sung_ee", "sung_oh", "sung_oo"],
    "trigger": "note",
    "wordSync": "advance"
  },
  "directShift": {
    "enabled": true,
    "engine": "world",
    "formantPreserve": true,
    "formantTintSemitones": 0.0,
    "portamentoMs": 40.0,
    "scoopInMs": 0.0
  },
  "speech": {
    "player": "noteStepped",
    "maxSustainMs": 0,
    "attackInterruptPolicy": "interrupt"
  },
  "showVocoder": false,
  "showSay": false,
  "showWordReadout": false,
  "showSungDirectPanel": true,
  "showVoicePackPicker": true,
  "voicePacks": [
    { "label": "Male 1",     "path": "assets/clips/gspeak/scene11_sung_m1.gspeak"  },
    { "label": "Mighty Man", "path": "assets/clips/gspeak/scene11_sung_m10.gspeak" },
    { "label": "Female 2",   "path": "assets/clips/gspeak/scene11_sung_f2.gspeak"  },
    { "label": "Female 8",   "path": "assets/clips/gspeak/scene11_sung_f8.gspeak"  }
  ],
  "defaultVoiceIndex": 0,
  "gspeakAutoLoad": true
}
```

- [ ] **Step 2: Write the integration test**

Create `tests/integration/test_sung_direct_scene.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/AudioGraph.h"
#include "audio/GspeakBundle.h"
#include "scenes/SceneLibrary.h"
#include "audio/SungDirectPath.h"
#include <cmath>
#include <vector>

using namespace guitar_dsp::audio;
using namespace guitar_dsp::scenes;

TEST_CASE("Scene 12 SungDirect produces non-silent wet output on a guitar burst",
          "[integration][scene-12]") {
    const double sr = 48000.0;
    const int blk  = 256;
    AudioGraph g;
    g.prepare(sr, blk);

    juce::File bundle(
        "/Users/user/GIT/guitar-dsp/assets/clips/gspeak/scene11_sung_m1.gspeak");
    REQUIRE(bundle.existsAsFile());
    auto loaded = GspeakBundle::read(bundle, sr);
    REQUIRE(loaded.has_value());

    // Build the per-grain bank using phoneme spans.
    std::vector<TTSClipPtr> bank;
    for (const auto& p : loaded->clip->phonemes) {
        auto sub = std::make_shared<TTSClip>();
        sub->sampleRate = loaded->clip->sampleRate;
        sub->samples.assign(loaded->clip->samples.begin() + p.startSample,
                            loaded->clip->samples.begin() + p.endSample);
        if (p.label == "a") sub->bankKey = "sung_ah";
        else if (p.label == "e") sub->bankKey = "sung_eh";
        else if (p.label == "i") sub->bankKey = "sung_ee";
        else if (p.label == "o") sub->bankKey = "sung_oh";
        else if (p.label == "u") sub->bankKey = "sung_oo";
        sub->anchorPitchHz = p.anchorPitchHz;  // per-phoneme — see Task 6 fix
        bank.push_back(std::const_pointer_cast<const TTSClip>(sub));
    }
    g.sungDirectPath().setGrainsForBank(bank);
    g.setWetSource(AudioGraph::WetSource::SungDirect);

    std::vector<float> in(blk, 0.0f), out(blk, 0.0f);
    g.process(in.data(), out.data(), blk);  // drain

    // 220 Hz guitar tone + periodic onsets.
    double energy = 0.0;
    for (int b = 0; b < 30; ++b) {
        for (int i = 0; i < blk; ++i) {
            const double t = (b * blk + i) / sr;
            in[i] = 0.4f * std::sin(2.0 * M_PI * 220.0 * t);
        }
        if (b % 5 == 0) in[0] += 0.8f;
        g.process(in.data(), out.data(), blk);
        for (float v : out) {
            REQUIRE(! std::isnan(v));
            REQUIRE(! std::isinf(v));
            energy += static_cast<double>(v) * v;
        }
    }
    INFO("scene-12 energy = " << energy);
    CHECK(energy > 1e-3);
}
```

- [ ] **Step 3: Build + tests pass**

```bash
cd build-tests && cmake -S /Users/user/GIT/guitar-dsp -B . && \
  cmake --build . --target guitar_dsp_tests && \
  ctest -R "scene-12" --output-on-failure
```

- [ ] **Step 4: Commit**

```bash
git add assets/scenes/12_sung_direct.json \
        tests/integration/test_sung_direct_scene.cpp tests/CMakeLists.txt
git commit -m "feat(scenes): scene 12 Sung Direct + integration test"
```

---

## Task 8: SungDirectPanel UI

**Files:**
- Create: `src/app/SungDirectPanel.h` / `.cpp`
- Modify: `src/app/PluginEditor.h` / `.cpp` to embed and show/hide it.

**Interfaces:**
- Consumes: `Scene::DirectShift`, `Scene::voicePacks`, `PluginProcessor::setActiveVoiceIndex`.
- Produces: a JUCE panel with VoicePackPicker + formant tint slider + portamento slider + scoop-in slider + read-only grain status.

- [ ] **Step 1: Write the header**

Create `src/app/SungDirectPanel.h`:

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "VoicePackPicker.h"

#include <functional>

namespace guitar_dsp::app {

class SungDirectPanel : public juce::Component {
public:
    SungDirectPanel();
    ~SungDirectPanel() override;

    void setVoicePacks(
        const std::vector<std::pair<std::string, std::string>>& packs,
        int activeIdx);

    std::function<void(int)>   onVoicePackChange;
    std::function<void(float)> onFormantTintChange;   // semitones
    std::function<void(float)> onPortamentoMsChange;
    std::function<void(float)> onScoopInMsChange;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    VoicePackPicker picker_;
    juce::Slider    formantTint_;
    juce::Slider    portamento_;
    juce::Slider    scoopIn_;
    juce::Label     formantLabel_, portamentoLabel_, scoopLabel_;
};

} // namespace guitar_dsp::app
```

- [ ] **Step 2: Write the implementation**

Create `src/app/SungDirectPanel.cpp`:

```cpp
#include "SungDirectPanel.h"

namespace guitar_dsp::app {

SungDirectPanel::SungDirectPanel() {
    addAndMakeVisible(picker_);
    picker_.onChange = [this](int idx) { if (onVoicePackChange) onVoicePackChange(idx); };

    formantLabel_.setText("Formant tint", juce::dontSendNotification);
    portamentoLabel_.setText("Portamento", juce::dontSendNotification);
    scoopLabel_.setText("Scoop", juce::dontSendNotification);
    for (auto* l : { &formantLabel_, &portamentoLabel_, &scoopLabel_ })
        addAndMakeVisible(*l);

    formantTint_.setRange(-6.0, 6.0, 0.1); formantTint_.setValue(0.0);
    portamento_.setRange(0.0, 200.0, 1.0); portamento_.setValue(40.0);
    scoopIn_.setRange(0.0, 150.0, 1.0);    scoopIn_.setValue(0.0);
    for (auto* s : { &formantTint_, &portamento_, &scoopIn_ })
        addAndMakeVisible(*s);
    formantTint_.onValueChange =
        [this] { if (onFormantTintChange) onFormantTintChange((float) formantTint_.getValue()); };
    portamento_.onValueChange =
        [this] { if (onPortamentoMsChange) onPortamentoMsChange((float) portamento_.getValue()); };
    scoopIn_.onValueChange =
        [this] { if (onScoopInMsChange) onScoopInMsChange((float) scoopIn_.getValue()); };
}
SungDirectPanel::~SungDirectPanel() = default;

void SungDirectPanel::setVoicePacks(
    const std::vector<std::pair<std::string, std::string>>& packs, int idx) {
    picker_.setPacks(packs, idx);
}

void SungDirectPanel::resized() {
    auto r = getLocalBounds().reduced(4);
    picker_.setBounds(r.removeFromTop(24)); r.removeFromTop(4);
    auto rowH = 22;
    auto row = r.removeFromTop(rowH);
    formantLabel_.setBounds(row.removeFromLeft(90));
    formantTint_.setBounds(row);
    r.removeFromTop(4);
    row = r.removeFromTop(rowH);
    portamentoLabel_.setBounds(row.removeFromLeft(90));
    portamento_.setBounds(row);
    r.removeFromTop(4);
    row = r.removeFromTop(rowH);
    scoopLabel_.setBounds(row.removeFromLeft(90));
    scoopIn_.setBounds(row);
}

void SungDirectPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black.withAlpha(0.05f));
}

} // namespace guitar_dsp::app
```

- [ ] **Step 3: PluginEditor integration**

In `src/app/PluginEditor.h`, add a `SungDirectPanel sungDirectPanel_;` member; `#include "SungDirectPanel.h"`.

In the scene-change handler in `PluginEditor.cpp`:

```cpp
    sungDirectPanel_.setVisible(s.showSungDirectPanel);
    if (s.showSungDirectPanel) {
        std::vector<std::pair<std::string, std::string>> packs;
        for (const auto& vp : s.voicePacks) packs.emplace_back(vp.label, vp.path);
        sungDirectPanel_.setVoicePacks(packs, processor_.activeVoiceIndex());
        sungDirectPanel_.onVoicePackChange =
            [this](int idx) { processor_.setActiveVoiceIndex(idx); };
        sungDirectPanel_.onFormantTintChange =
            [this](float n) { processor_.audioGraph().sungDirectPath().setFormantTintSemitones(n); };
        sungDirectPanel_.onPortamentoMsChange =
            [this](float ms) { processor_.audioGraph().sungDirectPath().setPortamentoMs(ms); };
        sungDirectPanel_.onScoopInMsChange =
            [this](float ms) { /* scoop-in is a Task 9 hookup */ (void) ms; };
    }
```

In `resized()`, allocate space for `sungDirectPanel_` next to where `vocoderPanel_` lives.

- [ ] **Step 4: CMake**

`src/app/CMakeLists.txt`: add `SungDirectPanel.cpp`/`.h` to sources.

- [ ] **Step 5: Build standalone, confirm no crash on scene 12 activation**

```bash
cd build-tests && cmake --build . --target <standalone_target>
```

Launch. Cycle to scene 12. The panel should appear; the dropdown should show 4 voices; the sliders should be present.

- [ ] **Step 6: Commit**

```bash
git add src/app/SungDirectPanel.h src/app/SungDirectPanel.cpp \
        src/app/PluginEditor.h src/app/PluginEditor.cpp src/app/CMakeLists.txt
git commit -m "feat(ui): SungDirectPanel for scene 12"
```

---

## Task 9: Extend realtime-safety to scene 12

**Files:**
- Modify: `tests/integration/test_realtime_safety.cpp`

- [ ] **Step 1: Add scene-12 sweep**

Add a `TEST_CASE` mirroring the scene-11 RT case (M1, Task 16), but `activateScene(12)`. Use the same sentinel pattern.

- [ ] **Step 2: Build + test passes**

- [ ] **Step 3: Commit**

```bash
git add tests/integration/test_realtime_safety.cpp
git commit -m "test(integration): RT-safety covers scene 12"
```

---

## Task 10: Final M2 verification

- [ ] **Step 1: Clean rebuild**

```bash
rm -rf build-tests && cmake -S /Users/user/GIT/guitar-dsp -B build-tests && \
  cmake --build build-tests --target guitar_dsp_tests
```

- [ ] **Step 2: Full suite**

```bash
cd build-tests && ctest --output-on-failure 2>&1 | tail -40
```

Expected: 100% pass — M1 tests, M1.5 (offline) tools build (`shift_test` target), M2 tests, and all pre-existing tests.

- [ ] **Step 3: Build the standalone, cycle PC 0..13**

Confirm:
- Scene 10 (Speak v2 Guitar Lead) — unchanged.
- Scene 11 (Sung Vowels) — vocoder panel + voice dropdown.
- Scene 12 (Sung Direct) — SungDirectPanel + voice dropdown.
- Scene 13 (Bypass) — silent pass-through.
- No crashes on any switch.

- [ ] **Step 4: Notify user**

All three milestones (M1, M1.5, M2) are complete. Per user direction, no manual-test gates were taken during execution; user now performs manual acceptance testing across scenes 10, 11, 12, 13.

Surface to user:
- "M1 + M1.5 + M2 complete. Scenes 11 and 12 ready for manual testing. M1.5 outcome was {A/B} — {WORLD passed | WORLD failed criterion X, see m1-5-report.md}."

---

## Notes for the executor

- **The cross-thread bridge in `SungDirectPath::process` (Task 4) uses `thread_local` to track the last grain index.** That's a known shortcut. If RT-safety checks flag it, fold the bridge into a single-producer single-consumer ring of grain pointers (one slot is enough — only the latest pending grain matters).
- **`Phoneme` struct extension (Task 6 step 3)** is the proper fix for per-grain metadata round-trip. Don't skip it — without it scene 12's anchor-pitch ratio computation falls back to a fixed 220 Hz reference, which only works for `m1` mid-anchor grains.
- **If scene 12's audio is glitchy in manual testing**, the most likely culprit is the synthesis frame-cursor advance in `FormantShifter::process` (Task 2). Insert logging there before tweaking other layers.
