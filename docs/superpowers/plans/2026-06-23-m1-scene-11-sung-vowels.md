# M1 — Scene 11 Sung Vowels + Voice-Pack Dropdown Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship scene 11 — a vocoded "sung vowels" scene driven by curated VocalSet samples, with a 4-voice runtime dropdown — without breaking any existing scene.

**Architecture:** Per-voice `.gspeak` bundles whose manifests carry `anchorPitchHz` + `bankKey` per grain. `ClipBankPlayer` gets a pitch-aware selection mode that filters grains by `bankKey` (cycling per onset) then picks the closest-anchor grain. Scene JSON gains a `voicePacks` array; runtime swaps bundles via the existing `tryAutoLoadGspeak_` path with a 50 ms output fade. Bypass scene moves from slot 11 to slot 13.

**Tech Stack:** C++17, JUCE, Catch2, Python 3 (offline bundle builder), CMake.

**Design spec:** [docs/superpowers/specs/2026-06-23-scene-11-sung-vowels-design.md](../specs/2026-06-23-scene-11-sung-vowels-design.md). Read sections 3, 4, 5, 7, 8 before starting.

## Global Constraints

- **No regression in existing scenes 0..10.** All existing integration and unit tests must keep passing without modification (except where explicitly noted in this plan).
- **Additive-only schema.** `.gspeak` manifest additions are optional fields. v2 manifests still load.
- **Public DSP interfaces frozen.** `VowelGrainLoop`, `PitchTrackedCarrier`, `ChannelVocoder`, `PitchShifter`, `Formant`, `GspeakBundle` keep their existing method signatures. New behavior arrives in new methods or new classes.
- **Test-first.** Every code change is preceded by its failing test.
- **RT-safety.** No allocations, locks, or message-thread calls inside any `process()` method on the audio thread. Use the existing pending-flag pattern (see `ClipBankPlayer::newBankFlag_`).
- **Commit frequently.** One commit per task. Co-author tag per the user's preference is unnecessary; user manages commit messages.
- **Bundle paths are relative to repo root.** `assets/clips/gspeak/scene11_sung_*.gspeak`.
- **VocalSet attribution stays intact.** Don't touch [assets/vocalset/ATTRIBUTION.md](../../../assets/vocalset/ATTRIBUTION.md).

---

## File Map

**Created:**
- `tools/build_sung_vowel_bundle.py` — offline bundle builder.
- `tests/fixtures/sung_vowels_test.gspeak` — synthetic 5-grain bundle for unit tests (built by a test-time Python helper).
- `tests/fixtures/build_sung_vowels_test_bundle.py` — script that produces the above.
- `assets/clips/gspeak/scene11_sung_m1.gspeak` — Male 1 voice (committed binary).
- `assets/clips/gspeak/scene11_sung_m10.gspeak` — Mighty Man.
- `assets/clips/gspeak/scene11_sung_f2.gspeak` — Female 2.
- `assets/clips/gspeak/scene11_sung_f8.gspeak` — Female 8.
- `assets/scenes/11_sung_vowels.json` — new scene definition.
- `assets/scenes/13_bypass.json` — renamed from `11_bypass.json`.
- `src/app/VoicePackPicker.h` and `.cpp` — JUCE ComboBox widget.
- `tests/unit/scenes/test_scene_library_sung_vowels.cpp`
- `tests/unit/scenes/test_scene_resolved_gspeak_path.cpp`
- `tests/integration/test_sung_vowels_scene.cpp`
- `tests/integration/test_voice_pack_swap.cpp`

**Modified:**
- `src/audio/TTSClip.h` — add optional grain-metadata fields.
- `src/audio/GspeakBundle.cpp` — read/write the new optional manifest fields.
- `src/audio/ClipBankPlayer.h` / `.cpp` — add pitch-aware selection mode + per-block detected-pitch atomic.
- `src/scenes/Scene.h` — add `VoicePack` struct, `voicePacks` vector, `defaultVoiceIndex`, `showVoicePackPicker`, `resolvedGspeakPath()` helper.
- `src/scenes/SceneLibrary.cpp` — parse the new fields.
- `src/app/PluginState.h` / `.cpp` — add `activeVoiceIndexByScene` map; round-trip in JSON.
- `src/app/PluginProcessor.h` / `.cpp` — `setActiveVoiceIndex(int)`, 50 ms fade-on-swap, change hardcoded default scene from 11 → 13.
- `src/app/VocoderPanel.h` / `.cpp` — embed `VoicePackPicker`.
- `src/midi/FCB1010Mapping.cpp` — extend `stockDefaults` for PC 11/12/13.
- `tests/CMakeLists.txt` — register new test files + the test-fixture build step.
- `tests/unit/audio/test_clip_bank_player.cpp` — extend with pitch-aware tests.
- `tests/unit/audio/test_gspeak_bundle.cpp` — extend with new-field round-trip + back-compat.
- `tests/unit/scenes/test_scene_library.cpp` — assert bypass at 13.
- `tests/unit/app/test_plugin_state.cpp` — assert `activeVoiceIndex` round-trip.
- `tests/integration/test_scene_switch.cpp` — extend cycle to PC 0..13.
- `tests/integration/test_realtime_safety.cpp` — add scene 11 to the audit.

---

## Task 1: Add grain-metadata fields to TTSClip (no behavior change)

**Files:**
- Modify: `src/audio/TTSClip.h`
- Test: `tests/unit/audio/test_gspeak_bundle.cpp` (smoke check that default-initialised fields exist)

**Interfaces:**
- Consumes: nothing.
- Produces: `TTSClip` now carries `std::string bankKey;`, `float anchorPitchHz = 0.0f;`, `std::string variantTag;` — all default-empty/zero. Downstream tasks read these.

- [ ] **Step 1: Add the failing test**

Open `tests/unit/audio/test_gspeak_bundle.cpp`. At the end of the file, add:

```cpp
TEST_CASE("TTSClip default grain-metadata fields are empty/zero",
          "[tts][clip]") {
    using guitar_dsp::audio::TTSClip;
    TTSClip c;
    REQUIRE(c.bankKey.empty());
    REQUIRE(c.anchorPitchHz == 0.0f);
    REQUIRE(c.variantTag.empty());
}
```

- [ ] **Step 2: Run; expect compile failure**

```bash
cd build-tests && cmake --build . --target guitar_dsp_tests 2>&1 | head -30
```

Expected: compile error on `bankKey` / `anchorPitchHz` / `variantTag`.

- [ ] **Step 3: Add the fields to TTSClip.h**

In `src/audio/TTSClip.h`, inside `struct TTSClip`, before `bool empty()`:

```cpp
    // Optional grain-metadata. Populated when this TTSClip is one grain
    // sliced from a sung-vowel bundle. Empty/zero for all legacy clips.
    std::string bankKey;             // e.g. "sung_ah" — joins to Scene::tts.bank entries.
    float       anchorPitchHz = 0.0f; // 0 = legacy clip (no anchor pitch known).
    std::string variantTag;          // e.g. "straight", "forte" — informational.
```

- [ ] **Step 4: Build + test passes**

```bash
cd build-tests && cmake --build . --target guitar_dsp_tests && ctest -R "TTSClip default" --output-on-failure
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/audio/TTSClip.h tests/unit/audio/test_gspeak_bundle.cpp
git commit -m "feat(tts-clip): add optional bankKey/anchorPitchHz/variantTag grain metadata"
```

---

## Task 2: Extend GspeakBundle to read/write new optional fields

**Files:**
- Modify: `src/audio/GspeakBundle.cpp`
- Test: `tests/unit/audio/test_gspeak_bundle.cpp`

**Interfaces:**
- Consumes: `TTSClip::bankKey`, `anchorPitchHz`, `variantTag` from Task 1.
- Produces: when `clip.bankKey != ""`, the writer adds `bankKey` / `anchorPitchHz` / `variantTag` to each phoneme JSON entry. The reader populates the corresponding fields on the loaded TTSClip *and* on each Phoneme (so per-grain identity survives split). Phonemes also gain an optional `anchorPitchHz` setter via Task 3 — but the schema lands here.

Manifest schema change (additive, per phoneme):

```json
{
  "label": "ah",
  "type": "Vowel",
  "startSample": 0,
  "endSample": 132300,
  "bankKey": "sung_ah",        // NEW — optional
  "anchorPitchHz": 147.0,       // NEW — optional, default 0.0
  "variantTag": "straight"      // NEW — optional
}
```

- [ ] **Step 1: Write a round-trip test for the new fields**

Append to `tests/unit/audio/test_gspeak_bundle.cpp`:

```cpp
TEST_CASE("GspeakBundle round-trips grain metadata fields",
          "[gspeak][bundle][grain-meta]") {
    using guitar_dsp::audio::GspeakBundle;
    using guitar_dsp::audio::TTSClip;
    using guitar_dsp::audio::Phoneme;

    juce::File tmp =
        juce::File::createTempFile(".gspeak_grain_meta_roundtrip.gspeak");

    TTSClip clip;
    clip.sampleRate = 48000.0;
    clip.samples.assign(48000, 0.0f);  // 1 s silence
    // v2 path requires sylsV2 populated; minimum is one syllable.
    guitar_dsp::audio::SyllableSpan syl;
    syl.startSample = 0;
    syl.endSample   = 48000;
    syl.vowelNucleusSample = 24000;
    syl.attackEndSample    = 0;
    syl.codaStartSample    = 48000;
    syl.phonemeIndices = {0};
    clip.sylsV2.push_back(syl);

    Phoneme p;
    p.label       = "ah";
    p.type        = Phoneme::Type::Vowel;
    p.startSample = 0;
    p.endSample   = 48000;
    clip.phonemes.push_back(p);

    clip.bankKey       = "sung_ah";
    clip.anchorPitchHz = 147.5f;
    clip.variantTag    = "straight";

    REQUIRE(GspeakBundle::write(tmp, clip, "test"));

    auto loaded = GspeakBundle::read(tmp, 48000.0);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->clip);
    CHECK(loaded->clip->bankKey       == "sung_ah");
    CHECK(loaded->clip->anchorPitchHz == Approx(147.5f));
    CHECK(loaded->clip->variantTag    == "straight");

    tmp.deleteFile();
}

TEST_CASE("GspeakBundle reads legacy manifest with no grain metadata",
          "[gspeak][bundle][grain-meta][backcompat]") {
    using guitar_dsp::audio::GspeakBundle;

    // Use an existing scene0 bundle as our legacy fixture.
    juce::File legacy(
        juce::File::getCurrentWorkingDirectory()
            .getChildFile("../assets/clips/gspeak/scene0.gspeak"));
    if (! legacy.existsAsFile()) {
        // Some CTest runs use a different cwd; try repo root.
        legacy = juce::File(
            "/Users/user/GIT/guitar-dsp/assets/clips/gspeak/scene0.gspeak");
    }
    REQUIRE(legacy.existsAsFile());

    auto loaded = GspeakBundle::read(legacy, 48000.0);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->clip);
    CHECK(loaded->clip->bankKey.empty());
    CHECK(loaded->clip->anchorPitchHz == 0.0f);
    CHECK(loaded->clip->variantTag.empty());
}
```

