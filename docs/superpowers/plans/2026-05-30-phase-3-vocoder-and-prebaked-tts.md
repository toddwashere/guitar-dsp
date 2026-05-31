# Phase 3: Vocoder + Prebaked TTS — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship the "speaking guitar" centerpiece — when the user activates a speaking scene (6, 7, or 8), a pre-baked TTS audio clip is played as the **modulator** of a 24-band channel vocoder whose **carrier** is the live guitar. Output sounds like the guitar is speaking the text in time with playing.

**Architecture:** Add a `Vocoder` branch to the existing `AudioGraph` alongside the no-op wet branch. Vocoder takes the live guitar as carrier input and a `TTSClip` audio stream as modulator. Scenes that opt-in carry a `tts.source` + `tts.clip` reference; the active scene's `TTSClipPlayer` advances each block. New `ITTSSource` interface with one concrete v1 implementation (`PrebakedTTSSource`) — Apple AVSpeechSynthesizer and Piper sources arrive in Phases 3.5 / 3.6 via the same interface.

**Tech Stack:** C++20 / JUCE 8 / Catch2 v3 — plus a small Python tool for offline TTS pre-baking. The Python tool uses [Piper](https://github.com/rhasspy/piper) as the pre-bake TTS engine: small (~63 MB voice model), local, deterministic, decent quality, single-binary invocation. Piper running offline-only here doesn't create a runtime dependency on the binary; only the resulting `.wav` is shipped.

**Reference spec:** [`docs/superpowers/specs/2026-05-29-while-my-guitar-gently-speaks-design.md`](../specs/2026-05-29-while-my-guitar-gently-speaks-design.md) — especially §5.2 (channel vocoder), §5.3 (IVocoder swap point), §6.1–6.3 (ITTSSource + TTS pre-bake pipeline), §6.4 (prewarming — N/A for prebaked source), §6.5 (failure handling), §7.2 (scene JSON `tts` block).

**Pre-flight assumption:** Phase 2 is merged into `main` (45/45 tests pass, scene system + MIDI/keyboard control works). Phase 2 scenes 6, 7, 8 currently have `mixer.dryWet = 0.0` (fully dry); Phase 3 changes that to `~0.85` and adds a `tts` block.

---

## Background for the implementing engineer

If you're new to this codebase, read these first:

- **Spec §5.2 (channel vocoder)** for the 24-band Bark-spaced math and sibilance noise injection.
- **Spec §5.3 (IVocoder)** for the swap-point interface — Phase 3 ships v1 (`ChannelVocoder`); a v2 neural vocoder replaces this class later via the same interface.
- **Spec §6.1 (ITTSSource interface)** for the cross-source contract. Phase 3 ships only `PrebakedTTSSource`; the interface exists from day one so Phase 3.5 / 3.6 just add subclasses.
- **Spec §6.3 (TTS pre-bake pipeline)** for the asset layout. Phase 3 implements a simplified version (just `audio.wav` per clip — `alignment.json` and `formants.json` are deferred to Phase 5 when karaoke text lands).
- **Existing code**: `src/audio/AudioGraph.{h,cpp}` — the wet path is currently a zeroed buffer; vocoder output replaces it. `src/audio/Mixer.{h,cpp}` — already handles dry+wet mixing with ramping. `src/scenes/SceneEngine.{h,cpp}` — currently exposes only `MixerParams` to the audio thread; this plan extends it with a TTS-clip reference.
- **JUCE basics if you haven't read them**: `juce::AudioBuffer<float>`, `juce::AudioFormatReader` (for loading WAVs), `juce::dsp::IIR::Filter` (for biquads), `juce::dsp::StateVariableTPTFilter` (alternative band filter).

**Threading rules to honor (same as previous phases):**
- The audio thread (`processBlock` and any `process` it calls) MUST NOT allocate, lock, or block. `RealtimeSentinel` catches violations in tests.
- TTSClipPlayer state (sample position, active clip) is owned by the audio thread; the message thread publishes "activate this clip" via a lock-free atomic snapshot in `SceneEngine`.
- Vocoder filter state lives in the audio thread; setters update target values, smoothed per-sample (where smoothing matters).

**What Phase 3 deliberately does NOT do** (preserving spec deferrals):
- No Apple AVSpeechSynthesizer source — Phase 3.5.
- No Piper subprocess source — Phase 3.6.
- No `TTSPrewarmer` (only matters for live sources; prebaked is always-ready).
- No karaoke phoneme text rendering — Phase 5.
- No `formants.json` / `alignment.json` output from the pre-bake tool (placeholder fields in `meta.json` only).
- No neural voice conversion — v2 roadmap.

If a task tempts you toward those: stop and report it as DONE_WITH_CONCERNS.

---

## File structure (created/modified across this plan)

```
guitar-dsp/
├── CMakeLists.txt
├── README.md                                       (modified, Task 18)
├── assets/
│   ├── scenes/
│   │   ├── 06_speaking_a.json                      (modified, Task 17)
│   │   ├── 07_speaking_b.json                      (modified, Task 17)
│   │   └── 08_speaking_finale.json                 (modified, Task 17)
│   └── tts/                                        (NEW)
│       ├── 06_hello_cleveland/
│       │   ├── audio.wav                           (binary, Task 7)
│       │   └── meta.json                           (Task 7)
│       ├── 07_mid_talk/
│       │   ├── audio.wav
│       │   └── meta.json
│       └── 08_gently_weeps/
│           ├── audio.wav
│           └── meta.json
├── src/
│   ├── audio/
│   │   ├── IVocoder.h                              (NEW, Task 1)
│   │   ├── ChannelVocoder.{h,cpp}                  (NEW, Tasks 2-5)
│   │   ├── TTSClip.h                               (NEW, Task 8)
│   │   ├── TTSClipPlayer.{h,cpp}                   (NEW, Task 9)
│   │   ├── ITTSSource.h                            (NEW, Task 10)
│   │   ├── PrebakedTTSSource.{h,cpp}               (NEW, Task 11)
│   │   ├── AudioGraph.{h,cpp}                      (modified, Task 12)
│   │   └── Mixer.h                                 (unchanged)
│   ├── scenes/
│   │   ├── Scene.h                                 (modified, Task 13: add TtsConfig)
│   │   ├── SceneLibrary.cpp                        (modified, Task 13: parse tts block)
│   │   └── SceneEngine.{h,cpp}                     (modified, Task 14: expose active TTS clip)
│   └── app/
│       └── PluginProcessor.{h,cpp}                 (modified, Task 15: build TTS-source registry, route to vocoder)
├── src/CMakeLists.txt                              (modified — new sources)
├── tools/
│   └── tts_prebake/                                (NEW)
│       ├── prebake.py                              (Task 6)
│       ├── requirements.txt                        (Task 6)
│       ├── voices/                                 (Task 6 — bundled .onnx)
│       │   └── en_US-amy-medium.onnx
│       │   └── en_US-amy-medium.onnx.json
│       └── README.md                               (Task 6)
├── tests/
│   ├── CMakeLists.txt                              (modified — new sources + fixtures)
│   ├── unit/
│   │   ├── audio/
│   │   │   ├── test_channel_vocoder.cpp            (Tasks 3-5)
│   │   │   ├── test_tts_clip_player.cpp            (Task 9)
│   │   │   └── test_prebaked_tts_source.cpp        (Task 11)
│   │   └── scenes/
│   │       └── test_scene_library_tts.cpp          (Task 13)
│   ├── integration/
│   │   └── test_speaking_scene.cpp                 (Task 16)
│   └── fixtures/
│       ├── tts/
│       │   ├── tiny_clip/audio.wav                 (Task 11 — short test clip)
│       │   └── tiny_clip/meta.json
│       └── scenes/
│           └── with_tts.json                       (Task 13)
└── docs/superpowers/plans/
    └── 2026-05-30-phase-3-vocoder-and-prebaked-tts.md
```

---

## Task 1: IVocoder interface

**Files:**
- Create: `src/audio/IVocoder.h`

The interface alone — no implementation. Sized for the swap point so a v2 neural vocoder can drop in later without touching the AudioGraph.

- [ ] **Step 1: Write `src/audio/IVocoder.h`**

```cpp
#pragma once

#include <cstddef>

namespace guitar_dsp::audio {

// Vocoder interface. Two audio streams in, one out:
//   carrier  — the harmonic source (live guitar)
//   modulator — the formant/envelope source (TTS audio)
// Output is the carrier shaped by the modulator's per-band envelopes —
// the signature "talkbox" or "vocoder" sound.
//
// All three buffers are mono, `numSamples` long. In-place aliasing
// (output == carrier) is permitted.
class IVocoder {
public:
    virtual ~IVocoder() = default;

    virtual void prepare(double sampleRate, int blockSize) = 0;
    virtual void reset() = 0;
    virtual void process(const float* carrier,
                         const float* modulator,
                         float* output,
                         std::size_t numSamples) = 0;

    // Wet level 0..1 — caller-side gain for the vocoder's contribution
    // to the wet bus. The mixer's dryWet is separate.
    virtual void setWetLevel(float v) = 0;

    // Sibilance noise injection 0..1 — adds noise where the modulator
    // has unvoiced high-frequency energy. 0 = none, 1 = full sibilance.
    virtual void setSibilance(float v) = 0;
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 2: Verify the header compiles standalone**

Add a smoke test at `tests/unit/audio/test_ivocoder_compiles.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/IVocoder.h"

TEST_CASE("IVocoder: header compiles + is abstract", "[audio][vocoder][smoke]") {
    static_assert(std::is_abstract_v<guitar_dsp::audio::IVocoder>,
                  "IVocoder must remain abstract");
    SUCCEED();
}
```

Add it to `tests/CMakeLists.txt` (new entry in the existing `add_executable` source list).

- [ ] **Step 3: Build and run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R "IVocoder"
```

Expected: 1 test passes.

- [ ] **Step 4: Commit**

```bash
git add src/audio/IVocoder.h tests/unit/audio/test_ivocoder_compiles.cpp tests/CMakeLists.txt
git commit -m "feat(audio): IVocoder interface (swap point for v2 neural)"
```

---

## Task 2: ChannelVocoder skeleton (no DSP yet, just the class)

**Files:**
- Create: `src/audio/ChannelVocoder.h`
- Create: `src/audio/ChannelVocoder.cpp`
- Modify: `src/CMakeLists.txt`

This task lands the class declaration + empty method bodies so the build wires up. DSP arrives in Tasks 3–5.

- [ ] **Step 1: Write `src/audio/ChannelVocoder.h`**

```cpp
#pragma once

#include <array>
#include <cstddef>

#include "IVocoder.h"

namespace guitar_dsp::audio {

// 24-band channel vocoder, Bark-scale band layout, biquad filter banks
// for both carrier and modulator, one-pole envelope follower per band,
// optional sibilance noise injection for fricatives.
//
// Algorithm per spec §5.2:
//   1. Split modulator into N bandpass filters.
//   2. Per band: one-pole envelope follower (~15 ms time constant).
//   3. Split carrier into the same N bands.
//   4. Multiply each carrier band by its modulator envelope.
//   5. Sum all bands.
//   6. Sibilance: detect high-band unvoiced energy in modulator, mix
//      noise shaped by the high bands into the sum.
class ChannelVocoder : public IVocoder {
public:
    static constexpr int kNumBands = 24;

    ChannelVocoder();
    ~ChannelVocoder() override = default;

    void prepare(double sampleRate, int blockSize) override;
    void reset() override;
    void process(const float* carrier,
                 const float* modulator,
                 float* output,
                 std::size_t numSamples) override;
    void setWetLevel(float v) override;
    void setSibilance(float v) override;

private:
    double sampleRate_ = 48000.0;

    // Bark-spaced center frequencies for the 24 bands (Hz).
    // Populated in prepare().
    std::array<float, kNumBands> bandCenters_{};

    // Per-band filter state (carrier and modulator share band layout).
    // Each entry is a 2-pole bandpass (Direct Form I), so we store
    // x[n-1], x[n-2], y[n-1], y[n-2] per filter.
    struct BiquadState {
        float xz1 = 0.0f, xz2 = 0.0f;
        float yz1 = 0.0f, yz2 = 0.0f;
    };
    struct BiquadCoefs {
        // Standard 2-pole bandpass biquad: a0 normalized to 1.
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
    };

    std::array<BiquadCoefs,  kNumBands> coefs_{};
    std::array<BiquadState,  kNumBands> carrierState_{};
    std::array<BiquadState,  kNumBands> modulatorState_{};

    // Per-band envelope follower output (one-pole LP of |modulator band|).
    std::array<float, kNumBands> envelope_{};
    float envelopeCoef_ = 0.0f;  // computed from ~15 ms time constant

    // Output level + sibilance, smoothed per sample.
    float wetLevel_   = 1.0f;
    float sibilance_  = 0.5f;

    // Sibilance noise generator state (white noise via xorshift).
    std::uint32_t noiseState_ = 0xC0FFEE01u;

    void recomputeCoefficients();
    static float singleBiquad(float x,
                              const BiquadCoefs& c,
                              BiquadState& s) noexcept;
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 2: Write `src/audio/ChannelVocoder.cpp` with stub bodies**

```cpp
#include "ChannelVocoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace guitar_dsp::audio {

namespace {

// Bark-scale band centers for 24 bands from 80 Hz to 10 kHz, evenly
// spaced on the Bark axis (Zwicker scale).
constexpr std::array<float, 24> kBarkCenters{{
     80.0f,  120.0f,  170.0f,  230.0f,  300.0f,  385.0f,
    495.0f,  630.0f,  790.0f,  980.0f, 1205.0f, 1470.0f,
   1785.0f, 2160.0f, 2615.0f, 3160.0f, 3800.0f, 4540.0f,
   5395.0f, 6385.0f, 7530.0f, 8845.0f, 9750.0f, 10000.0f,
}};

} // namespace

ChannelVocoder::ChannelVocoder() {
    std::copy(kBarkCenters.begin(), kBarkCenters.end(), bandCenters_.begin());
}

void ChannelVocoder::prepare(double sampleRate, int /*blockSize*/) {
    sampleRate_ = sampleRate;
    recomputeCoefficients();
    reset();
}

void ChannelVocoder::reset() {
    for (auto& s : carrierState_)    s = {};
    for (auto& s : modulatorState_)  s = {};
    std::fill(envelope_.begin(), envelope_.end(), 0.0f);
}

void ChannelVocoder::recomputeCoefficients() {
    // Q ≈ 6.0 gives reasonable band overlap on a Bark scale.
    constexpr float Q = 6.0f;
    const float fs = static_cast<float>(sampleRate_);

    for (int i = 0; i < kNumBands; ++i) {
        const float f0 = bandCenters_[static_cast<std::size_t>(i)];
        const float w0 = 2.0f * 3.14159265358979323846f * f0 / fs;
        const float cos_w0 = std::cos(w0);
        const float sin_w0 = std::sin(w0);
        const float alpha  = sin_w0 / (2.0f * Q);

        // Constant-skirt-gain 2-pole BPF (RBJ cookbook).
        const float a0 = 1.0f + alpha;
        BiquadCoefs c;
        c.b0 =  sin_w0 / 2.0f / a0;
        c.b1 =  0.0f;
        c.b2 = -sin_w0 / 2.0f / a0;
        c.a1 = -2.0f * cos_w0 / a0;
        c.a2 = (1.0f - alpha) / a0;
        coefs_[static_cast<std::size_t>(i)] = c;
    }

    // 15 ms envelope follower one-pole: coef = exp(-1/(t*fs)).
    constexpr float envT = 0.015f;
    envelopeCoef_ = std::exp(-1.0f / (envT * fs));
}

void ChannelVocoder::setWetLevel(float v) {
    wetLevel_ = std::clamp(v, 0.0f, 1.0f);
}

void ChannelVocoder::setSibilance(float v) {
    sibilance_ = std::clamp(v, 0.0f, 1.0f);
}

float ChannelVocoder::singleBiquad(float x,
                                   const BiquadCoefs& c,
                                   BiquadState& s) noexcept {
    const float y = c.b0 * x + c.b1 * s.xz1 + c.b2 * s.xz2
                  - c.a1 * s.yz1 - c.a2 * s.yz2;
    s.xz2 = s.xz1;  s.xz1 = x;
    s.yz2 = s.yz1;  s.yz1 = y;
    return y;
}

void ChannelVocoder::process(const float* carrier,
                             const float* modulator,
                             float* output,
                             std::size_t numSamples) {
    // Stub: passthrough carrier so the test in Task 3 can verify the
    // process path runs without crashing. Real DSP arrives in Task 3.
    std::memcpy(output, carrier, numSamples * sizeof(float));
    (void)modulator;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 3: Add to `src/CMakeLists.txt`**

In `add_library(guitar_dsp_audio STATIC ...)`, append `audio/ChannelVocoder.cpp`.

- [ ] **Step 4: Build to confirm it links**

```bash
cmake --build build --target guitar_dsp_tests guitar_dsp_app_Standalone
ctest --test-dir build --output-on-failure 2>&1 | tail -3
```

Expected: builds; all 46/46 tests still pass (46 = 45 prior + 1 IVocoder smoke).

- [ ] **Step 5: Commit**

```bash
git add src/audio/ChannelVocoder.h src/audio/ChannelVocoder.cpp src/CMakeLists.txt
git commit -m "feat(audio): ChannelVocoder class skeleton (24-band Bark, passthrough stub)"
```

---

## Task 3: ChannelVocoder DSP — modulator-shapes-carrier on a single band (TDD)

**Files:**
- Create: `tests/unit/audio/test_channel_vocoder.cpp`
- Modify: `src/audio/ChannelVocoder.cpp`
- Modify: `tests/CMakeLists.txt`

Implement the core algorithm one assertion at a time. First test: a sine carrier near a band center, with a modulator at the SAME frequency, should produce output amplitude tracking the modulator amplitude.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/audio/test_channel_vocoder.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "audio/ChannelVocoder.h"
#include "harness/SyntheticGuitar.h"

#include <vector>

using Catch::Matchers::WithinAbs;
using guitar_dsp::audio::ChannelVocoder;
using guitar_dsp::tests::SyntheticGuitar;

TEST_CASE("ChannelVocoder: modulator amplitude shapes carrier", "[audio][vocoder]") {
    ChannelVocoder voc;
    voc.prepare(48000.0, 512);
    voc.setWetLevel(1.0f);
    voc.setSibilance(0.0f);

    SyntheticGuitar gen{48000.0};

    // 1 second total, in two halves: loud modulator, then near-silent.
    constexpr int N = 48000;
    std::vector<float> carrier(N), modLoud(N / 2), modQuiet(N / 2), out(N);
    gen.sine(800.0f, 0.6f, carrier.data(), N);
    gen.sine(800.0f, 0.6f, modLoud.data(), N / 2);
    gen.sine(800.0f, 0.01f, modQuiet.data(), N / 2);

    std::vector<float> modulator(N);
    std::copy(modLoud.begin(),  modLoud.end(),  modulator.begin());
    std::copy(modQuiet.begin(), modQuiet.end(), modulator.begin() + N / 2);

    voc.process(carrier.data(), modulator.data(), out.data(), N);

    // RMS in the second half (modulator quiet) should be lower than first.
    auto rms = [](const float* p, int n) {
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += p[i] * p[i];
        return std::sqrt(s / n);
    };
    const double rmsFirst  = rms(out.data(),         N / 2);
    const double rmsSecond = rms(out.data() + N / 2, N / 2);

    INFO("rmsFirst=" << rmsFirst << " rmsSecond=" << rmsSecond);
    REQUIRE(rmsSecond < rmsFirst * 0.5);  // strongly attenuated
}
```

- [ ] **Step 2: Run to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests 2>&1 | tail -3
ctest --test-dir build --output-on-failure -R "ChannelVocoder: modulator"
```

Expected: test FAILS because the stub `process` is a passthrough — rmsSecond ≈ rmsFirst.

- [ ] **Step 3: Replace the stub `process` with the real algorithm**

In `src/audio/ChannelVocoder.cpp`, replace the stub `process` body with:

```cpp
void ChannelVocoder::process(const float* carrier,
                             const float* modulator,
                             float* output,
                             std::size_t numSamples) {
    const float oneMinusEnv = 1.0f - envelopeCoef_;

    for (std::size_t i = 0; i < numSamples; ++i) {
        const float c = carrier[i];
        const float m = modulator[i];

        float sum = 0.0f;
        for (int b = 0; b < kNumBands; ++b) {
            // Band-filter the modulator and track per-band envelope.
            const float mBand = singleBiquad(m, coefs_[static_cast<std::size_t>(b)],
                                                modulatorState_[static_cast<std::size_t>(b)]);
            const float absM = std::abs(mBand);
            envelope_[static_cast<std::size_t>(b)] =
                envelopeCoef_ * envelope_[static_cast<std::size_t>(b)] + oneMinusEnv * absM;

            // Band-filter the carrier and scale by modulator envelope.
            const float cBand = singleBiquad(c, coefs_[static_cast<std::size_t>(b)],
                                                carrierState_[static_cast<std::size_t>(b)]);
            sum += cBand * envelope_[static_cast<std::size_t>(b)];
        }

        output[i] = sum * wetLevel_;
    }
}
```

Note the `(void)modulator;` line from the stub is removed; the parameter is now actually used.

- [ ] **Step 4: Run to confirm pass**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R "ChannelVocoder: modulator"
```

Expected: PASS.

- [ ] **Step 5: Add `test_channel_vocoder.cpp` to `tests/CMakeLists.txt`** if not already present.

- [ ] **Step 6: Run full suite**

```bash
ctest --test-dir build --output-on-failure 2>&1 | tail -3
```

Expected: 47/47 (46 prior + 1 new vocoder test).

- [ ] **Step 7: Commit**

```bash
git add src/audio/ChannelVocoder.cpp tests/unit/audio/test_channel_vocoder.cpp tests/CMakeLists.txt
git commit -m "feat(audio): ChannelVocoder modulator-shapes-carrier algorithm"
```

---

## Task 4: ChannelVocoder — silence in / silence out + realtime safety (TDD)

**Files:**
- Modify: `tests/unit/audio/test_channel_vocoder.cpp`

Two more correctness/safety tests. Append to the existing test file.

- [ ] **Step 1: Append tests**

Append at the bottom of `tests/unit/audio/test_channel_vocoder.cpp`:

```cpp
TEST_CASE("ChannelVocoder: silent modulator produces silent output", "[audio][vocoder]") {
    ChannelVocoder voc;
    voc.prepare(48000.0, 512);
    voc.setWetLevel(1.0f);
    voc.setSibilance(0.0f);

    SyntheticGuitar gen{48000.0};
    constexpr int N = 4800;
    std::vector<float> carrier(N), modulator(N, 0.0f), out(N);
    gen.sine(800.0f, 0.6f, carrier.data(), N);

    voc.process(carrier.data(), modulator.data(), out.data(), N);

    float peak = 0.0f;
    for (int i = N - 480; i < N; ++i) peak = std::max(peak, std::abs(out[i]));
    REQUIRE(peak < 1e-3f);
}

TEST_CASE("ChannelVocoder: zero allocations on audio thread", "[audio][vocoder][realtime]") {
    ChannelVocoder voc;
    voc.prepare(48000.0, 512);
    voc.setWetLevel(1.0f);
    voc.setSibilance(0.4f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> carrier(512), modulator(512), out(512);
    gen.sine(800.0f, 0.5f, carrier.data(), 512);
    gen.sine(800.0f, 0.4f, modulator.data(), 512);

    guitar_dsp::tests::RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int i = 0; i < 938; ++i)  // 10 s of audio in 512-sample blocks
        voc.process(carrier.data(), modulator.data(), out.data(), 512);
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
```

You'll also need to add `#include "harness/RealtimeSentinel.h"` near the top of the file.

- [ ] **Step 2: Build + run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R ChannelVocoder
```

Expected: 3 tests pass (Task 3's + these 2).

- [ ] **Step 3: Commit**

```bash
git add tests/unit/audio/test_channel_vocoder.cpp
git commit -m "test(audio): ChannelVocoder silence + realtime-safety tests"
```

---

## Task 5: ChannelVocoder — sibilance noise injection (TDD)

**Files:**
- Modify: `src/audio/ChannelVocoder.cpp`
- Modify: `tests/unit/audio/test_channel_vocoder.cpp`

Sibilance: when the modulator has high-frequency energy but the carrier is quiet at those bands (e.g. fricatives like "s" / "sh"), inject shaped noise so consonants stay intelligible.

- [ ] **Step 1: Append the failing test**

Append at the bottom of `tests/unit/audio/test_channel_vocoder.cpp`:

```cpp
TEST_CASE("ChannelVocoder: sibilance noise activates with high-band modulator energy",
          "[audio][vocoder]") {
    ChannelVocoder voc;
    voc.prepare(48000.0, 512);
    voc.setWetLevel(1.0f);

    SyntheticGuitar gen{48000.0};
    constexpr int N = 4800;

    // A 5 kHz modulator (sibilant-band) with a 200 Hz carrier (low):
    // without sibilance noise injection, the high modulator band has
    // nothing to scale on the carrier side, so output is near silent.
    // With sibilance enabled, high-band noise should appear in the output.
    std::vector<float> carrier(N), modulator(N), outNoSib(N), outWithSib(N);
    gen.sine(200.0f,  0.6f, carrier.data(),   N);
    gen.sine(5000.0f, 0.6f, modulator.data(), N);

    voc.setSibilance(0.0f);
    voc.process(carrier.data(), modulator.data(), outNoSib.data(), N);

    voc.reset();
    voc.setSibilance(1.0f);
    voc.process(carrier.data(), modulator.data(), outWithSib.data(), N);

    auto rms = [](const float* p, int n) {
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += p[i] * p[i];
        return std::sqrt(s / n);
    };
    INFO("noSib=" << rms(outNoSib.data(),   N)
                  << "  withSib=" << rms(outWithSib.data(), N));
    REQUIRE(rms(outWithSib.data(), N) > rms(outNoSib.data(), N) * 3.0);
}
```

- [ ] **Step 2: Run to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R "sibilance"
```

Expected: FAILS — current `process` has no sibilance path.

- [ ] **Step 3: Add sibilance to `process`**

In `src/audio/ChannelVocoder.cpp`, replace the existing `process` body with:

```cpp
void ChannelVocoder::process(const float* carrier,
                             const float* modulator,
                             float* output,
                             std::size_t numSamples) {
    const float oneMinusEnv = 1.0f - envelopeCoef_;

    for (std::size_t i = 0; i < numSamples; ++i) {
        const float c = carrier[i];
        const float m = modulator[i];

        float sum = 0.0f;
        float sibEnvelope = 0.0f;  // sum of envelopes from high bands

        for (int b = 0; b < kNumBands; ++b) {
            const float mBand = singleBiquad(m, coefs_[static_cast<std::size_t>(b)],
                                                modulatorState_[static_cast<std::size_t>(b)]);
            const float absM = std::abs(mBand);
            envelope_[static_cast<std::size_t>(b)] =
                envelopeCoef_ * envelope_[static_cast<std::size_t>(b)] + oneMinusEnv * absM;

            const float cBand = singleBiquad(c, coefs_[static_cast<std::size_t>(b)],
                                                carrierState_[static_cast<std::size_t>(b)]);
            sum += cBand * envelope_[static_cast<std::size_t>(b)];

            // Bands 18..23 are >5 kHz; treat as sibilance candidates.
            if (b >= 18) sibEnvelope += envelope_[static_cast<std::size_t>(b)];
        }

        // Generate white noise via xorshift32, then attenuate by the
        // high-band modulator energy and the user's sibilance setting.
        noiseState_ ^= noiseState_ << 13;
        noiseState_ ^= noiseState_ >> 17;
        noiseState_ ^= noiseState_ << 5;
        const float noise = (static_cast<float>(noiseState_) / 2147483648.0f) - 1.0f;
        const float sibContrib = noise * sibEnvelope * sibilance_;

        output[i] = (sum + sibContrib) * wetLevel_;
    }
}
```

- [ ] **Step 4: Run to confirm pass**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R ChannelVocoder
```

Expected: all 4 vocoder tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/audio/ChannelVocoder.cpp tests/unit/audio/test_channel_vocoder.cpp
git commit -m "feat(audio): ChannelVocoder sibilance noise injection for fricatives"
```

---

## Task 6: Python pre-bake tool (Piper-based)

**Files:**
- Create: `tools/tts_prebake/prebake.py`
- Create: `tools/tts_prebake/requirements.txt`
- Create: `tools/tts_prebake/README.md`
- Create: `tools/tts_prebake/voices/.gitkeep`

The pre-bake tool runs OFFLINE (not at app runtime) to generate `audio.wav` per TTS clip. It uses the [Piper](https://github.com/rhasspy/piper) TTS model via the `piper-tts` Python package — small (~63 MB voice model), no GPU required, deterministic output.

Voice files are downloaded by the user from [Piper's voice catalog](https://github.com/rhasspy/piper/blob/master/VOICES.md) and placed in `tools/tts_prebake/voices/` (NOT committed — too large; the `.gitkeep` keeps the directory tracked).

- [ ] **Step 1: Write `tools/tts_prebake/prebake.py`**

```python
#!/usr/bin/env python3
"""
prebake.py — generate TTS audio clips for the speaking-guitar app.

Usage:
    python prebake.py --text "hello cleveland" --out assets/tts/06_hello_cleveland
    python prebake.py --config clips.yaml --out assets/tts/

Output per clip:
    <out>/audio.wav   — mono 48 kHz float32 WAV
    <out>/meta.json   — { "text": ..., "voice": ..., "duration_s": ... }

Requires:
    piper-tts (pip install piper-tts)
    A Piper voice .onnx file under tools/tts_prebake/voices/.
"""
import argparse
import json
import os
import subprocess
import sys
import wave
from pathlib import Path


def run_piper(text: str, voice_onnx: Path, out_wav: Path) -> None:
    """Invoke Piper to synthesize `text` to `out_wav`."""
    out_wav.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        sys.executable, "-m", "piper",
        "--model", str(voice_onnx),
        "--output_file", str(out_wav),
    ]
    proc = subprocess.run(cmd, input=text.encode("utf-8"), check=True)
    if proc.returncode != 0:
        raise SystemExit(f"piper failed for: {text!r}")


def wav_duration_seconds(path: Path) -> float:
    with wave.open(str(path), "rb") as f:
        return f.getnframes() / f.getframerate()


def bake_one(text: str, voice_onnx: Path, out_dir: Path) -> None:
    audio = out_dir / "audio.wav"
    run_piper(text, voice_onnx, audio)

    meta = {
        "text": text,
        "voice": voice_onnx.name,
        "duration_s": wav_duration_seconds(audio),
    }
    with open(out_dir / "meta.json", "w") as f:
        json.dump(meta, f, indent=2)
    print(f"baked {out_dir.name}: {meta['duration_s']:.2f}s")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--text", help="single text string to bake")
    ap.add_argument("--out",  required=True, help="output directory for the clip")
    ap.add_argument("--voice", default=None,
                    help="path to a Piper .onnx voice (default: first found in voices/)")
    args = ap.parse_args()

    if not args.text:
        ap.error("Phase 3 supports --text only; YAML batch mode comes in Phase 5.")

    voices_dir = Path(__file__).parent / "voices"
    if args.voice:
        voice = Path(args.voice)
    else:
        candidates = sorted(voices_dir.glob("*.onnx"))
        if not candidates:
            print(f"No voice .onnx in {voices_dir}/. Download one from "
                  "https://github.com/rhasspy/piper/blob/master/VOICES.md "
                  "and place it there.", file=sys.stderr)
            return 1
        voice = candidates[0]

    bake_one(args.text, voice, Path(args.out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 2: Write `tools/tts_prebake/requirements.txt`**

```
piper-tts>=1.2.0
```

- [ ] **Step 3: Write `tools/tts_prebake/README.md`**

```markdown
# tts_prebake

Offline TTS pre-baker for the speaking-guitar app. Generates `audio.wav` files that the running app loads as vocoder modulator material.

## Setup

```bash
cd tools/tts_prebake
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Download a Piper voice from https://github.com/rhasspy/piper/blob/master/VOICES.md and place both files in `voices/`:

- `<voice>.onnx`
- `<voice>.onnx.json`

Recommended for clarity at vocoder-modulator use: `en_US-amy-medium` (~63 MB total).

## Generating clips

```bash
python prebake.py --text "hello cleveland" --out ../../assets/tts/06_hello_cleveland
python prebake.py --text "I think therefore I riff" --out ../../assets/tts/07_mid_talk
python prebake.py --text "I look at you all see the love there that's sleeping" \
                  --out ../../assets/tts/08_gently_weeps
```

Commit the resulting `audio.wav` and `meta.json` files — they're the app's runtime assets.
```

- [ ] **Step 4: Create `voices/.gitkeep` and add to .gitignore for the .onnx files**

```bash
mkdir -p tools/tts_prebake/voices
touch tools/tts_prebake/voices/.gitkeep
```

Append to `.gitignore` (under the Python section):

```
# Piper voice files (download separately; too large to commit)
tools/tts_prebake/voices/*.onnx
tools/tts_prebake/voices/*.onnx.json
tools/tts_prebake/.venv/
```

- [ ] **Step 5: Commit**

```bash
git add tools/tts_prebake/ .gitignore
git commit -m "feat(tools): Python pre-bake tool (Piper-based)"
```

---

## Task 7: Generate and commit the 3 demo TTS clips

This task is **manual**: it requires a working Piper install + voice file. The output is binary `.wav` files that get committed.

- [ ] **Step 1: Set up the pre-bake environment** (one-time)

Per `tools/tts_prebake/README.md`:

```bash
cd tools/tts_prebake
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Then download the voice files manually:

```bash
curl -L -o voices/en_US-amy-medium.onnx \
  https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/amy/medium/en_US-amy-medium.onnx
curl -L -o voices/en_US-amy-medium.onnx.json \
  https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/amy/medium/en_US-amy-medium.onnx.json
```

- [ ] **Step 2: Bake the three clips**

```bash
mkdir -p ../../assets/tts
python prebake.py --text "hello cleveland" \
  --out ../../assets/tts/06_hello_cleveland
python prebake.py --text "I think therefore I riff" \
  --out ../../assets/tts/07_mid_talk
python prebake.py --text "I look at you all see the love there that's sleeping" \
  --out ../../assets/tts/08_gently_weeps
```

- [ ] **Step 3: Verify the output**

```bash
cd ../..
for d in assets/tts/*/; do
  echo "$d:"
  ls -la "$d"
  file "${d}audio.wav"
  cat "${d}meta.json"
done
```

Expected: each directory has `audio.wav` (mono, Piper's native rate — typically 22050 Hz) and `meta.json`.

- [ ] **Step 4: Commit**

```bash
git add assets/tts/
git commit -m "assets(tts): pre-baked clips for scenes 6, 7, 8"
```

**Note**: if the implementer cannot install Piper (e.g. CI environment), report DONE_WITH_CONCERNS and commit only `assets/tts/.gitkeep`. The integration test in Task 16 uses a fixture clip generated in Task 11; the app will fall back to silence on the modulator if these clips are missing.

---

## Task 8: TTSClip — in-memory clip data structure

**Files:**
- Create: `src/audio/TTSClip.h`

Simple value type: PCM data + sample rate + duration. Loaded by `PrebakedTTSSource`, consumed by `TTSClipPlayer`.

- [ ] **Step 1: Write `src/audio/TTSClip.h`**

```cpp
#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace guitar_dsp::audio {

// In-memory mono TTS clip. Owned by an `ITTSSource`, consumed by
// `TTSClipPlayer` on the audio thread. The samples vector is resized
// only off the audio thread.
struct TTSClip {
    std::string  name;             // for debug/UI
    double       sampleRate = 48000.0;
    std::vector<float> samples;    // mono float32, at sampleRate

    bool empty() const noexcept { return samples.empty(); }
    std::size_t lengthSamples() const noexcept { return samples.size(); }
    double durationSeconds() const noexcept {
        return sampleRate > 0.0 ? samples.size() / sampleRate : 0.0;
    }
};

using TTSClipPtr = std::shared_ptr<const TTSClip>;

} // namespace guitar_dsp::audio
```

- [ ] **Step 2: Commit**

```bash
git add src/audio/TTSClip.h
git commit -m "feat(audio): TTSClip in-memory mono clip struct"
```

(No test needed — pure POD; behavior gets exercised by Task 9 and beyond.)

---

## Task 9: TTSClipPlayer — audio-thread playback (TDD)

**Files:**
- Create: `src/audio/TTSClipPlayer.h`
- Create: `src/audio/TTSClipPlayer.cpp`
- Create: `tests/unit/audio/test_tts_clip_player.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

Plays a `TTSClipPtr` sample-by-sample, ending in silence. `setClip()` is called from the message thread (with `std::atomic<TTSClipPtr>` swap). `process()` runs on the audio thread and emits silence when no clip is active or when playback has finished.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/audio/test_tts_clip_player.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "audio/TTSClipPlayer.h"
#include "audio/TTSClip.h"
#include "harness/RealtimeSentinel.h"

#include <vector>

using guitar_dsp::audio::TTSClip;
using guitar_dsp::audio::TTSClipPlayer;
using guitar_dsp::audio::TTSClipPtr;
using guitar_dsp::tests::RealtimeSentinel;

namespace {
TTSClipPtr makeRamp(std::size_t n, float startVal, float endVal) {
    auto c = std::make_shared<TTSClip>();
    c->name = "ramp";
    c->sampleRate = 48000.0;
    c->samples.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n - 1);
        c->samples[i] = startVal + (endVal - startVal) * t;
    }
    return c;
}
}

TEST_CASE("TTSClipPlayer: no clip means silence", "[audio][tts]") {
    TTSClipPlayer p;
    p.prepare(48000.0, 256);

    std::vector<float> out(256, 0.42f);
    p.process(out.data(), out.size());
    for (float s : out) REQUIRE(s == 0.0f);
}

TEST_CASE("TTSClipPlayer: setClip + process emits the clip data", "[audio][tts]") {
    TTSClipPlayer p;
    p.prepare(48000.0, 256);

    auto clip = makeRamp(100, 0.1f, 1.0f);
    p.setClip(clip);

    std::vector<float> out(100);
    p.process(out.data(), 100);

    for (std::size_t i = 0; i < 100; ++i) {
        REQUIRE(out[i] == clip->samples[i]);
    }
}

TEST_CASE("TTSClipPlayer: emits silence after clip ends", "[audio][tts]") {
    TTSClipPlayer p;
    p.prepare(48000.0, 256);

    auto clip = makeRamp(50, 0.5f, 0.5f);
    p.setClip(clip);

    std::vector<float> out(100);
    p.process(out.data(), 100);

    for (std::size_t i = 0; i < 50; ++i) REQUIRE(out[i] == 0.5f);
    for (std::size_t i = 50; i < 100; ++i) REQUIRE(out[i] == 0.0f);
}

TEST_CASE("TTSClipPlayer: setClip after end restarts from sample 0", "[audio][tts]") {
    TTSClipPlayer p;
    p.prepare(48000.0, 256);

    auto clip = makeRamp(10, 0.1f, 1.0f);
    p.setClip(clip);

    std::vector<float> firstPass(20), secondPass(20);
    p.process(firstPass.data(),  firstPass.size());   // plays + ends
    p.setClip(clip);                                  // restart
    p.process(secondPass.data(), secondPass.size());

    for (std::size_t i = 0; i < 10; ++i) {
        REQUIRE(secondPass[i] == clip->samples[i]);
    }
}

TEST_CASE("TTSClipPlayer: zero allocations on audio thread", "[audio][tts][realtime]") {
    TTSClipPlayer p;
    p.prepare(48000.0, 512);
    p.setClip(makeRamp(48000, 0.0f, 0.5f));  // 1 s clip

    std::vector<float> out(512);

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int i = 0; i < 200; ++i) p.process(out.data(), out.size());
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
```

- [ ] **Step 2: Run to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests 2>&1 | tail -3
```

Expected: `TTSClipPlayer.h not found`.

- [ ] **Step 3: Write `src/audio/TTSClipPlayer.h`**

```cpp
#pragma once

#include <atomic>
#include <cstddef>

#include "TTSClip.h"

namespace guitar_dsp::audio {

// Plays a TTSClip on the audio thread. setClip() is called from the
// message thread; the audio thread sees the new clip on its next
// process() call (atomic shared_ptr swap). Emits silence when no clip
// is active or after the active clip has finished.
class TTSClipPlayer {
public:
    TTSClipPlayer();
    ~TTSClipPlayer() = default;

    void prepare(double sampleRate, int blockSize);
    void reset();

    // Message-thread API. Pass nullptr to clear.
    void setClip(TTSClipPtr clip);

    // Audio-thread API. Writes numSamples mono samples into `out`.
    void process(float* out, std::size_t numSamples) noexcept;

private:
    double sampleRate_ = 48000.0;

    std::atomic<bool>   newClipFlag_ {false};
    TTSClipPtr          pendingClip_;     // touched only on message thread
    TTSClipPtr          activeClip_;      // touched only on audio thread
    std::size_t         playPos_ = 0;     // audio thread
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 4: Write `src/audio/TTSClipPlayer.cpp`**

```cpp
#include "TTSClipPlayer.h"

#include <algorithm>
#include <cstring>

namespace guitar_dsp::audio {

TTSClipPlayer::TTSClipPlayer() = default;

void TTSClipPlayer::prepare(double sampleRate, int /*blockSize*/) {
    sampleRate_ = sampleRate;
    reset();
}

void TTSClipPlayer::reset() {
    playPos_ = 0;
}

void TTSClipPlayer::setClip(TTSClipPtr clip) {
    pendingClip_ = std::move(clip);
    newClipFlag_.store(true, std::memory_order_release);
}

void TTSClipPlayer::process(float* out, std::size_t numSamples) noexcept {
    // Pick up a pending clip if any. The shared_ptr move is allocation-
    // free; we deliberately move into `activeClip_` so the previous
    // active clip's destructor runs here (on the audio thread). The
    // previous clip's refcount is decremented; if this was the last
    // ref, the deallocation happens on the audio thread — that's a
    // soft real-time violation we accept for v1 (deallocations during
    // a hot scene change are rare and the cost is bounded). A future
    // hardening pass can swap in a deferred-deletion mechanism.
    if (newClipFlag_.exchange(false, std::memory_order_acquire)) {
        activeClip_ = std::move(pendingClip_);
        playPos_ = 0;
    }

    if (!activeClip_ || activeClip_->samples.empty()) {
        std::memset(out, 0, numSamples * sizeof(float));
        return;
    }

    const auto& samples = activeClip_->samples;
    const std::size_t remaining = (playPos_ < samples.size())
                                ? samples.size() - playPos_
                                : 0;
    const std::size_t toCopy = std::min(numSamples, remaining);

    if (toCopy > 0) {
        std::memcpy(out, samples.data() + playPos_, toCopy * sizeof(float));
        playPos_ += toCopy;
    }
    if (toCopy < numSamples) {
        std::memset(out + toCopy, 0, (numSamples - toCopy) * sizeof(float));
    }
}

} // namespace guitar_dsp::audio
```

**Note on the audio-thread refcount deallocation**: documented honestly in the comment. A future hardening commit can route old clips to a `juce::AbstractFifo` of "to be deleted on message thread" pointers. Not required for Phase 3 because clips are loaded once at startup and rarely freed.

- [ ] **Step 5: Update `src/CMakeLists.txt`**

In `add_library(guitar_dsp_audio STATIC ...)`, append `audio/TTSClipPlayer.cpp`.

- [ ] **Step 6: Update `tests/CMakeLists.txt`**

Add `unit/audio/test_tts_clip_player.cpp` to the test executable.

- [ ] **Step 7: Build + run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R TTSClipPlayer
```

Expected: 5 tests pass. Full suite: 52/52 (47 prior + 5 new).

- [ ] **Step 8: Commit**

```bash
git add src/audio/TTSClipPlayer.h src/audio/TTSClipPlayer.cpp src/CMakeLists.txt tests/unit/audio/test_tts_clip_player.cpp tests/CMakeLists.txt
git commit -m "feat(audio): TTSClipPlayer for audio-thread clip playback"
```

---

## Task 10: ITTSSource interface

**Files:**
- Create: `src/audio/ITTSSource.h`

The interface that Phases 3, 3.5, and 3.6 all implement. Pure header — implementation arrives in Task 11.

- [ ] **Step 1: Write `src/audio/ITTSSource.h`**

```cpp
#pragma once

#include <string>

#include "TTSClip.h"

namespace guitar_dsp::audio {

// Source of TTS clips for the vocoder's modulator input. Three concrete
// implementations are planned per spec §6.2:
//   - PrebakedTTSSource (Phase 3, this plan)
//   - AppleTTSSource    (Phase 3.5)
//   - PiperTTSSource    (Phase 3.6)
//
// Synthesize() is called from the message thread (scene activation,
// asset load). It returns a TTSClipPtr that's then handed to a
// TTSClipPlayer for audio-thread playback.
class ITTSSource {
public:
    virtual ~ITTSSource() = default;

    // Returns the clip for `key` (an opaque identifier — for the
    // prebaked source this is a clip directory name; for live sources
    // it's the text to synthesize). May return nullptr on failure;
    // caller falls back to a sibling source per the spec §6.5
    // fallback chain.
    virtual TTSClipPtr synthesize(const std::string& key) = 0;

    // For diagnostics / logging.
    virtual std::string sourceName() const = 0;
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 2: Commit**

```bash
git add src/audio/ITTSSource.h
git commit -m "feat(audio): ITTSSource interface (3-source swap point)"
```

---

## Task 11: PrebakedTTSSource (TDD)

**Files:**
- Create: `src/audio/PrebakedTTSSource.h`
- Create: `src/audio/PrebakedTTSSource.cpp`
- Create: `tests/unit/audio/test_prebaked_tts_source.cpp`
- Create: `tests/fixtures/tts/tiny_clip/audio.wav` (generated by test setup)
- Create: `tests/fixtures/tts/tiny_clip/meta.json`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

Loads `<root>/<key>/audio.wav` as a mono float buffer, resampling if needed to the prepared sample rate.

- [ ] **Step 1: Create the fixture clip**

Create `tests/fixtures/tts/tiny_clip/meta.json`:

```json
{
  "text": "test",
  "voice": "synthetic",
  "duration_s": 0.1
}
```

The `audio.wav` fixture will be generated by Task 11's first test via the existing `GoldenFile` harness, so we don't need to commit one separately. But for the test to find a real WAV, generate one now:

```bash
mkdir -p tests/fixtures/tts/tiny_clip
# Generate a 0.1 s 880 Hz sine, mono 48 kHz 24-bit WAV using existing infra.
# In the worktree, run the fixture-generator from Phase 1 with adjusted parameters.
# OR: write a tiny one-shot generator in the test itself.
```

The simplest approach: the test below WRITES the fixture if missing, using the `GoldenFile` harness, then reads it back. That way the fixture is reproducible and CI-portable.

- [ ] **Step 2: Write the failing tests**

Create `tests/unit/audio/test_prebaked_tts_source.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "audio/PrebakedTTSSource.h"
#include "harness/GoldenFile.h"

#include <cmath>
#include <filesystem>

using guitar_dsp::audio::PrebakedTTSSource;
using guitar_dsp::audio::TTSClipPtr;

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

void ensureFixtureWav() {
    const auto path = fixturePath("tts/tiny_clip/audio.wav");
    if (std::filesystem::exists(path)) return;

    constexpr double sr = 48000.0;
    constexpr int N = 4800;  // 0.1 s
    std::vector<float> samples(N);
    for (int i = 0; i < N; ++i) {
        samples[i] = 0.5f * std::sin(2.0 * 3.14159265358979 * 880.0 * i / sr);
    }
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    guitar_dsp::tests::GoldenFile::writeMonoWav(path, sr, samples.data(), N);
}

} // namespace

TEST_CASE("PrebakedTTSSource: loads a known clip into TTSClipPtr", "[audio][tts][prebaked]") {
    ensureFixtureWav();

    const auto root = fixturePath("tts");
    PrebakedTTSSource src{root};
    src.prepare(48000.0);

    auto clip = src.synthesize("tiny_clip");
    REQUIRE(clip);
    REQUIRE(clip->sampleRate == 48000.0);
    REQUIRE(clip->samples.size() == 4800);
    REQUIRE(clip->name == "tiny_clip");
}

TEST_CASE("PrebakedTTSSource: missing clip returns nullptr", "[audio][tts][prebaked]") {
    const auto root = fixturePath("tts");
    PrebakedTTSSource src{root};
    src.prepare(48000.0);

    auto clip = src.synthesize("nonexistent");
    REQUIRE_FALSE(clip);
}

TEST_CASE("PrebakedTTSSource: sourceName is descriptive", "[audio][tts][prebaked]") {
    PrebakedTTSSource src{"/dev/null"};
    REQUIRE(src.sourceName() == "prebaked");
}
```

- [ ] **Step 3: Run to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests 2>&1 | tail -3
```

Expected: `PrebakedTTSSource.h not found`.

- [ ] **Step 4: Write `src/audio/PrebakedTTSSource.h`**

```cpp
#pragma once

#include <string>

#include "ITTSSource.h"

namespace guitar_dsp::audio {

// Loads TTS clips from <rootDir>/<key>/audio.wav at synthesize() time.
// Resamples to the prepared sample rate. Returns nullptr on any
// failure (missing file, malformed WAV, wrong format).
class PrebakedTTSSource : public ITTSSource {
public:
    explicit PrebakedTTSSource(std::string rootDir);

    void prepare(double targetSampleRate);

    TTSClipPtr synthesize(const std::string& key) override;
    std::string sourceName() const override { return "prebaked"; }

private:
    std::string rootDir_;
    double      targetSampleRate_ = 48000.0;
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 5: Write `src/audio/PrebakedTTSSource.cpp`**

```cpp
#include "PrebakedTTSSource.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <filesystem>
#include <iostream>

namespace guitar_dsp::audio {

PrebakedTTSSource::PrebakedTTSSource(std::string rootDir)
    : rootDir_(std::move(rootDir)) {}

void PrebakedTTSSource::prepare(double targetSampleRate) {
    targetSampleRate_ = targetSampleRate;
}

TTSClipPtr PrebakedTTSSource::synthesize(const std::string& key) {
    namespace fs = std::filesystem;
    const fs::path audioPath = fs::path(rootDir_) / key / "audio.wav";
    if (!fs::exists(audioPath)) {
        std::cerr << "[PrebakedTTSSource] missing: " << audioPath << '\n';
        return nullptr;
    }

    juce::File inFile(audioPath.string());
    juce::WavAudioFormat format;
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        format.createReaderFor(inFile.createInputStream().release(), true));
    if (!reader) {
        std::cerr << "[PrebakedTTSSource] cannot read: " << audioPath << '\n';
        return nullptr;
    }

    const int srcLen = static_cast<int>(reader->lengthInSamples);
    const double srcRate = reader->sampleRate;

    juce::AudioBuffer<float> srcBuf(1, srcLen);
    reader->read(&srcBuf, 0, srcLen, 0, true, false);

    auto clip = std::make_shared<TTSClip>();
    clip->name = key;
    clip->sampleRate = targetSampleRate_;

    if (std::abs(srcRate - targetSampleRate_) < 0.5) {
        // No resample needed.
        clip->samples.assign(srcBuf.getReadPointer(0),
                             srcBuf.getReadPointer(0) + srcLen);
    } else {
        // Linear resample (good enough for vocoder modulator).
        const double ratio = srcRate / targetSampleRate_;
        const int outLen = static_cast<int>(srcLen / ratio);
        clip->samples.resize(static_cast<std::size_t>(outLen));
        const float* src = srcBuf.getReadPointer(0);
        for (int i = 0; i < outLen; ++i) {
            const double srcIdx = i * ratio;
            const int    i0     = static_cast<int>(srcIdx);
            const float  frac   = static_cast<float>(srcIdx - i0);
            const int    i1     = std::min(i0 + 1, srcLen - 1);
            clip->samples[static_cast<std::size_t>(i)] =
                (1.0f - frac) * src[i0] + frac * src[i1];
        }
    }

    return clip;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 6: Update `src/CMakeLists.txt`**

In `add_library(guitar_dsp_audio STATIC ...)`, append `audio/PrebakedTTSSource.cpp`.

In the same `target_link_libraries` for `guitar_dsp_audio`, add `juce::juce_audio_formats` to PUBLIC (it's needed for the WAV reader; was previously only linked into the test binary).

- [ ] **Step 7: Update `tests/CMakeLists.txt`**

Add `unit/audio/test_prebaked_tts_source.cpp` to the test executable.

- [ ] **Step 8: Build + run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R PrebakedTTSSource
```

Expected: 3 tests pass.

- [ ] **Step 9: Commit**

```bash
git add src/audio/PrebakedTTSSource.h src/audio/PrebakedTTSSource.cpp src/CMakeLists.txt tests/unit/audio/test_prebaked_tts_source.cpp tests/fixtures/tts/tiny_clip/meta.json tests/CMakeLists.txt
git commit -m "feat(audio): PrebakedTTSSource loads .wav clips with resampling"
```

(The `audio.wav` fixture is generated on first test run via the `ensureFixtureWav` helper, so it doesn't need to be in the commit. Add `tests/fixtures/tts/tiny_clip/audio.wav` to a `tests/.gitignore` if you want to be explicit.)

---

## Task 12: AudioGraph — install the vocoder branch (TDD)

**Files:**
- Modify: `src/audio/AudioGraph.h`
- Modify: `src/audio/AudioGraph.cpp`
- Modify: `tests/unit/audio/test_audio_graph.cpp`

The `AudioGraph` currently has a no-op wet branch (silent buffer fed to the mixer). Phase 3 replaces that with the vocoder pipeline: `TTSClipPlayer → wetBuffer → ChannelVocoder(carrier=postInput, modulator=wetBuffer) → mixer wet input`.

- [ ] **Step 1: Append failing test**

Append at the bottom of `tests/unit/audio/test_audio_graph.cpp`:

```cpp
TEST_CASE("AudioGraph: with TTS clip active + dryWet=1, output is vocoded",
          "[audio][graph][vocoder]") {
    AudioGraph graph;
    graph.prepare(48000.0, 512);
    graph.mixer().setDryWet(1.0f);
    graph.mixer().setMasterGainDb(0.0f);
    graph.mixer().reset();

    // Inject a clip: 0.5 s of 800 Hz modulator.
    auto clip = std::make_shared<guitar_dsp::audio::TTSClip>();
    clip->sampleRate = 48000.0;
    clip->samples.resize(24000);
    for (int i = 0; i < 24000; ++i) {
        clip->samples[i] = 0.6f * std::sin(2.0 * 3.14159265 * 800.0 * i / 48000.0);
    }
    graph.ttsClipPlayer().setClip(clip);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000), out(48000);
    gen.sine(800.0f, 0.6f, in.data(), in.size());

    // Process the full second in 512-sample blocks.
    for (std::size_t i = 0; i < in.size(); i += 512) {
        const auto n = std::min<std::size_t>(512, in.size() - i);
        graph.process(in.data() + i, out.data() + i, n);
    }

    // First half: vocoder active → output non-silent.
    float peakFirst = 0.0f;
    for (int i = 6000; i < 12000; ++i) peakFirst = std::max(peakFirst, std::abs(out[i]));
    REQUIRE(peakFirst > 0.05f);

    // Second half: clip ended → modulator silent → output near silent.
    float peakSecond = 0.0f;
    for (int i = 40000; i < 48000; ++i) peakSecond = std::max(peakSecond, std::abs(out[i]));
    REQUIRE(peakSecond < 0.02f);
}
```

You'll also need `#include "audio/TTSClip.h"` at the top of the test file, and probably `<cmath>`.

- [ ] **Step 2: Run to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests 2>&1 | tail -3
```

Expected: `graph.ttsClipPlayer()` doesn't exist.

- [ ] **Step 3: Update `src/audio/AudioGraph.h`**

Add includes near the top:

```cpp
#include "ChannelVocoder.h"
#include "TTSClipPlayer.h"
```

In the public section (after `mixer()` accessor), add:

```cpp
TTSClipPlayer& ttsClipPlayer() { return ttsClipPlayer_; }
ChannelVocoder& vocoder() { return vocoder_; }
```

In the private section (after `mixer_`), add:

```cpp
TTSClipPlayer  ttsClipPlayer_;
ChannelVocoder vocoder_;
```

- [ ] **Step 4: Update `src/audio/AudioGraph.cpp`**

In `prepare()`, after the existing component prepares, add:

```cpp
ttsClipPlayer_.prepare(sampleRate, blockSize);
vocoder_.prepare(sampleRate, blockSize);
vocoder_.setWetLevel(1.0f);
vocoder_.setSibilance(0.5f);
```

In `reset()`, add:

```cpp
ttsClipPlayer_.reset();
vocoder_.reset();
```

Replace the `process` body's wet-path section. The current `process` is:

```cpp
inputStage_.process(in, postInputBuffer_.data(), numSamples);
mixer_.process(postInputBuffer_.data(), wetBuffer_.data(), out, numSamples);
```

Change to:

```cpp
inputStage_.process(in, postInputBuffer_.data(), numSamples);

// Fill wetBuffer_ with the modulator (TTS playback). When no clip is
// active, this is silence and the vocoder will output silence too.
ttsClipPlayer_.process(wetBuffer_.data(), numSamples);

// Vocoder: carrier = post-input guitar, modulator = TTS playback.
// Re-uses wetBuffer_ in place (aliasing safe — vocoder reads then writes
// per sample).
vocoder_.process(postInputBuffer_.data(), wetBuffer_.data(),
                 wetBuffer_.data(), numSamples);

// Mixer: dry = post-input guitar, wet = vocoder output.
mixer_.process(postInputBuffer_.data(), wetBuffer_.data(), out, numSamples);
```

- [ ] **Step 5: Build + run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R AudioGraph
```

Expected: existing AudioGraph tests still pass + 1 new test passes.

- [ ] **Step 6: Run full suite to confirm no regressions**

```bash
ctest --test-dir build --output-on-failure 2>&1 | tail -3
```

Expected: 56/56 (52 prior + 1 new vocoded-output test + 3 PrebakedTTSSource tests).

- [ ] **Step 7: Commit**

```bash
git add src/audio/AudioGraph.h src/audio/AudioGraph.cpp tests/unit/audio/test_audio_graph.cpp
git commit -m "feat(audio): AudioGraph installs vocoder + TTSClipPlayer in wet branch"
```

---

## Task 13: Scene JSON — extend with `tts` block (TDD)

**Files:**
- Modify: `src/scenes/Scene.h`
- Modify: `src/scenes/SceneLibrary.cpp`
- Create: `tests/unit/scenes/test_scene_library_tts.cpp`
- Create: `tests/fixtures/scenes/with_tts.json`
- Modify: `tests/CMakeLists.txt`

Add `TtsConfig` to `Scene` and extend `SceneLibrary` to parse the `tts` JSON block. Phase 3 only honors `source: "prebaked"` + `clip: "<name>"`. Other values are ignored (for forward compat with Phase 3.5 / 3.6).

- [ ] **Step 1: Create the fixture**

`tests/fixtures/scenes/with_tts.json`:

```json
{
  "id": 42,
  "name": "Speaking — finale",
  "color": "#a64dff",
  "mixer": { "masterGainDb": -3.0, "dryWet": 0.85, "transitionMs": 30 },
  "tts": { "source": "prebaked", "clip": "08_gently_weeps" }
}
```

- [ ] **Step 2: Write the failing test**

Create `tests/unit/scenes/test_scene_library_tts.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "scenes/SceneLibrary.h"

#include <filesystem>

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

TEST_CASE("SceneLibrary: parses tts block with prebaked source", "[scenes][library][tts]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_tts.json"));
    REQUIRE(s.has_value());
    REQUIRE(s->tts.source == "prebaked");
    REQUIRE(s->tts.clip == "08_gently_weeps");
}

TEST_CASE("SceneLibrary: missing tts block leaves defaults", "[scenes][library][tts]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/valid_minimal.json"));
    REQUIRE(s.has_value());
    REQUIRE(s->tts.source.empty());
    REQUIRE(s->tts.clip.empty());
}
```

- [ ] **Step 3: Run to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests 2>&1 | tail -3
```

Expected: `Scene::tts` doesn't exist.

- [ ] **Step 4: Update `src/scenes/Scene.h`**

Add a TtsConfig struct and Scene field:

```cpp
struct TtsConfig {
    std::string source;   // "prebaked" | "apple" | "piper" | "" (none)
    std::string clip;     // identifier passed to ITTSSource::synthesize()
};

struct Scene {
    int           id        = 0;
    std::string   name      = "(unnamed)";
    std::uint32_t colorRgb  = 0xCCCCCCu;
    MixerParams   mixer{};
    TtsConfig     tts{};

    static Scene defaults(int id);
};
```

- [ ] **Step 5: Update `src/scenes/SceneLibrary.cpp` to parse the tts block**

In `loadOne()`, after the existing `mixer` block parsing, add:

```cpp
if (obj->hasProperty("tts")) {
    if (auto* t = obj->getProperty("tts").getDynamicObject()) {
        if (t->hasProperty("source"))
            s.tts.source = t->getProperty("source").toString().toStdString();
        if (t->hasProperty("clip"))
            s.tts.clip = t->getProperty("clip").toString().toStdString();
    }
}
```

- [ ] **Step 6: Update `tests/CMakeLists.txt`**

Add `unit/scenes/test_scene_library_tts.cpp` to the test executable.

- [ ] **Step 7: Build + run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R "SceneLibrary"
```

Expected: existing 4 SceneLibrary tests still pass + 2 new TTS tests = 6.

- [ ] **Step 8: Commit**

```bash
git add src/scenes/Scene.h src/scenes/SceneLibrary.cpp tests/unit/scenes/test_scene_library_tts.cpp tests/fixtures/scenes/with_tts.json tests/CMakeLists.txt
git commit -m "feat(scenes): parse tts block (source + clip) in Scene JSON"
```

---

## Task 14: SceneEngine — expose active TTS clip key (TDD)

**Files:**
- Modify: `src/scenes/SceneEngine.h`
- Modify: `src/scenes/SceneEngine.cpp`
- Modify: `tests/unit/scenes/test_scene_engine.cpp`

The audio thread needs to know "what TTS clip key should be playing right now." Add a separate atomic snapshot string for the active TTS clip key. The audio thread reads it; the orchestrator (PluginProcessor) compares against the currently-playing key and calls `TTSClipPlayer::setClip` when it changes.

Using `std::atomic<std::string>` would require an actual mutex on every platform. Instead, expose the clip key as a `std::atomic<int>` (the active scene id, which we already have!) plus a const-reference accessor for the scene struct on the message thread. The PluginProcessor handles the diff in its `processBlock` setup.

- [ ] **Step 1: Append the failing test**

Append to `tests/unit/scenes/test_scene_engine.cpp`:

```cpp
TEST_CASE("SceneEngine: activeTtsKey reflects the active scene's TTS clip",
          "[scenes][engine][tts]") {
    SceneEngine eng;
    Scene s0 = Scene::defaults(0);
    Scene s1 = Scene::defaults(1);
    s1.tts.source = "prebaked";
    s1.tts.clip = "test_clip";
    eng.loadScenes({s0, s1});

    REQUIRE(eng.activeTtsKey().empty());  // scene 0 has no tts

    eng.activateScene(1);
    REQUIRE(eng.activeTtsKey() == "test_clip");

    eng.activateScene(0);
    REQUIRE(eng.activeTtsKey().empty());
}
```

- [ ] **Step 2: Run to confirm failure**

Expected: `activeTtsKey` undeclared.

- [ ] **Step 3: Update `src/scenes/SceneEngine.h`**

Add a public method declaration in the message-thread API section:

```cpp
// Returns the active scene's TTS clip key, or empty string if none.
// Message-thread only.
std::string activeTtsKey() const;
```

- [ ] **Step 4: Update `src/scenes/SceneEngine.cpp`**

Add the implementation:

```cpp
std::string SceneEngine::activeTtsKey() const {
    if (activeIndex_ < 0) return {};
    return scenes_[static_cast<std::size_t>(activeIndex_)].tts.clip;
}
```

- [ ] **Step 5: Build + run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R "activeTtsKey"
```

Expected: pass.

- [ ] **Step 6: Commit**

```bash
git add src/scenes/SceneEngine.h src/scenes/SceneEngine.cpp tests/unit/scenes/test_scene_engine.cpp
git commit -m "feat(scenes): SceneEngine::activeTtsKey() exposes active TTS clip"
```

---

## Task 15: PluginProcessor — wire TTS source + clip changes

**Files:**
- Modify: `src/app/PluginProcessor.h`
- Modify: `src/app/PluginProcessor.cpp`

`PluginProcessor` owns a `PrebakedTTSSource` rooted at `AssetLocator::ttsDirectory()`. On every `processBlock`, it checks if the active scene's TTS key changed vs. the currently-playing one; on change, it loads the new clip via the source and calls `graph_.ttsClipPlayer().setClip(...)`.

Asset locator gets a tiny addition: `AssetLocator::ttsDirectory()`.

- [ ] **Step 1: Add `ttsDirectory()` to AssetLocator**

In `src/app/AssetLocator.h`, add:

```cpp
static std::string ttsDirectory();
```

In `src/app/AssetLocator.cpp`, implement:

```cpp
std::string AssetLocator::ttsDirectory() {
    const auto root = assetsRoot();
    if (root.empty()) return {};
    return (fs::path(root) / "tts").string();
}
```

- [ ] **Step 2: Update `src/app/PluginProcessor.h`**

Add includes:

```cpp
#include "audio/PrebakedTTSSource.h"
```

In private section, add:

```cpp
std::unique_ptr<audio::PrebakedTTSSource> prebakedTtsSource_;
std::string                                currentTtsClipKey_;  // audio thread
```

- [ ] **Step 3: Update `src/app/PluginProcessor.cpp::prepareToPlay`**

After the existing scene loading, add:

```cpp
prebakedTtsSource_ = std::make_unique<audio::PrebakedTTSSource>(
    AssetLocator::ttsDirectory());
prebakedTtsSource_->prepare(sampleRate);
currentTtsClipKey_.clear();
graph_.ttsClipPlayer().setClip(nullptr);
```

- [ ] **Step 4: Add the diff-and-load logic to `processBlock`**

In `processBlock`, near the top (just after the existing `sceneEngine_.currentMixerParams()` block), add:

```cpp
// Check if the active TTS clip key changed. This runs on the audio
// thread, but `sceneEngine_.activeTtsKey()` is a message-thread API
// — it accesses non-atomic state. To keep it safe, we instead read
// only what's atomic-safe: the active scene id. If it changed from
// the last call, we DEFER the actual clip lookup + setClip() to
// the message thread via callAsync. Audio-thread is allocation-free.
const int activeSceneId = sceneEngine_.getActiveSceneId();
if (activeSceneId != lastSeenSceneId_) {
    lastSeenSceneId_ = activeSceneId;
    juce::MessageManager::callAsync([this, activeSceneId] {
        // Re-check on the message thread (scene may have changed again).
        if (sceneEngine_.getActiveSceneId() != activeSceneId) return;
        const std::string key = sceneEngine_.activeTtsKey();
        if (key == currentTtsClipKey_) return;
        currentTtsClipKey_ = key;

        if (key.empty() || !prebakedTtsSource_) {
            graph_.ttsClipPlayer().setClip(nullptr);
            return;
        }
        auto clip = prebakedTtsSource_->synthesize(key);
        graph_.ttsClipPlayer().setClip(clip);  // nullptr is OK (silences)
    });
}
```

Also add `int lastSeenSceneId_ = -1;` as a private member (audio thread only).

(`activeTtsKey()` and `currentTtsClipKey_` are documented as message-thread only; the comparison in the lambda is the only place they're touched outside that contract.)

- [ ] **Step 5: Build + run all tests**

```bash
cmake --build build --target guitar_dsp_app_Standalone guitar_dsp_tests
ctest --test-dir build --output-on-failure 2>&1 | tail -3
```

Expected: builds; all tests still pass.

- [ ] **Step 6: Commit**

```bash
git add src/app/AssetLocator.h src/app/AssetLocator.cpp src/app/PluginProcessor.h src/app/PluginProcessor.cpp
git commit -m "feat(app): wire PrebakedTTSSource to active scene's TTS clip"
```

---

## Task 16: Integration test — speaking scene end-to-end

**Files:**
- Create: `tests/integration/test_speaking_scene.cpp`
- Modify: `tests/CMakeLists.txt`

Build a `SceneEngine` + `AudioGraph` + manually-injected `PrebakedTTSSource`, then verify that activating a TTS-tagged scene results in vocoded output during clip playback and silence after.

- [ ] **Step 1: Write the test**

Create `tests/integration/test_speaking_scene.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "audio/AudioGraph.h"
#include "audio/PrebakedTTSSource.h"
#include "harness/GoldenFile.h"
#include "harness/SyntheticGuitar.h"
#include "scenes/SceneEngine.h"

#include <cmath>
#include <filesystem>
#include <vector>

using guitar_dsp::audio::AudioGraph;
using guitar_dsp::audio::PrebakedTTSSource;
using guitar_dsp::scenes::Scene;
using guitar_dsp::scenes::SceneEngine;
using guitar_dsp::tests::SyntheticGuitar;

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

void ensureFixtureWav() {
    const auto path = fixturePath("tts/tiny_clip/audio.wav");
    if (std::filesystem::exists(path)) return;
    constexpr double sr = 48000.0;
    constexpr int N = 4800;
    std::vector<float> samples(N);
    for (int i = 0; i < N; ++i) {
        samples[i] = 0.5f * std::sin(2.0 * 3.14159265 * 880.0 * i / sr);
    }
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    guitar_dsp::tests::GoldenFile::writeMonoWav(path, sr, samples.data(), N);
}

} // namespace

TEST_CASE("integration: activate speaking scene -> vocoder produces output then silence",
          "[integration][speaking]") {
    ensureFixtureWav();

    // Wire it up.
    SceneEngine engine;
    Scene clean = Scene::defaults(0);
    Scene speak = Scene::defaults(1);
    speak.tts.source = "prebaked";
    speak.tts.clip = "tiny_clip";
    speak.mixer.dryWet = 1.0f;  // fully wet so the vocoder dominates
    engine.loadScenes({clean, speak});

    AudioGraph graph;
    graph.prepare(48000.0, 512);

    PrebakedTTSSource src{fixturePath("tts")};
    src.prepare(48000.0);

    // Activate the speaking scene and load the clip.
    engine.activateScene(1);
    auto clip = src.synthesize(engine.activeTtsKey());
    REQUIRE(clip);
    graph.ttsClipPlayer().setClip(clip);

    // Drive the mixer with the scene's params, then run audio.
    graph.mixer().setDryWet(speak.mixer.dryWet);
    graph.mixer().setMasterGainDb(speak.mixer.masterGainDb);
    graph.mixer().reset();

    SyntheticGuitar gen{48000.0};
    constexpr int N = 48000;
    std::vector<float> in(N), out(N);
    gen.sine(880.0f, 0.5f, in.data(), N);

    for (int i = 0; i < N; i += 512) {
        const int n = std::min(512, N - i);
        graph.process(in.data() + i, out.data() + i, static_cast<std::size_t>(n));
    }

    auto rms = [](const float* p, int n) {
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += p[i] * p[i];
        return std::sqrt(s / n);
    };
    const double rmsDuringClip = rms(out.data() + 1000, 3000);
    const double rmsAfterClip  = rms(out.data() + 40000, 4000);
    INFO("during=" << rmsDuringClip << " after=" << rmsAfterClip);
    REQUIRE(rmsDuringClip > 0.01);
    REQUIRE(rmsAfterClip  < rmsDuringClip * 0.3);
}
```

- [ ] **Step 2: Update `tests/CMakeLists.txt`**

Add `integration/test_speaking_scene.cpp` to the test executable.

- [ ] **Step 3: Build + run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R speaking
```

Expected: pass.

- [ ] **Step 4: Run full suite**

```bash
ctest --test-dir build --output-on-failure 2>&1 | tail -3
```

Expected: roughly 60/60 (count varies based on prior task additions).

- [ ] **Step 5: Commit**

```bash
git add tests/integration/test_speaking_scene.cpp tests/CMakeLists.txt
git commit -m "test(integration): end-to-end speaking scene with vocoder + prebaked TTS"
```

---

## Task 17: Update scenes 6/7/8 JSON to point at the TTS clips

**Files:**
- Modify: `assets/scenes/06_speaking_a.json`
- Modify: `assets/scenes/07_speaking_b.json`
- Modify: `assets/scenes/08_speaking_finale.json`

Add the `tts` block and bump `dryWet` so the vocoder is audible. (Phase 2 had these scenes at fully dry; the speaking content was always silent.)

- [ ] **Step 1: Update `06_speaking_a.json`**

```json
{
  "id": 6,
  "name": "Speaking A — hello",
  "color": "#a64dff",
  "mixer": { "masterGainDb": -3.0, "dryWet": 0.85, "transitionMs": 30 },
  "tts": { "source": "prebaked", "clip": "06_hello_cleveland" }
}
```

- [ ] **Step 2: Update `07_speaking_b.json`**

```json
{
  "id": 7,
  "name": "Speaking B — mid-talk",
  "color": "#a64dff",
  "mixer": { "masterGainDb": -3.0, "dryWet": 0.85, "transitionMs": 30 },
  "tts": { "source": "prebaked", "clip": "07_mid_talk" }
}
```

- [ ] **Step 3: Update `08_speaking_finale.json`**

```json
{
  "id": 8,
  "name": "Speaking finale — gently weeps",
  "color": "#a64dff",
  "mixer": { "masterGainDb": -2.0, "dryWet": 0.9, "transitionMs": 30 },
  "tts": { "source": "prebaked", "clip": "08_gently_weeps" }
}
```

- [ ] **Step 4: Validate JSON**

```bash
for f in assets/scenes/0{6,7,8}_*.json; do
  python3 -c "import json; json.load(open('$f'))" || echo "FAIL $f"
done
```

Expected: no FAIL output.

- [ ] **Step 5: Build + smoke**

```bash
cmake --build build --target guitar_dsp_app_Standalone
ctest --test-dir build --output-on-failure 2>&1 | tail -3
```

Expected: clean. Manual: launch the app and press `7` (scene 6 = key `7`); should hear the prebaked clip vocoded by the guitar input.

- [ ] **Step 6: Commit**

```bash
git add assets/scenes/0{6,7,8}_*.json
git commit -m "feat(scenes): scenes 6/7/8 now play TTS clips via vocoder"
```

---

## Task 18: README — document the speaking-guitar workflow

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Replace the "Project status" section**

Replace the trailing Project status section with:

```markdown
## Project status

This branch implements **Phase 3: Vocoder + Prebaked TTS**. Scenes 6, 7, and 8 now activate a 24-band channel vocoder whose modulator is a pre-baked TTS audio clip — the live guitar becomes the carrier, producing a "guitar speaks the words" effect.

### Generating TTS clips

Speaking-scene clips are generated offline by `tools/tts_prebake/prebake.py` (Piper-based — see `tools/tts_prebake/README.md` for setup). The committed clips under `assets/tts/` cover scenes 6, 7, and 8:

- `06_hello_cleveland/` — "hello cleveland"
- `07_mid_talk/` — "I think therefore I riff"
- `08_gently_weeps/` — opening lyric from the title track

To change a clip's text:

```bash
cd tools/tts_prebake && source .venv/bin/activate
python prebake.py --text "your new text here" --out ../../assets/tts/06_hello_cleveland
```

then rebuild the app — the post-build asset copy picks it up.

### Live behavior

Switch to scene 7 (key `7`, or PC 6 on the FCB1010) and play guitar. The vocoder shapes the guitar's harmonics with the TTS clip's envelope, producing intelligible-but-very-clearly-guitar speech. The active scene's `dryWet` controls how much of the dry guitar bleeds through.

### Subsequent phases (see plans directory)

- **Phase 3.5**: Apple `AVSpeechSynthesizer` source — live TTS for the future audience-text encore.
- **Phase 3.6**: Piper subprocess source — alternative live engine with bundled binary.
- **Phase 4**: Instrument Carousel — real per-scene DSP for scenes 1–5.
- **Phase 5**: Full-screen visualization (spectrogram, karaoke text overlay).
- **Phase 6**: Hardening + dress rehearsal.
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: README documents speaking-guitar workflow and clip generation"
```

---

## Task 19: Phase 3 wrap-up verification

- [ ] **Step 1: Clean build from scratch**

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target guitar_dsp_app_Standalone guitar_dsp_tests
```

Expected: clean.

- [ ] **Step 2: Full test pass**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all pass. Test count should be around 60–62 (Phase 2's 45 plus ~17 new across vocoder/clip-player/source/scene-tts/integration).

- [ ] **Step 3: Verify the bundle has TTS clips**

```bash
ls "build/src/app/guitar_dsp_app_artefacts/Release/Standalone/Guitar DSP.app/Contents/Resources/assets/tts/"
```

Expected: three directories `06_hello_cleveland`, `07_mid_talk`, `08_gently_weeps`, each with `audio.wav` and `meta.json`.

- [ ] **Step 4: Manual smoke**

```bash
open "build/src/app/guitar_dsp_app_artefacts/Release/Standalone/Guitar DSP.app"
```

Verify:
- App launches.
- Scenes 0–5 work as in Phase 2 (clean / carousel placeholders).
- Pressing `7` activates "Speaking A — hello" and plays the TTS clip through the vocoder while you play guitar.
- Pressing `8` (then `9`) activates the other two speaking scenes.
- Pressing `0` returns to clean.

- [ ] **Step 5: Final commit if anything was fixed**

If any cleanup commits were made during verification, commit them under message `chore: phase 3 wrap-up fixes`.

---

## Subsequent phase plans (preview)

- **Phase 3.5: Apple TTS source** — `AppleTTSSource` wrapping `AVSpeechSynthesizer` via Objective-C++. Adds a `LiveTTSSource` abstract base or extends `ITTSSource::synthesize` to handle "synthesize text right now" semantics. Wires through `PluginProcessor`'s source registry. Adds a `TTSPrewarmer` that synthesizes likely-next live clips in the background to hide the ~300 ms first-sample latency. ~10–12 tasks.

- **Phase 3.6: Piper subprocess source** — `PiperTTSSource` spawns the bundled Piper CLI as a subprocess, pipes text to stdin, reads PCM from stdout, packages as `TTSClip`. Bundles a Piper voice (~63 MB) into the .app via `Resources/`. Adds the third-source-fallback path to `PluginProcessor` so a live-source failure cascades cleanly (Piper → Apple → Prebaked). ~8–10 tasks.

- **Phase 4: Instrument Carousel** — replace scenes 1–5's placeholder gain-only behavior with real DSP per spec §5.1 (pitch shifter, octaver, formant shifter, filter, bit crusher, comb, tube sat, chorus, reverb stages, configured per scene). ~25–30 tasks.

- **Phase 5: Visualization** — JUCE OpenGL spectrogram backdrop, karaoke text overlay (consumes `alignment.json` from a Phase-5-extended pre-bake), Hydra-style full-screen show layout. ~15–20 tasks.

- **Phase 6: Hardening + pre-conference** — automated 4-hour soak in CI, golden-file renders for all scenes, latency measurement, dress rehearsal pass. ~10–12 tasks.

---

## Self-review

**Spec coverage (spec § → Phase 3 task):**
- §5.2 (channel vocoder): Tasks 1–5 (IVocoder interface + ChannelVocoder DSP + sibilance).
- §5.3 (IVocoder swap point): Task 1 establishes the interface.
- §6.1 (ITTSSource interface): Task 10.
- §6.2 (three sources): Task 11 ships `PrebakedTTSSource`; Apple/Piper deferred to 3.5/3.6 as explicit scope cuts.
- §6.3 (pre-bake pipeline): Tasks 6–7 ship a simplified Piper-based pre-bake tool. `alignment.json` / `formants.json` deferred to Phase 5 (karaoke) as noted.
- §6.4 (prewarming): N/A for prebaked; deferred to Phase 3.5 with live sources.
- §6.5 (failure handling): Honored — `PrebakedTTSSource` returns nullptr on failure; `TTSClipPlayer` emits silence on null clip. Full fallback chain across 3 sources arrives in Phases 3.5/3.6.
- §7.2 (scene `tts` block): Task 13 parses it.

**Placeholder scan:** No "TBD"/"TODO" left in plan text.

**Type consistency:** 
- `ChannelVocoder` API: `prepare`/`reset`/`process`/`setWetLevel`/`setSibilance` consistent across Tasks 1–5.
- `TTSClip` / `TTSClipPtr` / `TTSClipPlayer::setClip(TTSClipPtr)` consistent across Tasks 8/9/11/12/15/16.
- `ITTSSource::synthesize(std::string) → TTSClipPtr` consistent across Tasks 10/11/15/16.
- `Scene::tts.source` / `Scene::tts.clip` consistent across Tasks 13/14/17.
- `SceneEngine::activeTtsKey()` (Task 14) used in Tasks 15/16.
- `AudioGraph::ttsClipPlayer()` / `vocoder()` (Task 12) used in Tasks 15/16.

**Spec deferrals not violated:** Apple TTS, Piper TTS, full prewarming, karaoke text, neural vocoder — none touched.

---

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-30-phase-3-vocoder-and-prebaked-tts.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
