# Word-Sync Modes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the note-triggered TTS bug where multi-syllable words get cut by fast onsets and double-onsets play multiple words per pluck. Replace the single advance-on-every-onset behavior with three user-selectable modes: **Latch** (current word completes before next onset advances — recommended default), **Advance** (current behavior), **Syllable** (hyphenated text → per-syllable stepping).

**Architecture:** New `WordSyncMode` enum, an atomic-int storage on `NoteSteppedTTSPlayer` that picks the per-onset behavior, an optional `syllables` segmentation vector on `TTSClip`, and a new `WordAligner::alignSyllables` that equal-subdivides each word's measured duration into per-hyphen segments. Wired through `AudioGraph` → `PluginProcessor` → `PluginState`. Per-scene override via a new `wordSync` JSON field. UI: a 3-pill mode selector row in `VocoderPanel`.

**Tech Stack:** C++20, JUCE, Catch2.

**Spec:** [docs/superpowers/specs/2026-06-13-word-sync-modes-design.md](../specs/2026-06-13-word-sync-modes-design.md)

---

## File-touch summary

**New:**
- `src/audio/WordSyncMode.h` — enum + ↔ string helpers
- `src/app/WordSyncSelector.{h,cpp}` — 3-pill JUCE control for `VocoderPanel`

**Edited:**
- `src/audio/NoteSteppedTTSPlayer.{h,cpp}` — mode storage + branch + segmentation list selection
- `src/audio/TTSClip.h` — add `syllables` vector
- `src/audio/WordAligner.{h,cpp}` — add `alignSyllables`
- `src/audio/OnsetDetector.cpp` — default debounce 40 → 80 ms
- `src/audio/AudioGraph.{h,cpp}` — forward `setWordSyncMode`/`wordSyncMode`
- `src/scenes/Scene.h` — add `wordSync` string field on `TtsConfig`
- `src/scenes/SceneLibrary.cpp` — parse `wordSync` JSON
- `src/scenes/SceneEngine.{h,cpp}` — on scene activate, apply scene's wordSync (or global)
- `src/app/PluginProcessor.{h,cpp}` — forward + persist mode
- `src/app/PluginState.{h,cpp}` — `int wordSyncMode` field + JSON
- `src/app/VocoderPanel.{h,cpp}` — host `WordSyncSelector`
- `src/app/CMakeLists.txt` — register `WordSyncSelector.cpp`
- `assets/scenes/08_speaking_finale.json` — example: hyphenated text + `"wordSync": "syllable"`
- `README.md` — document the mode selector + per-scene override
- `tests/CMakeLists.txt` — register new test files
- `tests/unit/audio/test_note_stepped_player.cpp` — mode tests (extend)
- `tests/unit/audio/test_onset_detector.cpp` — min-interval test (extend)
- `tests/unit/audio/test_word_aligner.cpp` — `alignSyllables` tests (NEW file)
- `tests/unit/scenes/test_scene_library_tts.cpp` — `wordSync` parse test (extend)
- `tests/unit/app/test_plugin_state.cpp` — `wordSyncMode` round-trip

---

## Task 1: Onset detector min-interval default 40 → 80 ms

The single most impactful fix for "1 strum triggers 3 words." Independent of mode selection.

**Files:**
- Modify: `src/audio/OnsetDetector.cpp`
- Modify: `tests/unit/audio/test_onset_detector.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/audio/test_onset_detector.cpp`:

```cpp
TEST_CASE("OnsetDetector: default min-interval suppresses second onset 60 ms after first",
          "[audio][onset_detector][debounce]") {
    using guitar_dsp::audio::OnsetDetector;
    OnsetDetector det;
    det.prepare(48000.0);

    // Two transient pulses 60 ms apart.
    constexpr int kSamples = 48000 / 5;  // 0.2 s
    std::vector<float> buf(kSamples, 0.0f);
    buf[0]    = 0.8f;      // pulse #1 at t=0
    buf[2880] = 0.8f;      // pulse #2 at t=60 ms

    int onsetCount = 0;
    for (float s : buf)
        if (det.processSample(s)) ++onsetCount;
    // With 80 ms default min-interval, only the first pulse fires.
    REQUIRE(onsetCount == 1);
}
```

- [ ] **Step 2: Run to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "OnsetDetector.*min-interval"
```

Expected: FAIL (current debounce default is 40 ms; both pulses fire → `onsetCount == 2`).

- [ ] **Step 3: Change the default**

In `src/audio/OnsetDetector.cpp::prepare()`, find:

```cpp
setDebounceMs(40.0f);
```

Replace with:

```cpp
setDebounceMs(80.0f);
```

- [ ] **Step 4: Re-run all OnsetDetector tests**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "onset_detector|OnsetDetector"
```

Expected: all pass. If any pre-existing test pinned the 40 ms default and now fails, mark DONE_WITH_CONCERNS and flag (the spec asked to bundle min-interval with this work; an existing test breaking means the spec needs to be revisited).

- [ ] **Step 5: Commit**

```bash
git add src/audio/OnsetDetector.cpp tests/unit/audio/test_onset_detector.cpp
git commit -m "fix(audio): raise OnsetDetector default min-interval 40 -> 80 ms"
```

---

## Task 2: `WordSyncMode` enum + helpers

**Files:**
- Create: `src/audio/WordSyncMode.h`
- Modify: `src/CMakeLists.txt` (header-only — but the file needs to be discoverable; just adding it to disk is enough if the existing include path covers `src/audio/`. Verify and skip CMake edit if so.)

- [ ] **Step 1: Write the header**

`src/audio/WordSyncMode.h`:

```cpp
#pragma once

#include <string>

namespace guitar_dsp::audio {

// Defines how NoteSteppedTTSPlayer treats incoming onsets while a
// segment is already playing.
enum class WordSyncMode {
    Latch,       // ignore onsets while a segment is playing (recommended)
    Advance,     // every onset advances + restarts the next segment
    Syllable,    // step through TTSClip::syllables instead of words
};

inline const char* toString(WordSyncMode m) noexcept {
    switch (m) {
        case WordSyncMode::Latch:    return "latch";
        case WordSyncMode::Advance:  return "advance";
        case WordSyncMode::Syllable: return "syllable";
    }
    return "latch";
}

inline WordSyncMode wordSyncModeFromString(const std::string& s) noexcept {
    if (s == "advance")  return WordSyncMode::Advance;
    if (s == "syllable") return WordSyncMode::Syllable;
    return WordSyncMode::Latch;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 2: Verify it compiles by including it in the existing test target**

Append a tiny smoke check to `tests/unit/audio/test_note_stepped_player.cpp` (don't worry about the existing tests — this confirms the header is reachable):

```cpp
#include "audio/WordSyncMode.h"