- [ ] **Step 2: Build; expect the round-trip test to fail**

```bash
cd build-tests && cmake --build . --target guitar_dsp_tests && \
  ctest -R "grain metadata fields" --output-on-failure
```

Expected: FAIL (round-trip values don't survive the write/read cycle yet).

- [ ] **Step 3: Patch the writer**

In `src/audio/GspeakBundle.cpp`, inside `buildManifest`, after the existing `phonemes` array loop populates the per-phoneme `po`, add (still inside the loop, after the existing four setProperty calls):

```cpp
            if (! clip.bankKey.empty()) {
                po->setProperty("bankKey",       juce::String(clip.bankKey));
                po->setProperty("anchorPitchHz", clip.anchorPitchHz);
                po->setProperty("variantTag",    juce::String(clip.variantTag));
            }
```

(Per-clip metadata is duplicated on every phoneme of the clip — fine because each grain is a single-phoneme clip; downstream tools only read these from the phoneme array.)

- [ ] **Step 4: Patch the reader**

In `src/audio/GspeakBundle.cpp`, inside `read`, in the v2 phoneme-loading loop (around line 278), AFTER the existing four field reads, add:

```cpp
            // Optional grain-metadata fields (back-compat: missing → defaults).
            if (po->hasProperty("anchorPitchHz") && i == 0) {
                clip->anchorPitchHz = (float)(double) po->getProperty("anchorPitchHz");
            }
            if (po->hasProperty("bankKey") && i == 0) {
                clip->bankKey = po->getProperty("bankKey").toString().toStdString();
            }
            if (po->hasProperty("variantTag") && i == 0) {
                clip->variantTag = po->getProperty("variantTag").toString().toStdString();
            }
```

We read only from the first phoneme into the clip-level field because the bundle builder writes one phoneme per grain-clip; if a clip ever carries multiple phonemes, the metadata of phoneme 0 wins.

- [ ] **Step 5: Build + tests pass**

```bash
cd build-tests && cmake --build . --target guitar_dsp_tests && \
  ctest -R "grain.metadata|legacy.manifest" --output-on-failure
```

Expected: both new tests PASS. Run the full bundle test file to be sure nothing else broke:

```bash
ctest -R "gspeak_bundle" --output-on-failure
```

Expected: all PASS.

- [ ] **Step 6: Commit**

```bash
git add src/audio/GspeakBundle.cpp tests/unit/audio/test_gspeak_bundle.cpp
git commit -m "feat(gspeak-bundle): round-trip optional bankKey/anchorPitchHz/variantTag"
```

---

## Task 3: ClipBankPlayer pitch-aware selection mode

**Files:**
- Modify: `src/audio/ClipBankPlayer.h`, `src/audio/ClipBankPlayer.cpp`
- Test: `tests/unit/audio/test_clip_bank_player.cpp`

**Interfaces:**
- Consumes: `TTSClip::bankKey` and `anchorPitchHz` (Task 1).
- Produces: `ClipBankPlayer::setDetectedPitchHz(float)` (atomic; called every block from AudioGraph in Task 12); pitch-aware mode auto-engages when the active bank's first clip has `bankKey != ""`.

The new selection algorithm:

> On detected onset, if **anchor mode** is active:
> 1. Advance the **key cursor** (cycles unique bank keys in first-appearance order).
> 2. Filter bank by `bankKey == keyCursor`.
> 3. Pick the grain whose `anchorPitchHz` is closest to the most recent `detectedPitchHz_`. Ties broken by lowest grain index.
> 4. Start playback from that grain's sample 0.
>
> If anchor mode is **off** (no bankKey on any clip), behavior is the existing round-robin — unchanged.

- [ ] **Step 1: Write three failing tests**

Append to `tests/unit/audio/test_clip_bank_player.cpp`:

```cpp
TEST_CASE("ClipBankPlayer anchor mode picks closest pitch within bankKey",
          "[clipbank][anchor]") {
    using guitar_dsp::audio::ClipBankPlayer;
    using guitar_dsp::audio::TTSClip;

    auto makeGrain = [](const std::string& key, float anchor, float fillValue,
                        std::size_t lenSamples) {
        auto c = std::make_shared<TTSClip>();
        c->sampleRate    = 48000.0;
        c->samples.assign(lenSamples, fillValue);
        c->bankKey       = key;
        c->anchorPitchHz = anchor;
        return std::const_pointer_cast<const TTSClip>(c);
    };

    ClipBankPlayer p;
    p.prepare(48000.0, 64);
    // 3 ah grains at G2/D3/A3, 3 eh grains at the same anchors.
    std::vector<TTSClipPtr> bank = {
        makeGrain("sung_ah",  98.0f, 0.11f, 96),
        makeGrain("sung_ah", 147.0f, 0.12f, 96),
        makeGrain("sung_ah", 220.0f, 0.13f, 96),
        makeGrain("sung_eh",  98.0f, 0.21f, 96),
        makeGrain("sung_eh", 147.0f, 0.22f, 96),
        makeGrain("sung_eh", 220.0f, 0.23f, 96),
    };
    p.setBank(bank);
    // Drain the pending-bank flag with one no-onset block.
    {
        float in[64] = {0}; float out[64] = {0};
        p.process(in, out, 64);
    }
    // Simulate detected pitch at 200 Hz → closest anchor is 220 Hz.
    p.setDetectedPitchHz(200.0f);
    // First onset → first key (sung_ah), should pick the 220 Hz grain (index 2).
    float in[64] = {0}; float out[64] = {0};
    in[0] = 1.0f;  // synthetic transient
    p.process(in, out, 64);
    // The first non-zero sample of `out` should be ~0.13 (the 220 Hz grain).
    bool found = false;
    for (float v : out) if (v != 0.0f) { CHECK(v == Approx(0.13f)); found = true; break; }
    CHECK(found);

    // Next onset → key cursor advances to sung_eh; pitch is still ~200,
    // so we expect the 220 Hz eh grain (~0.23).
    for (auto& s : out) s = 0.0f;
    in[0] = 0.0f; in[32] = 1.0f;
    p.process(in, out, 64);
    found = false;
    for (std::size_t i = 33; i < 64; ++i) if (out[i] != 0.0f) {
        CHECK(out[i] == Approx(0.23f)); found = true; break;
    }
    CHECK(found);
}

TEST_CASE("ClipBankPlayer anchor-mode fallback when pitch unknown picks first",
          "[clipbank][anchor][fallback]") {
    using guitar_dsp::audio::ClipBankPlayer;
    using guitar_dsp::audio::TTSClip;
    auto g = [](const std::string& k, float anchor, float fill) {
        auto c = std::make_shared<TTSClip>();
        c->sampleRate = 48000.0;
        c->samples.assign(96, fill);
        c->bankKey = k; c->anchorPitchHz = anchor;
        return std::const_pointer_cast<const TTSClip>(c);
    };
    ClipBankPlayer p; p.prepare(48000.0, 64);
    p.setBank({ g("sung_ah", 98.0f, 0.5f),
                g("sung_ah", 220.0f, 0.6f) });
    float in[64] = {0}; float out[64] = {0}; p.process(in, out, 64); // drain
    // Default detected pitch is 0 — anchor mode falls back to first grain
    // of current key.
    in[0] = 1.0f;
    p.process(in, out, 64);
    bool found = false;
    for (float v : out) if (v != 0.0f) { CHECK(v == Approx(0.5f)); found = true; break; }
    CHECK(found);
}

TEST_CASE("ClipBankPlayer legacy mode unchanged when no bankKey",
          "[clipbank][legacy]") {
    using guitar_dsp::audio::ClipBankPlayer;
    using guitar_dsp::audio::TTSClip;
    auto g = [](float fill) {
        auto c = std::make_shared<TTSClip>();
        c->sampleRate = 48000.0;
        c->samples.assign(96, fill);
        return std::const_pointer_cast<const TTSClip>(c);
    };
    ClipBankPlayer p; p.prepare(48000.0, 64);
    p.setBank({ g(0.1f), g(0.2f) });
    float in[64] = {0}; float out[64] = {0}; p.process(in, out, 64);
    in[0] = 1.0f;
    p.process(in, out, 64);
    bool found = false;
    for (float v : out) if (v != 0.0f) { CHECK(v == Approx(0.1f)); found = true; break; }
    CHECK(found);
}
```

- [ ] **Step 2: Build; expect compile failure on `setDetectedPitchHz`**

```bash
cd build-tests && cmake --build . --target guitar_dsp_tests 2>&1 | tail -10
```

Expected: error on missing `setDetectedPitchHz`.

- [ ] **Step 3: Patch ClipBankPlayer.h**

In `src/audio/ClipBankPlayer.h`, inside the `public:` block, after `rewind`:

```cpp
    // Message or audio thread. Latest YIN-detected pitch in Hz, used by
    // anchor-aware selection. 0 means "unknown" — selection falls back to
    // the first grain of the current bank key.
    void setDetectedPitchHz(float hz) noexcept {
        detectedPitchHz_.store(hz, std::memory_order_relaxed);
    }
```

In the `private:` block, add after the existing atomics:

```cpp
    std::atomic<float> detectedPitchHz_ {0.0f};

    // Anchor mode state. Engaged when activeBank_'s first clip has bankKey != "".
    bool                     anchorMode_   = false;
    std::vector<std::string> uniqueKeys_;     // ordered, first-appearance
    int                      keyCursor_    = -1;
```

- [ ] **Step 4: Patch ClipBankPlayer.cpp**

In `src/audio/ClipBankPlayer.cpp`, replace the bank-swap drain block (the `if (newBankFlag_.exchange(...)) { ... }` block at the top of `process`) with:

```cpp
    if (newBankFlag_.exchange(false, std::memory_order_acquire)) {
        activeBank_ = std::move(pendingBank_);
        cursor_  = -1;
        playPos_ = 0;
        playing_ = false;
        onset_.reset();
        currentClipIndex_.store(-1, std::memory_order_relaxed);

        // Recompute anchor-mode + unique key list.
        anchorMode_  = false;
        uniqueKeys_.clear();
        keyCursor_   = -1;
        if (! activeBank_.empty() && activeBank_[0] &&
            ! activeBank_[0]->bankKey.empty()) {
            anchorMode_ = true;
            for (const auto& c : activeBank_) {
                if (! c) continue;
                const auto& k = c->bankKey;
                if (std::find(uniqueKeys_.begin(), uniqueKeys_.end(), k) ==
                    uniqueKeys_.end()) {
                    uniqueKeys_.push_back(k);
                }
            }
        }
    }
```

Then replace the per-sample onset-handling branch in the for-loop (the `if (haveBank && onset_.processSample(onsetSrc[i]))` block) with:

```cpp
        if (haveBank && onset_.processSample(onsetSrc[i])) {
            const int n = static_cast<int>(activeBank_.size());
            int next = -1;
            if (anchorMode_ && ! uniqueKeys_.empty()) {
                // Cycle keys; pick closest anchor within the new key.
                keyCursor_ = (keyCursor_ + 1) % static_cast<int>(uniqueKeys_.size());
                const auto& wantKey = uniqueKeys_[(std::size_t) keyCursor_];
                const float detected = detectedPitchHz_.load(std::memory_order_relaxed);
                float bestDist = std::numeric_limits<float>::infinity();
                for (int j = 0; j < n; ++j) {
                    const auto& c = activeBank_[(std::size_t) j];
                    if (! c || c->bankKey != wantKey) continue;
                    const float d = (detected <= 0.0f)
                        ? -static_cast<float>(j)  // prefer lowest index when pitch unknown
                        : std::fabs(c->anchorPitchHz - detected);
                    if (d < bestDist) { bestDist = d; next = j; }
                }
            }
            if (next < 0) {
                next = (cursor_ + 1) % n;  // legacy round-robin
            }
            cursor_  = next;
            playPos_ = 0;
            playing_ = true;
            currentClipIndex_.store(cursor_, std::memory_order_relaxed);
        }
```

Add `#include <algorithm>` (already included), `#include <cmath>`, `#include <limits>` at the top.

- [ ] **Step 5: Build + the three new tests pass**

```bash
cd build-tests && cmake --build . --target guitar_dsp_tests && \
  ctest -R "ClipBankPlayer.anchor|ClipBankPlayer.legacy" --output-on-failure
```

Expected: all PASS. Then sanity-check existing tests:

```bash
ctest -R "ClipBankPlayer|clip_bank_player" --output-on-failure
```

Expected: all PASS.

- [ ] **Step 6: Commit**

```bash
git add src/audio/ClipBankPlayer.h src/audio/ClipBankPlayer.cpp \
        tests/unit/audio/test_clip_bank_player.cpp
git commit -m "feat(clip-bank-player): pitch-aware anchor selection by bankKey"
```

---

## Task 4: Scene struct — VoicePack + resolvedGspeakPath()

**Files:**
- Modify: `src/scenes/Scene.h`
- Modify: `src/scenes/SceneLibrary.cpp`
- Test: `tests/unit/scenes/test_scene_resolved_gspeak_path.cpp` (new)

**Interfaces:**
- Consumes: nothing.
- Produces: `Scene::VoicePack { std::string label; std::string path; }`; `Scene::voicePacks` (empty default); `Scene::defaultVoiceIndex` (0 default); `Scene::showVoicePackPicker` (false default); `Scene::resolvedGspeakPath(int activeVoiceIndex)` returning the path to use.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/scenes/test_scene_resolved_gspeak_path.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "scenes/Scene.h"

using guitar_dsp::scenes::Scene;

TEST_CASE("Scene::resolvedGspeakPath falls back to gspeakPath when no voicePacks",
          "[scene][voicePacks]") {
    Scene s;
    s.gspeakPath = "assets/clips/gspeak/scene0.gspeak";
    CHECK(s.resolvedGspeakPath(0) == "assets/clips/gspeak/scene0.gspeak");
    CHECK(s.resolvedGspeakPath(99) == "assets/clips/gspeak/scene0.gspeak");
}

TEST_CASE("Scene::resolvedGspeakPath returns voicePacks[i].path when set",
          "[scene][voicePacks]") {
    Scene s;
    s.gspeakPath = "irrelevant.gspeak";
    s.voicePacks = {
        { "A", "a.gspeak" },
        { "B", "b.gspeak" },
        { "C", "c.gspeak" },
    };
    CHECK(s.resolvedGspeakPath(0) == "a.gspeak");
    CHECK(s.resolvedGspeakPath(1) == "b.gspeak");
    CHECK(s.resolvedGspeakPath(2) == "c.gspeak");
}

TEST_CASE("Scene::resolvedGspeakPath clamps out-of-range to defaultVoiceIndex",
          "[scene][voicePacks]") {
    Scene s;
    s.voicePacks = { { "A", "a.gspeak" }, { "B", "b.gspeak" } };
    s.defaultVoiceIndex = 1;
    CHECK(s.resolvedGspeakPath(-1) == "b.gspeak");
    CHECK(s.resolvedGspeakPath(99) == "b.gspeak");
}
```

- [ ] **Step 2: Register the new test file**

In `tests/CMakeLists.txt`, under `add_executable(guitar_dsp_tests ...)`, add the line in alphabetical position among the `unit/scenes/` block:

```cmake
    unit/scenes/test_scene_resolved_gspeak_path.cpp
```

- [ ] **Step 3: Build; expect compile failure**

```bash
cd build-tests && cmake --build . --target guitar_dsp_tests 2>&1 | tail -10
```

Expected: missing `VoicePack` / `voicePacks` / `resolvedGspeakPath`.

- [ ] **Step 4: Extend Scene.h**

In `src/scenes/Scene.h`, inside `struct Scene`, after the existing `Speech speech{};` member (before `bool showChat`):

```cpp
    struct VoicePack {
        std::string label;
        std::string path;
    };
    std::vector<VoicePack> voicePacks;        // empty = single-path scene
    int  defaultVoiceIndex   = 0;
    bool showVoicePackPicker = false;

    std::string resolvedGspeakPath(int activeVoiceIndex) const {
        if (voicePacks.empty()) return gspeakPath;
        if (activeVoiceIndex < 0 ||
            activeVoiceIndex >= static_cast<int>(voicePacks.size())) {
            activeVoiceIndex = defaultVoiceIndex;
            if (activeVoiceIndex < 0 ||
                activeVoiceIndex >= static_cast<int>(voicePacks.size())) {
                activeVoiceIndex = 0;
            }
        }
        return voicePacks[static_cast<std::size_t>(activeVoiceIndex)].path;
    }
```

- [ ] **Step 5: Build + tests pass**

```bash
cd build-tests && cmake -S /Users/user/GIT/guitar-dsp -B . && \
  cmake --build . --target guitar_dsp_tests && \
  ctest -R "resolvedGspeakPath" --output-on-failure
```

Expected: 3 PASS.

- [ ] **Step 6: Commit**

```bash
git add src/scenes/Scene.h tests/unit/scenes/test_scene_resolved_gspeak_path.cpp \
        tests/CMakeLists.txt
git commit -m "feat(scene): VoicePack + resolvedGspeakPath() helper"
```

---

## Task 5: SceneLibrary parses voicePacks/defaultVoiceIndex/showVoicePackPicker

**Files:**
- Modify: `src/scenes/SceneLibrary.cpp`
- Test: `tests/unit/scenes/test_scene_library_sung_vowels.cpp` (new)

**Interfaces:**
- Consumes: the `Scene::VoicePack` struct from Task 4.
- Produces: a JSON loader that fills `Scene::voicePacks` from a `"voicePacks": [{label,path}, …]` array and reads `"defaultVoiceIndex"` and `"showVoicePackPicker"`.

- [ ] **Step 1: Write the failing test**

Create `tests/unit/scenes/test_scene_library_sung_vowels.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "scenes/SceneLibrary.h"

using guitar_dsp::scenes::Scene;
using guitar_dsp::scenes::SceneLibrary;

TEST_CASE("SceneLibrary parses voicePacks for scene 11",
          "[scene-library][voicePacks]") {
    // Synthesize the JSON inline so the test is self-contained.
    juce::File tmp =
        juce::File::createTempFile("_test_voice_packs.json");
    tmp.replaceWithText(R"({
        "id": 11,
        "name": "Sung Vowels",
        "voicePacks": [
            { "label": "Male 1",     "path": "assets/clips/gspeak/scene11_sung_m1.gspeak"  },
            { "label": "Mighty Man", "path": "assets/clips/gspeak/scene11_sung_m10.gspeak" },
            { "label": "Female 2",   "path": "assets/clips/gspeak/scene11_sung_f2.gspeak"  },
            { "label": "Female 8",   "path": "assets/clips/gspeak/scene11_sung_f8.gspeak"  }
        ],
        "defaultVoiceIndex": 0,
        "showVoicePackPicker": true,
        "gspeakAutoLoad": true
    })");

    auto s = SceneLibrary::loadOne(tmp.getFullPathName().toStdString());
    REQUIRE(s.has_value());
    REQUIRE(s->voicePacks.size() == 4);
    CHECK(s->voicePacks[0].label == "Male 1");
    CHECK(s->voicePacks[1].label == "Mighty Man");
    CHECK(s->voicePacks[2].label == "Female 2");
    CHECK(s->voicePacks[3].label == "Female 8");
    CHECK(s->voicePacks[0].path  == "assets/clips/gspeak/scene11_sung_m1.gspeak");
    CHECK(s->defaultVoiceIndex == 0);
    CHECK(s->showVoicePackPicker == true);
    CHECK(s->gspeakAutoLoad == true);

    tmp.deleteFile();
}

