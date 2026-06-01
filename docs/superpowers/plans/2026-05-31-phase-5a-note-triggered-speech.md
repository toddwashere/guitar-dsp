# Note-Triggered Word-by-Word Speech Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the guitar speak one word per note — each plucked note's onset plays the next word of the scene's TTS phrase through the vocoder and advances a word index, auto-looping after the last word.

**Architecture:** A clean-signal `OnsetDetector` (audio thread) drives a `NoteSteppedTTSPlayer` (audio thread) that emits per-word audio segments — pre-computed off the audio thread by a uniform energy-gap `WordAligner` — as the vocoder's modulator. A scene `tts.trigger` field selects note-stepped vs the existing linear player. A minimal on-screen word readout makes it demoable; the full karaoke + spectrogram are Phase 5b.

**Tech Stack:** C++20, JUCE 8, Catch2 v3. No new dependencies.

**Reference spec:** [`docs/superpowers/specs/2026-05-31-note-triggered-speech-design.md`](../specs/2026-05-31-note-triggered-speech-design.md)

---

## Background for the implementing engineer

Read these first:

- **`src/audio/TTSClipPlayer.{h,cpp}`** — the existing linear player and the canonical message-thread→audio-thread handoff: `std::atomic<bool> newClipFlag_`, `pendingClip_` (message thread), `activeClip_` (audio thread); `process()` does `newClipFlag_.exchange(false, acquire)` then moves pending→active. **`NoteSteppedTTSPlayer` copies this exact pattern**, but advances through per-word segments on note onsets instead of playing linearly.
- **`src/audio/TTSClip.h`** — `struct TTSClip { string name; double sampleRate; vector<float> samples; }`, `TTSClipPtr = shared_ptr<const TTSClip>`. You add a `words` vector.
- **`src/audio/AudioGraph.cpp` `process()`** (lines ~38–59) — the vocoder branch currently does `ttsClipPlayer_.process(wet); vocoder_.process(postInput, wet, wet)`. You add a `noteSteppedPlayer_` and a modulator-source selector. `postInputBuffer_` already holds the **clean guitar** (post-InputStage, pre-vocoder) — that is the onset source.
- **`src/audio/CarouselMod.{h,cpp}`** — `EnvelopeFollower` is a good style reference for the bespoke `OnsetDetector`.
- **`src/scenes/Scene.h` `TtsConfig`** + **`src/scenes/SceneLibrary.cpp`** — you add a `trigger` field, parsed like the other `tts` fields.
- **`src/app/PluginProcessor.cpp`** scene-change `callAsync` (the block that builds `clip` and calls `graph_.ttsClipPlayer().setClip(clip)`) — you extend it to segment the clip + route to the note-stepped player for `trigger == "note"` scenes.

**Threading rules (non-negotiable, "cannot crash"):**
- `OnsetDetector::processSample`, `NoteSteppedTTSPlayer::process`: no heap alloc, no locks. The shared_ptr swap on clip pickup is the same accepted soft-RT cost as `TTSClipPlayer`.
- `WordAligner::align` runs on the **message thread** at synthesis/scene-change time, never on the audio thread.
- `RealtimeSentinel` tests guard the audio-thread paths.

---

## File structure

```
src/audio/TTSClip.h                      (modify, Task 1: WordSegment + words vector)
src/audio/OnsetDetector.{h,cpp}          (NEW, Task 2)
src/audio/WordAligner.{h,cpp}            (NEW, Task 3)
src/audio/NoteSteppedTTSPlayer.{h,cpp}   (NEW, Task 4)
src/scenes/Scene.h                       (modify, Task 5: tts.trigger)
src/scenes/SceneLibrary.cpp              (modify, Task 5: parse trigger)
src/audio/AudioGraph.{h,cpp}             (modify, Task 6: note-stepped player + selector)
src/app/PluginProcessor.{h,cpp}          (modify, Task 7: segment + route + word index)
src/app/WordReadout.{h,cpp}              (NEW, Task 8)
src/app/PluginEditor.{h,cpp}             (modify, Task 8: host WordReadout)
src/app/CMakeLists.txt                   (modify, Task 8)
src/CMakeLists.txt                       (modify, Tasks 2-4)
assets/scenes/06_speaking_a.json         (modify, Task 9: trigger note)
assets/scenes/07_speaking_b.json         (modify, Task 9)
assets/scenes/08_speaking_finale.json    (modify, Task 9)
tests/CMakeLists.txt                     (modify throughout)
tests/unit/audio/test_onset_detector.cpp (NEW, Task 2)
tests/unit/audio/test_word_aligner.cpp   (NEW, Task 3)
tests/unit/audio/test_note_stepped_player.cpp (NEW, Task 4)
tests/unit/scenes/test_scene_library_tts_trigger.cpp (NEW, Task 5)
tests/integration/test_note_triggered_speech.cpp (NEW, Task 9)
README.md                                (modify, Task 10)
```

---

## Task 1: WordSegment + TTSClip.words (TDD)

**Files:**
- Modify: `src/audio/TTSClip.h`
- Create: `tests/unit/audio/test_word_segment.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test** `tests/unit/audio/test_word_segment.cpp`

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/TTSClip.h"

using guitar_dsp::audio::TTSClip;
using guitar_dsp::audio::WordSegment;

TEST_CASE("TTSClip: carries optional word segments", "[audio][ttsclip]") {
    TTSClip clip;
    clip.samples.assign(1000, 0.0f);
    REQUIRE(clip.words.empty());

    clip.words.push_back(WordSegment{"hello", 0, 400});
    clip.words.push_back(WordSegment{"world", 400, 1000});
    REQUIRE(clip.words.size() == 2);
    REQUIRE(clip.words[0].word == "hello");
    REQUIRE(clip.words[1].startSample == 400);
    REQUIRE(clip.words[1].endSample == 1000);
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`** — append `unit/audio/test_word_segment.cpp`.

- [ ] **Step 3: Run to confirm failure**

Run: `cmake --build build --target guitar_dsp_tests 2>&1 | head -10`
Expected: FAIL — `WordSegment` not a member of `guitar_dsp::audio`.

- [ ] **Step 4: Update `src/audio/TTSClip.h`** — add the struct + member. Insert `WordSegment` before `struct TTSClip`, and add a `words` member to `TTSClip`:

```cpp
// One spoken word and the sample range it occupies within TTSClip::samples.
struct WordSegment {
    std::string word;
    std::size_t startSample = 0;   // inclusive
    std::size_t endSample   = 0;   // exclusive
};
```

Then inside `struct TTSClip`, after the `samples` member:

```cpp
    std::vector<WordSegment> words;   // empty = not segmented
```

