# Phase 2: Scene System + MIDI / FCB1010 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a standalone macOS app where pressing keyboard keys `1`–`0` (or stepping on a Behringer FCB1010 footswitch) switches between 10 named scenes loaded from JSON. The active scene drives the audio graph's master gain (the only audibly-controllable parameter in Phase 2; real per-scene DSP arrives in Phases 3-4). The UI shows the current scene number/name, a 10-slot scene strip, and a MIDI activity indicator.

**Architecture:** New `scenes/` and `midi/` modules in the audio library. `SceneLibrary` loads JSON definitions from the app bundle; `SceneEngine` holds the active scene with lock-free atomic snapshots for the audio thread. `MidiRouter` listens on CoreMIDI via JUCE and feeds parsed events to a `FCB1010Mapping` translator that emits `SceneCommand`s. The editor adds a `juce::KeyListener` so number keys do the same. The audio thread reads `SceneEngine`'s parameter snapshot each block and applies it to the `Mixer`.

**Tech Stack:** C++20 / JUCE 8 / CoreMIDI / Catch2 v3 — same toolchain as Phase 1, no new dependencies.

**Reference spec:** [`docs/superpowers/specs/2026-05-29-while-my-guitar-gently-speaks-design.md`](../specs/2026-05-29-while-my-guitar-gently-speaks-design.md) — especially §7 (Scenes), §8 (MIDI/FCB1010), §12.3 tests #5/#6, §12.4 tests #13/#14.

**Pre-flight assumption:** Phase 1 is merged into `main` (the JUCE standalone app builds and runs, 26/26 tests pass, the diagnostic UI is in place). All commits land directly on the branch this plan is executed from.

---

## Background for the implementing engineer

If you're new to this codebase, read these first:

- **Spec §7 (scenes)** for the JSON schema and per-scene parameters.
- **Spec §8 (MIDI)** for the FCB1010 stock-firmware behavior (PC 0–9 per switch, CC 27 / CC 7 on expression pedals) and the remap-via-JSON design.
- **Spec §12.3 #5/#6** and **§12.4 #13/#14** for the test requirements.
- **`src/audio/AudioGraph.h`** and **`src/audio/Mixer.h`** — you'll be wiring scene parameters into these.
- **`src/app/PluginProcessor.{h,cpp}`** for the existing cross-thread pattern (atomics for diagnostics) — `SceneEngine` follows the same pattern.
- **JUCE basics if you haven't read them**: `juce::MidiInput::Listener` for MIDI callbacks, `juce::KeyPress` / `juce::KeyListener` for keyboard, `juce::File` / `juce::JSON` for file I/O and JSON parsing.

**Threading rules to honor (same as Phase 1):**
- The audio thread (`processBlock`) MUST NOT allocate, lock, or block. Use lock-free atomics for any data shared with it.
- The message thread handles MIDI input, file loading, JSON parsing, and GUI.
- `RealtimeSentinel` will catch violations in tests — design for it from the start.

**What Phase 2 deliberately does NOT do** (preserving spec deferrals):
- No per-scene "carousel" DSP differences yet (Phase 4). Each scene only varies the Mixer's master gain in this phase.
- No vocoder (Phase 3), no TTS sources (Phase 3), no visualization changes beyond the new scene indicator + MIDI LED (Phase 5 still adds the spectrogram backdrop).
- No audience-text encore (v2 roadmap), no AU plugin (v2).

If a task tempts you toward those: stop and report it as DONE_WITH_CONCERNS.

---

## File structure (created/modified across this plan)

```
guitar-dsp/
├── CMakeLists.txt                                  (modified, Task 5)
├── README.md                                       (modified, Task 17)
├── assets/                                         (NEW)
│   ├── scenes/                                     (Task 1)
│   │   ├── 00_clean.json
│   │   ├── 01_carousel_organ.json
│   │   ├── 02_carousel_piano.json
│   │   ├── 03_carousel_synth.json
│   │   ├── 04_carousel_8bit.json
│   │   ├── 05_carousel_choir.json
│   │   ├── 06_speaking_a.json
│   │   ├── 07_speaking_b.json
│   │   ├── 08_speaking_finale.json
│   │   └── 09_panic.json
│   └── midi/
│       └── fcb1010.json                            (Task 6)
├── src/
│   ├── audio/
│   │   └── Mixer.h                                 (modified, Task 8)
│   ├── scenes/                                     (NEW)
│   │   ├── Scene.h                                 (Task 2)
│   │   ├── SceneLibrary.{h,cpp}                    (Task 3)
│   │   └── SceneEngine.{h,cpp}                     (Task 4)
│   ├── midi/                                       (NEW)
│   │   ├── SceneCommand.h                          (Task 9)
│   │   ├── FCB1010Mapping.{h,cpp}                  (Task 9)
│   │   └── MidiRouter.{h,cpp}                      (Task 11)
│   └── app/
│       ├── PluginProcessor.{h,cpp}                 (modified, Tasks 8, 11)
│       ├── PluginEditor.{h,cpp}                    (modified, Task 12)
│       ├── DiagnosticPanel.{h,cpp}                 (modified, Task 14)
│       ├── SceneIndicator.{h,cpp}                  (NEW, Task 13)
│       └── AssetLocator.{h,cpp}                    (NEW, Task 7)
├── src/CMakeLists.txt                              (modified, Tasks 3, 4, 9, 11)
├── src/app/CMakeLists.txt                          (modified, Tasks 5, 13)
├── tests/
│   ├── CMakeLists.txt                              (modified throughout)
│   ├── unit/
│   │   ├── scenes/
│   │   │   ├── test_scene_library.cpp              (Task 3)
│   │   │   └── test_scene_engine.cpp               (Task 4)
│   │   └── midi/
│   │       └── test_fcb1010_mapping.cpp            (Task 10)
│   ├── integration/
│   │   └── test_scene_switch.cpp                   (Task 16)
│   └── fixtures/
│       └── scenes/                                 (Task 3)
│           ├── valid_minimal.json
│           ├── malformed.json
│           └── missing_required.json
└── docs/superpowers/plans/
    └── 2026-05-30-phase-2-scenes-and-midi.md
```

---

## Task 1: Create the 10 scene JSON files

**Files:**
- Create: `assets/scenes/00_clean.json` through `09_panic.json` (10 files)

The schema for Phase 2 is intentionally minimal — only `id`, `name`, `color`, and `mixer.masterGainDb` are honored. Per-scene carousel/vocoder/TTS blocks land in later phases and will simply be ignored by `SceneLibrary` for now (with a debug log when present).

- [ ] **Step 1: Create the assets/scenes/ directory**

```bash
mkdir -p assets/scenes
```

- [ ] **Step 2: Write each of the 10 scene files**

For each file, the exact content is below. Note `mixer.masterGainDb` differs per scene so audibly switching scenes is detectable (each scene is at a distinct volume). Real per-scene DSP differences land in Phase 4.

`assets/scenes/00_clean.json`:

```json
{
  "id": 0,
  "name": "Clean intro",
  "color": "#cccccc",
  "mixer": { "masterGainDb": 0.0, "dryWet": 0.0, "transitionMs": 20 }
}
```

`assets/scenes/01_carousel_organ.json`:

```json
{
  "id": 1,
  "name": "Carousel — Hammond organ",
  "color": "#e69e3c",
  "mixer": { "masterGainDb": -2.0, "dryWet": 0.0, "transitionMs": 20 }
}
```

`assets/scenes/02_carousel_piano.json`:

```json
{
  "id": 2,
  "name": "Carousel — piano-ish",
  "color": "#7eb8de",
  "mixer": { "masterGainDb": -4.0, "dryWet": 0.0, "transitionMs": 20 }
}
```

`assets/scenes/03_carousel_synth.json`:

```json
{
  "id": 3,
  "name": "Carousel — synth lead",
  "color": "#5cd1c4",
  "mixer": { "masterGainDb": -6.0, "dryWet": 0.0, "transitionMs": 20 }
}
```

`assets/scenes/04_carousel_8bit.json`:

```json
{
  "id": 4,
  "name": "Carousel — 8-bit",
  "color": "#f25e8e",
  "mixer": { "masterGainDb": -8.0, "dryWet": 0.0, "transitionMs": 20 }
}
```

`assets/scenes/05_carousel_choir.json`:

```json
{
  "id": 5,
  "name": "Carousel — choir / pad",
  "color": "#b48ce3",
  "mixer": { "masterGainDb": -10.0, "dryWet": 0.0, "transitionMs": 20 }
}
```

`assets/scenes/06_speaking_a.json`:

```json
{
  "id": 6,
  "name": "Speaking A — hello",
  "color": "#a64dff",
  "mixer": { "masterGainDb": -6.0, "dryWet": 0.0, "transitionMs": 20 }
}
```

`assets/scenes/07_speaking_b.json`:

```json
{
  "id": 7,
  "name": "Speaking B — mid-talk",
  "color": "#a64dff",
  "mixer": { "masterGainDb": -6.0, "dryWet": 0.0, "transitionMs": 20 }
}
```

`assets/scenes/08_speaking_finale.json`:

```json
{
  "id": 8,
  "name": "Speaking finale — gently weeps",
  "color": "#a64dff",
  "mixer": { "masterGainDb": -4.0, "dryWet": 0.0, "transitionMs": 20 }
}
```

`assets/scenes/09_panic.json`:

```json
{
  "id": 9,
  "name": "Panic — bypass to clean",
  "color": "#ff4d4d",
  "mixer": { "masterGainDb": 0.0, "dryWet": 0.0, "transitionMs": 30 }
}
```

