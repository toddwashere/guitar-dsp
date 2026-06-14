# Mic Talkbox (Scene 3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Spec:** [docs/superpowers/specs/2026-06-13-mic-talkbox-design.md](../specs/2026-06-13-mic-talkbox-design.md)

**Goal:** Add a Scene 3 patch where the user's mic feeds the vocoder modulator continuously — a real-time talkbox. The user vocalizes vowels into the mic; the guitar's spectrum follows the voice.

**Architecture:** A new `MicShaper` class (noise gate + makeup gain + hard limiter) sits in `AudioGraph` between a per-block mic-sample buffer and the vocoder's modulator input. `ModulatorSource::Mic` is added as a fourth value alongside the Phase A `ClipBank`. `PluginProcessor::processBlock` extracts the mic block from the sidechain bus (AU) or main bus (standalone) and calls `AudioGraph::setMicBlock()` on every block while the active scene's `tts.source == "mic"`. WordReadout shows a "🎤 MIC" indicator on those scenes; VocoderPanel gains a global mic level meter for visibility on every scene.

**Tech Stack:** C++20, JUCE, Catch2, CMake + Ninja.

**Important constraint inherited from Phase A:** The carousel chain (`WetSource::Carousel`) and the vocoder branch (`WetSource::Vocoder`) are *mutually exclusive* on the wet bus. The scene-change handler early-returns when `carouselCfg.enabled` is true and bypasses the vocoder entirely. The Phase B spec's Scene 3 JSON proposes `carousel.enabled: true` for "rock dressing," but that would silently break the mic→modulator routing the same way it broke Phase A's clipBank loading. **This plan therefore drops the carousel block from Scene 3 JSON** and updates the spec to match. "Carousel-post-vocoder" routing is a separate follow-on that isn't in Phase B's scope.

**Build/test commands used throughout:**
```bash
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "<tag>" -s   # focused
./build/tests/guitar_dsp_tests              # full suite (run from repo root)
```

---

## Task 1: Update the spec — drop Scene 3's carousel block

The spec needs to reflect the carousel/vocoder mutual exclusivity discovered during Phase A. Update the spec FIRST so subsequent tasks have a coherent contract to point at.

**Files:**
- Modify: `docs/superpowers/specs/2026-06-13-mic-talkbox-design.md`

- [ ] **Step 1: Edit the spec's Scene 3 JSON example**

Find the `### Scene 3 JSON` section (around line 206-228). Replace the JSON block with:

```json
{
  "id": 3,
  "name": "Mic Talkbox",
  "color": "#22a2c0",
  "mixer": { "masterGainDb": -6.0, "dryWet": 0.9, "transitionMs": 30 }
}
```

Wait — Scene 3 also needs a `tts.source == "mic"` declaration so the activation branch fires. The correct replacement is:

```json
{
  "id": 3,
  "name": "Mic Talkbox",
  "color": "#22a2c0",
  "mixer": { "masterGainDb": -6.0, "dryWet": 0.9, "transitionMs": 30 },
  "tts": {
    "source": "mic",
    "clarity": 0.0
  }
}
```

Also update the prose immediately following the JSON. Replace:

> Carousel preset is a milder rock voicing than Scene 2 — talkbox tradition is
> "clean-ish with sustain," not "shredding." Tunable during implementation polish.

With:

> No carousel block on Scene 3 v1. Phase A discovered the carousel chain and
> the vocoder branch are mutually exclusive on the wet bus — enabling carousel
> would silently bypass the vocoder and the mic-as-modulator routing. A
> "carousel-post-vocoder" routing path that supports drive + harmonizer +
> reverb on top of vocoder output is a separate follow-on, not in Phase B's
> scope. See `vocal_guitar_phase_a_status.md` in user memory for the
> constraint.

- [ ] **Step 2: Commit the spec update**

```bash
git add docs/superpowers/specs/2026-06-13-mic-talkbox-design.md
git commit -m "docs(specs): mic-talkbox Scene 3 drops carousel block (Phase A constraint)"
```

---

## Task 2: `MicShaper` skeleton (silent stub)

Create a compilable but behavior-less `MicShaper` so the next tests can be written against a real class.

**Files:**
- Create: `src/audio/MicShaper.h`
- Create: `src/audio/MicShaper.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/audio/MicShaper.h`:

```cpp
#pragma once

#include <cstddef>

namespace guitar_dsp::audio {

// Fixed noise gate + makeup gain stage for routing the mic into the vocoder
// modulator input. Gate prevents room noise from gating the vocoder open
// during silence; makeup gain compensates for typical mic level (-30 dBFS
// peaks → -6 dBFS peaks) so the modulator excites the envelope follower
// reliably. Output is hard-limited to ±1.0 so a clipped modulator doesn't
// smear envelopes across the vocoder bands.
//
// Coefficients are not user-tunable in v1.
class MicShaper {
public:
    void prepare(double sampleRate);
    void reset();

    void process(const float* in, float* out, std::size_t numSamples) noexcept;

private:
    static constexpr float kGateThreshold     = 0.0025f;  // ~-52 dBFS
    static constexpr float kGateAttackMs      = 5.0f;
    static constexpr float kGateReleaseMs     = 50.0f;
    static constexpr float kMakeupGainLinear  = 4.0f;     // +12 dB

    float gateAttackCoef_  = 0.0f;
    float gateReleaseCoef_ = 0.0f;
    float gateGain_        = 0.0f;   // smoothed 0..1
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 2: Write a stub implementation**

Create `src/audio/MicShaper.cpp`:

```cpp
#include "MicShaper.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