- [ ] **Step 5: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "carries optional word"`
Expected: PASS. Full suite still green.

- [ ] **Step 6: Commit**

```bash
git add src/audio/TTSClip.h tests/unit/audio/test_word_segment.cpp tests/CMakeLists.txt
git commit -m "feat(audio): TTSClip carries optional per-word segments"
```

---

## Task 2: OnsetDetector (TDD)

**Files:**
- Create: `src/audio/OnsetDetector.h`, `src/audio/OnsetDetector.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/unit/audio/test_onset_detector.cpp`
- Modify: `tests/CMakeLists.txt`

Envelope-peak follower with hysteresis (arm/re-arm) + debounce. Fires one onset
per pluck; sustained notes don't retrigger; double-hits within the debounce
window are suppressed.

- [ ] **Step 1: Write the failing test** `tests/unit/audio/test_onset_detector.cpp`

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/OnsetDetector.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::OnsetDetector;

namespace {
// Append a decaying tone burst (a "pluck") then silence to `buf`.
void pluck(std::vector<float>& buf, int burstLen, int gapLen, float amp, double sr) {
    const int start = static_cast<int>(buf.size());
    for (int i = 0; i < burstLen; ++i) {
        const float env = amp * std::exp(-i / (0.05f * static_cast<float>(sr)));
        buf.push_back(env * std::sin(2.0f*3.14159265f*220.0f*(start+i)/sr));
    }
    for (int i = 0; i < gapLen; ++i) buf.push_back(0.0f);
}
int countOnsets(OnsetDetector& d, const std::vector<float>& buf) {
    int n = 0;
    for (float x : buf) if (d.processSample(x)) ++n;
    return n;
}
}

TEST_CASE("OnsetDetector: one onset per pluck", "[audio][onset]") {
    OnsetDetector d;
    d.prepare(48000.0);
    std::vector<float> buf;
    for (int k = 0; k < 5; ++k) pluck(buf, 4000, 4000, 0.8f, 48000.0);
    REQUIRE(countOnsets(d, buf) == 5);
}

TEST_CASE("OnsetDetector: sustained note fires once", "[audio][onset]") {
    OnsetDetector d;
    d.prepare(48000.0);
    std::vector<float> buf(48000);
    for (size_t i = 0; i < buf.size(); ++i)
        buf[i] = 0.8f * std::sin(2.0f*3.14159265f*220.0f*i/48000.0f);  // constant
    REQUIRE(countOnsets(d, buf) == 1);
}

TEST_CASE("OnsetDetector: silence produces no onsets", "[audio][onset]") {
    OnsetDetector d;
    d.prepare(48000.0);
    std::vector<float> buf(48000, 0.0f);
    REQUIRE(countOnsets(d, buf) == 0);
}

TEST_CASE("OnsetDetector: debounce suppresses a rapid double-hit",
          "[audio][onset]") {
    OnsetDetector d;
    d.prepare(48000.0);
    d.setDebounceMs(80.0f);
    std::vector<float> buf;
    // Two bursts only ~20 ms apart (within the 80 ms debounce) with a brief
    // dip between them; debounce should let only the first through.
    pluck(buf, 400, 100, 0.8f, 48000.0);   // ~8ms burst + ~2ms gap
    pluck(buf, 400, 4000, 0.8f, 48000.0);
    REQUIRE(countOnsets(d, buf) == 1);
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`** — append `unit/audio/test_onset_detector.cpp`.

- [ ] **Step 3: Run to confirm failure** — FAIL (`audio/OnsetDetector.h` not found).

- [ ] **Step 4: Write `src/audio/OnsetDetector.h`**

```cpp
#pragma once

namespace guitar_dsp::audio {

// Note-attack detector for a clean (pre-effect) guitar signal. A peak
// envelope follower (instant attack, exponential release) plus hysteresis
// (arm / re-arm) and a debounce window. processSample returns true on the
// single sample where an onset fires. Allocation-free.
class OnsetDetector {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setAttackThreshold(float linear) noexcept { attackThresh_ = linear; }
    void setRearmThreshold(float linear) noexcept  { rearmThresh_ = linear; }
    void setDebounceMs(float ms) noexcept;

    bool processSample(float x) noexcept;

private:
    double sampleRate_   = 48000.0;
    float  env_          = 0.0f;
    float  releaseCoef_  = 0.0f;
    float  attackThresh_ = 0.05f;
    float  rearmThresh_  = 0.02f;
    bool   armed_        = true;
    int    debounceSamples_ = 0;
    int    sinceOnset_      = 0;   // saturating counter
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 5: Write `src/audio/OnsetDetector.cpp`**

```cpp
#include "OnsetDetector.h"

#include <cmath>