TEST_CASE("WordSyncMode: string round-trip", "[audio][word_sync_mode]") {
    using guitar_dsp::audio::WordSyncMode;
    using guitar_dsp::audio::toString;
    using guitar_dsp::audio::wordSyncModeFromString;
    REQUIRE(wordSyncModeFromString("latch")    == WordSyncMode::Latch);
    REQUIRE(wordSyncModeFromString("advance")  == WordSyncMode::Advance);
    REQUIRE(wordSyncModeFromString("syllable") == WordSyncMode::Syllable);
    REQUIRE(wordSyncModeFromString("nonsense") == WordSyncMode::Latch);
    REQUIRE(std::string(toString(WordSyncMode::Syllable)) == "syllable");
}
```

- [ ] **Step 3: Build + run**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "WordSyncMode"
```

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add src/audio/WordSyncMode.h tests/unit/audio/test_note_stepped_player.cpp
git commit -m "feat(audio): add WordSyncMode enum + string helpers"
```

---

## Task 3: `NoteSteppedTTSPlayer` mode storage + Latch behavior

Add the atomic-int mode state to `NoteSteppedTTSPlayer` and implement Latch.

**Files:**
- Modify: `src/audio/NoteSteppedTTSPlayer.h`
- Modify: `src/audio/NoteSteppedTTSPlayer.cpp`
- Modify: `tests/unit/audio/test_note_stepped_player.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/audio/test_note_stepped_player.cpp`:

```cpp
namespace {

// Manually build a 3-word TTSClip: each word is 200 ms of a different tone
// followed by 100 ms of silence between words. Sample rate 48 k.
guitar_dsp::audio::TTSClipPtr makeThreeWordClip() {
    using guitar_dsp::audio::TTSClip;
    using guitar_dsp::audio::WordSegment;
    auto clip = std::make_shared<TTSClip>();
    clip->sampleRate = 48000.0;
    constexpr int wordSamples = 48000 / 5;     // 200 ms
    constexpr int gapSamples  = 48000 / 10;    // 100 ms
    clip->samples.resize(3 * (wordSamples + gapSamples), 0.0f);
    const float freqs[3] = { 220.0f, 330.0f, 440.0f };
    for (int w = 0; w < 3; ++w) {
        const int base = w * (wordSamples + gapSamples);
        for (int i = 0; i < wordSamples; ++i)
            clip->samples[base + i] = 0.5f * std::sin(
                2.0 * 3.14159265 * freqs[w] * i / 48000.0);
        clip->words.push_back(WordSegment{
            "word" + std::to_string(w),
            static_cast<std::size_t>(base),
            static_cast<std::size_t>(base + wordSamples)});
    }
    return clip;
}

// Generate a short pluck-like transient at position `at` in `buf`.
void plantOnset(std::vector<float>& buf, std::size_t at, float amp = 0.6f) {
    for (std::size_t i = 0; i < 64 && at + i < buf.size(); ++i)
        buf[at + i] = amp * std::exp(-static_cast<float>(i) * 0.05f);
}

} // namespace

TEST_CASE("NoteSteppedTTSPlayer Latch: onset during playback is ignored",
          "[audio][note_stepped][latch]") {
    using namespace guitar_dsp::audio;
    NoteSteppedTTSPlayer player;
    player.prepare(48000.0, 512);
    player.setMode(WordSyncMode::Latch);
    player.setClip(makeThreeWordClip());

    // Two onsets 100 ms apart. With Latch, only the first should advance the
    // word index (the first word is 200 ms long, so the second onset arrives
    // mid-word and should be ignored).
    std::vector<float> onsets(48000, 0.0f);
    plantOnset(onsets,  100);     // t ≈ 2 ms
    plantOnset(onsets, 4900);     // t ≈ 102 ms — should be ignored

    std::vector<float> out(48000);
    // Drive in 512-sample blocks.
    for (std::size_t i = 0; i < onsets.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, onsets.size() - i);
        player.process(onsets.data() + i, out.data() + i, n);
    }
    // Word index should have advanced exactly once (-1 -> 0).
    REQUIRE(player.currentWordIndex() == 0);
}

TEST_CASE("NoteSteppedTTSPlayer Latch: onset after current word advances",
          "[audio][note_stepped][latch]") {
    using namespace guitar_dsp::audio;
    NoteSteppedTTSPlayer player;
    player.prepare(48000.0, 512);
    player.setMode(WordSyncMode::Latch);
    player.setClip(makeThreeWordClip());

    // First onset at t=2 ms; second onset at t=350 ms (well past 200 ms word).
    std::vector<float> onsets(48000, 0.0f);
    plantOnset(onsets,    100);
    plantOnset(onsets,  16800);  // t ≈ 350 ms

    std::vector<float> out(48000);
    for (std::size_t i = 0; i < onsets.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, onsets.size() - i);
        player.process(onsets.data() + i, out.data() + i, n);
    }
    REQUIRE(player.currentWordIndex() == 1);
}

TEST_CASE("NoteSteppedTTSPlayer Advance: onset during playback advances and restarts",
          "[audio][note_stepped][advance]") {
    using namespace guitar_dsp::audio;
    NoteSteppedTTSPlayer player;
    player.prepare(48000.0, 512);
    player.setMode(WordSyncMode::Advance);
    player.setClip(makeThreeWordClip());

    std::vector<float> onsets(48000, 0.0f);
    plantOnset(onsets,  100);
    plantOnset(onsets, 4900);

    std::vector<float> out(48000);
    for (std::size_t i = 0; i < onsets.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, onsets.size() - i);
        player.process(onsets.data() + i, out.data() + i, n);
    }
    // Both onsets fire (Advance behavior) — index goes 0 then 1.
    // (The OnsetDetector inside the player uses its own debounce; with the
    // 100 ms spacing here we're well past the 80 ms default, so both fire.)
    REQUIRE(player.currentWordIndex() == 1);
}
```

- [ ] **Step 2: Run to confirm compile error**

```bash
cmake --build build --target guitar_dsp_tests -j 8
```

Expected: error — `setMode` doesn't exist on `NoteSteppedTTSPlayer`.

- [ ] **Step 3: Add mode storage + setter to the header**

In `src/audio/NoteSteppedTTSPlayer.h`:

Add `#include "WordSyncMode.h"` near the top.