TEST_CASE("SceneLibrary leaves voicePacks empty when absent",
          "[scene-library][voicePacks][backcompat]") {
    juce::File tmp =
        juce::File::createTempFile("_test_no_voice_packs.json");
    tmp.replaceWithText(R"({
        "id": 0,
        "name": "Legacy",
        "gspeakPath": "assets/clips/gspeak/scene0.gspeak"
    })");
    auto s = SceneLibrary::loadOne(tmp.getFullPathName().toStdString());
    REQUIRE(s.has_value());
    CHECK(s->voicePacks.empty());
    CHECK(s->defaultVoiceIndex == 0);
    CHECK(s->showVoicePackPicker == false);
    CHECK(s->gspeakPath == "assets/clips/gspeak/scene0.gspeak");
    tmp.deleteFile();
}
```

- [ ] **Step 2: Register the new test**

`tests/CMakeLists.txt`: add `unit/scenes/test_scene_library_sung_vowels.cpp` (alphabetical position).

- [ ] **Step 3: Build; expect FAIL on voicePacks not parsed**

```bash
cd build-tests && cmake -S /Users/user/GIT/guitar-dsp -B . && \
  cmake --build . --target guitar_dsp_tests && \
  ctest -R "voicePacks" --output-on-failure
```

- [ ] **Step 4: Patch SceneLibrary.cpp**

In `src/scenes/SceneLibrary.cpp`, find the existing `gspeakPath` handling (around the line `if (obj->hasProperty("gspeakPath"))`). Immediately after that block (and the `gspeakAutoLoad` block), add:

```cpp
    if (obj->hasProperty("defaultVoiceIndex"))
        s.defaultVoiceIndex = static_cast<int>(obj->getProperty("defaultVoiceIndex"));
    if (obj->hasProperty("showVoicePackPicker"))
        s.showVoicePackPicker = static_cast<bool>(obj->getProperty("showVoicePackPicker"));

    if (obj->hasProperty("voicePacks")) {
        if (auto* arr = obj->getProperty("voicePacks").getArray()) {
            s.voicePacks.clear();
            s.voicePacks.reserve(static_cast<std::size_t>(arr->size()));
            for (int i = 0; i < arr->size(); ++i) {
                auto* vp = (*arr)[i].getDynamicObject();
                if (! vp) continue;
                Scene::VoicePack pack;
                pack.label = vp->getProperty("label").toString().toStdString();
                pack.path  = vp->getProperty("path").toString().toStdString();
                if (! pack.path.empty()) s.voicePacks.push_back(std::move(pack));
            }
        }
    }