namespace guitar_dsp::audio {

void OnsetDetector::prepare(double sampleRate) noexcept {
    sampleRate_ = sampleRate;
    const double releaseMs = 30.0;
    releaseCoef_ = static_cast<float>(std::exp(-1.0 / (sampleRate * releaseMs / 1000.0)));
    setDebounceMs(40.0f);
    reset();
}

void OnsetDetector::reset() noexcept {
    env_ = 0.0f;
    armed_ = true;
    sinceOnset_ = debounceSamples_;   // allow an onset immediately
}

void OnsetDetector::setDebounceMs(float ms) noexcept {
    debounceSamples_ = static_cast<int>(ms * 0.001 * sampleRate_);
    if (debounceSamples_ < 1) debounceSamples_ = 1;
}

bool OnsetDetector::processSample(float x) noexcept {
    const float a = std::fabs(x);
    env_ = (a > env_) ? a : (env_ * releaseCoef_);   // instant attack, exp release

    if (sinceOnset_ < debounceSamples_) ++sinceOnset_;

    bool onset = false;
    if (armed_ && env_ >= attackThresh_ && sinceOnset_ >= debounceSamples_) {
        onset = true;
        armed_ = false;
        sinceOnset_ = 0;
    } else if (!armed_ && env_ <= rearmThresh_) {
        armed_ = true;
    }
    return onset;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 6: Add to `src/CMakeLists.txt`** — append `audio/OnsetDetector.cpp` to the `guitar_dsp_audio` source list.

- [ ] **Step 7: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "OnsetDetector"`
Expected: 4 tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/audio/OnsetDetector.h src/audio/OnsetDetector.cpp src/CMakeLists.txt \
        tests/unit/audio/test_onset_detector.cpp tests/CMakeLists.txt
git commit -m "feat(audio): OnsetDetector — note-attack detection (hysteresis + debounce)"
```

---

## Task 3: WordAligner (TDD)

**Files:**
- Create: `src/audio/WordAligner.h`, `src/audio/WordAligner.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/unit/audio/test_word_aligner.cpp`
- Modify: `tests/CMakeLists.txt`

Uniform energy-gap segmentation: split a clip's samples into `words.size()`
segments by the largest inter-word silence gaps. Pure, message-thread.

- [ ] **Step 1: Write the failing test** `tests/unit/audio/test_word_aligner.cpp`

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/WordAligner.h"

#include <cmath>
#include <string>
#include <vector>

using guitar_dsp::audio::WordAligner;
using guitar_dsp::audio::WordSegment;

namespace {
// Build a clip of `nWords` tone bursts separated by silence gaps.
std::vector<float> bursts(int nWords, int burstLen, int gapLen) {
    std::vector<float> b;
    for (int w = 0; w < nWords; ++w) {
        for (int i = 0; i < burstLen; ++i)
            b.push_back(0.6f * std::sin(2.0f*3.14159265f*300.0f*i/48000.0f));
        if (w < nWords - 1) for (int i = 0; i < gapLen; ++i) b.push_back(0.0f);
    }
    return b;
}
}

TEST_CASE("WordAligner: segments a 3-word clip on its gaps", "[audio][aligner]") {
    const int burst = 8000, gap = 4000;
    auto samples = bursts(3, burst, gap);
    std::vector<std::string> words{"one", "two", "three"};

    auto segs = WordAligner::align(samples, words, 48000.0);
    REQUIRE(segs.size() == 3);
    REQUIRE(segs[0].word == "one");
    REQUIRE(segs[2].word == "three");
    // Segments are contiguous and cover the whole clip.
    REQUIRE(segs[0].startSample == 0);
    REQUIRE(segs[2].endSample == samples.size());
    for (size_t i = 1; i < segs.size(); ++i)
        REQUIRE(segs[i].startSample == segs[i-1].endSample);
    // First boundary lands inside the first gap (between burst1 and burst2).
    REQUIRE(segs[0].endSample > static_cast<size_t>(burst));
    REQUIRE(segs[0].endSample < static_cast<size_t>(burst + gap));
}

TEST_CASE("WordAligner: single word spans the whole clip", "[audio][aligner]") {
    std::vector<float> samples(10000, 0.5f);
    auto segs = WordAligner::align(samples, {"solo"}, 48000.0);
    REQUIRE(segs.size() == 1);
    REQUIRE(segs[0].startSample == 0);
    REQUIRE(segs[0].endSample == 10000);
}

TEST_CASE("WordAligner: empty clip or no words yields no segments",
          "[audio][aligner]") {
    REQUIRE(WordAligner::align({}, {"a","b"}, 48000.0).empty());
    std::vector<float> s(100, 0.1f);
    REQUIRE(WordAligner::align(s, {}, 48000.0).empty());
}

TEST_CASE("WordAligner: gapless clip still returns N even segments",
          "[audio][aligner]") {
    std::vector<float> samples(9000, 0.5f);   // no silence anywhere
    auto segs = WordAligner::align(samples, {"a","b","c"}, 48000.0);
    REQUIRE(segs.size() == 3);
    REQUIRE(segs[0].startSample == 0);
    REQUIRE(segs[2].endSample == 9000);
    for (size_t i = 1; i < segs.size(); ++i)
        REQUIRE(segs[i].startSample == segs[i-1].endSample);
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`** — append `unit/audio/test_word_aligner.cpp`.

- [ ] **Step 3: Run to confirm failure** — FAIL (`audio/WordAligner.h` not found).

- [ ] **Step 4: Write `src/audio/WordAligner.h`**

```cpp
#pragma once

#include <string>
#include <vector>

#include "TTSClip.h"

namespace guitar_dsp::audio {

// Splits a clip's samples into one WordSegment per word using energy-gap
// segmentation: the N-1 largest inter-word silence gaps become boundaries.
// Uniform across all TTS backends (operates only on the produced PCM + the
// word list). Pure / message-thread only.
class WordAligner {
public:
    static std::vector<WordSegment> align(const std::vector<float>& samples,
                                          const std::vector<std::string>& words,
                                          double sampleRate);
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 5: Write `src/audio/WordAligner.cpp`**

```cpp
#include "WordAligner.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace guitar_dsp::audio {

std::vector<WordSegment> WordAligner::align(const std::vector<float>& samples,
                                            const std::vector<std::string>& words,
                                            double sampleRate) {
    std::vector<WordSegment> out;
    const std::size_t N = words.size();
    const std::size_t len = samples.size();
    if (N == 0 || len == 0) return out;
    if (N == 1) { out.push_back({words[0], 0, len}); return out; }

    // Smoothed peak envelope (instant attack, exp release ~5 ms).
    const float coef = static_cast<float>(std::exp(-1.0 / (sampleRate * 0.005)));
    std::vector<float> env(len);
    float e = 0.0f, peak = 0.0f;
    for (std::size_t i = 0; i < len; ++i) {
        const float a = std::fabs(samples[i]);
        e = (a > e) ? a : (e * coef);
        env[i] = e;
        peak = std::max(peak, e);
    }
    const float thresh = peak * 0.15f;

    // Inter-word gap runs (env < thresh), excluding leading/trailing silence.
    std::vector<std::pair<std::size_t, std::size_t>> gaps;  // (center, length)
    std::size_t runStart = 0; bool inRun = false;
    for (std::size_t i = 0; i < len; ++i) {
        const bool silent = (env[i] < thresh);
        if (silent && !inRun) { inRun = true; runStart = i; }
        else if (!silent && inRun) {
            inRun = false;
            if (runStart > 0)  // skip leading silence
                gaps.push_back({(runStart + i) / 2, i - runStart});
        }
    }
    // A run reaching the end is trailing silence — skip it (not added).

    // Keep the N-1 longest gaps; order their centers by position.
    std::sort(gaps.begin(), gaps.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    std::vector<std::size_t> boundaries;
    for (std::size_t i = 0; i < gaps.size() && boundaries.size() < N - 1; ++i)
        boundaries.push_back(gaps[i].first);
    // Not enough gaps: fill remaining boundaries evenly.
    while (boundaries.size() < N - 1)
        boundaries.push_back(len * (boundaries.size() + 1) / N);
    std::sort(boundaries.begin(), boundaries.end());

    std::size_t prev = 0;
    for (std::size_t i = 0; i < N; ++i) {
        const std::size_t end = (i < N - 1) ? boundaries[i] : len;
        out.push_back({words[i], prev, end});
        prev = end;
    }
    return out;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 6: Add to `src/CMakeLists.txt`** — append `audio/WordAligner.cpp`.

- [ ] **Step 7: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "WordAligner"`
Expected: 4 tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/audio/WordAligner.h src/audio/WordAligner.cpp src/CMakeLists.txt \
        tests/unit/audio/test_word_aligner.cpp tests/CMakeLists.txt
git commit -m "feat(audio): WordAligner — energy-gap word segmentation"
```

---

## Task 4: NoteSteppedTTSPlayer (TDD)

**Files:**
- Create: `src/audio/NoteSteppedTTSPlayer.h`, `src/audio/NoteSteppedTTSPlayer.cpp`
- Modify: `src/CMakeLists.txt`
- Create: `tests/unit/audio/test_note_stepped_player.cpp`
- Modify: `tests/CMakeLists.txt`

Note-triggered analogue of `TTSClipPlayer`. Holds the segmented clip + current
word index; an internal `OnsetDetector` watches the clean guitar; each onset
plays the next word's segment (wrapping to 0 after the last) as the modulator.

- [ ] **Step 1: Write the failing test** `tests/unit/audio/test_note_stepped_player.cpp`

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/NoteSteppedTTSPlayer.h"
#include "audio/TTSClip.h"
#include "harness/RealtimeSentinel.h"

#include <cmath>
#include <memory>
#include <vector>

using guitar_dsp::audio::NoteSteppedTTSPlayer;
using guitar_dsp::audio::TTSClip;
using guitar_dsp::audio::WordSegment;
using guitar_dsp::tests::RealtimeSentinel;

namespace {
// A clip whose 3 word-segments are filled with distinct constant values.
std::shared_ptr<const TTSClip> threeWordClip() {
    auto c = std::make_shared<TTSClip>();
    c->sampleRate = 48000.0;
    c->samples.assign(3000, 0.0f);
    for (int i = 0; i < 1000; ++i) c->samples[i] = 0.1f;
    for (int i = 1000; i < 2000; ++i) c->samples[i] = 0.2f;
    for (int i = 2000; i < 3000; ++i) c->samples[i] = 0.3f;
    c->words = { {"a",0,1000}, {"b",1000,2000}, {"c",2000,3000} };
    return c;
}
// One loud block (a pluck) then a silent block, written into onset/out scratch.
void pluckBlock(NoteSteppedTTSPlayer& p, std::vector<float>& mod) {
    std::vector<float> onset(2000, 0.8f);   // loud -> onset
    mod.assign(2000, 0.0f);
    p.process(onset.data(), mod.data(), mod.size());
}
void silentBlock(NoteSteppedTTSPlayer& p) {
    std::vector<float> onset(8000, 0.0f), mod(8000, 0.0f);  // long silence -> rearm
    p.process(onset.data(), mod.data(), mod.size());
}
}

TEST_CASE("NoteSteppedTTSPlayer: each onset advances one word, wraps",
          "[audio][notestep]") {
    NoteSteppedTTSPlayer p;
    p.prepare(48000.0, 8000);
    p.setClip(threeWordClip());

    REQUIRE(p.currentWordIndex() == -1);   // idle before any pluck

    std::vector<float> mod;
    pluckBlock(p, mod);  REQUIRE(p.currentWordIndex() == 0);  silentBlock(p);
    pluckBlock(p, mod);  REQUIRE(p.currentWordIndex() == 1);  silentBlock(p);
    pluckBlock(p, mod);  REQUIRE(p.currentWordIndex() == 2);  silentBlock(p);
    pluckBlock(p, mod);  REQUIRE(p.currentWordIndex() == 0);  // wrapped
}

TEST_CASE("NoteSteppedTTSPlayer: emits the active word's samples",
          "[audio][notestep]") {
    NoteSteppedTTSPlayer p;
    p.prepare(48000.0, 8000);
    p.setClip(threeWordClip());

    std::vector<float> onset(2000, 0.8f), mod(2000, 0.0f);
    p.process(onset.data(), mod.data(), mod.size());
    // After the onset fires (early in the block), the modulator should carry
    // word 0's value (0.1) somewhere in this block.
    float maxAbs = 0.0f;
    for (float v : mod) maxAbs = std::max(maxAbs, std::fabs(v));
    REQUIRE(maxAbs > 0.05f);   // word audio is playing, not silent
    REQUIRE(maxAbs < 0.15f);   // it's word 0 (0.1), not 0.2/0.3
}

TEST_CASE("NoteSteppedTTSPlayer: silence in, silence out (no pluck)",
          "[audio][notestep]") {
    NoteSteppedTTSPlayer p;
    p.prepare(48000.0, 8000);
    p.setClip(threeWordClip());
    std::vector<float> onset(2000, 0.0f), mod(2000, 1.0f);
    p.process(onset.data(), mod.data(), mod.size());
    for (float v : mod) REQUIRE(v == 0.0f);
    REQUIRE(p.currentWordIndex() == -1);
}

TEST_CASE("NoteSteppedTTSPlayer: process is allocation-free",
          "[audio][notestep][rt]") {
    NoteSteppedTTSPlayer p;
    p.prepare(48000.0, 512);
    p.setClip(threeWordClip());
    std::vector<float> onset(512), mod(512);
    for (int i = 0; i < 512; ++i)
        onset[static_cast<size_t>(i)] = 0.5f * std::sin(2.0f*3.14159265f*110.0f*i/48000.0f);
    p.process(onset.data(), mod.data(), mod.size());  // pick up clip

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int blk = 0; blk < 50; ++blk) p.process(onset.data(), mod.data(), mod.size());
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`** — append `unit/audio/test_note_stepped_player.cpp`.

- [ ] **Step 3: Run to confirm failure** — FAIL (`audio/NoteSteppedTTSPlayer.h` not found).

- [ ] **Step 4: Write `src/audio/NoteSteppedTTSPlayer.h`**

```cpp
#pragma once

#include <atomic>
#include <cstddef>

#include "OnsetDetector.h"
#include "TTSClip.h"

namespace guitar_dsp::audio {

// Plays a segmented TTSClip one word per guitar-note onset. An internal
// OnsetDetector watches the clean guitar (passed to process as onsetSrc); each
// onset advances to the next word (wrapping after the last) and plays that
// word's segment as the vocoder modulator. setClip uses the same atomic swap
// as TTSClipPlayer. Allocation-free in process().
class NoteSteppedTTSPlayer {
public:
    NoteSteppedTTSPlayer();

    void prepare(double sampleRate, int blockSize);
    void reset();

    // Message thread. Pass nullptr to clear.
    void setClip(TTSClipPtr clip);

    // Audio thread. onsetSrc = clean guitar; writes the modulator to modOut.
    void process(const float* onsetSrc, float* modOut, std::size_t numSamples) noexcept;

    // UI: current spoken word index, or -1 when idle.
    int currentWordIndex() const noexcept {
        return currentWordIndex_.load(std::memory_order_relaxed);
    }

private:
    OnsetDetector onset_;

    std::atomic<bool> newClipFlag_ {false};
    TTSClipPtr        pendingClip_;     // message thread
    TTSClipPtr        activeClip_;      // audio thread

    int         wordIndex_  = -1;       // audio thread
    std::size_t playPos_    = 0;        // audio thread
    std::size_t segEnd_     = 0;        // audio thread
    bool        playing_    = false;    // audio thread

    std::atomic<int> currentWordIndex_ {-1};
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 5: Write `src/audio/NoteSteppedTTSPlayer.cpp`**

```cpp
#include "NoteSteppedTTSPlayer.h"

namespace guitar_dsp::audio {

NoteSteppedTTSPlayer::NoteSteppedTTSPlayer() = default;

void NoteSteppedTTSPlayer::prepare(double sampleRate, int /*blockSize*/) {
    onset_.prepare(sampleRate);
    reset();
}

void NoteSteppedTTSPlayer::reset() {
    onset_.reset();
    wordIndex_ = -1;
    playPos_ = 0;
    segEnd_ = 0;
    playing_ = false;
    currentWordIndex_.store(-1, std::memory_order_relaxed);
}

void NoteSteppedTTSPlayer::setClip(TTSClipPtr clip) {
    pendingClip_ = std::move(clip);
    newClipFlag_.store(true, std::memory_order_release);
}

void NoteSteppedTTSPlayer::process(const float* onsetSrc, float* modOut,
                                   std::size_t numSamples) noexcept {
    if (newClipFlag_.exchange(false, std::memory_order_acquire)) {
        activeClip_ = std::move(pendingClip_);
        wordIndex_ = -1;
        playPos_ = 0;
        segEnd_ = 0;
        playing_ = false;
        onset_.reset();
        currentWordIndex_.store(-1, std::memory_order_relaxed);
    }

    const bool haveClip  = activeClip_ && !activeClip_->samples.empty();
    const bool haveWords = haveClip && !activeClip_->words.empty();

    for (std::size_t i = 0; i < numSamples; ++i) {
        if (onset_.processSample(onsetSrc[i]) && haveClip) {
            if (haveWords) {
                const int n = static_cast<int>(activeClip_->words.size());
                wordIndex_ = (wordIndex_ + 1) % n;
                playPos_ = activeClip_->words[static_cast<std::size_t>(wordIndex_)].startSample;
                segEnd_  = activeClip_->words[static_cast<std::size_t>(wordIndex_)].endSample;
            } else {
                wordIndex_ = 0;
                playPos_ = 0;
                segEnd_ = activeClip_->samples.size();
            }
            playing_ = true;
            currentWordIndex_.store(wordIndex_, std::memory_order_relaxed);
        }

        float s = 0.0f;
        if (playing_ && playPos_ < segEnd_ && playPos_ < activeClip_->samples.size()) {
            s = activeClip_->samples[playPos_++];
        } else {
            playing_ = false;
        }
        modOut[i] = s;
    }
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 6: Add to `src/CMakeLists.txt`** — append `audio/NoteSteppedTTSPlayer.cpp`.

- [ ] **Step 7: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "NoteSteppedTTSPlayer"`
Expected: 4 tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/audio/NoteSteppedTTSPlayer.h src/audio/NoteSteppedTTSPlayer.cpp src/CMakeLists.txt \
        tests/unit/audio/test_note_stepped_player.cpp tests/CMakeLists.txt
git commit -m "feat(audio): NoteSteppedTTSPlayer — one word per note onset"
```

---

## Task 5: Scene tts.trigger field (TDD)

**Files:**
- Modify: `src/scenes/Scene.h`
- Modify: `src/scenes/SceneLibrary.cpp`
- Create: `tests/unit/scenes/test_scene_library_tts_trigger.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test** `tests/unit/scenes/test_scene_library_tts_trigger.cpp`

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

TEST_CASE("SceneLibrary: parses tts.trigger", "[scenes][library][tts]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_tts_trigger.json"));
    REQUIRE(s.has_value());
    REQUIRE(s->tts.trigger == "note");
}

TEST_CASE("SceneLibrary: missing tts.trigger leaves empty", "[scenes][library][tts]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_tts_text.json"));
    REQUIRE(s.has_value());
    REQUIRE(s->tts.trigger.empty());
}
```

- [ ] **Step 2: Create the fixture** `tests/fixtures/scenes/with_tts_trigger.json`

```json
{
  "id": 33,
  "name": "Note-triggered fixture",
  "color": "#a64dff",
  "mixer": { "masterGainDb": -3.0, "dryWet": 0.9, "transitionMs": 20 },
  "tts": { "source": "prebaked", "clip": "mid_talk", "text": "hello there world", "trigger": "note" }
}
```

- [ ] **Step 3: Add to `tests/CMakeLists.txt`** — append `unit/scenes/test_scene_library_tts_trigger.cpp`.

- [ ] **Step 4: Run to confirm failure** — FAIL (`TtsConfig` has no member `trigger`).

- [ ] **Step 5: Update `src/scenes/Scene.h`** — add `trigger` to `TtsConfig`, after `fallback`:

```cpp
    std::string trigger;  // "note" (per-note word stepping) | "auto"/"" (linear)
```

- [ ] **Step 6: Update `src/scenes/SceneLibrary.cpp`** — in the existing `tts` parse block, after the `fallback` line, add:

```cpp
            if (t->hasProperty("trigger"))
                s.tts.trigger = t->getProperty("trigger").toString().toStdString();
```

- [ ] **Step 7: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "tts.trigger|missing tts.trigger"`
Expected: 2 tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/scenes/Scene.h src/scenes/SceneLibrary.cpp \
        tests/unit/scenes/test_scene_library_tts_trigger.cpp \
        tests/fixtures/scenes/with_tts_trigger.json tests/CMakeLists.txt
git commit -m "feat(scenes): TtsConfig gains trigger field (note vs auto)"
```

---

## Task 6: AudioGraph — note-stepped player + modulator selector (TDD)

**Files:**
- Modify: `src/audio/AudioGraph.h`
- Modify: `src/audio/AudioGraph.cpp`
- Modify: `tests/unit/audio/test_audio_graph.cpp`

`AudioGraph` gains a `NoteSteppedTTSPlayer` and an atomic modulator-source
selector. In the vocoder branch the modulator comes from either the linear
`ttsClipPlayer_` (default) or the `noteSteppedPlayer_` (driven by the clean
guitar in `postInputBuffer_`).

- [ ] **Step 1: Add the failing test** — append to `tests/unit/audio/test_audio_graph.cpp`:

```cpp
TEST_CASE("AudioGraph: note-stepped modulator vocodes a word on a pluck",
          "[audio][graph][notestep]") {
    using guitar_dsp::audio::AudioGraph;
    using guitar_dsp::audio::TTSClip;

    AudioGraph g;
    g.prepare(48000.0, 1024);
    g.mixer().setDryWet(1.0f); g.mixer().setMasterGainDb(0.0f); g.mixer().reset();

    auto clip = std::make_shared<TTSClip>();
    clip->sampleRate = 48000.0;
    clip->samples.assign(2000, 0.4f);          // a single "word" of tone-ish energy
    clip->words = { {"word", 0, 2000} };
    g.noteSteppedPlayer().setClip(clip);
    g.setModulatorSource(AudioGraph::ModulatorSource::NoteStepped);

    std::vector<float> in(1024), out(1024);
    for (int i = 0; i < 1024; ++i)             // loud guitar -> onset
        in[static_cast<size_t>(i)] = 0.8f * std::sin(2.0f*3.14159265f*110.0f*i/48000.0f);

    g.process(in.data(), out.data(), out.size());
    float peak = 0.0f;
    for (float x : out) peak = std::max(peak, std::fabs(x));
    REQUIRE(peak > 1e-3f);                      // the guitar "spoke" the word
    for (float x : out) REQUIRE(std::isfinite(x));
}
```

Confirm the test file already includes `"audio/TTSClip.h"`, `<memory>`, `<cmath>`, `<algorithm>`, `<vector>`; add any that are missing.

- [ ] **Step 2: Run to confirm failure** — FAIL (no `noteSteppedPlayer()` / `setModulatorSource` / `ModulatorSource`).

- [ ] **Step 3: Update `src/audio/AudioGraph.h`** — include, members, accessors, selector.

Add include:
```cpp
#include "NoteSteppedTTSPlayer.h"
```

In `public:`, after `TTSClipPlayer& ttsClipPlayer()`:
```cpp
    NoteSteppedTTSPlayer& noteSteppedPlayer() { return noteSteppedPlayer_; }

    enum class ModulatorSource { Linear, NoteStepped };
    void setModulatorSource(ModulatorSource s) noexcept {
        modulatorSource_.store(static_cast<int>(s), std::memory_order_relaxed);
    }
```

In `private:`, after `TTSClipPlayer ttsClipPlayer_;`:
```cpp
    NoteSteppedTTSPlayer noteSteppedPlayer_;
    std::atomic<int> modulatorSource_ {static_cast<int>(ModulatorSource::Linear)};
```

(`<atomic>` is already included from the Phase-4 wet-source selector.)

- [ ] **Step 4: Update `src/audio/AudioGraph.cpp`.**

In `prepare()`, after `ttsClipPlayer_.prepare(sampleRate, blockSize);`:
```cpp
    noteSteppedPlayer_.prepare(sampleRate, blockSize);
```

In `reset()`, after `ttsClipPlayer_.reset();`:
```cpp
    noteSteppedPlayer_.reset();
```

In `process()`, replace the vocoder-branch modulator fill so it picks the source:
```cpp
    } else {
        // Vocoder branch: modulator from the selected TTS player.
        if (modulatorSource_.load(std::memory_order_relaxed)
                == static_cast<int>(ModulatorSource::NoteStepped)) {
            // Onset source = clean guitar (postInputBuffer_); writes modulator.
            noteSteppedPlayer_.process(postInputBuffer_.data(),
                                       wetBuffer_.data(), numSamples);
        } else {
            ttsClipPlayer_.process(wetBuffer_.data(), numSamples);
        }
        vocoder_.process(postInputBuffer_.data(), wetBuffer_.data(),
                         wetBuffer_.data(), numSamples);
    }
```

- [ ] **Step 5: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "note-stepped modulator"`
Expected: PASS. Full suite green (`ctest --test-dir build 2>&1 | tail -2`).

- [ ] **Step 6: Commit**

```bash
git add src/audio/AudioGraph.h src/audio/AudioGraph.cpp tests/unit/audio/test_audio_graph.cpp
git commit -m "feat(audio): AudioGraph note-stepped modulator source"
```

---

## Task 7: PluginProcessor — segment clip + route + expose word index

**Files:**
- Modify: `src/app/PluginProcessor.h`
- Modify: `src/app/PluginProcessor.cpp`

On a scene change for a note-triggered speaking scene: split the clip into words
via `WordAligner` (message thread), push the segmented clip into the
note-stepped player, and set the modulator source. Also expose the current word
index + the active scene's word list for the UI.

- [ ] **Step 1: Update `src/app/PluginProcessor.h`** — add include + accessors.

Add include near the others:
```cpp
#include "audio/WordAligner.h"
```

In `public:` (near `sceneEngine()`):
```cpp
    // Current spoken word index for the active note-triggered scene (-1 idle).
    int currentSpokenWordIndex() const noexcept {
        return graph_.noteSteppedPlayer().currentWordIndex();
    }
    // The active scene's words (split on whitespace). Message thread.
    std::vector<std::string> activeSceneWords() const;
```

- [ ] **Step 2: Update `src/app/PluginProcessor.cpp` — implement `activeSceneWords()`.** Add near the other accessors (and add `#include <sstream>` at the top if not present):

```cpp
std::vector<std::string> PluginProcessor::activeSceneWords() const {
    const auto cfg = sceneEngine_.activeTtsConfig();
    std::vector<std::string> words;
    std::istringstream iss(cfg.text);
    std::string w;
    while (iss >> w) words.push_back(w);
    return words;
}
```

- [ ] **Step 3: Extend the scene-change callAsync in `processBlock`.** Find the block (after the carousel early-return added in Phase 4) where `cfg = sceneEngine_.activeTtsConfig()` and the clip is built via `synthesizeWithFallback` and `graph_.ttsClipPlayer().setClip(clip)`. After the `clip` is obtained and BEFORE the existing `graph_.ttsClipPlayer().setClip(clip)`, insert the note-triggered routing:

```cpp
            const bool noteTriggered =
                (cfg.trigger == "note" || cfg.trigger.empty());

            if (noteTriggered && clip && !clip->samples.empty()) {
                // Segment the clip into words on the message thread, then route
                // it through the note-stepped player. WordAligner needs a
                // mutable copy (TTSClipPtr is shared_ptr<const>).
                auto seg = std::make_shared<audio::TTSClip>(*clip);
                std::vector<std::string> words;
                {
                    std::istringstream iss(cfg.text.empty() ? clip->name : cfg.text);
                    std::string w;
                    while (iss >> w) words.push_back(w);
                }
                if (!words.empty()) {
                    seg->words = audio::WordAligner::align(seg->samples, words,
                                                           seg->sampleRate);
                }
                graph_.noteSteppedPlayer().setClip(seg);
                graph_.setModulatorSource(audio::AudioGraph::ModulatorSource::NoteStepped);
                graph_.ttsClipPlayer().setClip(nullptr);
                return;   // skip the linear-player path below
            }

            graph_.setModulatorSource(audio::AudioGraph::ModulatorSource::Linear);
```

The trailing `graph_.ttsClipPlayer().setClip(clip)` (already present) then runs
for `auto` scenes. Add `#include <sstream>` and `#include <string>` near the top
if not already present.

- [ ] **Step 4: Build + verify no regressions**

Run: `cmake --build build --target guitar_dsp_app_Standalone guitar_dsp_tests && ctest --test-dir build --output-on-failure 2>&1 | tail -3`
Expected: builds; full suite green. (Existing speaking-scene integration tests drive `AudioGraph` directly, not `PluginProcessor`'s callAsync, so they're unaffected; the live app now routes speaking scenes through the note-stepped player.)

- [ ] **Step 5: Commit**

```bash
git add src/app/PluginProcessor.h src/app/PluginProcessor.cpp
git commit -m "feat(app): segment + route speaking scenes through note-stepped player"
```

---

## Task 8: Minimal WordReadout UI

**Files:**
- Create: `src/app/WordReadout.h`, `src/app/WordReadout.cpp`
- Modify: `src/app/PluginEditor.h`, `src/app/PluginEditor.cpp`
- Modify: `src/app/CMakeLists.txt`

A small component that polls the processor's current word index + the active
scene's words and shows the current spoken word big. No automated test (it's a
thin polling view); verified by the manual smoke in Task 10.

- [ ] **Step 1: Write `src/app/WordReadout.h`**

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class PluginProcessor;

// Large readout of the word the guitar is currently "speaking" in a
// note-triggered scene. Polls the processor on a timer; shows the current
// word centered, with the previous/next words dimmed on either side.
class WordReadout : public juce::Component,
                    private juce::Timer {
public:
    explicit WordReadout(PluginProcessor& processor);
    ~WordReadout() override;

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    PluginProcessor& processor_;
    int lastIndex_ = -2;
};

} // namespace guitar_dsp
```

- [ ] **Step 2: Write `src/app/WordReadout.cpp`**

```cpp
#include "WordReadout.h"

#include "PluginProcessor.h"

namespace guitar_dsp {

WordReadout::WordReadout(PluginProcessor& processor) : processor_(processor) {
    setOpaque(true);
    startTimerHz(30);
}

WordReadout::~WordReadout() { stopTimer(); }

void WordReadout::timerCallback() {
    const int idx = processor_.currentSpokenWordIndex();
    if (idx != lastIndex_) { lastIndex_ = idx; repaint(); }
}

void WordReadout::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(12, 13, 18));

    const auto words = processor_.activeSceneWords();
    const int idx = processor_.currentSpokenWordIndex();
    auto area = getLocalBounds();

    if (words.empty() || idx < 0 || idx >= static_cast<int>(words.size())) {
        g.setColour(juce::Colour::fromRGB(90, 95, 110));
        g.setFont(juce::Font{juce::FontOptions{}.withHeight(16.0f)});
        g.drawText("(pluck a note to speak)", area, juce::Justification::centred);
        return;
    }

    auto dim = [&](int i) {
        return (i >= 0 && i < static_cast<int>(words.size()))
                 ? juce::String(words[static_cast<std::size_t>(i)]) : juce::String();
    };

    const int third = area.getWidth() / 3;
    auto left  = area.removeFromLeft(third);
    auto right = area.removeFromRight(third);
    auto mid   = area;

    g.setColour(juce::Colour::fromRGB(70, 74, 88));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(18.0f)});
    g.drawText(dim(idx - 1), left,  juce::Justification::centredRight);
    g.drawText(dim(idx + 1), right, juce::Justification::centredLeft);

    g.setColour(juce::Colour::fromRGB(240, 230, 180));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(34.0f).withStyle("Bold")});
    g.drawText(dim(idx), mid, juce::Justification::centred);
}

} // namespace guitar_dsp
```

- [ ] **Step 3: Add to `src/app/CMakeLists.txt`** — append `WordReadout.cpp` to the `target_sources(guitar_dsp_app PRIVATE ...)` list.

- [ ] **Step 4: Host it in `src/app/PluginEditor.h`** — add include + member.

Add include with the others:
```cpp
#include "WordReadout.h"
```
In `private:`, after `SceneIndicator sceneIndicator_;`:
```cpp
    WordReadout      wordReadout_;