In the public section after `setClip`:

```cpp
// Message thread. Default is WordSyncMode::Latch.
void setMode(WordSyncMode m) noexcept;
WordSyncMode mode() const noexcept;
```

In the private members, near the other atomics:

```cpp
std::atomic<int> mode_ {static_cast<int>(WordSyncMode::Latch)};
```

- [ ] **Step 4: Implement Latch + Advance branch in `process`**

In `src/audio/NoteSteppedTTSPlayer.cpp`:

After the existing `setClip` definition, add:

```cpp
void NoteSteppedTTSPlayer::setMode(WordSyncMode m) noexcept {
    mode_.store(static_cast<int>(m), std::memory_order_relaxed);
}
WordSyncMode NoteSteppedTTSPlayer::mode() const noexcept {
    return static_cast<WordSyncMode>(mode_.load(std::memory_order_relaxed));
}
```

In `process()`, replace the existing onset-handling block (currently always-advance) with mode-aware logic. Find:

```cpp
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
```

Replace with:

```cpp
if (onset_.processSample(onsetSrc[i]) && haveClip) {
    const auto m = static_cast<WordSyncMode>(mode_.load(std::memory_order_relaxed));
    const bool latchHolds = (m == WordSyncMode::Latch) && playing_;
    if (!latchHolds) {
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
}
```

Syllable mode swap is Task 5; for now Latch and Advance share the `haveWords` path.

- [ ] **Step 5: Re-run tests**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "NoteSteppedTTSPlayer"
```

Expected: 3 new tests pass + all pre-existing pass.

- [ ] **Step 6: Commit**

```bash
git add src/audio/NoteSteppedTTSPlayer.h src/audio/NoteSteppedTTSPlayer.cpp \
        tests/unit/audio/test_note_stepped_player.cpp
git commit -m "feat(audio): WordSyncMode storage + Latch/Advance behavior on NoteSteppedTTSPlayer"
```

---

## Task 4: `TTSClip::syllables` + `WordAligner::alignSyllables`

Add the optional segmentation list and the aligner that populates it from a hyphenated text input.

**Files:**
- Modify: `src/audio/TTSClip.h`
- Modify: `src/audio/WordAligner.h`
- Modify: `src/audio/WordAligner.cpp`
- Create: `tests/unit/audio/test_word_aligner.cpp` (if missing — check first)
- Modify: `tests/CMakeLists.txt` (only if creating the test file)

- [ ] **Step 1: Add `syllables` to `TTSClip`**

In `src/audio/TTSClip.h`, inside `struct TTSClip`, after `std::vector<WordSegment> words;`:

```cpp
std::vector<WordSegment> syllables;  // optional; empty = no syllable map
```

- [ ] **Step 2: Add `alignSyllables` declaration**

In `src/audio/WordAligner.h`, in the `class WordAligner` body:

```cpp
// Build per-syllable segments by:
//   (1) running align() to get per-word boundaries from the unhyphenated
//       words list, and
//   (2) within each word's [startSample, endSample) range, splitting into
//       N equal-duration sub-segments where N is the count of hyphen-bounded
//       fragments in the corresponding hyphenated text token.
// `hyphenatedWords` is the per-word hyphenated forms; size must match
// `words`. If a word has no hyphen, it contributes one syllable segment
// equal to the whole word.
static std::vector<WordSegment> alignSyllables(
    const std::vector<float>& samples,
    const std::vector<std::string>& words,
    const std::vector<std::string>& hyphenatedWords,
    double sampleRate);
```

- [ ] **Step 3: Write the failing tests**

Check whether `tests/unit/audio/test_word_aligner.cpp` already exists. If not, create it:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/WordAligner.h"
#include "audio/TTSClip.h"

#include <cmath>
#include <vector>
#include <string>

using guitar_dsp::audio::WordAligner;
using guitar_dsp::audio::WordSegment;

namespace {

// Helper: build samples with two "words" 200 ms tone + 100 ms silence between.
std::vector<float> makeTwoWordSamples() {
    constexpr int wordSamples = 48000 / 5;     // 200 ms
    constexpr int gapSamples  = 48000 / 10;    // 100 ms
    std::vector<float> samples(2 * wordSamples + gapSamples, 0.0f);
    for (int i = 0; i < wordSamples; ++i)
        samples[i] = 0.5f * std::sin(2.0 * 3.14159265 * 220.0 * i / 48000.0);
    const int word2Start = wordSamples + gapSamples;
    for (int i = 0; i < wordSamples; ++i)
        samples[word2Start + i] = 0.5f * std::sin(2.0 * 3.14159265 * 330.0 * i / 48000.0);
    return samples;
}

} // namespace

TEST_CASE("WordAligner::alignSyllables: each word's hyphen-count splits its time range",
          "[audio][word_aligner][syllables]") {
    const auto samples = makeTwoWordSamples();
    const std::vector<std::string> words           {"guitar",   "gently"};
    const std::vector<std::string> hyphenatedWords {"gui-tar",  "gent-ly"};
    auto syllables = WordAligner::alignSyllables(samples, words, hyphenatedWords, 48000.0);

    REQUIRE(syllables.size() == 4);
    REQUIRE(syllables[0].word == "gui");
    REQUIRE(syllables[1].word == "tar");
    REQUIRE(syllables[2].word == "gent");
    REQUIRE(syllables[3].word == "ly");

    // Each syllable's range must fit within its parent word's range.
    const auto wordSegs = WordAligner::align(samples, words, 48000.0);
    REQUIRE(syllables[0].startSample == wordSegs[0].startSample);
    REQUIRE(syllables[1].endSample   == wordSegs[0].endSample);
    REQUIRE(syllables[2].startSample == wordSegs[1].startSample);
    REQUIRE(syllables[3].endSample   == wordSegs[1].endSample);
    // Within word 1, syllables are equal halves (within +/- 1 sample).
    const std::size_t midWord0 = (wordSegs[0].startSample + wordSegs[0].endSample) / 2;
    REQUIRE(static_cast<long>(syllables[0].endSample) - static_cast<long>(midWord0) <= 1);
    REQUIRE(static_cast<long>(syllables[0].endSample) - static_cast<long>(midWord0) >= -1);
}

TEST_CASE("WordAligner::alignSyllables: word without hyphen contributes one syllable",
          "[audio][word_aligner][syllables]") {
    const auto samples = makeTwoWordSamples();
    const std::vector<std::string> words           {"guitar", "speaks"};
    const std::vector<std::string> hyphenatedWords {"gui-tar", "speaks"};
    auto syllables = WordAligner::alignSyllables(samples, words, hyphenatedWords, 48000.0);
    REQUIRE(syllables.size() == 3);
    REQUIRE(syllables[2].word == "speaks");
}
```