```

- [ ] **Step 5: Build + tests pass**

```bash
cd build-tests && cmake --build . --target guitar_dsp_tests && \
  ctest -R "voicePacks" --output-on-failure
```

Expected: 2 new tests PASS. Re-run all scene-library tests:

```bash
ctest -R "scene_library" --output-on-failure
```

Expected: all PASS (no regression).

- [ ] **Step 6: Commit**

```bash
git add src/scenes/SceneLibrary.cpp \
        tests/unit/scenes/test_scene_library_sung_vowels.cpp \
        tests/CMakeLists.txt
git commit -m "feat(scene-library): parse voicePacks/defaultVoiceIndex/showVoicePackPicker"
```

---

## Task 6: PluginState persists activeVoiceIndex per scene

**Files:**
- Modify: `src/app/PluginState.h`, `src/app/PluginState.cpp`
- Test: `tests/unit/app/test_plugin_state.cpp` (extend)

**Interfaces:**
- Consumes: nothing.
- Produces: `PluginStateData::activeVoiceIndexByScene` — a `std::map<int,int>` from scene id to chosen voice index. Default-empty (every scene falls through to `defaultVoiceIndex`).

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/app/test_plugin_state.cpp`:

```cpp
TEST_CASE("PluginState round-trips activeVoiceIndexByScene",
          "[plugin-state][voice-pack]") {
    using guitar_dsp::app::PluginState;
    using guitar_dsp::app::PluginStateData;

    PluginStateData in;
    in.activeVoiceIndexByScene[11] = 2;
    in.activeVoiceIndexByScene[12] = 3;
    auto json = PluginState::toJson(in);
    auto out  = PluginState::fromJson(json);
    REQUIRE(out.activeVoiceIndexByScene.size() == 2);
    CHECK(out.activeVoiceIndexByScene.at(11) == 2);
    CHECK(out.activeVoiceIndexByScene.at(12) == 3);
}

TEST_CASE("PluginState fromJson handles missing activeVoiceIndexByScene",
          "[plugin-state][voice-pack][backcompat]") {
    using guitar_dsp::app::PluginState;
    // Synthesize a legacy JSON blob (no voice index map).
    juce::String legacy = R"({ "sceneId": 0, "makeup": 4.0 })";
    auto out = PluginState::fromJson(legacy);
    CHECK(out.activeVoiceIndexByScene.empty());
}
```

- [ ] **Step 2: Build; FAIL on missing field**

- [ ] **Step 3: Add the field to PluginState.h**

In `src/app/PluginState.h`, inside `PluginStateData`, after the existing AI-feature fields (after `clearChatPedalId`):

```cpp
    // Per-scene runtime voice-pack index. Scene id → index into Scene::voicePacks.
    // Default-empty: each scene falls through to Scene::defaultVoiceIndex.
    std::map<int, int> activeVoiceIndexByScene{};
```

- [ ] **Step 4: Round-trip in PluginState.cpp**

Open `src/app/PluginState.cpp`. In `toJson`, after the existing serialisation lines, add:

```cpp
    auto* voicesObj = new juce::DynamicObject();
    for (const auto& kv : d.activeVoiceIndexByScene) {
        voicesObj->setProperty(juce::String(kv.first), kv.second);
    }
    obj->setProperty("activeVoiceIndexByScene", juce::var(voicesObj));
```

In `fromJson`, after the existing field reads:

```cpp
    if (obj->hasProperty("activeVoiceIndexByScene")) {
        if (auto* vo = obj->getProperty("activeVoiceIndexByScene").getDynamicObject()) {
            for (const auto& kv : vo->getProperties()) {
                const int sceneId = kv.name.toString().getIntValue();
                const int idx     = static_cast<int>(kv.value);
                d.activeVoiceIndexByScene[sceneId] = idx;
            }
        }
    }
```

- [ ] **Step 5: Build + tests pass**

```bash
cd build-tests && cmake --build . --target guitar_dsp_tests && \
  ctest -R "PluginState" --output-on-failure
```

- [ ] **Step 6: Commit**

```bash
git add src/app/PluginState.h src/app/PluginState.cpp tests/unit/app/test_plugin_state.cpp
git commit -m "feat(plugin-state): persist activeVoiceIndexByScene"
```

---

## Task 7: Test fixture — synthetic `sung_vowels_test.gspeak`

**Files:**
- Create: `tests/fixtures/build_sung_vowels_test_bundle.py`
- Create: `tests/fixtures/sung_vowels_test.gspeak` (built by the script)
- Modify: `tests/CMakeLists.txt` to invoke the script at configure time.

**Interfaces:**
- Consumes: the manifest schema from Task 2.
- Produces: a real `.gspeak` zip on disk that downstream integration tests load. 5 grains (sung_ah at 110/440/880, sung_eh at 220/440), all 1 second of a 440 Hz sine at sample rate 48000.

- [ ] **Step 1: Write the Python builder**

Create `tests/fixtures/build_sung_vowels_test_bundle.py`:

```python
#!/usr/bin/env python3
"""Build a 5-grain synthetic sung-vowels test bundle.

Outputs tests/fixtures/sung_vowels_test.gspeak. Designed for unit tests —
the audio content is a flat 440 Hz sine in every grain; what matters is
the manifest metadata.
"""
import json
import math
import os
import struct
import sys
import wave
import zipfile

HERE       = os.path.dirname(os.path.abspath(__file__))
OUT_PATH   = os.path.join(HERE, "sung_vowels_test.gspeak")
SAMPLE_RATE = 48000
GRAIN_SEC   = 1.0
GRAIN_SAMPLES = int(SAMPLE_RATE * GRAIN_SEC)

GRAINS = [
    # (bankKey, anchorPitchHz, label)
    ("sung_ah", 110.0, "ah"),
    ("sung_ah", 440.0, "ah"),
    ("sung_ah", 880.0, "ah"),
    ("sung_eh", 220.0, "eh"),
    ("sung_eh", 440.0, "eh"),
]

def sine_grain(seconds, freq, sr):
    n = int(seconds * sr)
    return [math.sin(2 * math.pi * freq * i / sr) * 0.3 for i in range(n)]

def write_wav_mono16(samples, sr):
    raw = b"".join(struct.pack("<h", max(-32768, min(32767, int(s * 32767))))
                    for s in samples)
    import io
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(raw)
    return buf.getvalue()

def main():
    total_samples = GRAIN_SAMPLES * len(GRAINS)
    audio = []
    phonemes = []
    syllables = []
    for i, (key, anchor, label) in enumerate(GRAINS):
        start = i * GRAIN_SAMPLES
        end   = start + GRAIN_SAMPLES
        audio.extend(sine_grain(GRAIN_SEC, 440.0, SAMPLE_RATE))
        phonemes.append({
            "label": label,
            "type": "Vowel",
            "startSample": start,
            "endSample": end,
            "bankKey": key,
            "anchorPitchHz": anchor,
            "variantTag": "test",
        })
        syllables.append({
            "startSample": start,
            "endSample": end,
            "vowelNucleusSample": (start + end) // 2,
            "attackEndSample": start,
            "codaStartSample": end,
            "nucleusIsFricative": False,
            "phonemeIndices": [i],
        })
    wav_bytes = write_wav_mono16(audio, SAMPLE_RATE)
    manifest = {
        "version": 1,
        "kind": "clip",
        "savedBy": "build_sung_vowels_test_bundle.py",
        "text": "test",
        "sampleRate": SAMPLE_RATE,
        "lengthSamples": total_samples,
        "clipKind": "v2",
        "syllables": syllables,
        "phonemes": phonemes,
    }
    with zipfile.ZipFile(OUT_PATH, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("manifest.json", json.dumps(manifest, indent=2))
        z.writestr("audio.wav", wav_bytes)
    print(f"Wrote {OUT_PATH} ({len(wav_bytes)} bytes audio, "
          f"{len(GRAINS)} grains)")

if __name__ == "__main__":
    sys.exit(main() or 0)
```

- [ ] **Step 2: Wire CMake to build the fixture**

In `tests/CMakeLists.txt`, BEFORE the `add_executable(guitar_dsp_tests ...)` call:

```cmake
# Synthetic test bundle for sung-vowels tests. Re-built whenever the
# builder script changes.
set(SUNG_VOWELS_TEST_BUNDLE
    ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/sung_vowels_test.gspeak)
add_custom_command(
    OUTPUT ${SUNG_VOWELS_TEST_BUNDLE}
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/build_sung_vowels_test_bundle.py
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/build_sung_vowels_test_bundle.py
    COMMENT "Building sung_vowels_test.gspeak fixture"
    VERBATIM)
add_custom_target(sung_vowels_test_bundle ALL DEPENDS ${SUNG_VOWELS_TEST_BUNDLE})
```

If `Python3_EXECUTABLE` isn't already discovered above in the file, add `find_package(Python3 REQUIRED COMPONENTS Interpreter)` near the top of `tests/CMakeLists.txt`.

Also make `guitar_dsp_tests` depend on the bundle:

```cmake
add_dependencies(guitar_dsp_tests sung_vowels_test_bundle)
```