void MicShaper::prepare(double sampleRate) {
    const auto coefForMs = [sampleRate](float ms) {
        const float samples = static_cast<float>(sampleRate * ms / 1000.0);
        return samples > 0.0f
            ? 1.0f - std::exp(-1.0f / samples)
            : 1.0f;
    };
    gateAttackCoef_  = coefForMs(kGateAttackMs);
    gateReleaseCoef_ = coefForMs(kGateReleaseMs);
    reset();
}

void MicShaper::reset() {
    gateGain_ = 0.0f;
}

void MicShaper::process(const float* in, float* out, std::size_t numSamples) noexcept {
    // Stub: copy input straight through. Behavior added in next tasks.
    std::copy(in, in + numSamples, out);
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 3: Add to the audio library CMake target**

Edit `src/CMakeLists.txt` and add `audio/MicShaper.cpp` to the `guitar_dsp_audio` library's source list (alphabetically near the other Mic/Mixer entries — insert after `audio/MicCapture.cpp`).

- [ ] **Step 4: Build to confirm**

Run: `cmake --build build --target guitar_dsp_tests`
Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add src/audio/MicShaper.h src/audio/MicShaper.cpp src/CMakeLists.txt
git commit -m "feat(audio): MicShaper skeleton (passthrough stub)"
```

---

## Task 3: `MicShaper` gates below threshold (TDD)

**Files:**
- Create: `tests/unit/audio/test_mic_shaper.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/audio/MicShaper.cpp`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/audio/test_mic_shaper.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/MicShaper.h"

#include <algorithm>
#include <cmath>
#include <vector>

using guitar_dsp::audio::MicShaper;

TEST_CASE("MicShaper: near-silence below threshold is attenuated",
          "[audio][mic_shaper]") {
    MicShaper s;
    s.prepare(48000.0);

    // Feed 1 second of near-silence below the -52 dBFS gate threshold.
    constexpr std::size_t N = 48000;
    std::vector<float> in(N, 0.0005f);   // ~-66 dBFS, well below threshold
    std::vector<float> out(N, 1.0f);
    s.process(in.data(), out.data(), N);

    // After the gate's release time settles (last quarter), output should be
    // very small — close to zero, not amplified by makeup gain.
    float maxTail = 0.0f;
    for (std::size_t i = N * 3 / 4; i < N; ++i)
        maxTail = std::max(maxTail, std::fabs(out[i]));
    REQUIRE(maxTail < 0.001f);
}
```

- [ ] **Step 2: Register the test in CMake**

Edit `tests/CMakeLists.txt` and add `unit/audio/test_mic_shaper.cpp` to the `guitar_dsp_tests` source list (place near `unit/audio/test_mic_capture.cpp`).

- [ ] **Step 3: Build and run — expect FAIL**

Run: `cmake --build build --target guitar_dsp_tests && ./build/tests/guitar_dsp_tests "[mic_shaper]" -s`
Expected: FAIL — the stub passes input through; output ≈ 0.0005, not 0.

- [ ] **Step 4: Implement the gate**

Replace the body of `MicShaper::process` in `src/audio/MicShaper.cpp`:

```cpp
void MicShaper::process(const float* in, float* out, std::size_t numSamples) noexcept {
    for (std::size_t i = 0; i < numSamples; ++i) {
        const float x = in[i];
        const float absx = std::fabs(x);

        // Per-sample envelope follower with attack/release smoothing on the
        // gate's open/closed target. The target is 1 when |x| > threshold,
        // 0 otherwise; the smoothed gateGain_ approaches the target.
        const float target = (absx > kGateThreshold) ? 1.0f : 0.0f;
        const float coef   = (target > gateGain_) ? gateAttackCoef_ : gateReleaseCoef_;
        gateGain_ += coef * (target - gateGain_);

        // Apply gate + makeup gain. Hard-limit to ±1.0 to protect the vocoder.
        float y = x * gateGain_ * kMakeupGainLinear;
        if (y >  1.0f) y =  1.0f;
        if (y < -1.0f) y = -1.0f;
        out[i] = y;
    }
}
```

- [ ] **Step 5: Build and run — expect PASS**

Run: `cmake --build build --target guitar_dsp_tests && ./build/tests/guitar_dsp_tests "[mic_shaper]" -s`
Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add tests/unit/audio/test_mic_shaper.cpp tests/CMakeLists.txt src/audio/MicShaper.cpp
git commit -m "feat(audio): MicShaper gates near-silence below -52 dBFS threshold"
```

---

## Task 4: `MicShaper` passes signal above threshold + applies makeup gain

**Files:**
- Modify: `tests/unit/audio/test_mic_shaper.cpp`

- [ ] **Step 1: Append the passes-above test**

Append to `tests/unit/audio/test_mic_shaper.cpp`:

```cpp
TEST_CASE("MicShaper: signal above threshold passes through with makeup gain",
          "[audio][mic_shaper]") {
    MicShaper s;
    s.prepare(48000.0);

    // Feed 1 second of a 0.05 amplitude sine wave (~-26 dBFS, well above
    // the -52 dBFS gate threshold).
    constexpr std::size_t N = 48000;
    std::vector<float> in(N), out(N);
    for (std::size_t i = 0; i < N; ++i)
        in[i] = 0.05f * std::sin(2.0f * 3.14159265f * 200.0f * i / 48000.0f);

    s.process(in.data(), out.data(), N);

    // After the gate's attack settles, peak should be ~0.05 × 4.0 = 0.2.
    // Look at the last quarter to skip the attack ramp.
    float peakTail = 0.0f;
    for (std::size_t i = N * 3 / 4; i < N; ++i)
        peakTail = std::max(peakTail, std::fabs(out[i]));
    REQUIRE(peakTail > 0.15f);
    REQUIRE(peakTail < 0.25f);
}
```

- [ ] **Step 2: Build and run — expect PASS**

Run: `cmake --build build --target guitar_dsp_tests && ./build/tests/guitar_dsp_tests "[mic_shaper]" -s`
Expected: PASS (Task 3's implementation already handles makeup gain).

- [ ] **Step 3: Commit**

```bash
git add tests/unit/audio/test_mic_shaper.cpp
git commit -m "test(audio): MicShaper passes signal above threshold with makeup gain"
```

---

## Task 5: `MicShaper` hard-limits to ±1.0

**Files:**
- Modify: `tests/unit/audio/test_mic_shaper.cpp`

- [ ] **Step 1: Append the hard-limit test**

Append to `tests/unit/audio/test_mic_shaper.cpp`:

```cpp
TEST_CASE("MicShaper: output hard-limited to +/-1.0 when input is loud",
          "[audio][mic_shaper]") {
    MicShaper s;
    s.prepare(48000.0);

    // Full-scale square-wave-ish input. Even before makeup gain, this is
    // ±1.0 — times 4.0 gain = ±4.0, must clip to ±1.0.
    constexpr std::size_t N = 48000;
    std::vector<float> in(N), out(N);
    for (std::size_t i = 0; i < N; ++i)
        in[i] = (i % 100 < 50) ? 1.0f : -1.0f;

    s.process(in.data(), out.data(), N);

    for (std::size_t i = N * 3 / 4; i < N; ++i)
        REQUIRE(std::fabs(out[i]) <= 1.0f);
}

TEST_CASE("MicShaper: process is allocation-free", "[audio][mic_shaper][rt]") {
    #include "harness/RealtimeSentinel.h"  // syntactically OK as a string — the
    // actual include lives at the file top. Remove this line if it triggers
    // a duplicate-include warning; the helper is already imported by the test
    // file once the RealtimeSentinel is referenced.

    using guitar_dsp::tests::RealtimeSentinel;

    MicShaper s;
    s.prepare(48000.0);

    constexpr std::size_t N = 512;
    std::vector<float> in(N), out(N);
    for (std::size_t i = 0; i < N; ++i)
        in[i] = 0.1f * std::sin(2.0f * 3.14159265f * 220.0f * i / 48000.0f);

    // Warm up once before locking allocation (the very first process can
    // touch denormal-handling state etc.).
    s.process(in.data(), out.data(), N);

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int blk = 0; blk < 50; ++blk)
        s.process(in.data(), out.data(), N);
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
```

**IMPORTANT:** the second test case above references `RealtimeSentinel` which needs a real `#include` at the top of the file. Adjust by adding `#include "harness/RealtimeSentinel.h"` near the existing includes (right after the `#include "audio/MicShaper.h"` line). Delete the inline `#include` shown in the test body — it's there only as inline documentation; the actual include must live at the file top.

- [ ] **Step 2: Build and run — expect PASS**

Run: `cmake --build build --target guitar_dsp_tests && ./build/tests/guitar_dsp_tests "[mic_shaper]" -s`
Expected: both new tests PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/audio/test_mic_shaper.cpp
git commit -m "test(audio): MicShaper hard-limits +/-1.0, RT allocation-free"
```

---

## Task 6: Wire `ModulatorSource::Mic` into `AudioGraph`

Add the enum value, the per-block mic-input setter, the scratch buffer, the MicShaper member, the `prepare`/`reset` wiring, and the process() branch.

**Files:**
- Modify: `src/audio/AudioGraph.h`
- Modify: `src/audio/AudioGraph.cpp`
- Modify: `tests/unit/audio/test_audio_graph.cpp`

- [ ] **Step 1: Write a failing wiring test**

Append to `tests/unit/audio/test_audio_graph.cpp`:

```cpp
TEST_CASE("AudioGraph: Mic modulator source routes mic samples to vocoder",
          "[audio][graph][mic]") {
    using guitar_dsp::audio::AudioGraph;

    AudioGraph g;
    g.prepare(48000.0, 512);

    // Build a sine-ish "mic" input (well above the gate threshold).
    constexpr std::size_t N = 512;
    std::vector<float> mic(N);
    for (std::size_t i = 0; i < N; ++i)
        mic[i] = 0.3f * std::sin(2.0f * 3.14159265f * 440.0f * i / 48000.0f);

    g.setModulatorSource(AudioGraph::ModulatorSource::Mic);
    g.setMicBlock(mic.data(), N);
    g.mixer().setDryWet(1.0f);

    // Build a guitar input — sustained sine to act as carrier.
    std::vector<float> in(N), out(N);
    for (std::size_t i = 0; i < N; ++i)
        in[i] = 0.4f * std::sin(2.0f * 3.14159265f * 110.0f * i / 48000.0f);

    g.process(in.data(), out.data(), N);

    // The vocoder produces output only when both carrier and modulator are
    // present. With mic silent the output is near-zero; with this mic feed,
    // some output energy must reach the wet bus.
    float peakOut = 0.0f;
    for (float v : out) peakOut = std::max(peakOut, std::fabs(v));
    REQUIRE(peakOut > 0.001f);
}
```

- [ ] **Step 2: Build — expect compile FAIL**

Run: `cmake --build build --target guitar_dsp_tests`
Expected: FAIL — `ModulatorSource::Mic` doesn't exist; `setMicBlock` doesn't exist.

- [ ] **Step 3: Extend the AudioGraph header**

Edit `src/audio/AudioGraph.h`:

1. Add `#include "MicShaper.h"` alongside the other audio includes near the top.
2. Change the enum:
   ```cpp
   enum class ModulatorSource { Linear, NoteStepped, ClipBank, Mic };
   ```
3. Add a public setter, near the existing `setModulatorSource`:
   ```cpp
   // Audio thread (called once per block from PluginProcessor::processBlock
   // BEFORE AudioGraph::process). Stores the mic samples for use by the Mic
   // modulator branch in process(). Truncates if numSamples > prepared block.
   // RT-safe.
   void setMicBlock(const float* mono, std::size_t numSamples) noexcept;
   ```
4. Add `MicShaper micShaper_;` as a private member, next to the other audio modules (e.g. after `ChannelVocoder vocoder_;`).
5. Add a private buffer `std::vector<float> micScratchBuffer_;` next to the other scratch buffers (`postInputBuffer_`, `wetBuffer_`).
6. Add a private member `std::size_t micScratchValidSamples_ = 0;` next to `micScratchBuffer_` — tracks how many samples are valid in the buffer this block.

- [ ] **Step 4: Extend `prepare()`, `reset()`, and add `setMicBlock` definition in AudioGraph.cpp**

In `src/audio/AudioGraph.cpp::prepare`, after the `clipBankPlayer_.prepare(...)` line, add:
```cpp
    micShaper_.prepare(sampleRate);
    micScratchBuffer_.assign(static_cast<std::size_t>(blockSize), 0.0f);
    micScratchValidSamples_ = 0;
```

In `AudioGraph::reset()`, after `clipBankPlayer_.reset();`, add:
```cpp
    micShaper_.reset();
    std::fill(micScratchBuffer_.begin(), micScratchBuffer_.end(), 0.0f);
    micScratchValidSamples_ = 0;
```

At the bottom of the file (above the closing `} // namespace guitar_dsp::audio`), add the new `setMicBlock` implementation:
```cpp
void AudioGraph::setMicBlock(const float* mono, std::size_t numSamples) noexcept {
    const std::size_t n = std::min(numSamples, micScratchBuffer_.size());
    if (mono != nullptr) {
        std::copy(mono, mono + n, micScratchBuffer_.begin());
        if (n < micScratchBuffer_.size())
            std::fill(micScratchBuffer_.begin() + static_cast<std::ptrdiff_t>(n),
                      micScratchBuffer_.end(), 0.0f);
        micScratchValidSamples_ = n;
    } else {
        std::fill(micScratchBuffer_.begin(), micScratchBuffer_.end(), 0.0f);
        micScratchValidSamples_ = 0;
    }
}
```

- [ ] **Step 5: Extend `process()` modulator branch**

In `src/audio/AudioGraph::process`, find the existing modulator-source branch (around line 78-89). Extend it with the Mic case so the three-way becomes four-way:

```cpp
        const int modSrc = modulatorSource_.load(std::memory_order_relaxed);
        if (modSrc == static_cast<int>(ModulatorSource::NoteStepped)) {
            noteSteppedPlayer_.process(postInputBuffer_.data(),
                                       wetBuffer_.data(), numSamples);
        } else if (modSrc == static_cast<int>(ModulatorSource::ClipBank)) {
            clipBankPlayer_.process(postInputBuffer_.data(),
                                    wetBuffer_.data(), numSamples);
        } else if (modSrc == static_cast<int>(ModulatorSource::Mic)) {
            // micScratchBuffer_ was populated by setMicBlock() earlier this
            // block. Shape (gate + makeup gain) and write to wetBuffer_ as
            // the vocoder modulator.
            micShaper_.process(micScratchBuffer_.data(),
                               wetBuffer_.data(), numSamples);
        } else {
            ttsClipPlayer_.process(wetBuffer_.data(), numSamples);
        }
```

- [ ] **Step 6: Build and run — expect PASS**

Run: `cmake --build build --target guitar_dsp_tests && ./build/tests/guitar_dsp_tests "[graph][mic]" -s`
Expected: PASS.

Also run a broader regression:
```
./build/tests/guitar_dsp_tests "[graph],[clip_bank],[note_stepped]" -s
```
Expected: all PASS.

- [ ] **Step 7: Commit**

```bash
git add src/audio/AudioGraph.h src/audio/AudioGraph.cpp \
        tests/unit/audio/test_audio_graph.cpp
git commit -m "feat(audio): AudioGraph routes ModulatorSource::Mic via MicShaper"
```

---

## Task 7: `AudioGraph::micPeak()` accessor for the level meter

A single atomic float updated each block in `setMicBlock` from the input samples. Used by VocoderPanel's meter (Task 12) and visible on every scene.

**Files:**
- Modify: `src/audio/AudioGraph.h`
- Modify: `src/audio/AudioGraph.cpp`

- [ ] **Step 1: Add the atomic + accessor to the header**

In `src/audio/AudioGraph.h`, add a private atomic alongside the other atomics:
```cpp
    std::atomic<float> micPeak_ {0.0f};
```

Add a public accessor near `detectedHz()`:
```cpp
    // Latest mic input peak (linear 0..1) from the most recent setMicBlock()
    // call. Updated whether or not ModulatorSource::Mic is active — drives
    // the always-visible mic meter on VocoderPanel.
    float micPeak() const noexcept { return micPeak_.load(std::memory_order_relaxed); }
```

- [ ] **Step 2: Update `setMicBlock` to publish the peak**

In `src/audio/AudioGraph.cpp::setMicBlock`, after the copy and before `micScratchValidSamples_ = n;` (in the non-null branch), insert:
```cpp
        float peak = 0.0f;
        for (std::size_t i = 0; i < n; ++i)
            peak = std::max(peak, std::fabs(mono[i]));
        micPeak_.store(peak, std::memory_order_relaxed);
```

In the null branch, after `micScratchValidSamples_ = 0;`, insert:
```cpp
        micPeak_.store(0.0f, std::memory_order_relaxed);
```

- [ ] **Step 3: Build to confirm**

Run: `cmake --build build --target guitar_dsp_tests`
Expected: clean build (no new test asserts on the peak yet — Task 12 uses it via the UI).

- [ ] **Step 4: Commit**

```bash
git add src/audio/AudioGraph.h src/audio/AudioGraph.cpp
git commit -m "feat(audio): AudioGraph publishes mic peak for visibility meter"
```

---

## Task 8: PluginProcessor forwards mic block when ModulatorSource::Mic is active

Extend the existing mic-sidechain plumbing in `processBlock` so the mic samples are forwarded to `AudioGraph::setMicBlock` every block while the active scene uses the mic modulator, regardless of whisper-capture state. Also keep forwarding even when the modulator isn't `Mic` — so the level meter stays live on every scene.

**Files:**
- Modify: `src/app/PluginProcessor.cpp`

- [ ] **Step 1: Inspect the existing mic-routing block**

Read `src/app/PluginProcessor.cpp` around lines 585-630 — the block under the comment `"// MicCapture sidechain routing"`. Note that the current block only runs when `micCapture_.isCapturing()`. The Phase B change is to ALWAYS extract the mic block (cheap), forward it to `AudioGraph::setMicBlock`, AND additionally feed `MicCapture::appendFromAudioBlock` when capturing.

- [ ] **Step 2: Refactor the mic-routing block**

Replace the entire `if (micCapture_.isCapturing()) { ... }` block (around lines 595-625) with:

```cpp
    // Mic bus extraction (sidechain bus 1 in AU; main bus 0 in standalone).
    // Runs every block so:
    //   - the vocoder gets a fresh mic block when ModulatorSource::Mic is active
    //   - the always-visible mic level meter stays live on every scene
    //   - whisper capture (when active) still gets fed
    {
        const float* micPtr = nullptr;
        int           micLen = 0;
        constexpr int kMaxBlock = 8192;
        float         tmp[kMaxBlock];

        if (getBusCount(/*isInput=*/true) >= 2
                && getChannelCountOfBus(true, 1) > 0) {
            auto micBus = getBusBuffer(buffer, /*isInput=*/true, /*busIdx=*/1);
            const int n = std::min(micBus.getNumSamples(), kMaxBlock);
            if (micBus.getNumChannels() == 1) {
                micPtr = micBus.getReadPointer(0);
                micLen = n;
            } else if (micBus.getNumChannels() >= 2) {
                const float* L = micBus.getReadPointer(0);
                const float* R = micBus.getReadPointer(1);
                for (int i = 0; i < n; ++i) tmp[i] = 0.5f * (L[i] + R[i]);
                micPtr = tmp;
                micLen = n;
            }
        } else {
            // Standalone fallback: bus 0 doubles as the mic input. The
            // vocoder self-modulates on the guitar; not the talkbox effect,
            // but documented in the spec as the standalone behavior.
            auto mainBus = getBusBuffer(buffer, /*isInput=*/true, /*busIdx=*/0);
            if (mainBus.getNumChannels() >= 1) {
                micPtr = mainBus.getReadPointer(0);
                micLen = mainBus.getNumSamples();
            }
        }

        if (micPtr != nullptr && micLen > 0) {
            graph_.setMicBlock(micPtr, static_cast<std::size_t>(micLen));
            if (micCapture_.isCapturing())
                micCapture_.appendFromAudioBlock(micPtr, micLen);
        } else {
            graph_.setMicBlock(nullptr, 0);  // clears the scratch + zeroes the meter
        }
    }
```

- [ ] **Step 3: Build to confirm**

Run: `cmake --build build --target guitar_dsp_tests`
Expected: clean build.

- [ ] **Step 4: Run a quick regression**

Run: `./build/tests/guitar_dsp_tests "[sidechain],[mic_capture]" -s`
Expected: existing sidechain + MicCapture tests still PASS (the new code preserves the previous append-when-capturing behavior).

- [ ] **Step 5: Commit**

```bash
git add src/app/PluginProcessor.cpp
git commit -m "feat(app): PluginProcessor forwards mic block to AudioGraph every block"
```

---

## Task 9: Scene activation routes `source == "mic"` to `ModulatorSource::Mic`

**Files:**
- Modify: `src/app/PluginProcessor.cpp`

- [ ] **Step 1: Find the scene-activation TTS branch**

Inside the scene-change lambda in `src/app/PluginProcessor.cpp` (around line 350), after the per-scene `wordSync` block but BEFORE the existing `if (cfg.source == "clipBank")` branch from Phase A, insert the new mic branch. Placement matters: mic-source scenes must be handled before any path that synthesizes clips.

- [ ] **Step 2: Insert the mic branch**

Locate the line:
```cpp
            // -------------------------------------------------------------
            // Clip-bank source (Phase A — Vocal Guitar, Scene 2)
```

Immediately ABOVE that comment block, insert:

```cpp
            // -------------------------------------------------------------
            // Mic source (Phase B — Mic Talkbox, Scene 3)
            // -------------------------------------------------------------
            // No clip to load — the mic stream becomes the modulator. Clear
            // the linear / note-stepped / clipBank players so a prior scene's
            // state doesn't bleed into this scene's audio.
            if (cfg.source == "mic") {
                static const std::string kMicKey = "mic:";
                if (currentTtsClipKey_ == kMicKey) return;
                currentTtsClipKey_ = kMicKey;

                graph_.setModulatorSource(audio::AudioGraph::ModulatorSource::Mic);
                graph_.ttsClipPlayer().setClip(nullptr);
                graph_.noteSteppedPlayer().setClip(nullptr);
                graph_.clipBankPlayer().setBank({});
                lastResolvedSource_.store(0, std::memory_order_relaxed);  // "none"
                return;
            }

```

- [ ] **Step 3: Build to confirm**

Run: `cmake --build build --target guitar_dsp_tests`
Expected: clean build.

- [ ] **Step 4: Quick regression**

Run: `./build/tests/guitar_dsp_tests`
Expected: 333 cases, 329 passed, 4 skipped (no new failures vs. main).

- [ ] **Step 5: Commit**

```bash
git add src/app/PluginProcessor.cpp
git commit -m "feat(app): scene activation routes source=mic to ModulatorSource::Mic"
```

---

## Task 10: WordReadout shows "🎤 MIC" on mic scenes; Rewind is inert

**Files:**
- Modify: `src/app/PluginProcessor.h`
- Modify: `src/app/WordReadout.cpp`

- [ ] **Step 1: Add an accessor on PluginProcessor**

In `src/app/PluginProcessor.h`, next to the existing `activeSceneIsClipBank()`, add:

```cpp
    // Phase B — Mic Talkbox (Scene 3). True when the active scene uses
    // the mic modulator source.
    bool activeSceneIsMic() const {
        return sceneEngine_.activeTtsConfig().source == "mic";
    }
```

Also extend the existing `rewindActive()` accessor so the mic case is a no-op (not currently asserted but cleaner than dispatching `rewindClipBank` on a mic scene):

Find the existing definition of `rewindActive()`. Replace with:
```cpp
    void rewindActive() noexcept {
        if (activeSceneIsMic())          /* no-op: no clip to rewind */ return;
        if (activeSceneIsClipBank())     graph_.rewindClipBank();
        else                              graph_.rewindSpoken();
    }
```

- [ ] **Step 2: Update WordReadout to show the mic label**

In `src/app/WordReadout.cpp`, find the existing `if (processor_.activeSceneIsClipBank()) { ... return; }` branch in `paint()`. Immediately ABOVE that branch, add:

```cpp
    if (processor_.activeSceneIsMic()) {
        const auto bounds = getLocalBounds().reduced(8);
        const float h = (float) std::min(bounds.getHeight(),
                                          (int) WordReadout::kCenterBaseHeight * 2);

        g.setColour(juce::Colour::fromRGB(0xE8, 0xE8, 0xE8));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(h)));
        g.drawFittedText("🎤 MIC",
                         bounds.removeFromTop(bounds.getHeight() - kPipStripHeight),
                         juce::Justification::centred, 1);
        // No Rewind pill on mic scenes — nothing to rewind.
        return;
    }
```

(If the inserted code's font construction call differs from the file's existing style, mirror what's already used in the clipBank branch above it.)

- [ ] **Step 3: Update `timerCallback` to repaint on scene change only**

Mic scenes don't have a cursor to poll. Existing `lastSceneId_` mechanism already triggers a repaint on scene change, so the mic branch is reached during the next paint. No additional polling is needed.

If your reading of `timerCallback` shows it already handles the no-cursor case gracefully (e.g., via `lastSceneId_`), no edit is needed here. Otherwise, gate the existing index-poll path so it doesn't run on mic scenes:

Find a section in `timerCallback()` that polls a cursor / index every tick. Wrap the existing per-tick polling in:
```cpp
    if (processor_.activeSceneIsMic()) {
        return;  // mic scenes have no cursor to poll
    }
```

placed at the TOP of the timerCallback body, after the lastSceneId_ check.

- [ ] **Step 4: Build to confirm**

Run: `cmake --build build --target guitar_dsp_tests`
Expected: clean build.

- [ ] **Step 5: Run existing word-readout tests**

Run: `./build/tests/guitar_dsp_tests "[word_readout]" -s`
Expected: PASS (no existing test uses mic scenes).

- [ ] **Step 6: Commit**

```bash
git add src/app/PluginProcessor.h src/app/WordReadout.cpp
git commit -m "feat(ui): WordReadout shows MIC label on mic scenes; Rewind inert"
```

---

## Task 11: VocoderPanel mic level meter (visible on every scene)

A small horizontal strip at the bottom of the panel showing live mic peak. Updates 30 Hz from a Timer.

**Files:**
- Modify: `src/app/VocoderPanel.h`
- Modify: `src/app/VocoderPanel.cpp`

- [ ] **Step 1: Add a meter strip Rectangle and atomic-backed peak in the header**

Edit `src/app/VocoderPanel.h`. Inside the class (under `private:`), add a member to cache the latest peak:

```cpp
    float lastMicPeak_ = 0.0f;
```

- [ ] **Step 2: Draw the meter in `paint()`**

In `src/app/VocoderPanel.cpp`, find the existing `paint()` method. After all existing drawing but before the closing brace, add:

```cpp
    // Mic level meter — small horizontal strip at the bottom of the panel.
    // Updates each Timer tick via timerCallback().
    const auto micStripBounds = getLocalBounds()
                                    .removeFromBottom(8)
                                    .reduced(4, 1);
    g.setColour(juce::Colour::fromRGB(0x22, 0x22, 0x22));
    g.fillRect(micStripBounds);

    if (lastMicPeak_ > 0.0001f) {
        const int w = static_cast<int>(micStripBounds.getWidth() * std::min(1.0f, lastMicPeak_));
        auto fill = micStripBounds.withWidth(w);
        const juce::Colour colour = lastMicPeak_ > 0.7f
            ? juce::Colour::fromRGB(0xE0, 0x60, 0x40)   // hot
            : juce::Colour::fromRGB(0x40, 0xC0, 0x60);  // green
        g.setColour(colour);
        g.fillRect(fill);
    } else {
        g.setColour(juce::Colour::fromRGB(0x55, 0x55, 0x55));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(8.0f)));
        g.drawText("no mic", micStripBounds, juce::Justification::centred);
    }
```

- [ ] **Step 3: Poll `processor_.audioGraph().micPeak()` from `timerCallback`**

Locate the existing `timerCallback()` in VocoderPanel.cpp. Read it to determine how it accesses the processor (likely via a stored reference `processor_` or the `PluginProcessor& p_` pattern). After the existing tick logic, add:

```cpp
    const float p = processor_.audioGraph().micPeak();
    if (std::fabs(p - lastMicPeak_) > 0.005f) {
        lastMicPeak_ = p;
        repaint();
    }
```

If `processor_.audioGraph()` doesn't exist as an accessor, use `processor_.graph()` or whatever the existing convention is — look at how the existing VocoderPanel reads vocoder makeup/sibilance for the canonical pattern.

- [ ] **Step 4: Confirm `AudioGraph` accessor exists**

In `src/app/PluginProcessor.h`, check that an accessor like `audio::AudioGraph& audioGraph()` exists (it should — VocoderPanel already reads vocoder state). If the canonical name is different (e.g., `graph()`), use that.

- [ ] **Step 5: Build to confirm**

Run: `cmake --build build --target guitar_dsp_tests`
Expected: clean build.

- [ ] **Step 6: Commit**

```bash
git add src/app/VocoderPanel.h src/app/VocoderPanel.cpp
git commit -m "feat(ui): VocoderPanel adds always-visible mic level meter"
```

---

## Task 12: Replace Scene 3 JSON (archive old)

Per the preservation principle, `03_carousel_piano.json` moves to `assets/scenes/archive/` rather than being deleted. The new Scene 3 JSON has NO carousel block (see Task 1's spec update for rationale).

**Files:**
- Move: `assets/scenes/03_carousel_piano.json` → `assets/scenes/archive/03_carousel_piano.json`
- Create: `assets/scenes/03_mic_talkbox.json`

- [ ] **Step 1: Archive the old scene**

```bash
git mv assets/scenes/03_carousel_piano.json assets/scenes/archive/03_carousel_piano.json
```

- [ ] **Step 2: Write the new Scene 3**

Create `assets/scenes/03_mic_talkbox.json`:

```json
{
  "id": 3,
  "name": "Mic Talkbox",
  "color": "#22a2c0",
  "mixer": { "masterGainDb": -6.0, "dryWet": 0.9, "transitionMs": 30 },
  "tts": {
    "source": "mic",
    "clarity": 0.0
  }
}
```

- [ ] **Step 3: Build + full regression**

```
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests
```
Expected: 333 cases, 329 passed, 4 skipped (or 334/330 if the previous tasks added a new mic-routing test).

- [ ] **Step 4: Commit**

```bash
git add assets/scenes/03_mic_talkbox.json \
        assets/scenes/archive/03_carousel_piano.json
git commit -m "feat(scenes): Scene 3 is Mic Talkbox; archive old piano-ish patch"
```

---

## Task 13: Integration test — mic-modulator scene routes end-to-end

A single test that constructs `AudioGraph`, sets `ModulatorSource::Mic`, feeds a sine-shaped mic block via `setMicBlock`, processes a guitar block, and asserts the vocoder produces nonzero output.

**Files:**
- Create: `tests/integration/test_mic_talkbox_scene.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the test**

Create `tests/integration/test_mic_talkbox_scene.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "audio/AudioGraph.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::AudioGraph;

TEST_CASE("Mic-talkbox scene: ModulatorSource::Mic produces vocoded output end-to-end",
          "[integration][mic][scene]") {
    AudioGraph g;
    g.prepare(48000.0, 512);

    g.setModulatorSource(AudioGraph::ModulatorSource::Mic);
    g.mixer().setDryWet(1.0f);

    // 2 seconds of carrier (guitar) + mic at 200 ms blocks.
    constexpr std::size_t blockSize = 512;
    constexpr std::size_t blocks    = 200;
    constexpr std::size_t N         = blockSize * blocks;

    std::vector<float> carrier(N), mic(N), out(N);
    for (std::size_t i = 0; i < N; ++i) {
        carrier[i] = 0.5f * std::sin(2.0f * 3.14159265f * 220.0f * i / 48000.0f);
        mic[i]     = 0.3f * std::sin(2.0f * 3.14159265f * 880.0f * i / 48000.0f);
    }

    for (std::size_t b = 0; b < blocks; ++b) {
        g.setMicBlock(mic.data() + b * blockSize, blockSize);
        g.process(carrier.data() + b * blockSize,
                  out.data() + b * blockSize,
                  blockSize);
    }

    // After the vocoder envelope followers settle, the wet bus must carry
    // nonzero output. Look at the last 200 ms.
    float peak = 0.0f;
    for (std::size_t i = N - 9600; i < N; ++i)
        peak = std::max(peak, std::fabs(out[i]));
    REQUIRE(peak > 0.001f);

    // micPeak() should track the mic input.
    REQUIRE(g.micPeak() > 0.1f);
}

TEST_CASE("Mic-talkbox scene: silent mic produces near-silent wet output",
          "[integration][mic][scene]") {
    AudioGraph g;
    g.prepare(48000.0, 512);

    g.setModulatorSource(AudioGraph::ModulatorSource::Mic);
    g.mixer().setDryWet(1.0f);

    constexpr std::size_t N = 48000;  // 1 second
    std::vector<float> carrier(N), out(N);
    for (std::size_t i = 0; i < N; ++i)
        carrier[i] = 0.5f * std::sin(2.0f * 3.14159265f * 220.0f * i / 48000.0f);

    std::vector<float> silentMic(512, 0.0f);

    for (std::size_t b = 0; b < N / 512; ++b) {
        g.setMicBlock(silentMic.data(), 512);
        g.process(carrier.data() + b * 512, out.data() + b * 512, 512);
    }

    // With no mic excitation, the modulator is silent → vocoder output near
    // zero. (Some bleed from the carrier-noise floor is possible — keep the
    // threshold loose.)
    float peakTail = 0.0f;
    for (std::size_t i = N - 9600; i < N; ++i)
        peakTail = std::max(peakTail, std::fabs(out[i]));
    REQUIRE(peakTail < 0.05f);
}
```

- [ ] **Step 2: Register in CMake**

Edit `tests/CMakeLists.txt` and add `integration/test_mic_talkbox_scene.cpp` to the `guitar_dsp_tests` source list (place near `integration/test_vocal_guitar_clip_bank_scene.cpp`).

- [ ] **Step 3: Build and run**

```
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "[integration][mic]" -s
```
Expected: both tests PASS.

- [ ] **Step 4: Full regression check**

```
./build/tests/guitar_dsp_tests
```
Expected: 2 more passing tests than the previous baseline (335 cases, 331 passed, 4 skipped), 0 failures.

- [ ] **Step 5: Commit**

```bash
git add tests/integration/test_mic_talkbox_scene.cpp tests/CMakeLists.txt
git commit -m "test(integration): mic-talkbox scene routes ModulatorSource::Mic end-to-end"
```

---

## Task 14: Manual verification

Plugin in Logic with sidechain mic routing is the designed surface. Standalone is a self-modulation fallback (documented in the spec).

**Files:** (none — manual)

- [ ] **Step 1: Build the standalone**

Run: `cmake --build build --target guitar_dsp_app_Standalone`

- [ ] **Step 2: Standalone smoke test (self-modulation fallback)**

Launch:
```
open "build/src/app/guitar_dsp_app_artefacts/Release/Standalone/Guitar Speak.app"
```

Switch to Scene 3 ("Mic Talkbox"). With your guitar plugged into the default input:
- The WordReadout shows "🎤 MIC".
- The VocoderPanel mic meter shows green when the input has signal.
- Picking a note produces some vocoder character (self-modulation on the guitar, not real talkbox).

This confirms the activation branch and metering work; the *real* talkbox check needs the AU plugin in Logic.

- [ ] **Step 3: AU plugin in Logic (the designed surface)**

Build the AU:
```
cmake --build build --target guitar_dsp_app_AU
```

In Logic:
1. Add a track for guitar (your usual setup).
2. Add a separate audio track for a mic input.
3. Insert "Guitar Speak" AU on the guitar track.
4. In the Guitar Speak plugin window, expose the sidechain (the host's standard sidechain UI) and set the mic track as the sidechain source.
5. Activate Scene 3 in the plugin.
6. Speak / hum "weeee" into the mic while picking the guitar.
7. Expected: the guitar's spectrum tracks your vowels in real time. Sustain a note and shift "weee → ahhh → ohhh" — the guitar's tone changes accordingly.

If sidechain routing isn't configurable in your host setup, document the workaround in `docs/au-logic-setup.md` (a sentence under a new "Mic Talkbox" subsection).

- [ ] **Step 4: Cross-scene switching smoke**

In either Standalone or AU:
- Switch from Scene 2 (clipBank) to Scene 3 (mic): the WordReadout label transitions from `"<key> • N / 10"` to `"🎤 MIC"`. No audio glitch.
- Switch back to Scene 2: cycling resumes from clip 0 (the scene-activation rewind from Phase A).

- [ ] **Step 5: No commit needed (validation only)**

---

## Self-Review

Spec sections vs. tasks:

- "Approach" + `ModulatorSource::Mic` enum → Task 6.
- "New: `src/audio/MicShaper.{h,cpp}`" → Tasks 2–5.
- "Modified: `AudioGraph.{h,cpp}`" → Tasks 6, 7.
- "`TtsConfig.source` accepts `mic`" → No code change required (source is free-form string); Task 9 keys on the value at activation.
- "`SceneEngine.cpp` (scene activation)" → Task 9 (lives in PluginProcessor in this codebase, same as Phase A).
- "`PluginProcessor` mic plumbing extension" → Task 8.
- "WordReadout" → Task 10.
- "VocoderPanel mic level meter" → Task 11.
- "Scene 3 JSON" → Task 12 (also archives old).
- "Risk: standalone mic/guitar separation" → documented in Task 8 and Task 14; no code mitigation, no test.
- "Risk: mic bleed / feedback" → not in code; flagged in spec.
- "Testing: MicShaper unit tests" → Tasks 3–5.
- "Testing: `AudioGraph_MicModulatorRoutes`" → Task 6.
- "Testing: manual / demo verification" → Task 14.

No spec section unaddressed. No "TBD"/"TODO" placeholders. Type names consistent across tasks: `MicShaper`, `ModulatorSource::Mic`, `setMicBlock`, `micPeak`, `activeSceneIsMic`, `rewindActive`.

---

Plan complete and saved to `docs/superpowers/plans/2026-06-13-mic-talkbox-plan.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