If the file is new, add it to `tests/CMakeLists.txt` (near `unit/audio/test_word_segment.cpp`):

```cmake
unit/audio/test_word_aligner.cpp
```

- [ ] **Step 4: Implement `alignSyllables`**

In `src/audio/WordAligner.cpp`, append:

```cpp
namespace {

std::vector<std::string> splitOnHyphen(const std::string& s) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : s) {
        if (c == '-') {
            if (!cur.empty()) { parts.push_back(cur); cur.clear(); }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    if (parts.empty()) parts.push_back(s);
    return parts;
}

} // namespace

std::vector<WordSegment> WordAligner::alignSyllables(
        const std::vector<float>& samples,
        const std::vector<std::string>& words,
        const std::vector<std::string>& hyphenatedWords,
        double sampleRate) {
    const auto wordSegs = align(samples, words, sampleRate);
    std::vector<WordSegment> result;
    if (wordSegs.size() != words.size()
            || hyphenatedWords.size() != words.size()) {
        return result;  // input shape mismatch -> empty
    }
    for (std::size_t w = 0; w < wordSegs.size(); ++w) {
        const auto& seg = wordSegs[w];
        const auto syllables = splitOnHyphen(hyphenatedWords[w]);
        const std::size_t n = syllables.size();
        const std::size_t total = seg.endSample - seg.startSample;
        for (std::size_t s = 0; s < n; ++s) {
            const std::size_t start = seg.startSample + (total * s)     / n;
            const std::size_t end   = seg.startSample + (total * (s + 1)) / n;
            result.push_back(WordSegment{syllables[s], start, end});
        }
    }
    return result;
}
```

- [ ] **Step 5: Re-run tests**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "WordAligner|word_aligner"
```

Expected: the 2 new tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/audio/TTSClip.h src/audio/WordAligner.h src/audio/WordAligner.cpp \
        tests/unit/audio/test_word_aligner.cpp tests/CMakeLists.txt
git commit -m "feat(audio): TTSClip::syllables + WordAligner::alignSyllables"
```

---

## Task 5: Syllable mode behavior in `NoteSteppedTTSPlayer`

When mode is Syllable AND `activeClip_->syllables` is non-empty, step through `syllables` instead of `words`. If syllables are missing, fall back to Latch behavior on words.

**Files:**
- Modify: `src/audio/NoteSteppedTTSPlayer.cpp`
- Modify: `tests/unit/audio/test_note_stepped_player.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/audio/test_note_stepped_player.cpp`:

```cpp
namespace {

// 3-syllable clip: built like makeThreeWordClip but with `syllables`
// populated and `words` left empty so the test isolates syllable behavior.
guitar_dsp::audio::TTSClipPtr makeThreeSyllableClip() {
    using guitar_dsp::audio::TTSClip;
    using guitar_dsp::audio::WordSegment;
    auto clip = std::make_shared<TTSClip>();
    clip->sampleRate = 48000.0;
    constexpr int sylSamples = 48000 / 5;       // 200 ms
    constexpr int gap         = 48000 / 20;     //  50 ms
    clip->samples.resize(3 * (sylSamples + gap), 0.0f);
    const float freqs[3] = { 220.0f, 247.0f, 277.0f };
    for (int s = 0; s < 3; ++s) {
        const int base = s * (sylSamples + gap);
        for (int i = 0; i < sylSamples; ++i)
            clip->samples[base + i] = 0.5f * std::sin(
                2.0 * 3.14159265 * freqs[s] * i / 48000.0);
        clip->syllables.push_back(WordSegment{
            "syl" + std::to_string(s),
            static_cast<std::size_t>(base),
            static_cast<std::size_t>(base + sylSamples)});
    }
    return clip;
}

} // namespace

TEST_CASE("NoteSteppedTTSPlayer Syllable: advances through syllables when populated",
          "[audio][note_stepped][syllable]") {
    using namespace guitar_dsp::audio;
    NoteSteppedTTSPlayer player;
    player.prepare(48000.0, 512);
    player.setMode(WordSyncMode::Syllable);
    player.setClip(makeThreeSyllableClip());

    // Three onsets 300 ms apart (well past 80 ms debounce AND past 200 ms
    // syllable length, so Latch-style guarding inside Syllable mode lets
    // each one advance).
    std::vector<float> onsets(48000, 0.0f);
    plantOnset(onsets,     100);
    plantOnset(onsets,  14400);   // t ≈ 300 ms
    plantOnset(onsets,  28800);   // t ≈ 600 ms

    std::vector<float> out(48000);
    for (std::size_t i = 0; i < onsets.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, onsets.size() - i);
        player.process(onsets.data() + i, out.data() + i, n);
    }
    REQUIRE(player.currentWordIndex() == 2);  // index across the 3 syllables
}

TEST_CASE("NoteSteppedTTSPlayer Syllable: falls back to Latch on words when no syllables",
          "[audio][note_stepped][syllable]") {
    using namespace guitar_dsp::audio;
    NoteSteppedTTSPlayer player;
    player.prepare(48000.0, 512);
    player.setMode(WordSyncMode::Syllable);
    // makeThreeWordClip has WORDS populated, no syllables. With syllable mode
    // requested but no syllables, behavior should match Latch on words.
    player.setClip(makeThreeWordClip());

    std::vector<float> onsets(48000, 0.0f);
    plantOnset(onsets,  100);
    plantOnset(onsets, 4900);   // 100 ms — inside first word, should be ignored (Latch fallback)

    std::vector<float> out(48000);
    for (std::size_t i = 0; i < onsets.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, onsets.size() - i);
        player.process(onsets.data() + i, out.data() + i, n);
    }
    REQUIRE(player.currentWordIndex() == 0);
}
```