(Place this right after the `add_executable(guitar_dsp_tests ...)` block.)

- [ ] **Step 3: Add the file to gitignore**

In `.gitignore`, near the existing `tests/fixtures/tts/*/audio.wav` line:

```
tests/fixtures/sung_vowels_test.gspeak
```

- [ ] **Step 4: Build once, verify the bundle exists**

```bash
cd build-tests && cmake -S /Users/user/GIT/guitar-dsp -B . && \
  cmake --build . --target sung_vowels_test_bundle && \
  ls -la tests/fixtures/sung_vowels_test.gspeak
```

Expected: file present, ~200–400 KB.

- [ ] **Step 5: Commit**

```bash
git add tests/fixtures/build_sung_vowels_test_bundle.py \
        tests/CMakeLists.txt .gitignore
git commit -m "test(fixtures): synthetic sung_vowels_test.gspeak builder"
```

---

## Task 8: Production bundle builder — produce all 4 voices

**Files:**
- Create: `tools/build_sung_vowel_bundle.py`
- Generated, committed: `assets/clips/gspeak/scene11_sung_{m1,m10,f2,f8}.gspeak`

**Interfaces:**
- Consumes: WAVs under `assets/vocalset/{m1,m10,f2,f8}/`.
- Produces: 4 `.gspeak` files. Each bundle's audio.wav is a concatenation of grains; the manifest's phoneme entries carry per-grain `bankKey`, `anchorPitchHz`, `variantTag`.

- [ ] **Step 1: Write the builder**

Create `tools/build_sung_vowel_bundle.py`:

```python
#!/usr/bin/env python3
"""Build per-voice sung-vowel .gspeak bundles from assets/vocalset/.

For each singer folder (m1, m10, f2, f8):
  1. Read long_tones/straight/*.wav → 1 grain per vowel (mid-anchor; estimate F0).
  2. Read scales/slow_piano/*_a.wav etc. → slice into 3 anchor grains per vowel.
  3. Concatenate all grains into one audio.wav with 200 ms silence pads.
  4. Emit manifest.json with per-grain bankKey/anchorPitchHz/variantTag.
  5. Zip → assets/clips/gspeak/scene11_sung_<voice>.gspeak.

Run with: python3 tools/build_sung_vowel_bundle.py
"""
import json
import math
import os
import struct
import sys
import wave
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SRC_ROOT  = REPO_ROOT / "assets" / "vocalset"
OUT_ROOT  = REPO_ROOT / "assets" / "clips" / "gspeak"
OUT_ROOT.mkdir(parents=True, exist_ok=True)

VOICES = ["m1", "m10", "f2", "f8"]
VOWELS = ["a", "e", "i", "o", "u"]
VOWEL_TO_BANK_KEY = {
    "a": "sung_ah",
    "e": "sung_eh",
    "i": "sung_ee",
    "o": "sung_oh",
    "u": "sung_oo",
}
PAD_MS         = 200
TARGET_SR      = 48000
LONG_TONE_SEC  = 3.0  # central window from each long_tones/straight clip
ANCHOR_SLICES_PER_VOWEL = 3  # from scales/slow_piano — low/mid/high


def read_wav_mono(path: Path):
    with wave.open(str(path), "rb") as w:
        sr   = w.getframerate()
        n    = w.getnframes()
        bits = w.getsampwidth()
        raw  = w.readframes(n)
    if bits == 2:
        fmt = f"<{n}h"
        samples = struct.unpack(fmt, raw)
        samples = [s / 32768.0 for s in samples]
    else:
        raise RuntimeError(f"unsupported sample width {bits} in {path}")
    if sr != TARGET_SR:
        # Simple linear resample.
        ratio = TARGET_SR / sr
        out_n = int(n * ratio)
        out = []
        for i in range(out_n):
            src = i / ratio
            i0 = int(src)
            frac = src - i0
            i1 = min(i0 + 1, n - 1)
            out.append((1 - frac) * samples[i0] + frac * samples[i1])
        samples = out
        sr = TARGET_SR
    return samples


def estimate_f0_autocorr(samples, sr, search_range=(60, 1000)):
    """Crude autocorrelation pitch estimate over the center 1 s window."""
    n = len(samples)
    if n < sr // 2:
        return 0.0
    mid = n // 2
    win = samples[mid - sr // 2 : mid + sr // 2]
    # Energy normalisation.
    energy = sum(x * x for x in win)
    if energy < 1e-6:
        return 0.0
    best_lag, best_corr = 0, 0.0
    min_lag = sr // search_range[1]
    max_lag = sr // search_range[0]
    for lag in range(min_lag, max_lag):
        c = 0.0
        for i in range(len(win) - lag):
            c += win[i] * win[i + lag]
        if c > best_corr:
            best_corr = c
            best_lag = lag
    return sr / best_lag if best_lag else 0.0


def trim_silence(samples, sr, threshold=0.01):
    n = len(samples)
    start = 0
    while start < n and abs(samples[start]) < threshold:
        start += 1
    end = n
    while end > start and abs(samples[end - 1]) < threshold:
        end -= 1
    return samples[start:end]


def center_window(samples, sr, sec):
    n = len(samples)
    want = int(sec * sr)
    if n <= want:
        return samples
    start = (n - want) // 2
    return samples[start : start + want]


def normalize_peak(samples, peak_dbfs=-3.0):
    max_abs = max((abs(s) for s in samples), default=0.0)
    if max_abs < 1e-6:
        return samples
    target = 10 ** (peak_dbfs / 20.0)
    g = target / max_abs
    return [s * g for s in samples]


def slice_scale_into_anchors(samples, sr, num_slices):
    """Detect note attacks by simple energy-rise; return num_slices grain ranges."""
    n = len(samples)
    win = sr // 50  # 20 ms RMS window
    rms = []
    for i in range(0, n - win, win):
        e = sum(samples[i+j] * samples[i+j] for j in range(win)) / win
        rms.append(math.sqrt(e))
    if not rms:
        return []
    rms_max = max(rms)
    if rms_max < 1e-6:
        return []
    threshold = rms_max * 0.25
    onsets = []
    in_note = False
    for k, r in enumerate(rms):
        if not in_note and r > threshold:
            onsets.append(k * win)
            in_note = True
        elif in_note and r < threshold * 0.5:
            in_note = False
    # Pick evenly spaced onsets across the detected list.
    if len(onsets) < num_slices:
        # fallback: split file into N equal pieces
        step = n // num_slices
        return [(i * step, (i + 1) * step) for i in range(num_slices)]
    pick_idx = [int(i * (len(onsets) - 1) / (num_slices - 1)) for i in range(num_slices)]
    grains = []
    for k in pick_idx:
        start = onsets[k]
        end = onsets[k + 1] if k + 1 < len(onsets) else n
        # Cap each grain at ~1.5 s to keep grains short.
        max_len = int(sr * 1.5)
        if end - start > max_len:
            end = start + max_len
        grains.append((start, end))
    return grains


def write_wav_mono16(samples, sr):
    raw = b"".join(
        struct.pack("<h", max(-32768, min(32767, int(s * 32767))))
        for s in samples)
    import io
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(raw)
    return buf.getvalue()


def build_for_voice(voice: str) -> Path:
    voice_root = SRC_ROOT / voice
    if not voice_root.is_dir():
        raise RuntimeError(f"missing {voice_root}")

    audio_out   = []
    phonemes    = []
    syllables   = []
    sample_idx  = 0
    pad_samples = int(PAD_MS / 1000.0 * TARGET_SR)
    silence     = [0.0] * pad_samples

    grain_index = 0
    for vowel in VOWELS:
        bank_key = VOWEL_TO_BANK_KEY[vowel]

        # 1. Mid-anchor grain from long_tones/straight.
        prefix = voice  # filenames use the same prefix as the dir name
        straight = (voice_root / "long_tones" / "straight" /
                    f"{prefix}_long_straight_{vowel}.wav")
        if straight.exists():
            samples = read_wav_mono(straight)
            samples = trim_silence(samples, TARGET_SR)
            samples = center_window(samples, TARGET_SR, LONG_TONE_SEC)
            samples = normalize_peak(samples)
            f0 = estimate_f0_autocorr(samples, TARGET_SR)
            start = sample_idx
            audio_out.extend(samples)
            sample_idx += len(samples)
            audio_out.extend(silence)
            sample_idx += len(silence)
            end = start + len(samples)
            phonemes.append({
                "label": vowel,
                "type": "Vowel",
                "startSample": start,
                "endSample":   end,
                "bankKey":     bank_key,
                "anchorPitchHz": round(f0, 2),
                "variantTag":  "straight",
            })
            syllables.append({
                "startSample": start, "endSample": end,
                "vowelNucleusSample": (start + end) // 2,
                "attackEndSample": start, "codaStartSample": end,
                "nucleusIsFricative": False,
                "phonemeIndices": [grain_index],
            })
            grain_index += 1

        # 2. Three anchor grains from scales/slow_piano (C scale).
        scale = (voice_root / "scales" / "slow_piano" /
                 f"{prefix}_scales_c_slow_piano_{vowel}.wav")
        if scale.exists():
            samples = read_wav_mono(scale)
            for (a, b) in slice_scale_into_anchors(samples, TARGET_SR,
                                                   ANCHOR_SLICES_PER_VOWEL):
                grain = samples[a:b]
                grain = trim_silence(grain, TARGET_SR)
                if len(grain) < TARGET_SR // 4:  # < 250 ms — skip
                    continue
                grain = normalize_peak(grain)
                f0 = estimate_f0_autocorr(grain, TARGET_SR)
                start = sample_idx
                audio_out.extend(grain); sample_idx += len(grain)
                audio_out.extend(silence); sample_idx += len(silence)
                end = start + len(grain)
                phonemes.append({
                    "label": vowel, "type": "Vowel",
                    "startSample": start, "endSample": end,
                    "bankKey": bank_key,
                    "anchorPitchHz": round(f0, 2),
                    "variantTag": "scale-slice",
                })
                syllables.append({
                    "startSample": start, "endSample": end,
                    "vowelNucleusSample": (start + end) // 2,
                    "attackEndSample": start, "codaStartSample": end,
                    "nucleusIsFricative": False,
                    "phonemeIndices": [grain_index],
                })
                grain_index += 1

    wav_bytes = write_wav_mono16(audio_out, TARGET_SR)
    manifest = {
        "version": 1, "kind": "clip",
        "savedBy": "build_sung_vowel_bundle.py",
        "text": f"sung-vowels-{voice}",
        "sampleRate": TARGET_SR,
        "lengthSamples": len(audio_out),
        "clipKind": "v2",
        "syllables": syllables, "phonemes": phonemes,
    }
    out_path = OUT_ROOT / f"scene11_sung_{voice}.gspeak"
    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("manifest.json", json.dumps(manifest, indent=2))
        z.writestr("audio.wav", wav_bytes)
    print(f"  -> {out_path.name}: {len(phonemes)} grains, "
          f"{len(audio_out)} samples")
    return out_path


def main():
    print("Building sung-vowel bundles…")
    for v in VOICES:
        print(f"Voice {v}…")
        build_for_voice(v)
    print("Done.")

if __name__ == "__main__":
    sys.exit(main() or 0)
```