```

- [ ] **Step 5: Wire it in `src/app/PluginEditor.cpp`** — ctor init + add + layout.

In the constructor initializer list, after `sceneIndicator_(p),`:
```cpp
      wordReadout_(p),
```
In the constructor body, after `addAndMakeVisible(sceneIndicator_);`:
```cpp
    addAndMakeVisible(wordReadout_);
```
Bump the window height so the new strip fits: change `setSize(720, 528);` to `setSize(720, 572);` and `setResizeLimits(520, 360, ...)` to `setResizeLimits(520, 400, 1800, 1200);`.
In `resized()`, after the `sceneIndicator_.setBounds(...)` line, add:
```cpp
    wordReadout_.setBounds(bounds.removeFromTop(44));
```

- [ ] **Step 6: Build**

Run: `cmake --build build --target guitar_dsp_app_Standalone 2>&1 | tail -3`
Expected: builds clean.

- [ ] **Step 7: Commit**

```bash
git add src/app/WordReadout.h src/app/WordReadout.cpp \
        src/app/PluginEditor.h src/app/PluginEditor.cpp src/app/CMakeLists.txt
git commit -m "feat(ui): WordReadout shows the word the guitar is speaking"
```

---

## Task 9: Note-trigger the speaking scenes + integration test (TDD)

**Files:**
- Modify: `assets/scenes/06_speaking_a.json`
- Modify: `assets/scenes/07_speaking_b.json`
- Modify: `assets/scenes/08_speaking_finale.json`
- Create: `tests/integration/test_note_triggered_speech.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing integration test** `tests/integration/test_note_triggered_speech.cpp`