- [ ] **Step 2: Run to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "NoteSteppedTTSPlayer.*[Ss]yllable"
```

Expected: FAIL — Syllable mode currently behaves the same as Latch (sees the `words` list, never reads `syllables`).

- [ ] **Step 3: Implement syllable-list selection in `process`**

In `src/audio/NoteSteppedTTSPlayer.cpp::process()`, find the existing block:

```cpp
const bool haveClip  = activeClip_ && !activeClip_->samples.empty();
const bool haveWords = haveClip && !activeClip_->words.empty();
```

Replace with:

```cpp
const bool haveClip      = activeClip_ && !activeClip_->samples.empty();
const auto modeNow       = static_cast<WordSyncMode>(mode_.load(std::memory_order_relaxed));
const bool wantSyllables = (modeNow == WordSyncMode::Syllable)
                            && haveClip
                            && !activeClip_->syllables.empty();
const std::vector<WordSegment>* segments = nullptr;
if (haveClip)
    segments = wantSyllables ? &activeClip_->syllables : &activeClip_->words;
const bool haveSegments  = (segments != nullptr) && !segments->empty();
```

Then, in the onset-handling block from Task 3, replace `haveWords` with `haveSegments` and replace `activeClip_->words` reads with `(*segments)` reads:

```cpp
if (onset_.processSample(onsetSrc[i]) && haveClip) {
    const bool latchHolds = (modeNow == WordSyncMode::Latch
                              || modeNow == WordSyncMode::Syllable)
                            && playing_;
    if (!latchHolds) {
        if (haveSegments) {
            const int n = static_cast<int>(segments->size());
            wordIndex_ = (wordIndex_ + 1) % n;
            playPos_ = (*segments)[static_cast<std::size_t>(wordIndex_)].startSample;
            segEnd_  = (*segments)[static_cast<std::size_t>(wordIndex_)].endSample;
        } else {
            wordIndex_ = 0;
            playPos_ = 0;
            segEnd_ = activeClip_->samples.size();
        }
        playing_ = true;
        currentWordIndex_.store(wordIndex_, std::memory_order_relaxed);
    }
}
```

Latch and Syllable both share the latch-during-playback guarantee (Syllable mode behaves like "Latch on syllables"). Advance keeps cutting (intentional).

NOTE: the `segments` pointer points into `activeClip_`'s storage. `activeClip_` is only replaced at block boundaries (top of `process` via the `newClipFlag_` swap), so the pointer is stable for the duration of the sample loop.

- [ ] **Step 4: Re-run tests**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "NoteSteppedTTSPlayer"
```

Expected: all 5 NoteSteppedTTSPlayer mode tests pass + any pre-existing pass.

- [ ] **Step 5: Commit**

```bash
git add src/audio/NoteSteppedTTSPlayer.cpp tests/unit/audio/test_note_stepped_player.cpp
git commit -m "feat(audio): Syllable mode steps through TTSClip::syllables"
```

---

## Task 6: Wire WordSyncMode through `AudioGraph` + `PluginProcessor`

**Files:**
- Modify: `src/audio/AudioGraph.h`
- Modify: `src/app/PluginProcessor.h`

- [ ] **Step 1: Add forwarders to `AudioGraph.h`**

In `src/audio/AudioGraph.h`, in the public section near the existing pitch-singing methods, add:

```cpp
// Word-sync mode for note-triggered TTS scenes.
void setWordSyncMode(WordSyncMode m) noexcept {
    noteSteppedPlayer_.setMode(m);
}
WordSyncMode wordSyncMode() const noexcept {
    return noteSteppedPlayer_.mode();
}
```

(`WordSyncMode` is reachable via the existing `NoteSteppedTTSPlayer.h` include.)

- [ ] **Step 2: Add forwarders to `PluginProcessor.h`**

In `src/app/PluginProcessor.h`, in the public section after the existing pitch-singing forwarders:

```cpp
// Word-sync mode (Latch / Advance / Syllable) for note-triggered TTS.
void setWordSyncMode(audio::WordSyncMode m) noexcept { graph_.setWordSyncMode(m); }
audio::WordSyncMode wordSyncMode() const noexcept    { return graph_.wordSyncMode(); }
```

- [ ] **Step 3: Build to confirm**

```bash
cmake --build build -j 8
ctest --test-dir build --output-on-failure 2>&1 | tail -5
```

Expected: clean build; all tests still pass.

- [ ] **Step 4: Commit**

```bash
git add src/audio/AudioGraph.h src/app/PluginProcessor.h
git commit -m "feat(app): wire WordSyncMode through AudioGraph + PluginProcessor"
```

---

## Task 7: Parse `wordSync` JSON field in `SceneLibrary`

**Files:**
- Modify: `src/scenes/Scene.h`
- Modify: `src/scenes/SceneLibrary.cpp`
- Modify: `tests/unit/scenes/test_scene_library_tts.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/unit/scenes/test_scene_library_tts.cpp`:

