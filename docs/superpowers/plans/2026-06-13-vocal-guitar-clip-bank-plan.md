# Vocal Guitar — Clip Bank (Scene 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Spec:** [docs/superpowers/specs/2026-06-13-vocal-guitar-clip-bank-design.md](../specs/2026-06-13-vocal-guitar-clip-bank-design.md)

**Goal:** Add a Scene 2 carousel patch where each pick attack triggers a different short vocal sample (10-clip bank, sequentially cycled) as the vocoder modulator, producing a cartoonish "Jack Black mouth-guitar" effect.

**Architecture:** New `ClipBankPlayer` class in `src/audio/` plays a fixed bank of short `TTSClip`s, advancing one clip per detected guitar onset via an internal `OnsetDetector`. Wired into `AudioGraph` as a third `ModulatorSource::ClipBank`, populated at scene activation by `PluginProcessor` reading a new `TtsConfig.bank` vector of clip keys via a second `PrebakedTTSSource` rooted at `assets/clips/vocal-guitar/`. The existing `WordReadout` shows the cursor; the existing Rewind pill resets it.

**Tech Stack:** C++20, JUCE (juce_audio_formats, juce_dsp, juce_core), Catch2 for tests, CMake + Ninja, Python 3 for the placeholder-clip generator.

**Build/test commands used throughout:**
```bash
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "<tag>" -s   # focused run
ctest --test-dir build --output-on-failure  # full suite
```

---

## Task 1: Add `bank` field to `TtsConfig` schema

The scene JSON's TTS block must carry an ordered list of clip keys for the bank. Smallest possible schema delta.

**Files:**
- Modify: `src/scenes/Scene.h`

- [ ] **Step 1: Add `bank` field to `TtsConfig`**

Edit `src/scenes/Scene.h` to add a `std::vector<std::string> bank` field to `TtsConfig`. After the existing `std::string fallback;` line and before `std::string trigger;`, add:

```cpp
    std::vector<std::string> bank; // ordered clip keys when source == "clipBank"; empty otherwise
```

Also add `#include <vector>` near the top of the file if not already present.

- [ ] **Step 2: Build to confirm the schema change compiles**

Run: `cmake --build build --target guitar_dsp_tests`
Expected: clean build (no new code paths consume `bank` yet, so it's just a default-initialized field).

- [ ] **Step 3: Commit**

```bash
git add src/scenes/Scene.h
git commit -m "feat(scenes): add bank field to TtsConfig for clip-bank source"
```

---

## Task 2: SceneLibrary parses `source: "clipBank"` + `bank: [...]`

Test-drive parsing of the new JSON shape, then implement.

**Files:**
- Create: `tests/fixtures/scenes/with_tts_clip_bank.json`
- Create: `tests/unit/scenes/test_scene_library_tts_bank.cpp`
- Modify: `src/scenes/SceneLibrary.cpp:63-83` (the TTS-block parser)
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add the fixture JSON**

Create `tests/fixtures/scenes/with_tts_clip_bank.json` with:

```json
{
  "id": 99,
  "name": "Test — clip bank",
  "color": "#ff0000",
  "mixer": { "masterGainDb": -6.0, "dryWet": 0.9, "transitionMs": 20 },
  "tts": {
    "source": "clipBank",
    "bank": ["a", "b", "c"],
    "trigger": "note",
    "clarity": 0.1
  }
}
```

- [ ] **Step 2: Write the failing test**

Create `tests/unit/scenes/test_scene_library_tts_bank.cpp`:

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

TEST_CASE("SceneLibrary: parses tts source clipBank and bank array",
          "[scenes][library][tts][bank]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_tts_clip_bank.json"));
    REQUIRE(s.has_value());
    REQUIRE(s->tts.source == "clipBank");
    REQUIRE(s->tts.bank.size() == 3);
    REQUIRE(s->tts.bank[0] == "a");
    REQUIRE(s->tts.bank[1] == "b");
    REQUIRE(s->tts.bank[2] == "c");
    REQUIRE(s->tts.trigger == "note");
}

TEST_CASE("SceneLibrary: tts without bank leaves bank empty",
          "[scenes][library][tts][bank]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_tts.json"));
    REQUIRE(s.has_value());
    REQUIRE(s->tts.bank.empty());
}
```

- [ ] **Step 3: Register the test in CMake**

Edit `tests/CMakeLists.txt` and add `unit/scenes/test_scene_library_tts_bank.cpp` to the `guitar_dsp_tests` source list, alongside the other `unit/scenes/test_scene_library_tts*.cpp` entries.

- [ ] **Step 4: Build and run the test — expect FAIL**

Run: `cmake --build build --target guitar_dsp_tests && ./build/tests/guitar_dsp_tests "[bank]" -s`
Expected: FAIL — the parser doesn't pick up the `bank` field yet, so `s->tts.bank.size() == 3` fails.

- [ ] **Step 5: Implement the parse**

In `src/scenes/SceneLibrary.cpp`, inside the `if (obj->hasProperty("tts"))` block (around line 63), after the existing `wordSync` and `clarity` parses, add:

```cpp
            if (t->hasProperty("bank")) {
                if (auto* arr = t->getProperty("bank").getArray()) {
                    s.tts.bank.clear();
                    s.tts.bank.reserve(static_cast<std::size_t>(arr->size()));
                    for (int i = 0; i < arr->size(); ++i)
                        s.tts.bank.push_back(
                            (*arr)[i].toString().toStdString());
                }
            }
```

- [ ] **Step 6: Build and run — expect PASS**

Run: `cmake --build build --target guitar_dsp_tests && ./build/tests/guitar_dsp_tests "[bank]" -s`
Expected: both bank tests PASS.

- [ ] **Step 7: Commit**

```bash
git add tests/fixtures/scenes/with_tts_clip_bank.json \
        tests/unit/scenes/test_scene_library_tts_bank.cpp \
        tests/CMakeLists.txt \
        src/scenes/SceneLibrary.cpp
git commit -m "feat(scenes): SceneLibrary parses clipBank source + bank array"
```

---

## Task 3: `ClipBankPlayer` skeleton (header + empty impl that compiles)

Start with the smallest compilable surface. No behavior yet; this just lets later tests be written against a real class.

**Files:**
- Create: `src/audio/ClipBankPlayer.h`
- Create: `src/audio/ClipBankPlayer.cpp`
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Write the header**

Create `src/audio/ClipBankPlayer.h`:

```cpp
#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

#include "OnsetDetector.h"
#include "TTSClip.h"

namespace guitar_dsp::audio {

// Plays a bank of short audio clips, advancing one clip per detected guitar
// onset. Each clip is an atomic unit — no internal segmentation. After the
// active clip finishes, output is silence until the next onset.
//
// RT-safe in process(); allocates only in setBank() (message thread).
class ClipBankPlayer {
public:
    ClipBankPlayer();