```cpp
#include <catch2/catch_test_macros.hpp>

#include "audio/AudioGraph.h"
#include "audio/TTSClip.h"

#include <cmath>
#include <memory>
#include <vector>

using guitar_dsp::audio::AudioGraph;
using guitar_dsp::audio::TTSClip;

namespace {
// Drive the graph with one "pluck" block (loud) then a silence block.
void pluck(AudioGraph& g, int n) {
    std::vector<float> in(static_cast<size_t>(n)), out(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        in[static_cast<size_t>(i)] = 0.8f * std::sin(2.0f*3.14159265f*110.0f*i/48000.0f);
    g.process(in.data(), out.data(), out.size());
}
void quiet(AudioGraph& g, int n) {
    std::vector<float> in(static_cast<size_t>(n), 0.0f), out(static_cast<size_t>(n), 0.0f);
    g.process(in.data(), out.data(), out.size());
}
}

TEST_CASE("integration: plucks step through words and loop",
          "[integration][notestep]") {
    AudioGraph g;
    g.prepare(48000.0, 2048);
    g.mixer().setDryWet(1.0f); g.mixer().setMasterGainDb(0.0f); g.mixer().reset();

    auto clip = std::make_shared<TTSClip>();
    clip->sampleRate = 48000.0;
    clip->samples.assign(3000, 0.3f);
    clip->words = { {"a",0,1000}, {"b",1000,2000}, {"c",2000,3000} };
    g.noteSteppedPlayer().setClip(clip);
    g.setModulatorSource(AudioGraph::ModulatorSource::NoteStepped);

    REQUIRE(g.noteSteppedPlayer().currentWordIndex() == -1);

    pluck(g, 2048); REQUIRE(g.noteSteppedPlayer().currentWordIndex() == 0); quiet(g, 8000);
    pluck(g, 2048); REQUIRE(g.noteSteppedPlayer().currentWordIndex() == 1); quiet(g, 8000);
    pluck(g, 2048); REQUIRE(g.noteSteppedPlayer().currentWordIndex() == 2); quiet(g, 8000);
    pluck(g, 2048); REQUIRE(g.noteSteppedPlayer().currentWordIndex() == 0);  // looped
}
```