```cpp
TEST_CASE("SceneLibrary: parses wordSync = syllable in TTS config",
          "[scenes][library][word_sync]") {
    using namespace guitar_dsp::scenes;
    const auto json = R"({
      "id": 99,
      "name": "test",
      "mixer": { "masterGainDb": 0.0, "dryWet": 1.0 },
      "tts": {
        "source": "prebaked",
        "clip": "test_clip",
        "text": "hello",
        "trigger": "note",
        "wordSync": "syllable"
      }
    })";
    const auto sceneOpt = SceneLibrary::parseSceneJson(json);
    REQUIRE(sceneOpt.has_value());
    REQUIRE(sceneOpt->tts.wordSync == "syllable");
}

TEST_CASE("SceneLibrary: missing wordSync defaults to \"global\"",
          "[scenes][library][word_sync]") {
    using namespace guitar_dsp::scenes;
    const auto json = R"({
      "id": 99, "name": "test",
      "mixer": { "masterGainDb": 0.0, "dryWet": 1.0 },
      "tts": { "source": "prebaked", "clip": "x", "text": "hi", "trigger": "note" }
    })";
    const auto sceneOpt = SceneLibrary::parseSceneJson(json);
    REQUIRE(sceneOpt.has_value());
    REQUIRE(sceneOpt->tts.wordSync == "global");
}
```

(If `SceneLibrary::parseSceneJson` isn't a public entry point, use whichever one is — e.g. `loadFromJsonString`. Check the existing test file for the actual call pattern, but the test SHAPE is what you need.)

- [ ] **Step 2: Run to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests -j 8
```

Expected: compile error — `wordSync` not a member of `TtsConfig`.

- [ ] **Step 3: Add `wordSync` to `TtsConfig`**

In `src/scenes/Scene.h`, inside `struct TtsConfig`, after the existing `trigger` field:

```cpp
std::string wordSync = "global";  // "global"|"latch"|"advance"|"syllable"
```

- [ ] **Step 4: Parse it in `SceneLibrary.cpp`**

In `src/scenes/SceneLibrary.cpp`, find the existing TTS-parse block (it handles `source`, `clip`, `text`, `voice`, `fallback`, `trigger`, `clarity`). After the `trigger` parse:

```cpp
if (t->hasProperty("wordSync"))
    s.tts.wordSync = t->getProperty("wordSync").toString().toStdString();
```

- [ ] **Step 5: Re-run tests**

```bash
cmake --build build --target guitar_dsp_tests -j 8
ctest --test-dir build --output-on-failure -R "wordSync|word_sync"
```

Expected: pass.

- [ ] **Step 6: Commit**

```bash
git add src/scenes/Scene.h src/scenes/SceneLibrary.cpp \
        tests/unit/scenes/test_scene_library_tts.cpp
git commit -m "feat(scenes): parse wordSync field in TTS config"
```

---

## Task 8: Apply per-scene `wordSync` in `SceneEngine`

On scene activation, look up the scene's `wordSync` field; if it's `"global"`, leave the player's mode alone; otherwise push the parsed value to the player.

**Files:**
- Modify: `src/scenes/SceneEngine.h`
- Modify: `src/scenes/SceneEngine.cpp`

- [ ] **Step 1: Read the existing SceneEngine surface**

```bash
grep -n "activateScene\|setMode\|noteSteppedPlayer\|audioGraph" src/scenes/SceneEngine.h src/scenes/SceneEngine.cpp | head -30
```

Note how `SceneEngine` is wired into the audio graph (it likely doesn't hold a direct pointer — instead, the message-thread caller pushes changes after `activateScene` returns).

**Implementation strategy:** rather than coupling `SceneEngine` to `AudioGraph`, expose the active scene's `wordSync` string via the existing `activeTtsConfig()` accessor (the field is already there from Task 7). The actual write to the player happens in `PluginProcessor`'s scene-change handler (which already runs on the message thread).

- [ ] **Step 2: Wire the scene-change handler**

In `src/app/PluginProcessor.cpp`, find the `juce::MessageManager::callAsync` block triggered by scene change (search for `activeSceneId` near the top of the file). Inside that block, after the existing TTS-config pull (which uses `sceneEngine_.activeTtsConfig()`), add:

```cpp
const auto& tcfg = sceneEngine_.activeTtsConfig();
if (tcfg.wordSync == "latch")
    graph_.setWordSyncMode(audio::WordSyncMode::Latch);
else if (tcfg.wordSync == "advance")
    graph_.setWordSyncMode(audio::WordSyncMode::Advance);
else if (tcfg.wordSync == "syllable")
    graph_.setWordSyncMode(audio::WordSyncMode::Syllable);
// "global" (or unrecognized): leave whatever the UI selected in place.
```

The `audio::WordSyncMode` and the `setWordSyncMode` forwarder were already added in Task 6.

- [ ] **Step 3: Build + run**

```bash
cmake --build build -j 8
ctest --test-dir build --output-on-failure 2>&1 | tail -5
```

Expected: clean build, all tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/app/PluginProcessor.cpp
git commit -m "feat(app): apply per-scene wordSync override on scene activation"
```

---

## Task 9: Persist `wordSyncMode` in `PluginState`

**Files:**
- Modify: `src/app/PluginState.h`
- Modify: `src/app/PluginState.cpp`
- Modify: `src/app/PluginProcessor.cpp`
- Modify: `tests/unit/app/test_plugin_state.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/app/test_plugin_state.cpp`:

```cpp
TEST_CASE("PluginState: wordSyncMode round-trips through JSON",
          "[app][state][word_sync]") {
    guitar_dsp::app::PluginStateData d;
    d.wordSyncMode = 2;  // Syllable
    const auto json = guitar_dsp::app::PluginState::toJson(d);
    const auto out  = guitar_dsp::app::PluginState::fromJson(json);
    REQUIRE(out.wordSyncMode == 2);
}

TEST_CASE("PluginState: wordSyncMode defaults to 0 (Latch) when absent",
          "[app][state][word_sync]") {
    const juce::String json = R"({ "sceneId": 0 })";
    const auto out = guitar_dsp::app::PluginState::fromJson(json);
    REQUIRE(out.wordSyncMode == 0);
}
```

- [ ] **Step 2: Add the field**

In `src/app/PluginState.h`, inside `PluginStateData`, after the existing pitch-singing-related fields, add:

```cpp
int wordSyncMode = 0;  // 0=Latch, 1=Advance, 2=Syllable
```

- [ ] **Step 3: Update JSON serialization**

In `src/app/PluginState.cpp::toJson()`, after the existing serialized fields:

```cpp
o->setProperty("wordSyncMode", d.wordSyncMode);
```

In `fromJson()`, after the matching block:

```cpp
if (o->hasProperty("wordSyncMode"))
    d.wordSyncMode = (int) o->getProperty("wordSyncMode");
```

- [ ] **Step 4: Wire into PluginProcessor save/restore**

In `src/app/PluginProcessor.cpp::getStateInformation`:

```cpp
d.wordSyncMode = static_cast<int>(graph_.wordSyncMode());
```

In `setStateInformation`:

```cpp
graph_.setWordSyncMode(static_cast<audio::WordSyncMode>(d.wordSyncMode));
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
git commit -m "feat(app): persist wordSyncMode in PluginState"
```

---

## Task 10: `WordSyncSelector` UI component

A small 3-pill selector hosted inside `VocoderPanel`. Shows the active mode highlighted; click to switch.

**Files:**
- Create: `src/app/WordSyncSelector.h`
- Create: `src/app/WordSyncSelector.cpp`
- Modify: `src/app/CMakeLists.txt`
- Modify: `src/app/VocoderPanel.h`
- Modify: `src/app/VocoderPanel.cpp`

- [ ] **Step 1: Write the header**

`src/app/WordSyncSelector.h`:

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class PluginProcessor;

// Three pills in a horizontal row: [ Latch ] [ Advance ] [ Syllable ].
// Active mode is highlighted; clicking switches modes. Polls the
// processor at 4 Hz so external changes (per-scene override) reflect.
class WordSyncSelector : public juce::Component, private juce::Timer {
public:
    explicit WordSyncSelector(PluginProcessor& p);
    ~WordSyncSelector() override;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<int> pillBounds(int index) const;  // 0..2

    PluginProcessor& processor_;
    int lastActiveIndex_ = -1;
};

} // namespace guitar_dsp
```

- [ ] **Step 2: Write the implementation**

`src/app/WordSyncSelector.cpp`:

```cpp
#include "WordSyncSelector.h"