    void prepare(double sampleRate, int blockSize);
    void reset();

    // Message thread. Swap the active bank atomically. Pass an empty vector
    // to clear. Bank order is the playback order.
    void setBank(std::vector<TTSClipPtr> clips);

    // Message thread. Reset cursor to "before the first clip"; next onset
    // plays clip 0. RT-safe via pending flag.
    void rewind() noexcept;

    // Audio thread.
    //   onsetSrc = clean guitar (drives OnsetDetector)
    //   modOut   = vocoder modulator output for this block
    void process(const float* onsetSrc, float* modOut, std::size_t numSamples) noexcept;

    // UI: current bank cursor (clip index), or -1 if idle / no clip yet played.
    int currentClipIndex() const noexcept {
        return currentClipIndex_.load(std::memory_order_relaxed);
    }
    int bankSize() const noexcept {
        return bankSize_.load(std::memory_order_relaxed);
    }

private:
    OnsetDetector onset_;

    std::atomic<bool> newBankFlag_ {false};
    std::vector<TTSClipPtr> pendingBank_;
    std::vector<TTSClipPtr> activeBank_;

    int         cursor_     = -1;   // last-triggered clip index
    std::size_t playPos_    = 0;    // sample offset within active clip
    bool        playing_    = false;

    std::atomic<int>  currentClipIndex_ {-1};
    std::atomic<int>  bankSize_         {0};
    std::atomic<bool> pendingRewind_    {false};
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 2: Write a stub implementation**

Create `src/audio/ClipBankPlayer.cpp`:

```cpp
#include "ClipBankPlayer.h"

#include <algorithm>

namespace guitar_dsp::audio {

ClipBankPlayer::ClipBankPlayer() = default;

void ClipBankPlayer::prepare(double sampleRate, int /*blockSize*/) {
    onset_.prepare(sampleRate);
    reset();
}

void ClipBankPlayer::reset() {
    onset_.reset();
    cursor_  = -1;
    playPos_ = 0;
    playing_ = false;
    currentClipIndex_.store(-1, std::memory_order_relaxed);
}

void ClipBankPlayer::setBank(std::vector<TTSClipPtr> clips) {
    pendingBank_ = std::move(clips);
    bankSize_.store(static_cast<int>(pendingBank_.size()),
                    std::memory_order_relaxed);
    newBankFlag_.store(true, std::memory_order_release);
}

void ClipBankPlayer::rewind() noexcept {
    pendingRewind_.store(true, std::memory_order_release);
}

void ClipBankPlayer::process(const float* /*onsetSrc*/, float* modOut,
                             std::size_t numSamples) noexcept {
    std::fill(modOut, modOut + numSamples, 0.0f);
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 3: Add to the audio library CMake target**

Edit `src/CMakeLists.txt` and add `audio/ClipBankPlayer.cpp` to the `guitar_dsp_audio` library's source list (alphabetically near the other ClipPlayer entries; insert after `audio/CarouselMod.cpp` to group with the other Carousel/Clip files).

- [ ] **Step 4: Build to confirm**

Run: `cmake --build build --target guitar_dsp_tests`
Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add src/audio/ClipBankPlayer.h src/audio/ClipBankPlayer.cpp src/CMakeLists.txt
git commit -m "feat(audio): ClipBankPlayer skeleton (silent stub)"
```

---

## Task 4: ClipBankPlayer — advance-on-onset + wrap (TDD)

The core behavior. Drive onsets in, observe cursor advancing.

**Files:**
- Create: `tests/unit/audio/test_clip_bank_player.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/audio/ClipBankPlayer.cpp`

- [ ] **Step 1: Write the test file with the first two tests**

Create `tests/unit/audio/test_clip_bank_player.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/ClipBankPlayer.h"
#include "audio/TTSClip.h"
#include "harness/RealtimeSentinel.h"

#include <cmath>
#include <memory>
#include <vector>

using guitar_dsp::audio::ClipBankPlayer;
using guitar_dsp::audio::TTSClip;
using guitar_dsp::audio::TTSClipPtr;
using guitar_dsp::tests::RealtimeSentinel;

namespace {

// One short clip filled with a single constant value, so the test can
// assert which clip's samples the player emitted.
TTSClipPtr makeClip(float value, std::size_t numSamples) {
    auto c = std::make_shared<TTSClip>();
    c->sampleRate = 48000.0;
    c->samples.assign(numSamples, value);
    return c;
}

// Generate a short pluck-like transient at position `at` in `buf`.
void plantOnset(std::vector<float>& buf, std::size_t at, float amp = 0.9f) {
    for (std::size_t i = 0; i < 64 && at + i < buf.size(); ++i)
        buf[at + i] = amp * std::exp(-static_cast<float>(i) * 0.05f);
}

} // namespace

TEST_CASE("ClipBankPlayer: first onset advances cursor from -1 to 0",
          "[audio][clip_bank]") {
    ClipBankPlayer p;
    p.prepare(48000.0, 512);
    p.setBank({ makeClip(0.1f, 2000), makeClip(0.2f, 2000) });

    REQUIRE(p.currentClipIndex() == -1);

    std::vector<float> onset(2000, 0.0f);
    plantOnset(onset, 100);
    std::vector<float> mod(2000, 0.0f);
    p.process(onset.data(), mod.data(), mod.size());

    REQUIRE(p.currentClipIndex() == 0);
}

TEST_CASE("ClipBankPlayer: subsequent onsets advance through bank then wrap",
          "[audio][clip_bank]") {
    ClipBankPlayer p;
    p.prepare(48000.0, 512);
    // 3-clip bank, each clip 800 ms so onsets land after the prior clip ends.
    constexpr std::size_t clipSamples = (48000 * 8) / 10;
    p.setBank({ makeClip(0.1f, clipSamples),
                makeClip(0.2f, clipSamples),
                makeClip(0.3f, clipSamples) });

    // Four onsets spaced 1100 ms apart (past each clip's 800 ms duration).
    constexpr std::size_t N = 48000 * 5;  // 5 seconds
    std::vector<float> onset(N, 0.0f);
    plantOnset(onset, 100);
    plantOnset(onset,  52800);   // ~1100 ms
    plantOnset(onset, 105600);   // ~2200 ms
    plantOnset(onset, 158400);   // ~3300 ms — should wrap to clip 0

    std::vector<float> mod(N, 0.0f);
    for (std::size_t i = 0; i < N; i += 512) {
        const std::size_t n = std::min<std::size_t>(512, N - i);
        p.process(onset.data() + i, mod.data() + i, n);
    }

    REQUIRE(p.currentClipIndex() == 0);  // wrapped
}
```

- [ ] **Step 2: Register the test in CMake**

Edit `tests/CMakeLists.txt` and add `unit/audio/test_clip_bank_player.cpp` to the `guitar_dsp_tests` source list (place near `unit/audio/test_note_stepped_player.cpp`).

- [ ] **Step 3: Build and run — expect FAIL**

Run: `cmake --build build --target guitar_dsp_tests && ./build/tests/guitar_dsp_tests "[clip_bank]" -s`
Expected: FAIL (`currentClipIndex()` returns -1; the stub does nothing).

- [ ] **Step 4: Implement advance-on-onset + clip playback in process()**

Replace the body of `ClipBankPlayer::process` in `src/audio/ClipBankPlayer.cpp` with:

```cpp
void ClipBankPlayer::process(const float* onsetSrc, float* modOut,
                             std::size_t numSamples) noexcept {
    // Drain pending bank swap (audio-thread-safe pointer move).
    if (newBankFlag_.exchange(false, std::memory_order_acquire)) {
        activeBank_ = std::move(pendingBank_);
        cursor_  = -1;
        playPos_ = 0;
        playing_ = false;
        onset_.reset();
        currentClipIndex_.store(-1, std::memory_order_relaxed);
    }

    // Drain pending rewind.
    if (pendingRewind_.exchange(false, std::memory_order_acquire)) {
        cursor_  = -1;
        playPos_ = 0;
        playing_ = false;
        onset_.reset();
        currentClipIndex_.store(-1, std::memory_order_relaxed);
    }

    const bool haveBank = !activeBank_.empty();

    for (std::size_t i = 0; i < numSamples; ++i) {
        if (haveBank && onset_.processSample(onsetSrc[i])) {
            // Each onset advances the cursor AND restarts playback from
            // the new clip's sample 0 — no latch, even if the prior clip
            // is still playing. This is the punchy "every-pick-different"
            // behavior the spec calls for.
            const int n = static_cast<int>(activeBank_.size());
            cursor_  = (cursor_ + 1) % n;
            playPos_ = 0;
            playing_ = true;
            currentClipIndex_.store(cursor_, std::memory_order_relaxed);
        }

        float s = 0.0f;
        if (playing_) {
            const auto& clip = activeBank_[static_cast<std::size_t>(cursor_)];
            if (clip && playPos_ < clip->samples.size()) {
                s = clip->samples[playPos_++];
            } else {
                playing_ = false;
            }
        }
        modOut[i] = s;
    }
}
```

- [ ] **Step 5: Build and run — expect PASS**

Run: `cmake --build build --target guitar_dsp_tests && ./build/tests/guitar_dsp_tests "[clip_bank]" -s`
Expected: both tests PASS.

- [ ] **Step 6: Commit**

```bash
git add tests/unit/audio/test_clip_bank_player.cpp \
        tests/CMakeLists.txt \
        src/audio/ClipBankPlayer.cpp
git commit -m "feat(audio): ClipBankPlayer advances cursor on onset, wraps after last"
```

---

## Task 5: ClipBankPlayer — emits the active clip's samples + silence after clip ends

Two more behaviors from the spec testing checklist.

**Files:**
- Modify: `tests/unit/audio/test_clip_bank_player.cpp`

- [ ] **Step 1: Add the "emits samples" + "silence after end" tests**

Append to `tests/unit/audio/test_clip_bank_player.cpp`:

```cpp
TEST_CASE("ClipBankPlayer: emits active clip's samples",
          "[audio][clip_bank]") {
    ClipBankPlayer p;
    p.prepare(48000.0, 512);
    p.setBank({ makeClip(0.10f, 200), makeClip(0.25f, 200) });

    std::vector<float> onset(400, 0.0f);
    plantOnset(onset, 50);
    std::vector<float> mod(400, 0.0f);
    p.process(onset.data(), mod.data(), mod.size());

    // After the onset settles and the clip plays, mod should contain the
    // first clip's value (0.10) at some sample inside the played region.
    bool sawClipValue = false;
    for (float v : mod)
        if (std::fabs(v - 0.10f) < 1e-5f) { sawClipValue = true; break; }
    REQUIRE(sawClipValue);
}

TEST_CASE("ClipBankPlayer: outputs zero after clip ends (no next onset)",
          "[audio][clip_bank]") {
    ClipBankPlayer p;
    p.prepare(48000.0, 512);
    // Short clip (300 samples). Process 600 samples; samples 300+ must be 0
    // (after the onset triggers and the clip exhausts).
    p.setBank({ makeClip(0.5f, 300) });

    std::vector<float> onset(600, 0.0f);
    plantOnset(onset, 0);
    std::vector<float> mod(600, 0.0f);
    p.process(onset.data(), mod.data(), mod.size());

    // The tail half of the buffer should be silent.
    for (std::size_t i = 400; i < 600; ++i)
        REQUIRE(mod[i] == 0.0f);
}
```

- [ ] **Step 2: Build and run — expect PASS**

Run: `cmake --build build --target guitar_dsp_tests && ./build/tests/guitar_dsp_tests "[clip_bank]" -s`
Expected: all four `[clip_bank]` tests PASS (no impl change needed — the Task 4 implementation already does this).

- [ ] **Step 3: Commit**

```bash
git add tests/unit/audio/test_clip_bank_player.cpp
git commit -m "test(audio): ClipBankPlayer emits samples + silences after clip end"
```

---

## Task 6: ClipBankPlayer — empty-bank silence, rewind, RT allocation-free

Round out the spec's testing checklist.

**Files:**
- Modify: `tests/unit/audio/test_clip_bank_player.cpp`

- [ ] **Step 1: Add the three remaining tests**

Append to `tests/unit/audio/test_clip_bank_player.cpp`:

```cpp
TEST_CASE("ClipBankPlayer: empty bank outputs silence and cursor stays -1",
          "[audio][clip_bank]") {
    ClipBankPlayer p;
    p.prepare(48000.0, 512);
    p.setBank({});  // empty

    std::vector<float> onset(500, 0.0f);
    plantOnset(onset, 50);   // would otherwise advance
    std::vector<float> mod(500, 1.0f);  // sentinel; must be zeroed by process
    p.process(onset.data(), mod.data(), mod.size());

    for (float v : mod) REQUIRE(v == 0.0f);
    REQUIRE(p.currentClipIndex() == -1);
}

TEST_CASE("ClipBankPlayer: rewind() resets cursor; next onset plays clip 0",
          "[audio][clip_bank]") {
    ClipBankPlayer p;
    p.prepare(48000.0, 512);
    constexpr std::size_t clipSamples = (48000 * 8) / 10;
    p.setBank({ makeClip(0.1f, clipSamples),
                makeClip(0.2f, clipSamples),
                makeClip(0.3f, clipSamples) });

    // Two onsets — cursor to 1.
    constexpr std::size_t N = 48000 * 3;
    std::vector<float> onset(N, 0.0f);
    plantOnset(onset, 100);
    plantOnset(onset, 52800);
    std::vector<float> mod(N, 0.0f);
    for (std::size_t i = 0; i < N; i += 512) {
        const std::size_t n = std::min<std::size_t>(512, N - i);
        p.process(onset.data() + i, mod.data() + i, n);
    }
    REQUIRE(p.currentClipIndex() == 1);

    // Rewind, then drain one block so the pending flag lands.
    p.rewind();
    std::vector<float> blank(512, 0.0f), bout(512, 0.0f);
    p.process(blank.data(), bout.data(), 512);
    REQUIRE(p.currentClipIndex() == -1);

    // A subsequent fresh onset advances to clip 0 (not 2).
    std::vector<float> trailing(N, 0.0f);
    plantOnset(trailing, 1000);
    for (std::size_t i = 0; i < N; i += 512) {
        const std::size_t n = std::min<std::size_t>(512, N - i);
        p.process(trailing.data() + i, bout.data(), n);
        if (i > 2000) break;
    }
    REQUIRE(p.currentClipIndex() == 0);
}

TEST_CASE("ClipBankPlayer: process is allocation-free",
          "[audio][clip_bank][rt]") {
    ClipBankPlayer p;
    p.prepare(48000.0, 512);
    p.setBank({ makeClip(0.1f, 4800), makeClip(0.2f, 4800) });

    // Drain the pending bank flag with one non-RT block.
    std::vector<float> onset(512, 0.0f), mod(512, 0.0f);
    p.process(onset.data(), mod.data(), 512);

    // Now lock allocation and process 50 blocks.
    for (int i = 0; i < 512; ++i)
        onset[static_cast<std::size_t>(i)] =
            0.5f * std::sin(2.0f * 3.14159265f * 110.0f * i / 48000.0f);

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int blk = 0; blk < 50; ++blk)
        p.process(onset.data(), mod.data(), mod.size());
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
```

- [ ] **Step 2: Build and run — expect PASS**

Run: `cmake --build build --target guitar_dsp_tests && ./build/tests/guitar_dsp_tests "[clip_bank]" -s`
Expected: all seven `[clip_bank]` tests PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/audio/test_clip_bank_player.cpp
git commit -m "test(audio): ClipBankPlayer empty-bank, rewind, RT-allocation-free"
```

---

## Task 7: Wire `ClipBankPlayer` into `AudioGraph` (`ModulatorSource::ClipBank`)

Add the new modulator-source enum value, the player member, the prepare/reset wiring, and the process() branch.

**Files:**
- Modify: `src/audio/AudioGraph.h`
- Modify: `src/audio/AudioGraph.cpp`
- Modify: `tests/unit/audio/test_audio_graph.cpp`

- [ ] **Step 1: Write a failing wiring test**

Append to `tests/unit/audio/test_audio_graph.cpp`:

```cpp
#include "audio/ClipBankPlayer.h"

TEST_CASE("AudioGraph: ClipBank modulator source routes clip bank to vocoder",
          "[audio][graph][clip_bank]") {
    using guitar_dsp::audio::AudioGraph;
    using guitar_dsp::audio::TTSClip;

    AudioGraph g;
    g.prepare(48000.0, 512);

    // Put a single clip in the bank so onsets emit a known value.
    auto clip = std::make_shared<TTSClip>();
    clip->sampleRate = 48000.0;
    clip->samples.assign(2000, 0.4f);
    g.clipBankPlayer().setBank({ clip });
    g.setModulatorSource(AudioGraph::ModulatorSource::ClipBank);
    g.mixer().setDryWet(1.0f);  // route wet so vocoder output reaches the mix

    // Drive a pluck-shaped guitar input — both carrier AND onset for the
    // clip-bank player.
    std::vector<float> in(2000, 0.0f), out(2000, 0.0f);
    for (std::size_t i = 0; i < 64; ++i)
        in[i] = 0.9f * std::exp(-static_cast<float>(i) * 0.05f);

    for (std::size_t i = 0; i < in.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, in.size() - i);
        g.process(in.data() + i, out.data() + i, n);
    }