- [ ] **Step 2: Add to `tests/CMakeLists.txt`** — append `integration/test_note_triggered_speech.cpp`.

- [ ] **Step 3: Build + run**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure -R "plucks step through words"`
Expected: PASS (Tasks 4 + 6 already implement the behavior; this is the end-to-end guard).

- [ ] **Step 4: Add `"trigger": "note"` to the three speaking scenes.**

`assets/scenes/06_speaking_a.json` — add `"trigger": "note"` to the `tts` block:
```json
{
  "id": 6,
  "name": "Speaking A — apple",
  "color": "#a64dff",
  "mixer": { "masterGainDb": -3.0, "dryWet": 0.85, "transitionMs": 30 },
  "tts": {
    "source": "apple",
    "text": "Welcome to the talk. The guitar is now speaking.",
    "voice": "com.apple.voice.compact.en-US.Samantha",
    "trigger": "note"
  }
}
```

`assets/scenes/07_speaking_b.json`:
```json
{
  "id": 7,
  "name": "Speaking B — piper",
  "color": "#a64dff",
  "mixer": { "masterGainDb": -3.0, "dryWet": 0.85, "transitionMs": 30 },
  "tts": {
    "source": "piper",
    "text": "I think therefore I riff",
    "voice": "en_US-amy-medium",
    "clip": "07_mid_talk",
    "fallback": "prebaked",
    "trigger": "note"
  }
}
```

`assets/scenes/08_speaking_finale.json` — also add a `text` field so the prebaked
finale can be word-segmented (the words should match the baked phrase; adjust
later via hot-reload if the clip says something different):
```json
{
  "id": 8,
  "name": "Speaking finale — gently weeps",
  "color": "#a64dff",
  "mixer": { "masterGainDb": -2.0, "dryWet": 0.9, "transitionMs": 30 },
  "tts": {
    "source": "prebaked",
    "clip": "08_gently_weeps",
    "text": "While my guitar gently weeps",
    "trigger": "note"
  }
}
```

- [ ] **Step 5: Validate + rebuild (asset copy)**

```bash
for f in assets/scenes/0[6-8]_*.json; do python3 -c "import json,sys; json.load(open('$f'))" && echo "ok $f"; done
cmake --build build --target guitar_dsp_app_Standalone 2>&1 | tail -3
```
Expected: 3 "ok" lines; clean build.

- [ ] **Step 6: Commit**

```bash
git add tests/integration/test_note_triggered_speech.cpp tests/CMakeLists.txt \
        assets/scenes/06_speaking_a.json assets/scenes/07_speaking_b.json \
        assets/scenes/08_speaking_finale.json