#include "PluginProcessor.h"
#include "audio/WordSyncMode.h"

namespace guitar_dsp {

namespace {
constexpr const char* kLabels[3] = { "Latch", "Advance", "Syllable" };
}

WordSyncSelector::WordSyncSelector(PluginProcessor& p) : processor_(p) {
    setOpaque(true);
    startTimerHz(4);
}

WordSyncSelector::~WordSyncSelector() { stopTimer(); }

void WordSyncSelector::timerCallback() {
    const int now = static_cast<int>(processor_.wordSyncMode());
    if (now != lastActiveIndex_) {
        lastActiveIndex_ = now;
        repaint();
    }
}

juce::Rectangle<int> WordSyncSelector::pillBounds(int index) const {
    auto area = getLocalBounds().reduced(6, 4);
    constexpr int gap = 6;
    const int w = (area.getWidth() - 2 * gap) / 3;
    return juce::Rectangle<int>(area.getX() + index * (w + gap),
                                area.getY(), w, area.getHeight());
}

void WordSyncSelector::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(16, 18, 24));
    const int active = static_cast<int>(processor_.wordSyncMode());
    for (int i = 0; i < 3; ++i) {
        const auto b = pillBounds(i);
        const bool on = (i == active);
        g.setColour(on ? juce::Colour::fromRGB(180, 140, 230)
                       : juce::Colour::fromRGB(34, 38, 46));
        g.fillRoundedRectangle(b.toFloat(), 4.0f);
        g.setColour(on ? juce::Colour::fromRGB(18, 20, 26)
                       : juce::Colour::fromRGB(150, 160, 175));
        g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)
                                 .withStyle(on ? "Bold" : "Regular")});
        g.drawText(kLabels[i], b, juce::Justification::centred);
    }
}

void WordSyncSelector::mouseDown(const juce::MouseEvent& e) {
    for (int i = 0; i < 3; ++i) {
        if (!pillBounds(i).contains(e.getPosition())) continue;
        processor_.setWordSyncMode(static_cast<audio::WordSyncMode>(i));
        repaint();
        return;
    }
}

} // namespace guitar_dsp
```

- [ ] **Step 3: Register in CMake**

In `src/app/CMakeLists.txt`, add `WordSyncSelector.cpp` alphabetically near `VocoderPanel.cpp`.

- [ ] **Step 4: Host inside `VocoderPanel`**

In `src/app/VocoderPanel.h`:

Add `#include "WordSyncSelector.h"` near the other includes.

In the private members:

```cpp
WordSyncSelector wordSyncSelector_;
```

In `src/app/VocoderPanel.cpp` constructor initializer list, append:

```cpp
, wordSyncSelector_(p)
```

In the constructor body, after the other `addAndMakeVisible(...)` calls:

```cpp
addAndMakeVisible(wordSyncSelector_);
```

In `resized()`, carve a strip BELOW the existing NoteReadout for the selector (~22 px tall):

```cpp
constexpr int selectorH = 22;
wordSyncSelector_.setBounds(area.removeFromBottom(selectorH));
```

(That `removeFromBottom` runs BEFORE the existing `area.removeFromBottom(readoutH);` that carves the NoteReadout strip. Order it so: selector first (bottom-most), then NoteReadout, then the 5 slider rows.)

- [ ] **Step 5: Build + visual smoke**

```bash
cmake --build build -j 8
```

Expected: clean build. Visual check in Task 12 manual smoke.

- [ ] **Step 6: Commit**

```bash
git add src/app/WordSyncSelector.h src/app/WordSyncSelector.cpp src/app/CMakeLists.txt \
        src/app/VocoderPanel.h src/app/VocoderPanel.cpp
git commit -m "feat(ui): WordSyncSelector hosted in VocoderPanel"
```

---

## Task 11: Author hyphens in scene 8 + populate `syllables`

The aligner runs over the active clip; we need the prebaked TTS pipeline to call `alignSyllables` when syllable mode is requested. For the prebaked source the cleanest hook is at clip-load time inside `PrebakedTTSSource`.

**Files:**
- Modify: `assets/scenes/08_speaking_finale.json`
- Modify: `src/audio/PrebakedTTSSource.cpp` (or whichever class loads the prebaked clip — confirm by reading)
- Possibly: `src/scenes/SceneEngine.cpp` (if alignment is triggered downstream rather than on load)

**Read first to choose the right insertion point:**