    // The cursor must have advanced (proving the clip-bank branch was taken).
    REQUIRE(g.clipBankPlayer().currentClipIndex() == 0);
}
```

(`#include <memory>` is already pulled in transitively via the existing AudioGraph test header set; if the build complains, add `#include <memory>` at the top of the test file.)

- [ ] **Step 2: Build — expect compile FAIL**

Run: `cmake --build build --target guitar_dsp_tests`
Expected: FAIL — `g.clipBankPlayer()` doesn't exist; `ModulatorSource::ClipBank` doesn't exist.

- [ ] **Step 3: Extend the AudioGraph header**

Edit `src/audio/AudioGraph.h`:

1. Add `#include "ClipBankPlayer.h"` near the other audio includes.
2. Inside the `enum class ModulatorSource`, add `ClipBank` as a third value:
   ```cpp
   enum class ModulatorSource { Linear, NoteStepped, ClipBank };
   ```
3. Add an accessor near the `noteSteppedPlayer()` accessor:
   ```cpp
   ClipBankPlayer& clipBankPlayer() { return clipBankPlayer_; }
   const ClipBankPlayer& clipBankPlayer() const { return clipBankPlayer_; }
   ```
4. Add a member alongside `noteSteppedPlayer_`:
   ```cpp
   ClipBankPlayer clipBankPlayer_;
   ```

- [ ] **Step 4: Extend `prepare()` and `reset()` in AudioGraph.cpp**

In `src/audio/AudioGraph.cpp::prepare`, after the `noteSteppedPlayer_.prepare(...)` call, add:

```cpp
    clipBankPlayer_.prepare(sampleRate, blockSize);
```

In `AudioGraph::reset()`, after `noteSteppedPlayer_.reset();`, add:

```cpp
    clipBankPlayer_.reset();
```

- [ ] **Step 5: Extend the `process()` modulator branch**

In `src/audio/AudioGraph::process`, find the existing `if (modulatorSource_.load(...) == NoteStepped) { ... } else { ttsClipPlayer_.process(...); }` block (around lines 76-83). Replace with a three-way branch:

```cpp
        const int modSrc = modulatorSource_.load(std::memory_order_relaxed);
        if (modSrc == static_cast<int>(ModulatorSource::NoteStepped)) {
            // Onset source = clean guitar (postInputBuffer_); writes modulator.
            noteSteppedPlayer_.process(postInputBuffer_.data(),
                                       wetBuffer_.data(), numSamples);
        } else if (modSrc == static_cast<int>(ModulatorSource::ClipBank)) {
            // Same shape as NoteStepped — onset source is the clean guitar.
            clipBankPlayer_.process(postInputBuffer_.data(),
                                    wetBuffer_.data(), numSamples);
        } else {
            ttsClipPlayer_.process(wetBuffer_.data(), numSamples);
        }
```

- [ ] **Step 6: Build and run — expect PASS**

Run: `cmake --build build --target guitar_dsp_tests && ./build/tests/guitar_dsp_tests "[graph][clip_bank]" -s`
Expected: PASS.

Also run the full `[graph]` and `[note_stepped]` sets to confirm no regression:
```bash
./build/tests/guitar_dsp_tests "[graph],[note_stepped]" -s
```

- [ ] **Step 7: Commit**

```bash
git add src/audio/AudioGraph.h src/audio/AudioGraph.cpp \
        tests/unit/audio/test_audio_graph.cpp
git commit -m "feat(audio): AudioGraph routes ModulatorSource::ClipBank to vocoder"
```

---

## Task 8: `AssetLocator::vocalGuitarClipsDirectory()`

Resolve `<assetsRoot>/clips/vocal-guitar/` so a second `PrebakedTTSSource` can load from there.

**Files:**
- Modify: `src/app/AssetLocator.h`
- Modify: `src/app/AssetLocator.cpp`

- [ ] **Step 1: Add the declaration**

Edit `src/app/AssetLocator.h`. Inside the `class AssetLocator { public: ... }` section, after `ttsDirectory()`, add:

```cpp
    // Returns <assetsRoot>/clips/vocal-guitar/ — the root for the Phase A
    // "vocal guitar" clip bank (Scene 2). Each subdir is one clip key, with
    // an `audio.wav` inside. No `meta.json` needed.
    static std::string vocalGuitarClipsDirectory();
```

- [ ] **Step 2: Add the implementation**

Edit `src/app/AssetLocator.cpp`. After the existing `ttsDirectory()` definition, add:

```cpp
std::string AssetLocator::vocalGuitarClipsDirectory() {
    const auto root = assetsRoot();
    if (root.empty()) return {};
    return (fs::path(root) / "clips" / "vocal-guitar").string();
}
```

- [ ] **Step 3: Build to confirm**

Run: `cmake --build build --target guitar_dsp_tests`
Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add src/app/AssetLocator.h src/app/AssetLocator.cpp
git commit -m "feat(app): AssetLocator resolves assets/clips/vocal-guitar/"
```

---

## Task 9: Scaffold 10 placeholder clip WAVs + the build script

Create a small Python generator that synthesizes 10 short tonal stabs and writes them as WAVs. The user can replace each with a real recording later. The engine works end-to-end with the placeholders.

**Files:**
- Create: `scripts/build_vocal_guitar_clips.py`
- Create: `assets/clips/vocal-guitar/00_wee/audio.wav` … `09_ner-ner-ner/audio.wav` (generated)

- [ ] **Step 1: Write the generator script**

Create `scripts/build_vocal_guitar_clips.py`:

```python
#!/usr/bin/env python3
"""
Generate 10 placeholder WAVs for the vocal-guitar clip bank (Scene 2).

Each clip is a short tone/burst at a different pitch/duration so the user can
audibly hear the per-pick cycling work end-to-end. Replace these with real
recorded vocal-guitar samples later — same filename/folder layout.
"""
import math
import os
import struct
import wave
from pathlib import Path

CLIPS = [
    # (folder name, duration ms, pitch Hz, shape)
    ("00_wee",          300, 1200, "tone"),
    ("01_doo",          250,  600, "tone"),
    ("02_ner",          200,  400, "noise"),
    ("03_new",          250,  900, "tone"),
    ("04_yeah",         400,  500, "tone"),
    ("05_brrr",         400,  150, "noise"),
    ("06_skronk",       500,  250, "noise"),
    ("07_weeeeee",     1200, 1400, "tone"),
    ("08_ahhhh",       1200,  700, "tone"),
    ("09_ner-ner-ner",  700,  450, "noise"),
]

SR = 48000

def synth(duration_ms, hz, shape):
    n = int(SR * duration_ms / 1000)
    out = []
    for i in range(n):
        # ~10 ms attack/release envelope so there's no click.
        env = min(1.0, i / (SR * 0.01), (n - i) / (SR * 0.01))
        if shape == "tone":
            s = 0.5 * env * math.sin(2 * math.pi * hz * i / SR)
        else:
            # Pitched noise: tone * (1 + 0.4 * white).
            import random
            s = 0.5 * env * math.sin(2 * math.pi * hz * i / SR) * \
                (1.0 + 0.4 * (random.random() - 0.5))
        out.append(int(max(-1.0, min(1.0, s)) * 32767))
    return out

def write_wav(path, samples):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", s) for s in samples))

def main():
    root = Path(__file__).resolve().parent.parent / "assets" / "clips" / "vocal-guitar"
    for name, dur, hz, shape in CLIPS:
        path = root / name / "audio.wav"
        if path.exists():
            continue
        write_wav(path, synth(dur, hz, shape))
        print(f"wrote {path}")

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Make it executable and run it**

Run:
```bash
chmod +x scripts/build_vocal_guitar_clips.py
python3 scripts/build_vocal_guitar_clips.py
ls assets/clips/vocal-guitar
```
Expected: 10 subfolders with `audio.wav` each.

- [ ] **Step 3: Commit**

```bash
git add scripts/build_vocal_guitar_clips.py \
        assets/clips/vocal-guitar/
git commit -m "feat(assets): placeholder vocal-guitar clip bank (10 short tones)"
```

---

## Task 10: PluginProcessor owns a second `PrebakedTTSSource` for the vocal-guitar root

The bank loader needs a separate source so its root is `assets/clips/vocal-guitar/`, not `assets/tts/`.

**Files:**
- Modify: `src/app/PluginProcessor.h`
- Modify: `src/app/PluginProcessor.cpp`

- [ ] **Step 1: Add the member**

Edit `src/app/PluginProcessor.h`. Near the existing `prebakedTtsSource_` member (around line 232), add:

```cpp
    std::unique_ptr<audio::PrebakedTTSSource> vocalGuitarSource_;
```

- [ ] **Step 2: Instantiate it in `prepareToPlay()`**

Edit `src/app/PluginProcessor.cpp`. Just after the existing `prebakedTtsSource_->prepare(sampleRate);` call (around line 137), add:

```cpp
    vocalGuitarSource_ = std::make_unique<audio::PrebakedTTSSource>(
        AssetLocator::vocalGuitarClipsDirectory());
    vocalGuitarSource_->prepare(sampleRate);
```

- [ ] **Step 3: Build to confirm**

Run: `cmake --build build --target guitar_dsp_tests`
Expected: clean build (no new code path uses `vocalGuitarSource_` yet).

- [ ] **Step 4: Commit**

```bash
git add src/app/PluginProcessor.h src/app/PluginProcessor.cpp
git commit -m "feat(app): instantiate second PrebakedTTSSource for vocal-guitar bank"
```

---

## Task 11: PluginProcessor routes `source: clipBank` to `ClipBankPlayer.setBank()`

Add a branch in the scene-activation logic that loads each `cfg.bank[i]` clip via `vocalGuitarSource_` and hands the vector to `ClipBankPlayer`. Sets the modulator source to `ClipBank` and clears the linear / note-stepped players to avoid stale state.

**Files:**
- Modify: `src/app/PluginProcessor.cpp` (around the scene-change callback near line 350)

- [ ] **Step 1: Find the scene-activation TTS branch**

The scene-activation logic lives in `src/app/PluginProcessor.cpp` around line 350, inside the lambda that runs when the scene id changes. It currently dispatches on `cfg.source == "prebaked"` vs. live sources via the `synthesizeWithFallback` chain and then on `cfg.trigger == "note"`.

The clip-bank path is structurally different: it does NOT call `synthesizeWithFallback`, does NOT use word-sync, and does NOT use `currentTtsClipKey_` for caching (the bank is the unit, not a single key). It runs BEFORE the existing source-dispatch and short-circuits.

- [ ] **Step 2: Insert the clip-bank branch**

In `src/app/PluginProcessor.cpp`, locate the line that reads:

```cpp
            const auto cfg = sceneEngine_.activeTtsConfig();
```

(immediately followed by the `setClarity(cfg.clarity)` call and then the per-scene `wordSync` lines).

After the existing `wordSync` block ends (after the `else if (cfg.wordSync == "syllable")` branch and the comment line `// "global" (or unrecognized): leave whatever the UI selected in place.`), and **before** the line:

```cpp
            std::string key;
            if (cfg.source == "prebaked") key = cfg.clip;
```

insert:

```cpp
            // -------------------------------------------------------------
            // Clip-bank source (Phase A — Vocal Guitar, Scene 2)
            // -------------------------------------------------------------
            // Distinct from "prebaked": the unit of playback is the BANK,
            // not a single clip. Each onset advances to the next clip.
            // Skip the synthesizeWithFallback chain and the
            // currentTtsClipKey_ short-circuit entirely.
            if (cfg.source == "clipBank") {
                // Build a stable key from the bank contents so a re-entry
                // to the same scene with an unchanged bank short-circuits.
                std::string bankKey = "clipBank:";
                for (const auto& k : cfg.bank) { bankKey += k; bankKey += '|'; }
                if (bankKey == currentTtsClipKey_) return;
                currentTtsClipKey_ = bankKey;

                std::vector<audio::TTSClipPtr> clips;
                clips.reserve(cfg.bank.size());
                if (vocalGuitarSource_) {
                    for (const auto& key : cfg.bank) {
                        auto clip = vocalGuitarSource_->synthesize(key);
                        if (clip && !clip->samples.empty())
                            clips.push_back(std::move(clip));
                    }
                }

                graph_.clipBankPlayer().setBank(std::move(clips));
                graph_.clipBankPlayer().rewind();  // start at clip 0 on entry
                graph_.setModulatorSource(audio::AudioGraph::ModulatorSource::ClipBank);
                graph_.ttsClipPlayer().setClip(nullptr);
                graph_.noteSteppedPlayer().setClip(nullptr);
                lastResolvedSource_.store(1, std::memory_order_relaxed);  // "prebaked-ish"
                return;
            }
```

- [ ] **Step 3: Build to confirm — no new tests yet**

Run: `cmake --build build --target guitar_dsp_tests`
Expected: clean build.

- [ ] **Step 4: Commit**

```bash
git add src/app/PluginProcessor.cpp
git commit -m "feat(app): scene activation routes source=clipBank to ClipBankPlayer.setBank"
```

---

## Task 12: Replace Scene 2 JSON (archive the old one)

Per the preservation principle in the spec, the old `02_carousel_distortion.json` moves to `assets/scenes/archive/` rather than being deleted. The new Scene 2 JSON takes its place.

**Files:**
- Move: `assets/scenes/02_carousel_distortion.json` → `assets/scenes/archive/02_carousel_distortion.json`
- Create: `assets/scenes/02_vocal_guitar.json`

- [ ] **Step 1: Archive the old scene**

Run:
```bash
mkdir -p assets/scenes/archive
git mv assets/scenes/02_carousel_distortion.json assets/scenes/archive/02_carousel_distortion.json
```

- [ ] **Step 2: Confirm `SceneLibrary` ignores the archive subfolder**

`SceneLibrary::loadDirectory` uses `fs::directory_iterator` (non-recursive). Subfolders are skipped. No code change needed. Sanity-check by reading `src/scenes/SceneLibrary.cpp:188-191` and confirming the iterator type.

- [ ] **Step 3: Write the new Scene 2**

Create `assets/scenes/02_vocal_guitar.json`:

```json
{
  "id": 2,
  "name": "Vocal Guitar — clip bank",
  "color": "#e07b00",
  "mixer": { "masterGainDb": -6.0, "dryWet": 0.9, "transitionMs": 30 },
  "tts": {
    "source": "clipBank",
    "bank": [
      "00_wee", "01_doo", "02_ner", "03_new", "04_yeah",
      "05_brrr", "06_skronk", "07_weeeeee", "08_ahhhh", "09_ner-ner-ner"
    ],
    "trigger": "note",
    "clarity": 0.1
  },
  "carousel": {
    "enabled": true,
    "drive": 8.0,
    "waveshaper": { "type": "tanh", "amount": 1.2 },
    "harmonizer": { "intervals": [7, 12], "detuneCents": [0, 0], "mix": 0.35 },
    "reverb": { "roomSize": 0.25, "wet": 0.12 },
    "outputTrimDb": -3.0
  }
}
```

- [ ] **Step 4: Build and run the full suite — confirm no regression**

Run: `cmake --build build --target guitar_dsp_tests && ctest --test-dir build --output-on-failure`
Expected: all pre-existing tests still PASS. The new scene file is loaded by `SceneLibrary` but no test currently asserts on it.

- [ ] **Step 5: Commit**

```bash
git add assets/scenes/02_vocal_guitar.json \
        assets/scenes/archive/02_carousel_distortion.json
git commit -m "feat(scenes): Scene 2 is Vocal Guitar clip bank; archive old distorted patch"
```

---

## Task 13: WordReadout shows the clip-bank cursor + the Rewind pill resets it

The existing readout polls the active scene and the note-stepped player's `currentWordIndex()`. Extend it to also recognize the clip-bank case, where it displays the cursor as `"clip N / M"` plus the active clip's key, and the Rewind pill calls `ClipBankPlayer::rewind()`.

**Files:**
- Modify: `src/audio/AudioGraph.h` (add a clip-bank-aware rewind forwarder)
- Modify: `src/app/PluginProcessor.h` (expose ClipBankPlayer cursor/key/size to UI)
- Modify: `src/app/PluginProcessor.cpp` (forward the rewind to the right player)
- Modify: `src/app/WordReadout.cpp` (display + rewind dispatch)

- [ ] **Step 1: Add an AudioGraph rewind forwarder**

In `src/audio/AudioGraph.h`, next to `rewindSpoken()`, add:

```cpp
    // Rewind the clip-bank cursor (Scene 2 / Phase A). Independent of the
    // note-stepped rewind. Message thread; RT-safe via the player's pending flag.
    void rewindClipBank() noexcept { clipBankPlayer_.rewind(); }
```

- [ ] **Step 2: Expose the bank state on `PluginProcessor`**

In `src/app/PluginProcessor.h`, near the existing `activeSceneClarity()` accessor (around line 180), add:

```cpp
    // Phase A — Vocal Guitar (Scene 2). True when the active scene uses
    // the clip-bank modulator source.
    bool activeSceneIsClipBank() const {
        return sceneEngine_.activeTtsConfig().source == "clipBank";
    }
    int  clipBankCursor() const { return graph_.clipBankPlayer().currentClipIndex(); }
    int  clipBankSize()   const { return graph_.clipBankPlayer().bankSize(); }
    // Returns the current clip's key (e.g. "03_new") or empty when idle.
    std::string clipBankCurrentKey() const {
        const auto cfg = sceneEngine_.activeTtsConfig();
        const int  idx = clipBankCursor();
        if (idx < 0 || idx >= static_cast<int>(cfg.bank.size())) return {};
        return cfg.bank[static_cast<std::size_t>(idx)];
    }
    // Issued by the Rewind pill on WordReadout. Picks the right player.
    void rewindActive() noexcept {
        if (activeSceneIsClipBank()) graph_.rewindClipBank();
        else                          graph_.rewindSpoken();
    }
```

- [ ] **Step 3: Update WordReadout to dispatch via `rewindActive()`**

In `src/app/WordReadout.cpp`, find the existing `processor_.rewindSpoken();` call (around line 122). Replace it with:

```cpp
        processor_.rewindActive();
```

- [ ] **Step 4: Update WordReadout to display the clip-bank cursor**

In `src/app/WordReadout.cpp`, inside the `paint()` method (or wherever the "centered word" text is rendered — search for the text-drawing logic that consumes the spoken-word index), branch on `processor_.activeSceneIsClipBank()`:

```cpp
    if (processor_.activeSceneIsClipBank()) {
        const int cursor = processor_.clipBankCursor();
        const int total  = processor_.clipBankSize();
        const auto key   = processor_.clipBankCurrentKey();
        juce::String label;
        if (cursor < 0) {
            label = "vocal guitar  •  " + juce::String(total) + " clips";
        } else {
            label = juce::String(key) + "  •  " + juce::String(cursor + 1)
                  + " / " + juce::String(total);
        }
        // Use the same drawing path the existing word readout uses; replace
        // the text it draws with `label`. Keep the existing centerBaseHeight /
        // pip-strip layout — the clip-bank scene reuses the same component
        // bounds so layout doesn't shift on scene change.
        // (Implementation detail: emit a single juce::Graphics::drawText
        //  with `label` at the existing centered position, scaled to fit.)
        return;  // skip the regular per-word render path
    }
```

If the existing render path is structured such that an early-return here would skip something necessary (e.g., the pip strip), move the branch lower and only override the *centered text*. Read `paint()` end-to-end before inserting; the clip-bank case is "one centered line, no pip strip" (no per-word progress to show).

- [ ] **Step 5: Build and confirm**

Run: `cmake --build build --target guitar_dsp_tests`
Expected: clean build. The existing WordReadout unit test (`unit/app/test_word_readout.cpp`) may need a quick read to confirm the layout constants are intact — if anything fails, narrow the branch placement and re-run.

- [ ] **Step 6: Run the existing word-readout test**

Run: `./build/tests/guitar_dsp_tests "[word_readout]" -s`
Expected: PASS — the existing tests use scenes whose source is not `"clipBank"`, so the new branch is skipped.

- [ ] **Step 7: Commit**

```bash
git add src/audio/AudioGraph.h \
        src/app/PluginProcessor.h \
        src/app/PluginProcessor.cpp \
        src/app/WordReadout.cpp
git commit -m "feat(ui): WordReadout shows clip-bank cursor + Rewind resets the bank"
```

---

## Task 14: Integration test — clip-bank scene activates end-to-end

A single integration test that drives `PluginProcessor` through a scene activation to Scene 2 and confirms the cursor advances on a synthetic pluck.

**Files:**
- Create: `tests/integration/test_vocal_guitar_clip_bank_scene.cpp`
- Create: `tests/fixtures/clips/vocal-guitar/00_test/audio.wav` (generated in test)
- Create: `tests/fixtures/clips/vocal-guitar/01_test/audio.wav` (generated in test)
- Create: `tests/fixtures/scenes/with_clip_bank_scene.json`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add the scene fixture**

Create `tests/fixtures/scenes/with_clip_bank_scene.json`:

```json
{
  "id": 50,
  "name": "Test — vocal guitar",
  "color": "#000000",
  "mixer": { "masterGainDb": 0.0, "dryWet": 1.0, "transitionMs": 5 },
  "tts": {
    "source": "clipBank",
    "bank": ["00_test", "01_test"],
    "trigger": "note",
    "clarity": 0.0
  },
  "carousel": { "enabled": false }
}
```

- [ ] **Step 2: Write the integration test**

Create `tests/integration/test_vocal_guitar_clip_bank_scene.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "audio/AudioGraph.h"
#include "audio/PrebakedTTSSource.h"
#include "audio/TTSClip.h"
#include "harness/GoldenFile.h"

#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

using guitar_dsp::audio::AudioGraph;
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

void ensureClipFixture(const std::string& key, float value) {
    const auto path = fixturePath("clips/vocal-guitar/" + key + "/audio.wav");
    if (std::filesystem::exists(path)) return;
    constexpr int N = 4800;  // 100 ms
    std::vector<float> samples(N, value);
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());
    guitar_dsp::tests::GoldenFile::writeMonoWav(path, 48000.0,
                                                 samples.data(), N);
}

} // namespace

TEST_CASE("Clip-bank scene: ClipBankPlayer.setBank wires through to AudioGraph",
          "[integration][clip_bank][scene]") {
    ensureClipFixture("00_test", 0.1f);
    ensureClipFixture("01_test", 0.2f);

    // Load the bank via PrebakedTTSSource — same path PluginProcessor takes.
    PrebakedTTSSource src{ fixturePath("clips/vocal-guitar") };
    src.prepare(48000.0);

    auto c0 = src.synthesize("00_test");
    auto c1 = src.synthesize("01_test");
    REQUIRE(c0);
    REQUIRE(c1);

    AudioGraph g;
    g.prepare(48000.0, 512);
    g.clipBankPlayer().setBank({ c0, c1 });
    g.clipBankPlayer().rewind();
    g.setModulatorSource(AudioGraph::ModulatorSource::ClipBank);
    g.mixer().setDryWet(1.0f);

    // Two pluck-shaped onsets spaced 1100 ms apart.
    constexpr std::size_t N = 48000 * 2;
    std::vector<float> in(N, 0.0f), out(N, 0.0f);
    for (std::size_t i = 0; i < 64; ++i) in[i]       = 0.9f * std::exp(-(int)i * 0.05f);
    for (std::size_t i = 0; i < 64; ++i) in[52800 + i] = 0.9f * std::exp(-(int)i * 0.05f);

    // Drain one block first so the pending setBank flag lands before the
    // RT thread sentinel-y region (cosmetic — no RT sentinel here).
    g.process(in.data(), out.data(), 512);

    for (std::size_t i = 512; i < N; i += 512) {
        const std::size_t n = std::min<std::size_t>(512, N - i);
        g.process(in.data() + i, out.data() + i, n);
    }

    // After two onsets, cursor should be on clip 1.
    REQUIRE(g.clipBankPlayer().currentClipIndex() == 1);
}
```

- [ ] **Step 3: Register the test in CMake**

Edit `tests/CMakeLists.txt` and add `integration/test_vocal_guitar_clip_bank_scene.cpp` to the `guitar_dsp_tests` source list (place near the other `integration/test_note_triggered_speech.cpp` entry).

- [ ] **Step 4: Build and run**

Run: `cmake --build build --target guitar_dsp_tests && ./build/tests/guitar_dsp_tests "[integration][clip_bank]" -s`
Expected: PASS.

- [ ] **Step 5: Run the full suite to confirm no regression**

Run: `ctest --test-dir build --output-on-failure`
Expected: all tests PASS.

- [ ] **Step 6: Commit**

```bash
git add tests/fixtures/scenes/with_clip_bank_scene.json \
        tests/fixtures/clips/vocal-guitar/ \
        tests/integration/test_vocal_guitar_clip_bank_scene.cpp \
        tests/CMakeLists.txt
git commit -m "test(integration): clip-bank scene wires through AudioGraph end-to-end"
```

---

## Task 15: Manual verification of Scene 2 in the standalone app

The unit + integration tests prove the engine works. This task validates the *demo experience*: launching the app, selecting Scene 2, playing the guitar, hearing the cycle.

**Files:** (none — manual)

- [ ] **Step 1: Build the standalone app**

Run: `cmake --build build --target guitar_dsp_app_Standalone`
Expected: a clean build, the `.app` bundle at `build/src/app/guitar_dsp_app_artefacts/Debug/Standalone/Guitar DSP.app` (or `Release/` if you configured Release).

- [ ] **Step 2: Launch + select Scene 2**

Run: `open "build/src/app/guitar_dsp_app_artefacts/Debug/Standalone/Guitar DSP.app"`

In the app, switch to **Scene 2 — "Vocal Guitar — clip bank"**.

Confirm:
- The WordReadout shows `"vocal guitar  •  10 clips"` initially.
- The mic-meter (if present from other scenes) is unaffected — irrelevant here.

- [ ] **Step 3: Play 12 notes — confirm cycling**

Pick a note on the guitar 12 times in a row (any rhythm, ≥ ~250 ms apart so the onset detector doesn't dedupe).

Expected:
- Each pick → a different placeholder tone modulates the guitar.
- WordReadout shows `"00_wee  •  1 / 10"`, then `"01_doo  •  2 / 10"`, …, `"09_ner-ner-ner  •  10 / 10"`, then wraps back to `"00_wee  •  1 / 10"` on pick #11.

If the cycling doesn't happen, double-check that:
- The placeholder WAVs exist under `assets/clips/vocal-guitar/<key>/audio.wav`.
- `AssetLocator::vocalGuitarClipsDirectory()` returns a path that exists from the app's runtime.
- A `vocalGuitarSource_` is logging successful synthesizes (peek at stderr).

- [ ] **Step 4: Test Rewind**

After a few picks (cursor on, say, clip 3), click the Rewind pill on the WordReadout.

Expected:
- WordReadout returns to `"vocal guitar  •  10 clips"`.
- Next pick plays clip 0 (`"00_wee"`).

- [ ] **Step 5: Switch to Scene 1, then back to Scene 2 — confirm reset**

Switch to Scene 1 (Developers!) via the carousel or FCB1010, then back to Scene 2.

Expected:
- On returning to Scene 2, cursor is reset to "before clip 0" (the `clipBankPlayer().rewind()` call in PluginProcessor's scene-activation branch).
- Next pick plays `00_wee`.

- [ ] **Step 6: No commit needed (validation only)**

This task does not produce code changes. If any of the above expectations failed, file the symptom + suspected file with the next task; otherwise this task is "done = observed".

---

## Self-Review

Spec sections vs. tasks:

- "Approach" + `ClipBankPlayer` description → Tasks 3, 4, 5, 6 (player class + behavior tests).
- "New components: `src/audio/ClipBankPlayer.{h,cpp}`" → Task 3.
- "`assets/clips/vocal-guitar/` folder + 10 clips" → Task 9.
- "`src/scenes/Scene.h` — `TtsConfig` extensions (`bank`)" → Task 1.
- "`src/scenes/SceneLibrary.cpp` — parse `bank`" → Task 2.
- "`src/audio/AudioGraph.{h,cpp}` — `ModulatorSource::ClipBank`" → Task 7.
- "`src/scenes/SceneEngine.cpp` (or wherever scene activation lives)" → Tasks 10, 11 (PluginProcessor is where activation lives in this codebase, per the file survey).
- "`src/audio/PrebakedTTSSource` … verify `meta.json` optional" → No change needed; the existing implementation already handles missing `meta.json` (the if-block at the bottom of `synthesize()` is fully optional). Documented inline; no task required.
- "`src/app/WordReadout` (UI)" → Task 13.
- "Scene 2 JSON" → Task 12 (also archives old).
- "Testing: unit tests" → Tasks 4, 5, 6.
- "Testing: manual / demo verification" → Task 15.
- "Risk: clip-recording quality" → Task 9 (placeholder content; recording is a follow-on content task, not a code task).
- "Risk: onset detector sensitivity" → Reuse of `OnsetDetector` is implicit in Task 3's `ClipBankPlayer` design; the tuning is the existing detector's default, addressed in Task 15 if it surfaces.
- "Risk: vocoder makeup gain" → Manual verification in Task 15; no code task because tuning lives on the existing `VocoderPanel` sliders.
- "Replaces (no hard deletes)" → Task 12.

No spec section is unaddressed. No "TBD"/"TODO" placeholders. Type names are consistent: `ClipBankPlayer`, `TTSClipPtr`, `ModulatorSource::ClipBank`, `vocalGuitarSource_`, `clipBankPlayer()` — used identically across tasks.

---

Plan complete and saved to `docs/superpowers/plans/2026-06-13-vocal-guitar-clip-bank-plan.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