- [ ] **Step 2: Run it once**

```bash
cd /Users/user/GIT/guitar-dsp && python3 tools/build_sung_vowel_bundle.py
ls -la assets/clips/gspeak/scene11_sung_*.gspeak
```

Expected: 4 files produced; each 3–6 MB.

- [ ] **Step 3: Verify bundle integrity with the existing reader**

```bash
cd build-tests && cmake --build . --target guitar_dsp_tests && \
  ctest -R "gspeak_bundle" --output-on-failure
```

Existing bundle tests still pass; the legacy back-compat test from Task 2 covers the new bundles' parseability (each loads via `GspeakBundle::read`).

- [ ] **Step 4: Commit script + bundles**

```bash
git add tools/build_sung_vowel_bundle.py \
        assets/clips/gspeak/scene11_sung_*.gspeak
git commit -m "feat(bundles): produce 4 sung-vowel bundles from assets/vocalset"
```

---

## Task 9: Move bypass to slot 13; introduce scene 11 + 12 (12 is placeholder for M2)

**Files:**
- Create: `assets/scenes/13_bypass.json`
- Create: `assets/scenes/11_sung_vowels.json`
- Delete: `assets/scenes/11_bypass.json`
- Modify: `src/app/PluginProcessor.cpp` (change `activateScene(11)` → `activateScene(13)`)
- Modify: `tests/unit/scenes/test_scene_library.cpp` if any existing test asserts a bypass at id 11.

**Interfaces:**
- Consumes: VoicePack parsing from Task 5.
- Produces: a working scene 11 JSON that the runtime will pick up next launch.

- [ ] **Step 1: Write a regression-pinning test**

In `tests/unit/scenes/test_scene_library.cpp` (or create a new tiny test file `tests/unit/scenes/test_scene_library_bypass_slot.cpp` if the existing file is read-only conceptually), append:

```cpp
TEST_CASE("Scene 13 is Bypass; scene 11 is Sung Vowels", "[scene-library][slots]") {
    using guitar_dsp::scenes::SceneLibrary;
    const auto scenes = SceneLibrary::loadDirectory(
        "/Users/user/GIT/guitar-dsp/assets/scenes");
    const auto byId = [&](int id) {
        for (auto& s : scenes) if (s.id == id) return &s;
        return (const guitar_dsp::scenes::Scene*) nullptr;
    };
    REQUIRE(byId(11) != nullptr);
    CHECK(byId(11)->name == "Sung Vowels");
    REQUIRE(byId(13) != nullptr);
    CHECK(byId(13)->name == "Bypass");
    // Ensure no leftover scene at slot 11 named "Bypass".
    for (auto& s : scenes) {
        if (s.id == 11) CHECK(s.name != "Bypass");
    }
}
```

- [ ] **Step 2: Build; expect FAIL (no scene 11, scene 13 still missing)**

- [ ] **Step 3: Rename bypass JSON**

```bash
git mv assets/scenes/11_bypass.json assets/scenes/13_bypass.json
```

Then edit `assets/scenes/13_bypass.json`, changing the `"id": 11` line to `"id": 13`.

- [ ] **Step 4: Create scene 11 JSON**

Create `assets/scenes/11_sung_vowels.json`:

```json
{
  "id": 11,
  "name": "Sung Vowels",
  "color": "#c84cff",
  "mixer": { "masterGainDb": -3.0, "dryWet": 0.95, "transitionMs": 30 },
  "vocoder": { "enabled": true, "bypass": false },
  "tts": {
    "source": "clipBank",
    "bank": ["sung_ah", "sung_eh", "sung_ee", "sung_oh", "sung_oo"],
    "trigger": "note",
    "wordSync": "advance",
    "clarity": 0.0
  },
  "speech": {
    "player": "noteStepped",
    "maxSustainMs": 0,
    "attackInterruptPolicy": "interrupt"
  },
  "showVocoder": true,
  "showSay": false,
  "showWordReadout": false,
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

- [ ] **Step 5: Update PluginProcessor's hardcoded default-scene from 11 → 13**

In `src/app/PluginProcessor.cpp`, find the line `sceneEngine_.activateScene(11);` (around line 162). Change to:

```cpp
        // Bypass scene is now at slot 13 (was 11 before sung-vowels landed).
        sceneEngine_.activateScene(13);
```

- [ ] **Step 6: Build + test passes**

```bash
cd build-tests && cmake --build . --target guitar_dsp_tests && \
  ctest -R "Scene 13 is Bypass" --output-on-failure
```

- [ ] **Step 7: Commit**

```bash
git add assets/scenes/11_sung_vowels.json assets/scenes/13_bypass.json \
        src/app/PluginProcessor.cpp \
        tests/unit/scenes/test_scene_library.cpp
git rm assets/scenes/11_bypass.json 2>/dev/null || true
git commit -m "feat(scenes): scene 11 = Sung Vowels (4 voices), bypass moves to slot 13"
```

---

## Task 10: FCB1010 stock defaults — extend PC mapping to 11/12/13

**Files:**
- Modify: `src/midi/FCB1010Mapping.cpp`
- Test: `tests/unit/midi/test_fcb1010_mapping.cpp` (extend)

**Interfaces:**
- Consumes: nothing.
- Produces: `stockDefaults()` now maps PC 0..13 → scene 0..13.

- [ ] **Step 1: Add the failing test**

Append to `tests/unit/midi/test_fcb1010_mapping.cpp`:

```cpp
TEST_CASE("FCB1010 stock defaults map PC 0..13 to scenes 0..13",
          "[fcb1010][stock-defaults]") {
    using guitar_dsp::midi::FCB1010Mapping;
    using guitar_dsp::midi::SceneCommandType;
    auto m = FCB1010Mapping::stockDefaults();
    for (int pc = 0; pc < 14; ++pc) {
        const auto msg = juce::MidiMessage::programChange(1, pc);
        auto cmd = m.translate(msg);
        REQUIRE(cmd.has_value());
        REQUIRE(cmd->type == SceneCommandType::ActivateScene);
        REQUIRE(cmd->payload == pc);
    }
}
```

- [ ] **Step 2: Build; FAIL on PC 10..13**

- [ ] **Step 3: Extend stockDefaults**

In `src/midi/FCB1010Mapping.cpp`, change the loop bound from 10 to 14:

```cpp
    for (int i = 0; i < 14; ++i) m.programChangeToScene_[i] = i;
```

- [ ] **Step 4: Build + tests pass**

- [ ] **Step 5: Commit**

```bash
git add src/midi/FCB1010Mapping.cpp tests/unit/midi/test_fcb1010_mapping.cpp
git commit -m "feat(fcb1010): extend stock PC mapping to slots 11/12/13"
```

---

## Task 11: PluginProcessor — `setActiveVoiceIndex` + 50 ms swap fade

**Files:**
- Modify: `src/app/PluginProcessor.h` / `.cpp`
- Test: `tests/integration/test_voice_pack_swap.cpp` (new — minimal smoke test here; full coverage in Task 14)

**Interfaces:**
- Consumes: `Scene::resolvedGspeakPath(int)` from Task 4, `PluginState::activeVoiceIndexByScene` from Task 6.
- Produces: `PluginProcessor::setActiveVoiceIndex(int)` — message-thread entry that updates the persisted state, resolves the new path, re-invokes the existing `tryAutoLoadGspeak_` path, and arms a per-swap output fade.

- [ ] **Step 1: Declare the API**

In `src/app/PluginProcessor.h`, in the `public:` section near the other AI/scene methods, add:

```cpp
    // Message thread. Switch the active voice for the current scene by index
    // into Scene::voicePacks. No-op if the scene has no voicePacks. Triggers
    // a bundle reload and a brief output fade across the handover.
    void setActiveVoiceIndex(int idx);
    int  activeVoiceIndex() const noexcept;
```

In the `private:` section add:

```cpp
    // 50 ms output fade armed by voice-pack swaps.
    std::atomic<int>  voicePackSwapFadeSamples_ {0};
    std::atomic<bool> voicePackSwapFadeArmed_   {false};
    int               voicePackSwapFadeCounter_ = 0;
```

- [ ] **Step 2: Implement in .cpp**

In `src/app/PluginProcessor.cpp`, add the implementation near other scene helpers:

```cpp
int PluginProcessor::activeVoiceIndex() const noexcept {
    const int id = sceneEngine_.getActiveSceneId();
    auto it = stateData_.activeVoiceIndexByScene.find(id);
    if (it != stateData_.activeVoiceIndexByScene.end()) return it->second;
    const auto& s = sceneEngine_.getActiveScene();
    return s.defaultVoiceIndex;
}

void PluginProcessor::setActiveVoiceIndex(int idx) {
    const auto& scene = sceneEngine_.getActiveScene();
    if (scene.voicePacks.empty()) return;
    if (idx < 0) idx = 0;
    if (idx >= static_cast<int>(scene.voicePacks.size()))
        idx = static_cast<int>(scene.voicePacks.size()) - 1;
    const int id = scene.id;
    stateData_.activeVoiceIndexByScene[id] = idx;

    // Arm the 50 ms output fade BEFORE the bundle reload so the audio
    // thread starts ramping immediately.
    voicePackSwapFadeSamples_.store(
        static_cast<int>(0.050 * getSampleRate()),
        std::memory_order_relaxed);
    voicePackSwapFadeArmed_.store(true, std::memory_order_release);

    // Re-use the existing auto-load path. tryAutoLoadGspeak_ reads
    // scene.gspeakPath; temporarily swap in the resolved path.
    auto sceneCopy        = scene;
    sceneCopy.gspeakPath  = scene.resolvedGspeakPath(idx);
    sceneCopy.gspeakAutoLoad = true;
    (void) tryAutoLoadGspeak_(sceneCopy);
}
```

- [ ] **Step 3: Drain the fade in processBlock**

In `src/app/PluginProcessor.cpp`, find `processBlock` and, AFTER `audioGraph_.process(...)` but BEFORE the buffer is returned, add:

```cpp
    if (voicePackSwapFadeArmed_.exchange(false, std::memory_order_acquire)) {
        voicePackSwapFadeCounter_ =
            voicePackSwapFadeSamples_.load(std::memory_order_relaxed);
    }
    if (voicePackSwapFadeCounter_ > 0) {
        const int n = buffer.getNumSamples();
        const int total = std::max(1, voicePackSwapFadeCounter_);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
            auto* w = buffer.getWritePointer(ch);
            for (int i = 0; i < n && voicePackSwapFadeCounter_ > 0; ++i) {
                const float g =
                    static_cast<float>(total - voicePackSwapFadeCounter_)
                        / static_cast<float>(total);
                // Symmetric: ramp DOWN for the first half, UP for the second.
                const float halfPos = (total - voicePackSwapFadeCounter_)
                                    / static_cast<float>(total);
                const float gain = halfPos < 0.5f
                    ? 1.0f - 2.0f * halfPos
                    : 2.0f * (halfPos - 0.5f);
                (void) g;  // unused — we use gain.
                w[i] *= gain;
                if (ch == buffer.getNumChannels() - 1)
                    --voicePackSwapFadeCounter_;
            }
        }
    }