```bash
grep -n "alignSyllables\|WordAligner\|words = \|->words" src/audio/PrebakedTTSSource.cpp src/audio/AppleTTSSource.mm src/audio/PiperTTSSource.cpp 2>/dev/null | head
```

You'll find an existing call to `WordAligner::align(...)` that populates `clip->words`. Right after that call, if the clip's source text contains any `-`, ALSO call `alignSyllables` with the hyphenated word list and populate `clip->syllables`.

- [ ] **Step 1: Update scene 8**

Edit `assets/scenes/08_speaking_finale.json` to:

```json
{
  "id": 8,
  "name": "Speaking — Prebaked (finale)",
  "color": "#a64dff",
  "mixer": { "masterGainDb": -2.0, "dryWet": 0.9, "transitionMs": 30 },
  "tts": {
    "source": "prebaked",
    "clip": "08_gently_weeps",
    "text": "And now my gui-tar gent-ly speaks for me.",
    "trigger": "note",
    "wordSync": "syllable",
    "clarity": 0.5
  }
}
```

- [ ] **Step 2: Populate `syllables` in the prebaked source**

Open the source (likely `src/audio/PrebakedTTSSource.cpp` — verify with the grep above). Find where it constructs a `TTSClip`, calls `WordAligner::align`, and assigns `clip->words`. The text it aligns is the source `text` field with whitespace tokenization.

Add a sibling step: if the text contains `-`, build the hyphenated-words list and call `alignSyllables`:

```cpp
// At the same scope as the existing WordAligner::align call.
const bool anyHyphen = sourceText.find('-') != std::string::npos;
if (anyHyphen) {
    // Build per-word hyphenated list. words = whitespace-split with hyphens
    // stripped; hyphenatedWords = whitespace-split keeping hyphens.
    std::vector<std::string> plainWords;
    std::vector<std::string> hyphenatedWords;
    {
        std::istringstream iss(sourceText);
        std::string token;
        while (iss >> token) {
            hyphenatedWords.push_back(token);
            std::string plain;
            for (char c : token) if (c != '-') plain += c;
            plainWords.push_back(plain);
        }
    }
    clip->syllables = WordAligner::alignSyllables(
        clip->samples, plainWords, hyphenatedWords, clip->sampleRate);
}
```

(Place after the existing `clip->words = WordAligner::align(...)` call. Reuse whatever variable name the existing code uses for the input text.)

The existing `clip->words` should be built from the PLAIN-words list (without hyphens). If the current code already aligns on the raw text (hyphens included), the word boundaries will look wrong — replace its align call to use `plainWords` too.

- [ ] **Step 3: Build + run tests + manual smoke**

```bash
cmake --build build -j 8
ctest --test-dir build --output-on-failure 2>&1 | tail -10
```

Expected: all tests pass.

- [ ] **Step 4: Commit**

```bash
git add assets/scenes/08_speaking_finale.json src/audio/PrebakedTTSSource.cpp
git commit -m "feat(scenes): scene 8 syllable-mode with hyphenated text + clip syllables"
```

---

## Task 12: README + final gates

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Document word-sync modes**

Open `README.md`. Find the "Pitch-tracked singing carrier (toggle)" subsection. Add a new sibling subsection:

```markdown
### Word-sync modes for note-triggered speech

When a TTS scene uses `"trigger": "note"`, you can choose how guitar
onsets drive the speech. The selector below the vocoder sliders has
three options:

- **Latch (default)** — one word per onset, hard latch. The current
  word plays to completion before the next onset advances. Most
  reliable 1:1 mapping.
- **Advance** — every onset advances and restarts the next word.
  Responsive but can cut multi-syllable words.
- **Syllable** — requires the scene's `text` to include hyphens
  (e.g. `"gui-tar gent-ly"`). Each hyphen-bounded fragment becomes
  one syllable segment; onsets step through syllables.

A scene can override the global UI selection via its TTS config:

`"wordSync": "latch" | "advance" | "syllable" | "global"` (default
`"global"`).

If a scene requests Syllable mode but its text has no hyphens, it
falls back to Latch on words.

**When the conversational AI flow lands (future spec):** when Syllable
mode is active for the response output, the LLM's system prompt must
include guidance to hyphenate multi-syllable words. Without that, the
AI response will use the Latch fallback and lose the syllable-level
sync.
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

Expected: previous baseline + ~10 new tests pass; 3 pre-existing AI HTTP-mock failures unchanged.

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

In Logic on scene 8:
1. Latch mode: 8 slow plucks → exactly 8 advances through the words, no cuts.
2. Advance mode: same plucking → words get cut (regression check that the old behavior is still available).
3. Syllable mode (scene 8 default now): play 11 notes ("And", "now", "my", "gui", "tar", "gent", "ly", "speaks", "for", "me", "."?) — each syllable advances on its own pluck.

- [ ] **Step 7: Commit**

```bash
git add README.md
git commit -m "docs(readme): word-sync modes (Latch / Advance / Syllable)"
```

---

## Spec → plan coverage map

| Spec section | Covered by |
|---|---|
| §1 Bug summary | Tasks 1, 3, 5 |
| §2.1 Latch | Task 3 |
| §2.2 Advance | Task 3 |
| §2.3 Syllable | Tasks 4, 5 |
| §3 UI selector | Task 10 |
| §4 Per-scene override | Tasks 7, 8 |
| §5 Onset min-interval | Task 1 |
| §6 Hyphenation sources | §6.3 LLM note → documented in README §12.1; SayPanel UX hint → out-of-scope cleanup follow-up (see §10 of spec) |
| §7 Architecture (WordSyncMode enum, segmentation, scene config, persistence) | Tasks 2, 3, 4, 5, 6, 7, 8, 9 |
| §8 Testing | Tasks 1, 3, 4, 5, 7, 9, 12 |
| §9 File-touch summary | All tasks |
| §10 Out of scope | Not built — by definition |
| §11 Verification | Task 12 |

**One spec item NOT directly covered by a task: the SayPanel "Add hyphens for syllable steps" status hint (§6.2 of spec).** Reason: the SayPanel exists but the status hint is purely cosmetic UX polish and adds plumbing through a UI surface that's not on the main demo path. Flag as a polish follow-up after this plan ships; the core Syllable mode behavior works without it.