git commit -m "feat(scenes): speaking scenes 6/7/8 are note-triggered + integration test"
```

---

## Task 10: README + Phase 5a wrap-up verification

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update `README.md`.** Replace the "Project status" lead so it reads:

```markdown
## Project status

This branch implements **Phase 5a: note-triggered word-by-word speech** — the
core "While My Guitar Gently Speaks" effect. In a speaking scene (`6`–`8`), the
TTS phrase is pre-split into per-word audio segments and **each plucked note
speaks the next word** through the vocoder, auto-looping after the last word.
Nothing speaks until you play, so the performer paces the whole sentence.

Pieces: `audio::OnsetDetector` (note-attack detection on the clean guitar),
`audio::WordAligner` (uniform energy-gap word segmentation — same for all three
TTS backends), and `audio::NoteSteppedTTSPlayer` (one word per onset, feeding the
vocoder modulator). A scene's `tts.trigger` selects `"note"` (default) vs
`"auto"` (the original linear playback). A minimal on-screen word readout shows
the current word.
```

Then update the "Subsequent phases" list — replace the Phase 5 bullet with:
```markdown
- **Phase 5b**: Full-screen show view — spectrogram backdrop + polished karaoke text.
- **Phase 6**: Hardening + dress rehearsal.
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: README documents Phase 5a note-triggered speech"
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
Expected: all pass; 4 pre-existing opt-in tests skipped. New count ≈ 130 + ~18.