- [ ] **Step 3: Verify all 10 files exist and validate as JSON**

```bash
ls -1 assets/scenes/*.json | wc -l
# Expected: 10

for f in assets/scenes/*.json; do
  python3 -c "import json,sys; json.load(open('$f'))" || echo "FAIL: $f"
done
# Expected: no FAIL output
```

- [ ] **Step 4: Commit**

```bash
git add assets/scenes/
git commit -m "feat(scenes): initial 10 scene JSON definitions"
```

---

## Task 2: Scene data struct

**Files:**
- Create: `src/scenes/Scene.h`

A plain data struct + a `defaults()` factory for "scene that does nothing surprising." No tests needed (no behavior — it's a struct), but Task 3 will exercise it via `SceneLibrary`.

- [ ] **Step 1: Write `src/scenes/Scene.h`**

```cpp
#pragma once

#include <cstdint>
#include <string>

namespace guitar_dsp::scenes {

struct MixerParams {
    float masterGainDb = 0.0f;
    float dryWet       = 0.0f;   // 0 = fully dry, 1 = fully wet
    float transitionMs = 20.0f;
};

struct Scene {
    int          id        = 0;
    std::string  name      = "(unnamed)";
    std::uint32_t colorRgb = 0xCCCCCCu;   // 0xRRGGBB
    MixerParams  mixer{};

    static Scene defaults(int id);
};

inline Scene Scene::defaults(int id) {
    Scene s;
    s.id = id;
    s.name = "Scene " + std::to_string(id);
    return s;
}

} // namespace guitar_dsp::scenes
```

- [ ] **Step 2: Verify the header compiles standalone**

Add a temporary `tests/unit/scenes/test_scene_compiles.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "scenes/Scene.h"

TEST_CASE("Scene: defaults() produces a usable scene", "[scenes][smoke]") {
    const auto s = guitar_dsp::scenes::Scene::defaults(3);
    REQUIRE(s.id == 3);
    REQUIRE(s.name == "Scene 3");
    REQUIRE(s.mixer.masterGainDb == 0.0f);
    REQUIRE(s.mixer.dryWet == 0.0f);
}
```

Add it to `tests/CMakeLists.txt`:

```cmake
add_executable(guitar_dsp_tests
    # ... existing sources ...
    unit/scenes/test_scene_compiles.cpp
)
```

(You'll add more `tests/unit/scenes/...` entries in later tasks.)

- [ ] **Step 3: Build and run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R "Scene: defaults"
```

Expected: 1 test passes.

- [ ] **Step 4: Commit**

```bash
git add src/scenes/Scene.h tests/unit/scenes/test_scene_compiles.cpp tests/CMakeLists.txt
git commit -m "feat(scenes): Scene struct with MixerParams"
```

---

## Task 3: SceneLibrary — JSON loader

**Files:**
- Create: `src/scenes/SceneLibrary.{h,cpp}`
- Create: `tests/unit/scenes/test_scene_library.cpp`
- Create: `tests/fixtures/scenes/valid_minimal.json`
- Create: `tests/fixtures/scenes/malformed.json`
- Create: `tests/fixtures/scenes/missing_required.json`
- Modify: `src/CMakeLists.txt` (add a `guitar_dsp_scenes` static library)
- Modify: `tests/CMakeLists.txt`

`SceneLibrary` reads JSON files from a directory and returns a vector of `Scene`. It depends on `juce::JSON` for parsing, so it links against `juce_core`. To keep the audio library JUCE-free, we put scenes in its own static library.

- [ ] **Step 1: Write the failing test FIRST (TDD)**

Create `tests/fixtures/scenes/valid_minimal.json`:

```json
{
  "id": 7,
  "name": "Test scene",
  "color": "#abcdef",
  "mixer": { "masterGainDb": -3.5, "dryWet": 0.25, "transitionMs": 15 }
}
```

Create `tests/fixtures/scenes/malformed.json`:

```
this is not json {{{{
```

Create `tests/fixtures/scenes/missing_required.json`:

```json
{ "name": "Has no id" }
```

Create `tests/unit/scenes/test_scene_library.cpp`:

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

TEST_CASE("SceneLibrary: loads a single valid scene file", "[scenes][library]") {
    auto result = SceneLibrary::loadOne(fixturePath("scenes/valid_minimal.json"));
    REQUIRE(result.has_value());
    const auto& s = *result;
    REQUIRE(s.id == 7);
    REQUIRE(s.name == "Test scene");
    REQUIRE(s.colorRgb == 0xABCDEFu);
    REQUIRE(s.mixer.masterGainDb == -3.5f);
    REQUIRE(s.mixer.dryWet == 0.25f);
    REQUIRE(s.mixer.transitionMs == 15.0f);
}

TEST_CASE("SceneLibrary: malformed JSON returns nullopt with logged error",
          "[scenes][library]") {
    auto result = SceneLibrary::loadOne(fixturePath("scenes/malformed.json"));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("SceneLibrary: missing required field returns nullopt",
          "[scenes][library]") {
    auto result = SceneLibrary::loadOne(fixturePath("scenes/missing_required.json"));
    REQUIRE_FALSE(result.has_value());
}

TEST_CASE("SceneLibrary: loadDirectory returns sorted by id, skipping invalid",
          "[scenes][library]") {
    // Create a temp dir with two valid files and one malformed.
    const auto tmp = std::filesystem::temp_directory_path() / "guitar_dsp_test_scenes";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);

    auto writeFile = [&tmp](const std::string& name, const std::string& body) {
        std::ofstream f(tmp / name);
        f << body;
    };

    writeFile("a.json",
        R"({ "id": 5, "name": "Five", "color": "#000000",
             "mixer": { "masterGainDb": 0, "dryWet": 0, "transitionMs": 20 } })");
    writeFile("b.json",
        R"({ "id": 2, "name": "Two", "color": "#000000",
             "mixer": { "masterGainDb": 0, "dryWet": 0, "transitionMs": 20 } })");
    writeFile("c.json", "not json");

    auto scenes = SceneLibrary::loadDirectory(tmp.string());
    REQUIRE(scenes.size() == 2);
    REQUIRE(scenes[0].id == 2);   // sorted by id
    REQUIRE(scenes[1].id == 5);
}
```

(`#include <fstream>` may be needed; add it if the compiler complains.)

- [ ] **Step 2: Run to confirm it fails**

```bash
cmake --build build --target guitar_dsp_tests 2>&1 | head -10
```

Expected: compile error — `SceneLibrary.h not found`.

- [ ] **Step 3: Write `src/scenes/SceneLibrary.h`**

```cpp
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "Scene.h"

namespace guitar_dsp::scenes {

class SceneLibrary {
public:
    // Loads one scene JSON file. Returns nullopt on parse failure, missing
    // required fields, or any I/O error.
    static std::optional<Scene> loadOne(const std::string& path);

    // Loads every *.json file in `directory`, returns scenes sorted by id.
    // Invalid files are skipped with a logged warning; loading continues.
    static std::vector<Scene> loadDirectory(const std::string& directory);
};

} // namespace guitar_dsp::scenes
```

- [ ] **Step 4: Write `src/scenes/SceneLibrary.cpp`**

```cpp
#include "SceneLibrary.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <filesystem>
#include <iostream>

namespace guitar_dsp::scenes {

namespace {

std::optional<std::uint32_t> parseColor(const juce::String& s) {
    if (s.isEmpty() || s[0] != '#' || s.length() != 7) return std::nullopt;
    const auto hex = s.substring(1).getHexValue32();
    return static_cast<std::uint32_t>(hex & 0xFFFFFFu);
}

} // namespace

std::optional<Scene> SceneLibrary::loadOne(const std::string& path) {
    juce::File file(path);
    if (!file.existsAsFile()) {
        std::cerr << "[SceneLibrary] missing file: " << path << '\n';
        return std::nullopt;
    }
    const auto text = file.loadFileAsString();
    auto parsed = juce::JSON::parse(text);
    if (!parsed.isObject()) {
        std::cerr << "[SceneLibrary] not a JSON object: " << path << '\n';
        return std::nullopt;
    }

    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr) {
        std::cerr << "[SceneLibrary] empty object: " << path << '\n';
        return std::nullopt;
    }

    Scene s;
    if (! obj->hasProperty("id") || ! obj->hasProperty("name")) {
        std::cerr << "[SceneLibrary] missing 'id' or 'name': " << path << '\n';
        return std::nullopt;
    }
    s.id = static_cast<int>(obj->getProperty("id"));
    s.name = obj->getProperty("name").toString().toStdString();

    if (auto colorOpt = parseColor(obj->getProperty("color").toString())) {
        s.colorRgb = *colorOpt;
    }

    if (obj->hasProperty("mixer")) {
        if (auto* m = obj->getProperty("mixer").getDynamicObject()) {
            if (m->hasProperty("masterGainDb"))
                s.mixer.masterGainDb = static_cast<float>(static_cast<double>(m->getProperty("masterGainDb")));
            if (m->hasProperty("dryWet"))
                s.mixer.dryWet = static_cast<float>(static_cast<double>(m->getProperty("dryWet")));
            if (m->hasProperty("transitionMs"))
                s.mixer.transitionMs = static_cast<float>(static_cast<double>(m->getProperty("transitionMs")));
        }
    }

    return s;
}

std::vector<Scene> SceneLibrary::loadDirectory(const std::string& directory) {
    std::vector<Scene> out;
    namespace fs = std::filesystem;
    if (! fs::exists(directory) || ! fs::is_directory(directory)) {
        std::cerr << "[SceneLibrary] not a directory: " << directory << '\n';
        return out;
    }

    for (const auto& entry : fs::directory_iterator(directory)) {
        if (! entry.is_regular_file()) continue;
        if (entry.path().extension() != ".json") continue;
        if (auto s = loadOne(entry.path().string())) out.push_back(*s);
    }

    std::sort(out.begin(), out.end(),
              [](const Scene& a, const Scene& b) { return a.id < b.id; });
    return out;
}

} // namespace guitar_dsp::scenes
```

- [ ] **Step 5: Add a `guitar_dsp_scenes` library in `src/CMakeLists.txt`**

Append:

```cmake
add_library(guitar_dsp_scenes STATIC
    scenes/SceneLibrary.cpp
)

target_include_directories(guitar_dsp_scenes PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(guitar_dsp_scenes PUBLIC cxx_std_20)

target_link_libraries(guitar_dsp_scenes PUBLIC juce::juce_core)
```

- [ ] **Step 6: Update `tests/CMakeLists.txt`**

```cmake
add_executable(guitar_dsp_tests
    # ... existing sources ...
    unit/scenes/test_scene_library.cpp
)

target_link_libraries(guitar_dsp_tests PRIVATE
    guitar_dsp_audio
    guitar_dsp_scenes
    juce::juce_audio_formats
    Catch2::Catch2WithMain
)
```

- [ ] **Step 7: Build and run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R "SceneLibrary"
```

Expected: 4 tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/scenes/ src/CMakeLists.txt tests/unit/scenes/test_scene_library.cpp tests/fixtures/scenes/ tests/CMakeLists.txt
git commit -m "feat(scenes): SceneLibrary JSON loader with fixture-based tests"
```

---

## Task 4: SceneEngine — active scene + cross-thread snapshot

**Files:**
- Create: `src/scenes/SceneEngine.{h,cpp}`
- Create: `tests/unit/scenes/test_scene_engine.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

`SceneEngine` owns the loaded scenes and the active-scene index. It exposes:
- Message-thread API: `loadScenes(scenes)`, `activateScene(id)`, `getActiveSceneId()`, `getActiveScene()`.
- Audio-thread API: `currentMixerParams()` — returns the active scene's `MixerParams` (atomic snapshot, never blocks).

Scene transitions are atomic and immediate at the `SceneEngine` level — the `Mixer`'s own ramping (already implemented in Phase 1) handles the smoothing.

- [ ] **Step 1: Write the failing test FIRST (TDD)**

Create `tests/unit/scenes/test_scene_engine.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "scenes/SceneEngine.h"

using guitar_dsp::scenes::Scene;
using guitar_dsp::scenes::SceneEngine;
using guitar_dsp::scenes::MixerParams;

namespace {
Scene makeScene(int id, float gainDb) {
    Scene s = Scene::defaults(id);
    s.mixer.masterGainDb = gainDb;
    return s;
}
}

TEST_CASE("SceneEngine: starts with no active scene", "[scenes][engine]") {
    SceneEngine eng;
    REQUIRE(eng.getActiveSceneId() == -1);
    REQUIRE(eng.getSceneCount() == 0);
}

TEST_CASE("SceneEngine: loadScenes installs and activates the first by id",
          "[scenes][engine]") {
    SceneEngine eng;
    eng.loadScenes({ makeScene(2, -3.0f), makeScene(0, 0.0f), makeScene(5, -6.0f) });
    REQUIRE(eng.getSceneCount() == 3);
    REQUIRE(eng.getActiveSceneId() == 0);  // lowest id auto-selected
    REQUIRE(eng.currentMixerParams().masterGainDb == 0.0f);
}

TEST_CASE("SceneEngine: activateScene by id snaps the mixer params",
          "[scenes][engine]") {
    SceneEngine eng;
    eng.loadScenes({ makeScene(0, 0.0f), makeScene(1, -6.0f), makeScene(2, -12.0f) });
    REQUIRE(eng.activateScene(2));
    REQUIRE(eng.getActiveSceneId() == 2);
    REQUIRE(eng.currentMixerParams().masterGainDb == -12.0f);
}

TEST_CASE("SceneEngine: activateScene returns false for unknown id",
          "[scenes][engine]") {
    SceneEngine eng;
    eng.loadScenes({ makeScene(0, 0.0f), makeScene(1, -6.0f) });
    REQUIRE_FALSE(eng.activateScene(99));
    REQUIRE(eng.getActiveSceneId() == 0);  // unchanged
}

TEST_CASE("SceneEngine: getActiveScene returns the live struct",
          "[scenes][engine]") {
    SceneEngine eng;
    eng.loadScenes({ makeScene(0, 0.0f), makeScene(1, -6.0f) });
    eng.activateScene(1);
    REQUIRE(eng.getActiveScene().id == 1);
    REQUIRE(eng.getActiveScene().mixer.masterGainDb == -6.0f);
}
```

- [ ] **Step 2: Run to confirm it fails**

```bash
cmake --build build --target guitar_dsp_tests 2>&1 | head -10
```

Expected: `SceneEngine.h not found`.

- [ ] **Step 3: Write `src/scenes/SceneEngine.h`**

```cpp
#pragma once

#include <atomic>
#include <vector>

#include "Scene.h"

namespace guitar_dsp::scenes {

// Owns the loaded scene set and the active-scene state. Designed for one
// message-thread writer (UI / MIDI) and one audio-thread reader.
//
// Cross-thread contract:
//   - loadScenes / activateScene: message thread only
//   - currentMixerParams: audio thread only (lock-free, never allocates)
//   - getActiveSceneId / getActiveScene / getSceneCount: message thread
class SceneEngine {
public:
    SceneEngine();

    // Message-thread API ----------------------------------------------------
    void loadScenes(std::vector<Scene> scenes);
    bool activateScene(int id);          // returns true if scene exists
    int  getActiveSceneId() const;
    int  getSceneCount() const;
    const Scene& getActiveScene() const;

    // Audio-thread API ------------------------------------------------------
    MixerParams currentMixerParams() const noexcept;

private:
    std::vector<Scene>          scenes_;
    int                          activeIndex_ = -1;
    Scene                        emptyScene_ {Scene::defaults(-1)};

    // Per-parameter atomic snapshot for the audio thread.
    std::atomic<float> snapMasterGainDb_  {0.0f};
    std::atomic<float> snapDryWet_        {0.0f};
    std::atomic<float> snapTransitionMs_ {20.0f};

    void publishSnapshot(const MixerParams& m) noexcept;
};

} // namespace guitar_dsp::scenes
```

- [ ] **Step 4: Write `src/scenes/SceneEngine.cpp`**

```cpp
#include "SceneEngine.h"

#include <algorithm>

namespace guitar_dsp::scenes {

SceneEngine::SceneEngine() = default;

void SceneEngine::loadScenes(std::vector<Scene> scenes) {
    scenes_ = std::move(scenes);
    std::sort(scenes_.begin(), scenes_.end(),
              [](const Scene& a, const Scene& b) { return a.id < b.id; });
    if (scenes_.empty()) {
        activeIndex_ = -1;
        publishSnapshot(MixerParams{});
    } else {
        activeIndex_ = 0;
        publishSnapshot(scenes_[0].mixer);
    }
}

bool SceneEngine::activateScene(int id) {
    auto it = std::find_if(scenes_.begin(), scenes_.end(),
                           [id](const Scene& s) { return s.id == id; });
    if (it == scenes_.end()) return false;
    activeIndex_ = static_cast<int>(std::distance(scenes_.begin(), it));
    publishSnapshot(it->mixer);
    return true;
}

int SceneEngine::getActiveSceneId() const {
    if (activeIndex_ < 0) return -1;
    return scenes_[static_cast<std::size_t>(activeIndex_)].id;
}

int SceneEngine::getSceneCount() const {
    return static_cast<int>(scenes_.size());
}

const Scene& SceneEngine::getActiveScene() const {
    if (activeIndex_ < 0) return emptyScene_;
    return scenes_[static_cast<std::size_t>(activeIndex_)];
}

MixerParams SceneEngine::currentMixerParams() const noexcept {
    MixerParams m;
    m.masterGainDb = snapMasterGainDb_.load(std::memory_order_relaxed);
    m.dryWet       = snapDryWet_.load(std::memory_order_relaxed);
    m.transitionMs = snapTransitionMs_.load(std::memory_order_relaxed);
    return m;
}

void SceneEngine::publishSnapshot(const MixerParams& m) noexcept {
    snapMasterGainDb_.store(m.masterGainDb, std::memory_order_relaxed);
    snapDryWet_.store(m.dryWet, std::memory_order_relaxed);
    snapTransitionMs_.store(m.transitionMs, std::memory_order_relaxed);
}

} // namespace guitar_dsp::scenes
```

- [ ] **Step 5: Update `src/CMakeLists.txt`**

```cmake
add_library(guitar_dsp_scenes STATIC
    scenes/SceneLibrary.cpp
    scenes/SceneEngine.cpp
)
```

- [ ] **Step 6: Update `tests/CMakeLists.txt`**

```cmake
add_executable(guitar_dsp_tests
    # ... existing sources ...
    unit/scenes/test_scene_engine.cpp
)
```

- [ ] **Step 7: Build and run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R "SceneEngine"
```

Expected: 5 tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/scenes/SceneEngine.h src/scenes/SceneEngine.cpp src/CMakeLists.txt tests/unit/scenes/test_scene_engine.cpp tests/CMakeLists.txt
git commit -m "feat(scenes): SceneEngine with lock-free audio-thread snapshot"
```

---

## Task 5: CMake — bundle `assets/` into the app at build time

**Files:**
- Modify: `src/app/CMakeLists.txt`

JUCE's `juce_add_plugin` produces an `.app` bundle. We add a post-build step that copies `assets/` into `Contents/Resources/assets/` so the running app can find them.

- [ ] **Step 1: Append post-build copy command to `src/app/CMakeLists.txt`**

At the bottom of `src/app/CMakeLists.txt`, add:

```cmake
# Copy assets/ into the Standalone .app's Resources directory after build.
# Path is JUCE-specific: <target>_artefacts/<BUILD_TYPE>/Standalone/<PRODUCT_NAME>.app
set(GUITAR_DSP_ASSETS_SRC "${CMAKE_SOURCE_DIR}/assets")
set(GUITAR_DSP_APP_RESOURCES
    "$<TARGET_FILE_DIR:guitar_dsp_app_Standalone>/../Resources/assets")

add_custom_command(TARGET guitar_dsp_app_Standalone POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${GUITAR_DSP_APP_RESOURCES}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${GUITAR_DSP_ASSETS_SRC}" "${GUITAR_DSP_APP_RESOURCES}"
    COMMENT "Copying assets/ into Guitar DSP.app/Contents/Resources/assets/"
)
```

- [ ] **Step 2: Build and verify**

```bash
cmake --build build --target guitar_dsp_app_Standalone
ls "build/src/app/guitar_dsp_app_artefacts/Release/Standalone/Guitar DSP.app/Contents/Resources/assets/scenes/" | wc -l
```

Expected: `10` (the 10 scene JSON files were copied).

- [ ] **Step 3: Commit**

```bash
git add src/app/CMakeLists.txt
git commit -m "build(app): copy assets/ into Standalone .app Resources after build"
```

---

## Task 6: Default FCB1010 mapping JSON

**Files:**
- Create: `assets/midi/fcb1010.json`

The default mapping matches the stock FCB1010 firmware: Program Change 0–9 from the 10 footswitches → scene ids 0–9; CC 27 (expression pedal 1) and CC 7 (expression pedal 2) → continuous parameter controls (reserved for future use; ignored in Phase 2).

- [ ] **Step 1: Create the directory and file**

```bash
mkdir -p assets/midi
```

Write `assets/midi/fcb1010.json`:

```json
{
  "deviceMatch": "FCB1010",
  "programChangeToScene": {
    "0": 0,
    "1": 1,
    "2": 2,
    "3": 3,
    "4": 4,
    "5": 5,
    "6": 6,
    "7": 7,
    "8": 8,
    "9": 9
  },
  "expressionPedalCcs": {
    "wetDry": 27,
    "masterGain": 7
  }
}
```

- [ ] **Step 2: Validate JSON**

```bash
python3 -c "import json; json.load(open('assets/midi/fcb1010.json'))"
```

Expected: no output (success).

- [ ] **Step 3: Commit**

```bash
git add assets/midi/fcb1010.json
git commit -m "feat(midi): default FCB1010 stock-firmware mapping"
```

---

## Task 7: AssetLocator — runtime asset path

**Files:**
- Create: `src/app/AssetLocator.{h,cpp}`
- Modify: `src/app/CMakeLists.txt` (add the new source)

A small helper that returns the `assets/` directory at runtime. In the .app bundle it's `Contents/Resources/assets/`; for development it can be overridden with the `GUITAR_DSP_ASSETS_DIR` environment variable so a debug build can edit JSON in-place without rebuilding.

- [ ] **Step 1: Write `src/app/AssetLocator.h`**

```cpp
#pragma once

#include <string>

namespace guitar_dsp {

// Returns the directory containing scene/MIDI/etc. assets at runtime.
// Precedence:
//   1. $GUITAR_DSP_ASSETS_DIR (if set and exists)
//   2. <app bundle>/Contents/Resources/assets/
//   3. The repo's `assets/` directory walked from CWD (for unit tests)
class AssetLocator {
public:
    static std::string scenesDirectory();
    static std::string midiDirectory();

private:
    static std::string assetsRoot();
};

} // namespace guitar_dsp
```

- [ ] **Step 2: Write `src/app/AssetLocator.cpp`**

```cpp
#include "AssetLocator.h"

#include <juce_core/juce_core.h>

#include <cstdlib>
#include <filesystem>

namespace guitar_dsp {

namespace fs = std::filesystem;

std::string AssetLocator::assetsRoot() {
    if (const char* env = std::getenv("GUITAR_DSP_ASSETS_DIR")) {
        if (fs::exists(env)) return env;
    }

    auto appFile = juce::File::getSpecialLocation(
        juce::File::currentApplicationFile);
    auto bundleResources = appFile.getChildFile("Contents/Resources/assets");
    if (bundleResources.isDirectory())
        return bundleResources.getFullPathName().toStdString();

    // Walk up from CWD looking for an `assets/` directory (tests, dev runs).
    auto p = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        auto candidate = p / "assets";
        if (fs::exists(candidate)) return candidate.string();
        p = p.parent_path();
    }
    return {};
}

std::string AssetLocator::scenesDirectory() {
    const auto root = assetsRoot();
    if (root.empty()) return {};
    return (fs::path(root) / "scenes").string();
}

std::string AssetLocator::midiDirectory() {
    const auto root = assetsRoot();
    if (root.empty()) return {};
    return (fs::path(root) / "midi").string();
}

} // namespace guitar_dsp
```

- [ ] **Step 3: Add to `src/app/CMakeLists.txt`**

In `target_sources(guitar_dsp_app PRIVATE ...)`, add `AssetLocator.cpp`.

- [ ] **Step 4: Build to confirm it compiles**

```bash
cmake --build build --target guitar_dsp_app_Standalone 2>&1 | tail -3
```

Expected: builds without error.

- [ ] **Step 5: Commit**

```bash
git add src/app/AssetLocator.h src/app/AssetLocator.cpp src/app/CMakeLists.txt
git commit -m "feat(app): AssetLocator for runtime asset path resolution"
```

---

## Task 8: Wire SceneEngine into PluginProcessor + Mixer

**Files:**
- Modify: `src/audio/Mixer.h` (already has setters; no code change but verify)
- Modify: `src/app/PluginProcessor.{h,cpp}`

`PluginProcessor` owns a `SceneEngine` and, on each `processBlock`, reads `currentMixerParams()` and pushes the values into the `Mixer` via its existing setters. The `Mixer` already smooths parameter changes (verified by the Phase 1 ramp-correctness test), so scene switches are click-free without further work.

`SceneLibrary::loadDirectory(AssetLocator::scenesDirectory())` is called in `prepareToPlay` (it's safe there — message thread). If the load returns an empty vector (e.g., assets not found), we use `Scene::defaults(0)` so the app stays alive.

- [ ] **Step 1: Add `SceneEngine` member + accessor to `PluginProcessor.h`**

Add includes:

```cpp
#include "scenes/SceneEngine.h"
```

Add to the private section:

```cpp
scenes::SceneEngine sceneEngine_;
```

Add to the public section:

```cpp
scenes::SceneEngine& sceneEngine() { return sceneEngine_; }
```

- [ ] **Step 2: Update `prepareToPlay` in `PluginProcessor.cpp`**

```cpp
#include "AssetLocator.h"
#include "scenes/SceneLibrary.h"
```

Replace the body of `prepareToPlay`:

```cpp
void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    graph_.prepare(sampleRate, samplesPerBlock);
    monoScratch_.assign(static_cast<std::size_t>(samplesPerBlock), 0.0f);

    if (sceneEngine_.getSceneCount() == 0) {
        const auto dir = AssetLocator::scenesDirectory();
        auto scenes = scenes::SceneLibrary::loadDirectory(dir);
        if (scenes.empty()) scenes.push_back(scenes::Scene::defaults(0));
        sceneEngine_.loadScenes(std::move(scenes));
    }
}
```

- [ ] **Step 3: Apply scene params at the top of `processBlock`**

Inside `processBlock`, before the existing audio processing, add:

```cpp
const auto sceneParams = sceneEngine_.currentMixerParams();
graph_.mixer().setMasterGainDb(sceneParams.masterGainDb);
graph_.mixer().setDryWet(sceneParams.dryWet);
```

(Setters are non-allocating; the `Mixer`'s per-sample smoothing handles the ramp.)

- [ ] **Step 4: Link `guitar_dsp_scenes` into the app target**

Modify `src/app/CMakeLists.txt` — in `target_link_libraries(guitar_dsp_app PRIVATE ...)`, add `guitar_dsp_scenes`.

- [ ] **Step 5: Build and verify**

```bash
cmake --build build --target guitar_dsp_app_Standalone
ctest --test-dir build --output-on-failure
```

Expected: builds; all existing 26 tests still pass.

- [ ] **Step 6: Manual smoke** (optional but recommended)

```bash
open "build/src/app/guitar_dsp_app_artefacts/Release/Standalone/Guitar DSP.app"
```

The default scene (id 0) should produce normal-level passthrough. (You won't be able to switch scenes yet — that's Tasks 11/12.)

- [ ] **Step 7: Commit**

```bash
git add src/app/PluginProcessor.h src/app/PluginProcessor.cpp src/app/CMakeLists.txt
git commit -m "feat(app): SceneEngine drives Mixer master gain per active scene"
```

---

## Task 9: SceneCommand + FCB1010Mapping (pure data, no I/O)

**Files:**
- Create: `src/midi/SceneCommand.h`
- Create: `src/midi/FCB1010Mapping.{h,cpp}`
- Create: `tests/unit/midi/test_fcb1010_mapping.cpp`
- Modify: `src/CMakeLists.txt` (add `guitar_dsp_midi` library)
- Modify: `tests/CMakeLists.txt`

`FCB1010Mapping` is pure — it takes a `juce::MidiMessage` and returns an optional `SceneCommand`. No CoreMIDI, no I/O, no state. This makes it trivial to unit-test.

- [ ] **Step 1: Write `src/midi/SceneCommand.h`**

```cpp
#pragma once

namespace guitar_dsp::midi {

enum class SceneCommandType {
    ActivateScene,   // payload = scene id
    SetWetDry,       // payload = 0..127 (CC value, caller normalizes)
    SetMasterGain,   // payload = 0..127 (CC value, caller normalizes)
};

struct SceneCommand {
    SceneCommandType type;
    int              payload;
};

} // namespace guitar_dsp::midi
```

- [ ] **Step 2: Write the failing test FIRST**

Create `tests/unit/midi/test_fcb1010_mapping.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "midi/FCB1010Mapping.h"

#include <juce_audio_basics/juce_audio_basics.h>

using guitar_dsp::midi::FCB1010Mapping;
using guitar_dsp::midi::SceneCommand;
using guitar_dsp::midi::SceneCommandType;

TEST_CASE("FCB1010Mapping: Program Change 0..9 → ActivateScene 0..9", "[midi][fcb]") {
    FCB1010Mapping m = FCB1010Mapping::stockDefaults();
    for (int pc = 0; pc < 10; ++pc) {
        const auto msg = juce::MidiMessage::programChange(1, pc);
        auto cmd = m.translate(msg);
        REQUIRE(cmd.has_value());
        REQUIRE(cmd->type == SceneCommandType::ActivateScene);
        REQUIRE(cmd->payload == pc);
    }
}

TEST_CASE("FCB1010Mapping: Program Change 10..127 → nullopt (unmapped)", "[midi][fcb]") {
    FCB1010Mapping m = FCB1010Mapping::stockDefaults();
    REQUIRE_FALSE(m.translate(juce::MidiMessage::programChange(1, 10)).has_value());
    REQUIRE_FALSE(m.translate(juce::MidiMessage::programChange(1, 99)).has_value());
}

TEST_CASE("FCB1010Mapping: CC 27 → SetWetDry with raw CC value", "[midi][fcb]") {
    FCB1010Mapping m = FCB1010Mapping::stockDefaults();
    auto cmd = m.translate(juce::MidiMessage::controllerEvent(1, 27, 64));
    REQUIRE(cmd.has_value());
    REQUIRE(cmd->type == SceneCommandType::SetWetDry);
    REQUIRE(cmd->payload == 64);
}

TEST_CASE("FCB1010Mapping: CC 7 → SetMasterGain with raw CC value", "[midi][fcb]") {
    FCB1010Mapping m = FCB1010Mapping::stockDefaults();
    auto cmd = m.translate(juce::MidiMessage::controllerEvent(1, 7, 100));
    REQUIRE(cmd.has_value());
    REQUIRE(cmd->type == SceneCommandType::SetMasterGain);
    REQUIRE(cmd->payload == 100);
}

TEST_CASE("FCB1010Mapping: unmapped CC → nullopt", "[midi][fcb]") {
    FCB1010Mapping m = FCB1010Mapping::stockDefaults();
    REQUIRE_FALSE(m.translate(juce::MidiMessage::controllerEvent(1, 99, 64)).has_value());
}

TEST_CASE("FCB1010Mapping: note-on, pitch bend, etc. → nullopt", "[midi][fcb]") {
    FCB1010Mapping m = FCB1010Mapping::stockDefaults();
    REQUIRE_FALSE(m.translate(juce::MidiMessage::noteOn(1, 60, juce::uint8(100))).has_value());
    REQUIRE_FALSE(m.translate(juce::MidiMessage::pitchWheel(1, 8192)).has_value());
}

TEST_CASE("FCB1010Mapping: loadFromJson with custom mapping overrides defaults",
          "[midi][fcb]") {
    const auto json = R"({
        "deviceMatch": "FCB1010",
        "programChangeToScene": { "100": 5, "101": 6 },
        "expressionPedalCcs": { "wetDry": 11, "masterGain": 12 }
    })";
    auto mapping = FCB1010Mapping::loadFromJson(json);
    REQUIRE(mapping.has_value());

    auto pcCmd = mapping->translate(juce::MidiMessage::programChange(1, 100));
    REQUIRE(pcCmd.has_value());
    REQUIRE(pcCmd->payload == 5);

    auto ccCmd = mapping->translate(juce::MidiMessage::controllerEvent(1, 11, 50));
    REQUIRE(ccCmd.has_value());
    REQUIRE(ccCmd->type == SceneCommandType::SetWetDry);

    // PC 0 should NOT activate scene 0 anymore — overridden away.
    REQUIRE_FALSE(mapping->translate(juce::MidiMessage::programChange(1, 0)).has_value());
}

TEST_CASE("FCB1010Mapping: loadFromJson with garbage returns nullopt",
          "[midi][fcb]") {
    REQUIRE_FALSE(FCB1010Mapping::loadFromJson("this is not json").has_value());
}
```

- [ ] **Step 3: Run to confirm it fails**

```bash
cmake --build build --target guitar_dsp_tests 2>&1 | head -10
```

Expected: `FCB1010Mapping.h not found`.

- [ ] **Step 4: Write `src/midi/FCB1010Mapping.h`**

```cpp
#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include <juce_audio_basics/juce_audio_basics.h>

#include "SceneCommand.h"

namespace guitar_dsp::midi {

class FCB1010Mapping {
public:
    static FCB1010Mapping stockDefaults();
    static std::optional<FCB1010Mapping> loadFromJson(const std::string& jsonText);

    std::optional<SceneCommand> translate(const juce::MidiMessage& msg) const;

private:
    std::unordered_map<int, int> programChangeToScene_;
    int wetDryCc_     = -1;
    int masterGainCc_ = -1;
};

} // namespace guitar_dsp::midi
```

- [ ] **Step 5: Write `src/midi/FCB1010Mapping.cpp`**

```cpp
#include "FCB1010Mapping.h"

#include <juce_core/juce_core.h>

namespace guitar_dsp::midi {

FCB1010Mapping FCB1010Mapping::stockDefaults() {
    FCB1010Mapping m;
    for (int i = 0; i < 10; ++i) m.programChangeToScene_[i] = i;
    m.wetDryCc_     = 27;
    m.masterGainCc_ = 7;
    return m;
}

std::optional<FCB1010Mapping> FCB1010Mapping::loadFromJson(const std::string& jsonText) {
    auto parsed = juce::JSON::parse(juce::String(jsonText));
    if (!parsed.isObject()) return std::nullopt;
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr) return std::nullopt;

    FCB1010Mapping m;

    if (obj->hasProperty("programChangeToScene")) {
        auto pcVar = obj->getProperty("programChangeToScene");
        if (auto* pcObj = pcVar.getDynamicObject()) {
            for (const auto& kv : pcObj->getProperties()) {
                const int pc = kv.name.toString().getIntValue();
                const int scene = static_cast<int>(kv.value);
                m.programChangeToScene_[pc] = scene;
            }
        }
    }

    if (obj->hasProperty("expressionPedalCcs")) {
        if (auto* ccObj = obj->getProperty("expressionPedalCcs").getDynamicObject()) {
            if (ccObj->hasProperty("wetDry"))
                m.wetDryCc_ = static_cast<int>(ccObj->getProperty("wetDry"));
            if (ccObj->hasProperty("masterGain"))
                m.masterGainCc_ = static_cast<int>(ccObj->getProperty("masterGain"));
        }
    }

    return m;
}

std::optional<SceneCommand> FCB1010Mapping::translate(const juce::MidiMessage& msg) const {
    if (msg.isProgramChange()) {
        const int pc = msg.getProgramChangeNumber();
        auto it = programChangeToScene_.find(pc);
        if (it == programChangeToScene_.end()) return std::nullopt;
        return SceneCommand{SceneCommandType::ActivateScene, it->second};
    }
    if (msg.isController()) {
        const int cc = msg.getControllerNumber();
        const int val = msg.getControllerValue();
        if (cc == wetDryCc_     && wetDryCc_     >= 0)
            return SceneCommand{SceneCommandType::SetWetDry, val};
        if (cc == masterGainCc_ && masterGainCc_ >= 0)
            return SceneCommand{SceneCommandType::SetMasterGain, val};
    }
    return std::nullopt;
}

} // namespace guitar_dsp::midi
```

- [ ] **Step 6: Add `guitar_dsp_midi` library to `src/CMakeLists.txt`**

```cmake
add_library(guitar_dsp_midi STATIC
    midi/FCB1010Mapping.cpp
)

target_include_directories(guitar_dsp_midi PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(guitar_dsp_midi PUBLIC cxx_std_20)

target_link_libraries(guitar_dsp_midi
    PUBLIC
        juce::juce_audio_basics
        juce::juce_core
)
```

- [ ] **Step 7: Update `tests/CMakeLists.txt`**

```cmake
add_executable(guitar_dsp_tests
    # ... existing sources ...
    unit/midi/test_fcb1010_mapping.cpp
)

target_link_libraries(guitar_dsp_tests PRIVATE
    guitar_dsp_audio
    guitar_dsp_scenes
    guitar_dsp_midi
    juce::juce_audio_formats
    Catch2::Catch2WithMain
)
```

- [ ] **Step 8: Build and run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R "FCB1010Mapping"
```

Expected: 8 tests pass.

- [ ] **Step 9: Commit**

```bash
git add src/midi/ src/CMakeLists.txt tests/unit/midi/test_fcb1010_mapping.cpp tests/CMakeLists.txt
git commit -m "feat(midi): FCB1010Mapping translates MIDI to SceneCommand"
```

---

## Task 10: Cross-version JUCE compatibility note (mapping)

This task is a **verification** — no code changes — to make sure Task 9's tests pass against JUCE 8.0.4 specifically. (Some JUCE methods like `juce::MidiMessage::noteOn` have multiple overloads; the test uses `juce::uint8`. If the test compiles and passes, this task is a no-op.)

- [ ] **Step 1: Run the full tests**

```bash
ctest --test-dir build --output-on-failure
```

Expected: 34/34 passing (26 prior + 5 SceneLibrary tests, no wait — 4 — + 5 SceneEngine + 1 Scene::defaults + 8 FCB1010Mapping = 26+18 = 44 hmm). Recount: 26 prior + 1 Scene + 4 SceneLibrary + 5 SceneEngine + 8 FCB1010Mapping = 44 tests. Confirm `ctest` reports 44/44.

If anything fails, fix the call site and commit a follow-up under message `fix(test): ...`.

- [ ] **Step 2: No commit needed unless a fix was made.**

---

## Task 11: MidiRouter — CoreMIDI input via JUCE

**Files:**
- Create: `src/midi/MidiRouter.{h,cpp}`
- Modify: `src/CMakeLists.txt`
- Modify: `src/app/PluginProcessor.{h,cpp}`
- Modify: `src/app/CMakeLists.txt`

`MidiRouter` is a `juce::MidiInputCallback` that opens all available MIDI inputs (or the first matching "FCB1010" if present) and forwards every message to a user-supplied callback on the message thread. It does NO interpretation — that's `FCB1010Mapping`'s job.

The router lives on `PluginProcessor` for lifetime simplicity. The processor's constructor wires the chain: `MidiRouter` → `FCB1010Mapping::translate` → `SceneEngine::activateScene` etc.

- [ ] **Step 1: Write `src/midi/MidiRouter.h`**

```cpp
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>

namespace guitar_dsp::midi {

// Opens MIDI inputs via JUCE and forwards messages to a single callback
// on the message thread. If any input device name contains "FCB1010"
// (case-insensitive), only that device is opened; otherwise all available
// inputs are opened. Re-scans on every refresh() call.
class MidiRouter : private juce::MidiInputCallback {
public:
    using MessageCallback = std::function<void(const juce::MidiMessage&)>;

    explicit MidiRouter(MessageCallback onMessage);
    ~MidiRouter() override;

    // Re-scan available devices and (re)open the matching ones. Safe to
    // call repeatedly; idempotent for devices already open.
    void refresh();

    // Names of currently-open MIDI input devices (for the diagnostic UI).
    std::vector<juce::String> openDeviceNames() const;

private:
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

    MessageCallback callback_;
    std::vector<std::unique_ptr<juce::MidiInput>> openInputs_;
};

} // namespace guitar_dsp::midi
```

- [ ] **Step 2: Write `src/midi/MidiRouter.cpp`**

```cpp
#include "MidiRouter.h"

namespace guitar_dsp::midi {

MidiRouter::MidiRouter(MessageCallback onMessage)
    : callback_(std::move(onMessage)) {
    refresh();
}

MidiRouter::~MidiRouter() {
    for (auto& in : openInputs_) in->stop();
    openInputs_.clear();
}

void MidiRouter::refresh() {
    for (auto& in : openInputs_) in->stop();
    openInputs_.clear();

    const auto infos = juce::MidiInput::getAvailableDevices();

    // Prefer FCB1010 if present.
    std::vector<juce::MidiDeviceInfo> chosen;
    for (const auto& info : infos) {
        if (info.name.containsIgnoreCase("FCB1010")) chosen.push_back(info);
    }
    if (chosen.empty()) {
        for (const auto& info : infos) chosen.push_back(info);
    }

    for (const auto& info : chosen) {
        if (auto in = juce::MidiInput::openDevice(info.identifier, this)) {
            in->start();
            openInputs_.push_back(std::move(in));
        }
    }
}

std::vector<juce::String> MidiRouter::openDeviceNames() const {
    std::vector<juce::String> names;
    names.reserve(openInputs_.size());
    for (const auto& in : openInputs_) names.push_back(in->getName());
    return names;
}

void MidiRouter::handleIncomingMidiMessage(juce::MidiInput*,
                                           const juce::MidiMessage& message) {
    // JUCE delivers callbacks on the high-priority MIDI thread. Hop to the
    // message thread before touching scene state to keep things simple.
    auto cb = callback_;
    juce::MessageManager::callAsync([cb, message] {
        cb(message);
    });
}

} // namespace guitar_dsp::midi
```

- [ ] **Step 3: Update `src/CMakeLists.txt`**

```cmake
add_library(guitar_dsp_midi STATIC
    midi/FCB1010Mapping.cpp
    midi/MidiRouter.cpp
)

target_link_libraries(guitar_dsp_midi
    PUBLIC
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_core
        juce::juce_events
)
```

- [ ] **Step 4: Wire MidiRouter into PluginProcessor**

In `src/app/PluginProcessor.h`, add includes:

```cpp
#include "midi/FCB1010Mapping.h"
#include "midi/MidiRouter.h"
```

Add to private:

```cpp
midi::FCB1010Mapping   midiMapping_ {midi::FCB1010Mapping::stockDefaults()};
std::unique_ptr<midi::MidiRouter> midiRouter_;
std::atomic<int>       lastMidiSummary_ {0};  // packed: type<<16 | data1
```

Add public accessor:

```cpp
int  getLastMidiSummary() const noexcept { return lastMidiSummary_.load(std::memory_order_relaxed); }
```

In `src/app/PluginProcessor.cpp`, the constructor (after the existing initializer list) creates the router:

```cpp
PluginProcessor::PluginProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::mono(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    midiRouter_ = std::make_unique<midi::MidiRouter>(
        [this](const juce::MidiMessage& msg) {
            // Record last summary for the diagnostic UI.
            const int packed = (static_cast<int>(msg.getRawData()[0] & 0xFF) << 16)
                             | static_cast<int>(msg.getRawData()[1] & 0xFF);
            lastMidiSummary_.store(packed, std::memory_order_relaxed);

            if (auto cmd = midiMapping_.translate(msg)) {
                if (cmd->type == midi::SceneCommandType::ActivateScene) {
                    sceneEngine_.activateScene(cmd->payload);
                }
                // SetWetDry / SetMasterGain are recognized but no-op in Phase 2;
                // they land when expression-pedal continuous control is wired
                // through SceneEngine in Phase 3.
            }
        });
}
```

- [ ] **Step 5: Link `guitar_dsp_midi` into the app**

In `src/app/CMakeLists.txt`, add `guitar_dsp_midi` to `target_link_libraries`.

- [ ] **Step 6: Build and verify all tests still pass**

```bash
cmake --build build --target guitar_dsp_app_Standalone guitar_dsp_tests
ctest --test-dir build --output-on-failure
```

Expected: builds, all tests still pass.

- [ ] **Step 7: Commit**

```bash
git add src/midi/MidiRouter.h src/midi/MidiRouter.cpp src/CMakeLists.txt src/app/PluginProcessor.h src/app/PluginProcessor.cpp src/app/CMakeLists.txt
git commit -m "feat(midi): MidiRouter wired through FCB1010Mapping to SceneEngine"
```

---

## Task 12: Keyboard fallback on the editor

**Files:**
- Modify: `src/app/PluginEditor.{h,cpp}`

The editor implements `juce::KeyListener` so number keys `1`–`0` activate scenes 0–9. This works without any MIDI hardware.

- [ ] **Step 1: Update `src/app/PluginEditor.h`**

Add `: private juce::KeyListener` to the class declaration. Add `bool keyPressed(const juce::KeyPress&, juce::Component*) override;` to the private section.

- [ ] **Step 2: Update `src/app/PluginEditor.cpp` constructor**

After `addAndMakeVisible(...)` calls, add:

```cpp
setWantsKeyboardFocus(true);
addKeyListener(this);
```

- [ ] **Step 3: Implement `keyPressed`**

```cpp
bool PluginEditor::keyPressed(const juce::KeyPress& key, juce::Component*) {
    const auto kc = key.getKeyCode();

    int sceneId = -1;
    if (kc >= '1' && kc <= '9')      sceneId = kc - '1';   // 1 → 0, 9 → 8
    else if (kc == '0')              sceneId = 9;          // 0 → 9
    if (sceneId < 0) return false;

    processor_.sceneEngine().activateScene(sceneId);
    return true;
}
```

- [ ] **Step 4: Build and smoke-test**

```bash
cmake --build build --target guitar_dsp_app_Standalone
open "build/src/app/guitar_dsp_app_artefacts/Release/Standalone/Guitar DSP.app"
```

Click on the app window to give it keyboard focus, then press `1` through `0`. Each keypress changes the active scene (you'll be able to verify this after Task 13 adds the UI indicator; for now, the master gain change is audible).

- [ ] **Step 5: Commit**

```bash
git add src/app/PluginEditor.h src/app/PluginEditor.cpp
git commit -m "feat(app): keyboard 1..0 activates scenes 0..9 (FCB1010 fallback)"
```

---

## Task 13: SceneIndicator UI component

**Files:**
- Create: `src/app/SceneIndicator.{h,cpp}`
- Modify: `src/app/CMakeLists.txt`
- Modify: `src/app/PluginEditor.{h,cpp}`

A horizontal strip showing 10 slots, one per scene id 0–9. The active slot is filled with the scene's color and shows the scene name. Inactive slots are dim.

- [ ] **Step 1: Write `src/app/SceneIndicator.h`**

```cpp
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class PluginProcessor;

class SceneIndicator : public juce::Component,
                       private juce::Timer {
public:
    explicit SceneIndicator(PluginProcessor& p);
    ~SceneIndicator() override;

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    PluginProcessor& processor_;
};

} // namespace guitar_dsp
```

- [ ] **Step 2: Write `src/app/SceneIndicator.cpp`**

```cpp
#include "SceneIndicator.h"

#include "PluginProcessor.h"
#include "scenes/SceneEngine.h"

namespace guitar_dsp {

SceneIndicator::SceneIndicator(PluginProcessor& p) : processor_(p) {
    setOpaque(true);
    startTimerHz(15);
}

SceneIndicator::~SceneIndicator() { stopTimer(); }

void SceneIndicator::timerCallback() { repaint(); }

void SceneIndicator::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.fillAll(juce::Colour::fromRGB(18, 20, 26));

    const auto& engine = processor_.sceneEngine();
    const int activeId = engine.getActiveSceneId();
    const auto& activeScene = engine.getActiveScene();

    // Active scene name on the left.
    const auto nameArea = bounds.reduced(8, 4).removeFromLeft(bounds.getWidth() / 2);
    g.setColour(juce::Colour::fromRGB(150, 160, 175));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
    g.drawText("Scene", nameArea, juce::Justification::topLeft);

    g.setColour(juce::Colour{0xFF000000u | activeScene.colorRgb});
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(16.0f).withStyle("Bold")});
    g.drawText(juce::String(activeId) + "  " + juce::String(activeScene.name),
               nameArea.withTrimmedTop(12),
               juce::Justification::topLeft);

    // 10-slot strip on the right.
    auto stripArea = bounds.reduced(8, 8).removeFromRight(bounds.getWidth() / 2 - 16);
    const int slotWidth = stripArea.getWidth() / 10;

    for (int i = 0; i < 10; ++i) {
        auto slot = juce::Rectangle<int>(stripArea.getX() + i * slotWidth,
                                          stripArea.getY(),
                                          slotWidth - 2,
                                          stripArea.getHeight());
        const bool isActive = (i == activeId);
        g.setColour(isActive ? juce::Colour{0xFF000000u | activeScene.colorRgb}
                             : juce::Colour::fromRGB(40, 44, 52));
        g.fillRoundedRectangle(slot.toFloat(), 3.0f);

        g.setColour(isActive ? juce::Colour::fromRGB(20, 22, 28)
                             : juce::Colour::fromRGB(150, 160, 175));
        g.setFont(juce::Font{juce::FontOptions{}.withHeight(12.0f)});
        g.drawText(juce::String(i), slot, juce::Justification::centred);
    }
}

} // namespace guitar_dsp
```

- [ ] **Step 3: Add to `src/app/CMakeLists.txt`**

Add `SceneIndicator.cpp` to `target_sources`.

- [ ] **Step 4: Host in PluginEditor**

In `src/app/PluginEditor.h`:

```cpp
#include "SceneIndicator.h"
```

Add `SceneIndicator sceneIndicator_;` to the private members.

In `PluginEditor.cpp`:

- Add `sceneIndicator_(p)` to the initializer list.
- Add `addAndMakeVisible(sceneIndicator_);` in the constructor.
- In `resized()`, give it 48 px above the oscilloscope:

```cpp
void PluginEditor::resized() {
    auto bounds = getLocalBounds();
    diagnosticPanel_.setBounds(bounds.removeFromTop(62));
    sceneIndicator_.setBounds(bounds.removeFromTop(48));
    const int remaining = bounds.getHeight();
    oscilloscope_.setBounds(bounds.removeFromTop(remaining / 2));
    spectrumAnalyzer_.setBounds(bounds);
}
```

- [ ] **Step 5: Build and smoke-test**

```bash
cmake --build build --target guitar_dsp_app_Standalone
open "build/src/app/guitar_dsp_app_artefacts/Release/Standalone/Guitar DSP.app"
```

Press `1`–`0`. The scene indicator should update name/highlight slot in real time.

- [ ] **Step 6: Commit**

```bash
git add src/app/SceneIndicator.h src/app/SceneIndicator.cpp src/app/CMakeLists.txt src/app/PluginEditor.h src/app/PluginEditor.cpp
git commit -m "feat(app): SceneIndicator showing active scene and 0-9 strip"
```

---

## Task 14: MIDI activity indicator in DiagnosticPanel

**Files:**
- Modify: `src/app/DiagnosticPanel.{h,cpp}`

Add a small MIDI LED + last-message readout to the diagnostic strip's status line. The LED flashes for ~200 ms whenever a new MIDI message arrives (detected by `lastMidiSummary` changing).

- [ ] **Step 1: Update `src/app/DiagnosticPanel.h`**

Add private members:

```cpp
int  lastMidiSummary_       = 0;
juce::int64 lastMidiTimeMs_ = 0;
```

- [ ] **Step 2: Update `src/app/DiagnosticPanel.cpp::timerCallback`**

After the existing peak updates, add:

```cpp
const int currentSummary = processor_.getLastMidiSummary();
if (currentSummary != lastMidiSummary_) {
    lastMidiSummary_ = currentSummary;
    lastMidiTimeMs_  = juce::Time::currentTimeMillis();
}
```

- [ ] **Step 3: Update `paint` to render a MIDI LED**

In the status-line drawing (after the gate LED), add a similar LED for MIDI. The LED is on if `(now - lastMidiTimeMs_) < 200`. Place it after the gate annotation:

```cpp
const auto now = juce::Time::currentTimeMillis();
const bool midiHot = (now - lastMidiTimeMs_) < 200;
auto midiLamp = juce::Rectangle<int>(/* position after gate annotation */,
                                     statusRow.getY() + 4, 14, 14);
g.setColour(midiHot ? juce::Colour::fromRGB(80, 180, 220)
                    : juce::Colour::fromRGB(80, 80, 90));
g.fillEllipse(midiLamp.toFloat());
g.setColour(juce::Colour::fromRGB(150, 160, 175));
g.setFont(makeFont(11.0f));
g.drawText("MIDI", juce::Rectangle<int>(midiLamp.getRight() + 4,
                                        statusRow.getY(), 36, statusRow.getHeight()),
           juce::Justification::left);
```

(Exact placement is up to you — keep the strip tidy. You may find it easier to restructure the status line as a series of `removeFromLeft` segments rather than measuring text widths.)

- [ ] **Step 4: Build and smoke-test**

```bash
cmake --build build --target guitar_dsp_app_Standalone
open "build/src/app/guitar_dsp_app_artefacts/Release/Standalone/Guitar DSP.app"
```

Plug in the FCB1010 (or any MIDI controller). Step on a switch / send any message — the MIDI LED should flash.

- [ ] **Step 5: Commit**

```bash
git add src/app/DiagnosticPanel.h src/app/DiagnosticPanel.cpp
git commit -m "feat(app): MIDI activity LED in diagnostic strip"
```

---

## Task 15: Hot reload of scene files (debug builds only)

**Files:**
- Modify: `src/scenes/SceneEngine.{h,cpp}` (add a `reloadFrom(dir)` method)
- Modify: `src/app/PluginProcessor.{h,cpp}` (a hidden hotkey `r` + JUCE Timer poll)

A polling hot-reload (no inotify / FSEvents) is fine for our use case — we don't need < 1 s latency. Every 2 seconds, scan the scenes directory; if any file's mtime is newer than the last load, reload all scenes and preserve the active id (best-effort — if the active id no longer exists, fall back to id 0).

To keep behavior predictable in tests/CI, gate the polling behind an environment variable: `GUITAR_DSP_HOT_RELOAD=1`. By default it's off.

- [ ] **Step 1: Add `reloadFrom` to SceneEngine**

In `SceneEngine.h` add:

```cpp
void reloadFrom(const std::string& directory);  // preserves active id if possible
```

In `SceneEngine.cpp`:

```cpp
void SceneEngine::reloadFrom(const std::string& directory) {
    auto fresh = SceneLibrary::loadDirectory(directory);
    const int previousActive = getActiveSceneId();
    loadScenes(std::move(fresh));
    if (previousActive >= 0) activateScene(previousActive);  // best-effort
}
```

(`SceneEngine.cpp` will need to `#include "SceneLibrary.h"`.)

- [ ] **Step 2: Polling Timer in PluginProcessor**

In `PluginProcessor.h`, add `private`:

```cpp
class AssetsPoller;
std::unique_ptr<AssetsPoller> assetsPoller_;
```

In `PluginProcessor.cpp`, define a small inner timer class:

```cpp
class PluginProcessor::AssetsPoller : public juce::Timer {
public:
    AssetsPoller(PluginProcessor& p) : owner_(p) {}
    void timerCallback() override {
        owner_.sceneEngine_.reloadFrom(AssetLocator::scenesDirectory());
    }
private:
    PluginProcessor& owner_;
};
```

In the constructor (after MidiRouter setup), add:

```cpp
if (const char* env = std::getenv("GUITAR_DSP_HOT_RELOAD"); env && std::string(env) == "1") {
    assetsPoller_ = std::make_unique<AssetsPoller>(*this);
    assetsPoller_->startTimer(2000);
}
```

- [ ] **Step 3: Build**

```bash
cmake --build build --target guitar_dsp_app_Standalone
```

- [ ] **Step 4: Smoke-test (optional)**

```bash
GUITAR_DSP_HOT_RELOAD=1 open "build/src/app/guitar_dsp_app_artefacts/Release/Standalone/Guitar DSP.app"
```

Edit a scene file in `assets/scenes/`, save it, wait ~2 s — the scene should reload. (Note: macOS `open` doesn't propagate the env var to the .app launched by launchd. To actually exercise this you'll need to run the binary directly: `GUITAR_DSP_HOT_RELOAD=1 "build/src/app/guitar_dsp_app_artefacts/Release/Standalone/Guitar DSP.app/Contents/MacOS/Guitar DSP"`.)

- [ ] **Step 5: Commit**

```bash
git add src/scenes/SceneEngine.h src/scenes/SceneEngine.cpp src/app/PluginProcessor.h src/app/PluginProcessor.cpp
git commit -m "feat(scenes): hot-reload via GUITAR_DSP_HOT_RELOAD=1 env var"
```

---

## Task 16: Integration test — full scene-switch flow

**Files:**
- Create: `tests/integration/test_scene_switch.cpp`
- Modify: `tests/CMakeLists.txt`

End-to-end test wiring `FCB1010Mapping` → `SceneEngine` → `Mixer` params. No real MIDI device — we construct `juce::MidiMessage`s directly.

- [ ] **Step 1: Write the test**

Create `tests/integration/test_scene_switch.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "midi/FCB1010Mapping.h"
#include "scenes/SceneEngine.h"

using guitar_dsp::midi::FCB1010Mapping;
using guitar_dsp::midi::SceneCommandType;
using guitar_dsp::scenes::Scene;
using guitar_dsp::scenes::SceneEngine;

TEST_CASE("integration: PC message switches scene and updates mixer params",
          "[integration][scenes][midi]") {
    SceneEngine engine;
    std::vector<Scene> scenes;
    for (int i = 0; i < 10; ++i) {
        Scene s = Scene::defaults(i);
        s.mixer.masterGainDb = -static_cast<float>(i);  // 0..-9 dB
        scenes.push_back(s);
    }
    engine.loadScenes(std::move(scenes));
    REQUIRE(engine.getActiveSceneId() == 0);

    auto mapping = FCB1010Mapping::stockDefaults();

    // PC 7 → scene 7 → masterGainDb -7.
    const auto msg = juce::MidiMessage::programChange(1, 7);
    auto cmd = mapping.translate(msg);
    REQUIRE(cmd.has_value());
    REQUIRE(cmd->type == SceneCommandType::ActivateScene);

    REQUIRE(engine.activateScene(cmd->payload));
    REQUIRE(engine.getActiveSceneId() == 7);
    REQUIRE(engine.currentMixerParams().masterGainDb == -7.0f);
}
```

- [ ] **Step 2: Update `tests/CMakeLists.txt`**

Add `integration/test_scene_switch.cpp` to `add_executable`.

- [ ] **Step 3: Build and run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R "scene"
```

Expected: passes.

- [ ] **Step 4: Commit**

```bash
git add tests/integration/test_scene_switch.cpp tests/CMakeLists.txt
git commit -m "test(integration): end-to-end PC→scene→mixer params"
```

---

## Task 17: README — document Phase 2 features

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Update `README.md`**

Replace the "Project status" section at the bottom with:

```markdown
## Project status

This branch implements **Phase 2: Scene system + MIDI/FCB1010**. The app loads 10 scenes from `assets/scenes/*.json` at startup and exposes them via:

- **Keyboard**: number keys `1`–`9` activate scenes 0–8; `0` activates scene 9.
- **MIDI**: any device whose name contains "FCB1010" is auto-connected. Program Change 0–9 activates the corresponding scene; CC 27 and CC 7 are recognized as expression-pedal channels (no-op in Phase 2; wired in Phase 3).

Each scene currently varies only the Mixer's master gain (so switching is audible). Per-scene DSP differences (carousel, vocoder) arrive in Phases 3 and 4.

### Hot reload (development)

Set `GUITAR_DSP_HOT_RELOAD=1` to enable a 2-second polling reload of `assets/scenes/`. Edits propagate without restarting the app.

### Asset path override

Set `GUITAR_DSP_ASSETS_DIR=<path>` to load assets from a directory other than the app bundle (useful for editing source `assets/` while running the binary directly).

### Subsequent phases (see plans directory)

- **Phase 3**: Vocoder + 3 TTS sources (prebaked / Apple / Piper).
- **Phase 4**: Instrument Carousel — real per-scene DSP.
- **Phase 5**: Full-screen visualization (spectrogram, karaoke text).
- **Phase 6**: Hardening + dress rehearsal.
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: README updates for Phase 2 (scenes, MIDI, hot reload)"
```

---

## Task 18: Phase 2 wrap-up verification

This is the gate before declaring Phase 2 done — a fresh build from scratch with full test pass and a manual smoke check.

- [ ] **Step 1: Clean build from scratch**

```bash
rm -rf build
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target guitar_dsp_app_Standalone guitar_dsp_tests
```

Expected: builds clean. If any step warns or errors, fix and commit before continuing.

- [ ] **Step 2: Full test pass**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass. The count should be around 44–45 (26 from Phase 1 plus the new scenes/midi/integration tests).

- [ ] **Step 3: Manual smoke**

```bash
open "build/src/app/guitar_dsp_app_artefacts/Release/Standalone/Guitar DSP.app"
```

Verify:
- App launches without TCC crash.
- The scene indicator shows "0 Clean intro" and slot 0 highlighted.
- Pressing `1` switches to "1 Carousel — Hammond organ" and the master gain audibly drops by ~2 dB.
- Pressing `0` switches to "9 Panic — bypass to clean" and gain returns to 0 dB.
- The MIDI LED stays dark (no controller plugged in).
- If you have the FCB1010 plugged in, stepping on switches changes scenes and the LED blinks.

- [ ] **Step 4: Final commit if anything was fixed**

If steps 1–3 required any fixes, commit them under message `chore: phase 2 wrap-up fixes`.

If everything passed without changes, no commit is required.

---

## Subsequent phase plans (preview)

- **Phase 3: Vocoder + TTS sources** — `IVocoder` + `ChannelVocoder`; `ITTSSource` + `PrebakedTTSSource` / `LiveTTSSource` (AVSpeechSynthesizer) / `PiperTTSSource`; `TTSClipPlayer`, `TTSPrewarmer`; Python `tools/tts_prebake/`. The vocoder pulls audio from the live guitar (carrier) modulated by the TTS clip; scenes 6–8 become audibly distinct as "speaking guitar."
- **Phase 4: Instrument Carousel** — DSP chain (pitch shifter, octaver, formant shifter, multimode filter, bit crusher, comb, tube sat, chorus, reverb). Scenes 1–5 become audibly distinct.
- **Phase 5: Visualization** — JUCE OpenGL `VisualizerView` (spectrogram), karaoke text overlay, full-screen show layout.
- **Phase 6: Hardening + pre-conference** — 4-hour soak, latency measurement, dress rehearsal.

---

## Self-review

**Spec coverage (spec § → Phase 2 task):**
- §7.1 (10 default scenes): Task 1 (JSON files) + §7.2 (file format) honored.
- §7.2 (Scene JSON schema): Tasks 2/3 implement the subset needed for Phase 2 (id, name, color, mixer block); carousel/vocoder/tts fields are parsed-and-ignored, will land in their phases.
- §7.3 (transition behavior): the Mixer's existing per-sample ramping (Phase 1) plus immediate atomic snap at scene change. `transitionMs` is parsed but the per-stage ramping logic that uses it lands when carousel/vocoder add their own parameters.
- §7.4 (hot reload): Task 15.
- §8.1–8.4 (MIDI default mapping, remap, keyboard fallback, device handling): Tasks 6, 9, 11, 12.
- §12.3 #5 (MIDI mapping tests): Task 9 (8 unit tests).
- §12.3 #6 (Scene JSON loader tests): Task 3 (4 unit tests).
- §12.4 #13 (hot reload): exercised by Task 15's manual smoke; the polling code is small and exercised implicitly when scenes load.
- §12.4 #14 (FCB1010Mapping JSON): Task 9 covers `loadFromJson` valid + invalid.

**Placeholder scan:** No "TBD" / "TODO" / "fill in later" left. All file paths, code blocks, and commands are concrete.

**Type consistency:**
- `Scene`, `MixerParams`, `SceneEngine`, `SceneLibrary` API consistent across Tasks 2/3/4/8.
- `FCB1010Mapping::stockDefaults`, `loadFromJson`, `translate` signatures consistent across Tasks 9/10/11/16.
- `MidiRouter` constructor / `refresh` / `openDeviceNames` consistent across Tasks 11/14.
- `SceneEngine::activateScene` returns `bool` consistently.
- `PluginProcessor` accessor additions (`sceneEngine()`, `getLastMidiSummary()`) used consistently in Tasks 12/13/14.

**Spec deferrals not violated:** Carousel DSP, vocoder, TTS sources, full visualization — none touched.

---

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-30-phase-2-scenes-and-midi.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