```

(This is a triangular dip — drops to 0 at the midpoint of the 50 ms window and recovers — which masks the bundle-handover transient.)

- [ ] **Step 4: Wire up auto-restore on scene activation**

In `PluginProcessor::tryAutoLoadGspeak_`, change the early-return:

```cpp
bool PluginProcessor::tryAutoLoadGspeak_(const scenes::Scene& scene) {
    // Use voice-pack-aware resolved path when set.
    const int idx = activeVoiceIndex();
    const auto resolved = scene.resolvedGspeakPath(idx);
    if (resolved.empty() || !scene.gspeakAutoLoad) return false;
    const auto path = AssetLocator::resolveForRead(resolved);
    // ...rest of existing function (just substitute `path` for the old `scene.gspeakPath`-derived path).
```

Adjust the existing function body to use `resolved` everywhere `scene.gspeakPath` was previously read. Keep the file-existence + error log paths intact.

- [ ] **Step 5: Add a stub integration test that compiles**

Create `tests/integration/test_voice_pack_swap.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "scenes/Scene.h"
#include "scenes/SceneEngine.h"
#include "scenes/SceneLibrary.h"

using guitar_dsp::scenes::Scene;
using guitar_dsp::scenes::SceneEngine;
using guitar_dsp::scenes::SceneLibrary;

TEST_CASE("Scene 11 voicePacks resolve to existing bundle paths",
          "[integration][voice-pack-swap]") {
    auto scenes = SceneLibrary::loadDirectory(
        "/Users/user/GIT/guitar-dsp/assets/scenes");
    const Scene* s11 = nullptr;
    for (auto& s : scenes) if (s.id == 11) s11 = &s;
    REQUIRE(s11 != nullptr);
    REQUIRE(s11->voicePacks.size() == 4);
    for (std::size_t i = 0; i < s11->voicePacks.size(); ++i) {
        const auto p = s11->resolvedGspeakPath(static_cast<int>(i));
        const juce::File abs(
            "/Users/user/GIT/guitar-dsp/" + juce::String(p));
        INFO("voice " << i << " path=" << p);
        REQUIRE(abs.existsAsFile());
    }
}
```

Register it in `tests/CMakeLists.txt` next to other `integration/` files.

- [ ] **Step 6: Build + tests pass**

```bash
cd build-tests && cmake --build . --target guitar_dsp_tests && \
  ctest -R "voice-pack-swap|setActiveVoiceIndex" --output-on-failure
```

(Full RT-safety and audio-side checks come in Task 14.)

- [ ] **Step 7: Commit**

```bash
git add src/app/PluginProcessor.h src/app/PluginProcessor.cpp \
        tests/integration/test_voice_pack_swap.cpp tests/CMakeLists.txt
git commit -m "feat(processor): setActiveVoiceIndex with 50 ms swap fade"
```

---

## Task 12: AudioGraph publishes detected pitch into ClipBankPlayer

**Files:**
- Modify: `src/audio/AudioGraph.cpp` (one-line addition inside `process`)

**Interfaces:**
- Consumes: `ClipBankPlayer::setDetectedPitchHz(float)` (Task 3); `AudioGraph::detectedHz()` (already exists).
- Produces: every block, ClipBankPlayer's anchor-mode selection knows the latest YIN pitch.

- [ ] **Step 1: Locate the publish-detected-pitch site**

In `src/audio/AudioGraph.cpp`, find where `detectedHz_.store(...)` is called (after `pitchCarrier_.process` produces its `State`). Add immediately after that store:

```cpp
        clipBankPlayer_.setDetectedPitchHz(state.freqHz);
```

(Inside the same scope where `state` is in-scope.)

- [ ] **Step 2: Build + existing tests still pass**

```bash
cd build-tests && cmake --build . --target guitar_dsp_tests && \
  ctest --output-on-failure 2>&1 | tail -20
```

Expected: no regression in audio-graph or clip-bank tests.

- [ ] **Step 3: Commit**

```bash
git add src/audio/AudioGraph.cpp
git commit -m "feat(audio-graph): publish detected pitch to ClipBankPlayer for anchor mode"
```

---

## Task 13: VoicePackPicker UI widget + VocoderPanel integration

**Files:**
- Create: `src/app/VoicePackPicker.h` / `.cpp`
- Modify: `src/app/VocoderPanel.h` / `.cpp` to embed it.
- Modify: `src/app/CMakeLists.txt` (or wherever the app sources are listed) to add the new files.

**Interfaces:**
- Consumes: `Scene::voicePacks`, `Scene::showVoicePackPicker`, `PluginProcessor::setActiveVoiceIndex`.
- Produces: a JUCE ComboBox visible when `showVoicePackPicker == true`.

- [ ] **Step 1: Write the widget header**

Create `src/app/VoicePackPicker.h`:

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

namespace guitar_dsp::app {

// Small ComboBox for picking a sung-vowel voice. Populated from
// Scene::voicePacks; emits an integer index on selection change.
class VoicePackPicker : public juce::Component, private juce::ComboBox::Listener {
public:
    VoicePackPicker();
    ~VoicePackPicker() override;

    // Replace the visible list. Pass an empty vector to clear.
    void setPacks(const std::vector<std::pair<std::string, std::string>>& labelPathPairs,
                  int activeIndex);

    // Fired when the user picks a new voice.
    std::function<void(int newIndex)> onChange;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void comboBoxChanged(juce::ComboBox* cb) override;

    juce::Label    label_;
    juce::ComboBox combo_;
};

} // namespace guitar_dsp::app
```

Create `src/app/VoicePackPicker.cpp`:

```cpp
#include "VoicePackPicker.h"

namespace guitar_dsp::app {

VoicePackPicker::VoicePackPicker() {
    label_.setText("Voice", juce::dontSendNotification);
    label_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(label_);
    combo_.addListener(this);
    addAndMakeVisible(combo_);
}

VoicePackPicker::~VoicePackPicker() { combo_.removeListener(this); }

void VoicePackPicker::setPacks(
    const std::vector<std::pair<std::string, std::string>>& labelPathPairs,
    int activeIndex) {
    combo_.clear(juce::dontSendNotification);
    if (labelPathPairs.empty()) {
        setVisible(false);
        return;
    }
    setVisible(true);
    int id = 1;
    for (const auto& [label, path] : labelPathPairs) {
        combo_.addItem(juce::String(label), id++);
    }
    if (activeIndex < 0 || activeIndex >= static_cast<int>(labelPathPairs.size()))
        activeIndex = 0;
    combo_.setSelectedId(activeIndex + 1, juce::dontSendNotification);
}

void VoicePackPicker::resized() {
    auto r = getLocalBounds();
    label_.setBounds(r.removeFromLeft(56));
    combo_.setBounds(r);
}

void VoicePackPicker::paint(juce::Graphics&) {}

void VoicePackPicker::comboBoxChanged(juce::ComboBox* cb) {
    if (cb != &combo_) return;
    const int idx = combo_.getSelectedId() - 1;
    if (idx < 0) return;
    if (onChange) onChange(idx);
}

} // namespace guitar_dsp::app
```

- [ ] **Step 2: Register in app CMakeLists**

In `src/app/CMakeLists.txt`, add `VoicePackPicker.cpp` / `.h` to the source list (next to `VocoderPanel.cpp`).

- [ ] **Step 3: Embed in VocoderPanel**

In `src/app/VocoderPanel.h`, after the existing private members, add:

```cpp
    VoicePackPicker voicePackPicker_;
```

Add `#include "VoicePackPicker.h"` at the top.

In `src/app/VocoderPanel.cpp`:

- Add `addAndMakeVisible(voicePackPicker_);` inside the constructor.
- In `resized()`, allocate ~24 px tall at the top of the panel for the picker (above existing controls).
- Expose a setter the PluginEditor calls when the scene changes:

```cpp
void VocoderPanel::setVoicePacks(
    const std::vector<std::pair<std::string, std::string>>& packs,
    int activeIdx) {
    voicePackPicker_.setPacks(packs, activeIdx);
}

void VocoderPanel::setOnVoicePackChange(std::function<void(int)> cb) {
    voicePackPicker_.onChange = std::move(cb);
}
```

And declare them in the header.

- [ ] **Step 4: Wire from PluginEditor**

In `src/app/PluginEditor.cpp`, in the scene-change handler (or wherever VocoderPanel is updated on scene change), add:

```cpp
    const auto& s = processor_.getActiveScene();
    std::vector<std::pair<std::string, std::string>> packs;
    for (const auto& vp : s.voicePacks) packs.emplace_back(vp.label, vp.path);
    vocoderPanel_.setVoicePacks(packs, processor_.activeVoiceIndex());
    vocoderPanel_.setOnVoicePackChange(
        [this](int idx) { processor_.setActiveVoiceIndex(idx); });
    // Visibility:
    vocoderPanel_.setVisible(s.showVocoder);
```

(If `processor_.getActiveScene()` doesn't exist, use the existing pattern through `sceneEngine_` — match how other panels read the active scene.)

- [ ] **Step 5: Build the standalone app and verify the dropdown appears on scene 11**

```bash
cd build-tests && cmake -S /Users/user/GIT/guitar-dsp -B . && \
  cmake --build . --target guitar_dsp_app 2>&1 | tail -10
```

If the standalone-build target name differs (check `CMakeLists.txt`), use that. Launch it and observe scene 11 — VocoderPanel should show a Voice dropdown.

(For the workflow: don't manually test extensively yet — the user does that after all milestones. Just confirm the panel does not crash on load.)

- [ ] **Step 6: Commit**

```bash
git add src/app/VoicePackPicker.h src/app/VoicePackPicker.cpp \
        src/app/VocoderPanel.h src/app/VocoderPanel.cpp \
        src/app/PluginEditor.cpp src/app/CMakeLists.txt
git commit -m "feat(ui): VoicePackPicker on VocoderPanel, scene 11 4-voice dropdown"
```

---

## Task 14: Integration test — sung_vowels_scene end-to-end

**Files:**
- Create: `tests/integration/test_sung_vowels_scene.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: all earlier tasks.
- Produces: an end-to-end test that loads scene 11's m1 bundle, feeds it a synthetic guitar burst (440 Hz sine + transient), and asserts non-silent, NaN-free wet output.

- [ ] **Step 1: Write the test**

Create `tests/integration/test_sung_vowels_scene.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/AudioGraph.h"
#include "audio/GspeakBundle.h"
#include <cmath>
#include <vector>

using namespace guitar_dsp::audio;

TEST_CASE("Scene 11 sung-vowel bundle drives non-silent wet output",
          "[integration][scene-11]") {
    const double sr = 48000.0;
    const int blk  = 256;
    AudioGraph g;
    g.prepare(sr, blk);

    // Load the m1 bundle directly into the clip bank as grain clips.
    juce::File bundle(
        "/Users/user/GIT/guitar-dsp/assets/clips/gspeak/scene11_sung_m1.gspeak");
    REQUIRE(bundle.existsAsFile());
    auto loaded = GspeakBundle::read(bundle, sr);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->clip);

    // Split the master clip into per-grain sub-clips, copying metadata
    // from each phoneme entry (the bundle's grain identity is one
    // phoneme per grain).
    std::vector<TTSClipPtr> bank;
    for (const auto& p : loaded->clip->phonemes) {
        auto sub = std::make_shared<TTSClip>();
        sub->sampleRate = loaded->clip->sampleRate;
        sub->samples.assign(
            loaded->clip->samples.begin() + p.startSample,
            loaded->clip->samples.begin() + p.endSample);
        sub->bankKey       = p.label.empty() ? std::string("sung_") + p.label : "";
        // The reader populated the clip-level metadata from phoneme 0;
        // for multi-grain bundles we need per-phoneme. Use po's stored
        // bankKey: it survived through the reader, but only on phoneme 0.
        // For this test, set bankKey from the label.
        if (p.label == "a") sub->bankKey = "sung_ah";
        if (p.label == "e") sub->bankKey = "sung_eh";
        if (p.label == "i") sub->bankKey = "sung_ee";
        if (p.label == "o") sub->bankKey = "sung_oh";
        if (p.label == "u") sub->bankKey = "sung_oo";
        // anchorPitchHz lives on the phoneme entry only on phoneme 0; for
        // the integration test we trust the slot index → anchor mapping is
        // OK to leave 0 (picker falls back to first-of-key).
        bank.push_back(std::const_pointer_cast<const TTSClip>(sub));
    }
    g.clipBankPlayer().setBank(bank);
    g.setModulatorSource(AudioGraph::ModulatorSource::ClipBank);
    g.setWetSource(AudioGraph::WetSource::Vocoder);

    // Drain the bank-swap flag.
    std::vector<float> in(blk, 0.0f), out(blk, 0.0f);
    g.process(in.data(), out.data(), blk);

    // Synthetic guitar: 440 Hz sine + sharp transient on sample 0.
    for (int b = 0; b < 20; ++b) {
        for (int i = 0; i < blk; ++i) {
            const double t = (b * blk + i) / sr;
            in[i] = 0.4f * std::sin(2.0 * M_PI * 440.0 * t);
        }
        if (b == 5) in[0] += 0.8f;  // strike
        g.process(in.data(), out.data(), blk);
    }

    // Assert non-NaN, non-silent output after the strike.
    double energy = 0.0;
    int    nonzero_blocks = 0;
    for (int b = 0; b < 10; ++b) {
        in[0] += 0.8f;  // periodic strikes to keep onset detector active
        g.process(in.data(), out.data(), blk);
        for (float v : out) {
            REQUIRE(! std::isnan(v));
            REQUIRE(! std::isinf(v));
            energy += static_cast<double>(v) * v;
            if (std::fabs(v) > 1e-4) ++nonzero_blocks;
        }
    }
    INFO("post-strike energy = " << energy);
    CHECK(energy > 1e-3);
    CHECK(nonzero_blocks > 0);
}
```

- [ ] **Step 2: Register in CMakeLists**

`tests/CMakeLists.txt`: add `integration/test_sung_vowels_scene.cpp` next to other integration tests.

- [ ] **Step 3: Build + test passes**

```bash
cd build-tests && cmake -S /Users/user/GIT/guitar-dsp -B . && \
  cmake --build . --target guitar_dsp_tests && \
  ctest -R "scene-11" --output-on-failure
```

- [ ] **Step 4: Commit**

```bash
git add tests/integration/test_sung_vowels_scene.cpp tests/CMakeLists.txt
git commit -m "test(integration): scene 11 sung-vowels end-to-end"
```

---

## Task 15: Extend scene-switch integration test to PC 0..13

**Files:**
- Modify: `tests/integration/test_scene_switch.cpp`

- [ ] **Step 1: Replace the existing single-PC assertion with a full cycle**

Replace the body of the test in `tests/integration/test_scene_switch.cpp` with:

```cpp
TEST_CASE("integration: PC 0..13 each activate the corresponding scene id",
          "[integration][scenes][midi]") {
    using guitar_dsp::scenes::SceneLibrary;
    using guitar_dsp::scenes::SceneEngine;
    using guitar_dsp::midi::FCB1010Mapping;
    using guitar_dsp::midi::SceneCommandType;

    auto scenes = SceneLibrary::loadDirectory(
        "/Users/user/GIT/guitar-dsp/assets/scenes");
    REQUIRE(!scenes.empty());

    SceneEngine engine;
    engine.loadScenes(std::move(scenes));
    auto mapping = FCB1010Mapping::stockDefaults();

    for (int pc = 0; pc < 14; ++pc) {
        const auto msg = juce::MidiMessage::programChange(1, pc);
        auto cmd = mapping.translate(msg);
        REQUIRE(cmd.has_value());
        REQUIRE(cmd->type == SceneCommandType::ActivateScene);
        const bool ok = engine.activateScene(cmd->payload);
        if (! ok) continue;  // Skip undefined slots (12 doesn't exist until M2).
        REQUIRE(engine.getActiveSceneId() == pc);
    }
}
```

- [ ] **Step 2: Build + test passes**

- [ ] **Step 3: Commit**

```bash
git add tests/integration/test_scene_switch.cpp
git commit -m "test(integration): cycle PC 0..13 through scene engine"
```

---

## Task 16: Extend realtime-safety test to include scene 11

**Files:**
- Modify: `tests/integration/test_realtime_safety.cpp`

- [ ] **Step 1: Identify the scene-list under test**

Search the file for whatever sweep of scenes it currently exercises. Add scene 11 explicitly. If the test cycles "all scenes from the asset dir," it already covers scene 11 — in which case just ensure it loads bundle m1 and runs.

If the test does NOT auto-iterate, add a test case:

```cpp
TEST_CASE("RT-safety: scene 11 produces no allocations on the audio thread",
          "[integration][realtime-safety][scene-11]") {
    using guitar_dsp::scenes::SceneLibrary;
    using guitar_dsp::scenes::SceneEngine;

    auto scenes = SceneLibrary::loadDirectory(
        "/Users/user/GIT/guitar-dsp/assets/scenes");
    SceneEngine engine;
    engine.loadScenes(std::move(scenes));
    REQUIRE(engine.activateScene(11));

    // Use whatever existing RT-safety sentinel API this file already uses
    // — same shape as the existing scene-N RT tests in this file.
    // Pseudocode (replace with the file's actual pattern):
    //   AllocationSentinel s;
    //   AudioGraph g; g.prepare(48000.0, 256);
    //   for (int b = 0; b < 100; ++b) {
    //       std::vector<float> in(256, 0.1f), out(256, 0.0f);
    //       g.process(in.data(), out.data(), 256);
    //   }
    //   CHECK(s.allocations() == 0);
}
```

Look at the existing tests in the same file for the sentinel API; replicate that exact pattern.

- [ ] **Step 2: Build + test passes**

- [ ] **Step 3: Commit**

```bash
git add tests/integration/test_realtime_safety.cpp
git commit -m "test(integration): RT-safety covers scene 11"
```

---

## Task 17: Final M1 verification sweep

- [ ] **Step 1: Clean rebuild from scratch**

```bash
rm -rf build-tests && cmake -S /Users/user/GIT/guitar-dsp -B build-tests && \
  cmake --build build-tests --target guitar_dsp_tests
```

Expected: clean build, no new warnings.

- [ ] **Step 2: Run the full suite**

```bash
cd build-tests && ctest --output-on-failure 2>&1 | tail -30
```

Expected: 100% pass, including all M1-new tests and all pre-existing tests.

- [ ] **Step 3: Build standalone**

Identify the standalone target (look in `src/app/CMakeLists.txt` for an `add_executable` or `juce_add_gui_app` call) and build it:

```bash
cmake --build build-tests --target <standalone_target_name>
```

Launch once to confirm no crash on insertion (scene 13 = Bypass).

- [ ] **Step 4: Sanity-spotcheck a manual scene cycle**

In the launched app, send PC 0..13 via a virtual MIDI port (or use the SceneIndicator UI). Confirm each PC activates the expected scene, scene 11 shows the VoicePackPicker, and the dropdown contains 4 voices. No crash.

- [ ] **Step 5: Move on to M1.5**

M1 is complete. Proceed to [docs/superpowers/plans/2026-06-23-m1-5-world-shifter-validation.md](2026-06-23-m1-5-world-shifter-validation.md).

---

## Notes for the executor

- **If a step's expected command output differs:** check the previous step before assuming the plan is wrong. Often the difference is just a build-cache state, not a real failure.
- **If `cmake -S … -B build-tests` reruns succeed but the new tests aren't discovered:** re-run cmake configuration (`cmake -B build-tests`) — the test list is generated at configure time from `tests/CMakeLists.txt`.
- **The bundle builder writes binary files into the repo.** Each `.gspeak` is 3–6 MB. That's fine for this project (already commits 22 MB of source WAVs).
- **VoicePackPicker visibility:** the picker hides itself when given an empty list; existing scenes (which don't set `showVoicePackPicker`) will see it hidden naturally.