- [ ] **Step 5: Manual smoke** (requires a guitar / input signal)

```bash
open "build/src/app/guitar_dsp_app_artefacts/Release/Standalone/Guitar DSP.app"
```
Verify with a signal at the input:
- Activate scene 6/7/8 (keys `7`/`8`/`9`). No speech until you pluck.
- Each pluck speaks the next word (vocoded) and the WordReadout advances.
- After the last word, the next pluck loops back to word 0.
- Instrument scenes (`2`–`6`) and clean/panic unaffected; no clicks/crashes.

State explicitly whether audio was actually auditioned or only build/tests
verified (no input signal in CI).

- [ ] **Step 6: Final commit if any cleanup was made**

```bash
git commit -am "chore: phase 5a wrap-up fixes"   # only if something changed
```

---

## Self-review

**Spec coverage (spec § → task):**
- §3.1 OnsetDetector → Task 2.
- §3.2 WordAligner → Task 3.
- §3.3 WordSegment on TTSClip → Task 1.
- §3.4 NoteSteppedTTSPlayer → Task 4.
- §3.5 minimal word readout → Task 8.
- §4 scene `tts.trigger` → Task 5.
- §5 AudioGraph wiring (note-stepped player + modulator selector) → Task 6; PluginProcessor segment+route → Task 7.
- §6 word-index + words exposure → Task 7.
- §7 testing (onset, aligner, player+RT, integration, no-regressions) → Tasks 2,3,4 (unit), Task 9 (integration), Task 4 (RT-safety).
- §8 out-of-scope (spectrogram/show view/engine-true timing) → not implemented; deferred to 5b in README (Task 10).

**Placeholder scan:** No "TBD"/"add error handling"/"similar to Task N" — every code step shows complete code.

**Type consistency:**
- `audio::WordSegment { std::string word; std::size_t startSample, endSample; }` + `TTSClip::words` — Task 1, used by Tasks 3, 4, 6, 7, 9.
- `audio::OnsetDetector` (`prepare(sr)`, `reset`, `setAttackThreshold`, `setRearmThreshold`, `setDebounceMs`, `processSample`) — Task 2, used by Task 4.
- `audio::WordAligner::align(samples, words, sampleRate) → vector<WordSegment>` — Task 3, used by Task 7.
- `audio::NoteSteppedTTSPlayer` (`prepare(sr,blk)`, `reset`, `setClip`, `process(onsetSrc, modOut, n)`, `currentWordIndex()`) — Task 4, used by Tasks 6, 7, 9.
- `AudioGraph::noteSteppedPlayer()`, `AudioGraph::ModulatorSource{Linear,NoteStepped}`, `setModulatorSource()` — Task 6, used by Tasks 7, 9.
- `TtsConfig::trigger` — Task 5, used by Task 7, 9.
- `PluginProcessor::currentSpokenWordIndex()` / `activeSceneWords()` — Task 7, used by Task 8.

---

## Execution handoff

Plan complete. Per project convention this executes **subagent-driven on a git worktree** — each implementer subagent's first instruction is to `cd` into the worktree's absolute path before any other action (prevents the stray-to-main bug).

1. **Subagent-Driven (recommended)** — fresh subagent per task, two-stage review between tasks.
2. **Inline Execution** — batch execution with checkpoints.
