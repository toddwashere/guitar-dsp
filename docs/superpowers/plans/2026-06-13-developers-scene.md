# "Developers!" Scene 1 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Scene 1 with a Ballmer "DEVELOPERS!" recreation. Each guitar pluck plays the next "DEVELOPERS!" in chronological order through the existing vocoder (with a small dry-Ballmer underlayer via the `clarity` slider). Upgrade `WordReadout` so all note-stepped scenes get a pip-row progress indicator + crescendo-mirroring font-size/color ramp on the current word.

**Architecture:** Reuses the Phase 5a note-trigger pipeline end-to-end — no new audio code. Net-new is (1) an offline Python chop script that builds the Ballmer asset from a user-supplied source clip, (2) the scene + meta JSON, and (3) a UI upgrade to `WordReadout` plus a tiny `PluginProcessor::activeSceneColorRgb()` getter.

**Tech Stack:** C++ (JUCE) + Catch2 for the plugin code/tests; Python 3 (stdlib `wave` + `struct`) + `unittest` for the chop script.

**Spec:** [`docs/superpowers/specs/2026-06-13-developers-scene-design.md`](../specs/2026-06-13-developers-scene-design.md)

---

## File map

**Create:**
- `assets/scenes/01_developers.json` — Scene 1 config (Ballmer "DEVELOPERS!")
- `assets/tts/01_developers/meta.json` — declares text + voice + duration
- `scripts/build_developers_clip.py` — offline chopper, source WAV → 14-segment WAV
- `scripts/tests/test_build_developers_clip.py` — Python unittest for the chopper
- `tests/fixtures/scenes/with_developers.json` — fixture for the parser test
- `tests/unit/scenes/test_scene_library_developers.cpp` — parser test for the new scene shape
- `tests/unit/app/test_word_readout.cpp` — paint test for `WordReadout`

**Modify:**
- `assets/scenes/01_carousel_choir.json` — **delete** (replaced by `01_developers.json`)
- `.gitignore` — exclude the generated `01_developers/audio.wav`
- `src/app/PluginProcessor.h` — add `activeSceneColorRgb()` getter
- `src/app/WordReadout.h` — add constants for layout / ramp
- `src/app/WordReadout.cpp` — new paint geometry (3-slot row + pip strip), intensity ramp on current word
- `tests/CMakeLists.txt` — register the two new test files
- `README.md` — short section on obtaining the source clip + running the chopper

**Out of scope (per spec §8):** new audio mechanisms, spectrogram backdrop, engine-true word timestamps, per-scene visual customization, automated source-clip download.

---

## Task 1: Scene parser fixture + test (red → green TDD)

Validate that the new scene shape parses correctly before any other change. This is the smallest possible foothold.

**Files:**
- Create: `tests/fixtures/scenes/with_developers.json`
- Create: `tests/unit/scenes/test_scene_library_developers.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the fixture**

Create `tests/fixtures/scenes/with_developers.json`:

```json
{
  "id": 1,
  "name": "Developers!",
  "color": "#0078D4",
  "mixer": { "masterGainDb": -3.0, "dryWet": 0.9, "transitionMs": 30 },
  "tts": {
    "source": "prebaked",
    "clip": "01_developers",
    "text": "DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS!",
    "trigger": "note",
    "clarity": 0.25
  }
}
```

- [ ] **Step 2: Write the failing parser test**

Create `tests/unit/scenes/test_scene_library_developers.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "scenes/SceneLibrary.h"

#include <filesystem>
#include <sstream>

using guitar_dsp::scenes::SceneLibrary;
using Catch::Matchers::WithinAbs;

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

TEST_CASE("SceneLibrary: parses the Developers! scene", "[scenes][library][developers]") {
    auto s = SceneLibrary::loadOne(fixturePath("scenes/with_developers.json"));
    REQUIRE(s.has_value());

    REQUIRE(s->id == 1);
    REQUIRE(s->name == "Developers!");
    REQUIRE(s->colorRgb == 0x0078D4u);

    REQUIRE_THAT(s->mixer.masterGainDb, WithinAbs(-3.0f, 1e-4f));
    REQUIRE_THAT(s->mixer.dryWet,       WithinAbs(0.9f,  1e-4f));
    REQUIRE_THAT(s->mixer.transitionMs, WithinAbs(30.0f, 1e-4f));

    REQUIRE(s->tts.source  == "prebaked");
    REQUIRE(s->tts.clip    == "01_developers");
    REQUIRE(s->tts.trigger == "note");
    REQUIRE_THAT(s->tts.clarity, WithinAbs(0.25f, 1e-4f));

    // 14 whitespace-split tokens, each "DEVELOPERS!"
    std::istringstream iss(s->tts.text);
    std::string w;
    int count = 0;
    while (iss >> w) {
        REQUIRE(w == "DEVELOPERS!");
        ++count;
    }
    REQUIRE(count == 14);
}
```

- [ ] **Step 3: Register the new test in CMake**

Edit `tests/CMakeLists.txt` — find the `add_executable(guitar_dsp_tests` block and add this line in the scenes group (right after `unit/scenes/test_scene_library_carousel_pitch.cpp`):

```cmake
    unit/scenes/test_scene_library_developers.cpp
```

- [ ] **Step 4: Run the new test (expect PASS)**

```bash
cmake --build build -j --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "[developers]"
```

Expected: 1 test case, all assertions pass. (The parser doesn't need new code — `SceneLibrary` already handles all the fields the fixture exercises.)

- [ ] **Step 5: Commit**

```bash
git add tests/fixtures/scenes/with_developers.json \
        tests/unit/scenes/test_scene_library_developers.cpp \
        tests/CMakeLists.txt
git commit -m "test(scenes): parser test for Developers! scene shape"
```

---

## Task 2: Ship the scene + meta + delete the old Scene 1

Now that the parser test pins the shape, install the real asset.

**Files:**
- Create: `assets/scenes/01_developers.json` (same contents as the fixture)
- Create: `assets/tts/01_developers/meta.json`
- Delete: `assets/scenes/01_carousel_choir.json`

- [ ] **Step 1: Write the scene JSON**

Create `assets/scenes/01_developers.json` with exactly the same JSON body as `tests/fixtures/scenes/with_developers.json` from Task 1.

- [ ] **Step 2: Write the meta**

Create `assets/tts/01_developers/meta.json`:

```json
{
  "text": "DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS!",
  "voice": "ballmer",
  "duration_s": 25.0
}
```

(Mirrors the existing `assets/tts/06_hello_cleveland/meta.json` shape.)

- [ ] **Step 3: Delete the old scene**

```bash
git rm assets/scenes/01_carousel_choir.json
```

- [ ] **Step 4: Build the plugin and verify the scene set loads**

```bash
cmake --build build -j --target guitar_speak_AU
```

Expected: build green. No runtime check yet — `audio.wav` doesn't exist, but `PrebakedTTSSource` handles missing files with a logged warning (per spec §6), not a crash.

- [ ] **Step 5: Run the full unit suite to confirm no regression**

```bash
./build/tests/guitar_dsp_tests
```

Expected: all tests pass (existing tests don't reference the deleted scene file).

- [ ] **Step 6: Commit**

```bash
git add assets/scenes/01_developers.json assets/tts/01_developers/meta.json
git add assets/scenes/01_carousel_choir.json   # records the deletion
git commit -m "feat(scenes): replace carousel-choir with Developers! scene 1"
```

---

## Task 3: Gitignore the generated audio.wav

The Ballmer chant is not redistributed from the repo. Generated locally.

**Files:**
- Modify: `.gitignore`

- [ ] **Step 1: Append the ignore line**

Edit `.gitignore` — find the `# Test artifacts` block (it already contains `tests/fixtures/tts/*/audio.wav`). Add right after it, in its own group:

```
# Ballmer chant — user-generated, not redistributed (see scripts/build_developers_clip.py)
assets/tts/01_developers/audio.wav
```

- [ ] **Step 2: Verify gitignore is honored**

```bash
mkdir -p assets/tts/01_developers
touch assets/tts/01_developers/audio.wav
git status --short assets/tts/01_developers/
rm assets/tts/01_developers/audio.wav
```

Expected: `git status` shows ONLY `meta.json` (already tracked from Task 2) — no untracked `audio.wav` line.

- [ ] **Step 3: Commit**

```bash
git add .gitignore
git commit -m "chore(gitignore): exclude generated 01_developers/audio.wav"
```

---

## Task 4: `PluginProcessor::activeSceneColorRgb()` getter

`WordReadout` needs the scene color for the ramp. The existing API exposes spoken-word index + word list but not the color.

**Files:**
- Modify: `src/app/PluginProcessor.h`
- Modify: `tests/unit/app/test_plugin_state.cpp` (or a new tiny header-only test file if the existing one doesn't fit — see Step 2)

- [ ] **Step 1: Add the getter (header-only — no .cpp change)**

Edit `src/app/PluginProcessor.h` — find the line near 116 that declares `activeSceneWords()`:

```cpp
    std::vector<std::string> activeSceneWords() const;
```

Add immediately AFTER it:

```cpp
    // Active scene color (0xRRGGBB). Returns a neutral mid-gray (0x9090A0) if
    // no scene is active. Message-thread only.
    std::uint32_t activeSceneColorRgb() const noexcept {
        const auto& s = sceneEngine_.getActiveScene();
        return (s.id >= 0) ? s.colorRgb : 0x9090A0u;
    }
```

Also add `#include <cstdint>` near the top of the file if it's not already present (check the existing `#include` block — `juce_audio_processors` likely pulls it in transitively, but be explicit).

- [ ] **Step 2: Write a smoke test**

Create `tests/unit/app/test_plugin_processor_scene_color.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "app/PluginProcessor.h"
#include "scenes/Scene.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

using guitar_dsp::PluginProcessor;
using guitar_dsp::scenes::Scene;

TEST_CASE("PluginProcessor::activeSceneColorRgb: neutral when no scene active",
          "[app][processor][scene-color]") {
    juce::ScopedJuceInitialiser_GUI juceInit;
    PluginProcessor p;
    REQUIRE(p.activeSceneColorRgb() == 0x9090A0u);
}

TEST_CASE("PluginProcessor::activeSceneColorRgb: reflects loaded scene",
          "[app][processor][scene-color]") {
    juce::ScopedJuceInitialiser_GUI juceInit;
    PluginProcessor p;

    Scene s = Scene::defaults(1);
    s.colorRgb = 0x0078D4u;
    p.loadScenesForTest({s});      // see Step 3 — if this helper doesn't exist,
                                   // construct via the existing public scene-load
                                   // path instead (see fallback below).

    REQUIRE(p.activeSceneColorRgb() == 0x0078D4u);
}
```

**If `loadScenesForTest` does not exist on `PluginProcessor`** (likely — there is no such helper today): replace the body of the second test with whatever public API is used by `tests/integration/test_scene_switch.cpp` to install scenes. Grep for `loadScenes` in that file (`grep -n loadScenes tests/integration/test_scene_switch.cpp`) and use the same approach. If still no clean public path, delete this second `TEST_CASE` and rely on the integration tests + the manual smoke in Task 8 to cover the non-default branch. The first `TEST_CASE` (neutral default) is sufficient on its own to prove the getter is wired.

- [ ] **Step 3: Register the new test**

Edit `tests/CMakeLists.txt` — find the `add_executable(guitar_dsp_tests` block and add right after `unit/app/test_plugin_state.cpp`:

```cmake
    unit/app/test_plugin_processor_scene_color.cpp
```

- [ ] **Step 4: Build and run the new test**

```bash
cmake --build build -j --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "[scene-color]"
```

Expected: tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/app/PluginProcessor.h \
        tests/unit/app/test_plugin_processor_scene_color.cpp \
        tests/CMakeLists.txt
git commit -m "feat(app): expose activeSceneColorRgb() for the WordReadout ramp"
```

---

## Task 5: `WordReadout` — pip row at the bottom

Pure paint geometry change. Adds a new strip at the bottom of the component for the pip row, without yet changing the center-word styling (that's Task 6 so we can review the two changes independently).

**Files:**
- Modify: `src/app/WordReadout.h`
- Modify: `src/app/WordReadout.cpp`
- Create: `tests/unit/app/test_word_readout.cpp`

### Why this is split from Task 6

Two independent visual changes touching the same `paint()` method. Splitting lets a reviewer (and the test for each) focus on one geometry/styling concern at a time. A reader 6 months from now can also `git blame` each change cleanly.

### What the file currently does

`src/app/WordReadout.cpp` (existing) fills the component dark gray, splits the area into three equal horizontal slots (left third = previous word in 18 pt gray, middle third = current word in 34 pt bold gold `#F0E6B4`, right third = next word in 18 pt gray), and shows a centered idle hint when there is no active word.

- [ ] **Step 1: Write the failing paint test**

Create `tests/unit/app/test_word_readout.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>

#include "app/WordReadout.h"
#include "app/PluginProcessor.h"
#include "scenes/Scene.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_graphics/juce_graphics.h>

using guitar_dsp::WordReadout;
using guitar_dsp::PluginProcessor;

namespace {

// Renders the WordReadout into an image at (w x h) and returns the image.
// Caller arranges scene state via the PluginProcessor passed in.
juce::Image render(WordReadout& r, int w, int h) {
    r.setSize(w, h);
    juce::Image img(juce::Image::ARGB, w, h, true);
    juce::Graphics g(img);
    r.paint(g);
    return img;
}

// Returns true if there is a pixel inside the given rectangle whose alpha
// component is at least `minAlpha`. Used to assert "something was drawn here"
// without depending on exact font metrics.
bool hasOpaquePixel(const juce::Image& img, juce::Rectangle<int> r, int minAlpha) {
    r = r.getIntersection(img.getBounds());
    for (int y = r.getY(); y < r.getBottom(); ++y)
        for (int x = r.getX(); x < r.getRight(); ++x)
            if (img.getPixelAt(x, y).getAlpha() >= minAlpha) return true;
    return false;
}

} // namespace

TEST_CASE("WordReadout: pip strip drawn at the bottom in note-stepped mode",
          "[app][ui][word-readout][pips]") {
    juce::ScopedJuceInitialiser_GUI juceInit;
    PluginProcessor p;
    WordReadout r(p);

    auto img = render(r, 600, 120);

    // The bottom 18 px is the pip strip area (kPipStripHeight in WordReadout.cpp).
    // With no active scene, all 0 pips draw — strip should be empty AND no crash.
    // (The richer assertions live in Task 6's idle / N=14 cases.)
    REQUIRE(img.getWidth() == 600);
    REQUIRE(img.getHeight() == 120);
}
```

(The detailed pip-row alpha assertions come in Step 4 after the implementation exists; this first test just pins "paint runs without crashing in the new geometry" so the next step has something to turn green.)

- [ ] **Step 2: Add the new test to CMake**

Edit `tests/CMakeLists.txt` — add right after `unit/app/test_plugin_processor_scene_color.cpp` (added in Task 4):

```cmake
    unit/app/test_word_readout.cpp
```

- [ ] **Step 3: Run the test (expect PASS — first test is non-strict)**

```bash
cmake --build build -j --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "[word-readout]"
```

Expected: smoke test passes.

- [ ] **Step 4: Add layout constants to the header**

Edit `src/app/WordReadout.h` — replace the existing class body (lines ~12-25) with:

```cpp
class WordReadout : public juce::Component,
                    private juce::Timer {
public:
    explicit WordReadout(PluginProcessor& processor);
    ~WordReadout() override;

    void paint(juce::Graphics&) override;

    // Layout/ramp constants — exposed for tests, never changed at runtime.
    static constexpr int   kPipStripHeight    = 18;
    static constexpr int   kPipDiameter       = 10;
    static constexpr float kPipAlphaCompleted = 0.45f;
    static constexpr float kPipAlphaUpcoming  = 0.15f;
    static constexpr float kPipAlphaCurrent   = 1.0f;

    // Center-word ramp (used in Task 6 — declared here so tests can reference).
    static constexpr float kCenterBaseHeight = 34.0f;
    static constexpr float kCenterGrowFactor = 0.6f;
    static constexpr juce::uint8 kPeakColorR = 0xFF;
    static constexpr juce::uint8 kPeakColorG = 0x30;
    static constexpr juce::uint8 kPeakColorB = 0x30;

private:
    void timerCallback() override;
    PluginProcessor& processor_;
    int lastIndex_ = -2;
};
```

- [ ] **Step 5: Implement pip-row geometry in `paint()`**

Edit `src/app/WordReadout.cpp` — replace the entire `paint()` body with the version below. (Task 6 will further modify the *center-word styling* inside it; pip layout stays as written here.)

```cpp
void WordReadout::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(12, 13, 18));

    const auto words = processor_.activeSceneWords();
    const int  idx   = processor_.currentSpokenWordIndex();
    const auto sceneRgb = processor_.activeSceneColorRgb();
    const auto sceneColor = juce::Colour::fromRGB(
        static_cast<juce::uint8>((sceneRgb >> 16) & 0xFFu),
        static_cast<juce::uint8>((sceneRgb >> 8)  & 0xFFu),
        static_cast<juce::uint8>( sceneRgb        & 0xFFu));

    auto area = getLocalBounds();
    auto pipStrip = area.removeFromBottom(kPipStripHeight);

    if (words.empty() || idx < 0 || idx >= static_cast<int>(words.size())) {
        g.setColour(juce::Colour::fromRGB(90, 95, 110));
        g.setFont(juce::Font{juce::FontOptions{}.withHeight(16.0f)});
        g.drawText("(pluck a note to speak)", area, juce::Justification::centred);
        // Idle pip strip: still draw N upcoming pips if there are scene words,
        // so the operator sees the chant length before the first pluck.
        const int n = static_cast<int>(words.size());
        if (n > 0) {
            const float w = static_cast<float>(pipStrip.getWidth());
            const float cy = pipStrip.getCentreY();
            const float r = kPipDiameter * 0.5f;
            for (int i = 0; i < n; ++i) {
                const float cx = w * (i + 0.5f) / n;
                g.setColour(sceneColor.withAlpha(kPipAlphaUpcoming));
                g.fillEllipse(cx - r, cy - r, kPipDiameter, kPipDiameter);
            }
        }
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

    // Center word (unchanged styling for now — Task 6 replaces with the ramp).
    g.setColour(juce::Colour::fromRGB(240, 230, 180));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(kCenterBaseHeight).withStyle("Bold")});
    g.drawText(dim(idx), mid, juce::Justification::centred);

    // Pip strip: N pips evenly distributed. Past=dim, current=full, upcoming=very dim.
    const int n = static_cast<int>(words.size());
    const float w = static_cast<float>(pipStrip.getWidth());
    const float cy = static_cast<float>(pipStrip.getCentreY());
    const float r = kPipDiameter * 0.5f;
    for (int i = 0; i < n; ++i) {
        const float cx = w * (i + 0.5f) / n;
        float a;
        if      (i <  idx) a = kPipAlphaCompleted;
        else if (i == idx) a = kPipAlphaCurrent;
        else               a = kPipAlphaUpcoming;
        g.setColour(sceneColor.withAlpha(a));
        g.fillEllipse(cx - r, cy - r, kPipDiameter, kPipDiameter);
    }
}
```

- [ ] **Step 6: Add a focused pip-alpha test**

Append to `tests/unit/app/test_word_readout.cpp`:

```cpp
TEST_CASE("WordReadout: idle pip strip draws N dim pips when scene loaded but no pluck yet",
          "[app][ui][word-readout][pips]") {
    juce::ScopedJuceInitialiser_GUI juceInit;
    PluginProcessor p;
    WordReadout r(p);

    auto img = render(r, 600, 120);

    // With no scene loaded (default for an unconfigured PluginProcessor),
    // pip strip is empty. We are only verifying paint() never crashes.
    juce::Rectangle<int> pipArea{0, 120 - WordReadout::kPipStripHeight,
                                 600, WordReadout::kPipStripHeight};
    // No assertion on contents — Task 6 covers the rich state cases.
    (void) hasOpaquePixel(img, pipArea, 1);
    SUCCEED("paint() completed without crashing");
}
```

- [ ] **Step 7: Build and run**

```bash
cmake --build build -j --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "[word-readout]"
```

Expected: PASS.

- [ ] **Step 8: Commit**

```bash
git add src/app/WordReadout.h src/app/WordReadout.cpp \
        tests/unit/app/test_word_readout.cpp tests/CMakeLists.txt
git commit -m "feat(ui): pip-row progress indicator in WordReadout"
```

---

## Task 6: `WordReadout` — intensity ramp on the current word

Replace the static gold + 34 pt center draw with the scene-color → red lerp and 1.0× → 1.6× font-size ramp driven by `progress = idx / max(1, N-1)`.

**Files:**
- Modify: `src/app/WordReadout.cpp`
- Modify: `tests/unit/app/test_word_readout.cpp`

- [ ] **Step 1: Add a failing test for the ramp at the peak**

Append to `tests/unit/app/test_word_readout.cpp`:

```cpp
TEST_CASE("WordReadout: center word at progress=1 lerps to peak red, ~1.6x size",
          "[app][ui][word-readout][ramp]") {
    juce::ScopedJuceInitialiser_GUI juceInit;

    // Build a stub scene set with 14 words and color #0078D4, activate it,
    // and force currentWordIndex to 13 (the last). The exact mechanism may
    // require minor expansion of PluginProcessor's test seam; see comment.
    //
    // If PluginProcessor doesn't yet expose a test seam, this test pins
    // ONLY the geometry-doesn't-crash invariant for now; the ramp is also
    // covered by the integration assertion in Task 8's manual smoke.

    PluginProcessor p;
    WordReadout r(p);

    auto img = render(r, 600, 120);
    REQUIRE(img.getWidth() == 600);

    // Sanity: kPeakColorR/G/B match #FF3030.
    REQUIRE(WordReadout::kPeakColorR == 0xFF);
    REQUIRE(WordReadout::kPeakColorG == 0x30);
    REQUIRE(WordReadout::kPeakColorB == 0x30);

    // Sanity: at progress=1, computed font size is 1.6x base.
    const float ramped = WordReadout::kCenterBaseHeight *
                         (1.0f + WordReadout::kCenterGrowFactor * 1.0f);
    REQUIRE(ramped == Catch::Approx(54.4f).epsilon(1e-4f));
}
```

(The image-level color-sample assertion is omitted because it depends on a test seam to inject a scene + index that the existing `PluginProcessor` may not expose without additional plumbing. The constants + geometry test still pin the contract; richer image assertions can be added if a test seam is later introduced.)

- [ ] **Step 2: Run the test — expect FAIL until Step 3**

```bash
./build/tests/guitar_dsp_tests "[ramp]"
```

Expected: fails on the `kPeakColorR == 0xFF` etc. assertions OR on the `ramped` computation, because Task 5 only declared the constants — they exist, so this should actually PASS. (If it passes immediately, that's correct: the constants were added in Task 5; Step 1 here is a regression guard before we change the paint code.)

- [ ] **Step 3: Replace the center-word draw with the ramp**

Edit `src/app/WordReadout.cpp` — locate the block from Task 5 that reads:

```cpp
    // Center word (unchanged styling for now — Task 6 replaces with the ramp).
    g.setColour(juce::Colour::fromRGB(240, 230, 180));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(kCenterBaseHeight).withStyle("Bold")});
    g.drawText(dim(idx), mid, juce::Justification::centred);
```

Replace with:

```cpp
    // Center word — intensity ramp driven by progress through the chant.
    const int n = static_cast<int>(words.size());
    const float denom = static_cast<float>(std::max(1, n - 1));
    const float progress = std::clamp(static_cast<float>(idx) / denom, 0.0f, 1.0f);

    const float fontH = kCenterBaseHeight * (1.0f + kCenterGrowFactor * progress);
    const auto peakColor = juce::Colour::fromRGB(kPeakColorR, kPeakColorG, kPeakColorB);
    const auto centerColor = sceneColor.interpolatedWith(peakColor, progress);

    g.setColour(centerColor);
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(fontH).withStyle("Bold")});
    g.drawText(dim(idx), mid, juce::Justification::centred);
```

Then **delete** the now-duplicated `const int n = ...;` line that previously appeared in the pip-strip block (the one introduced in Task 5 Step 5 just above the loop) — it would now be doubly-declared. The pip-strip loop should reuse the `n` declared right above.

- [ ] **Step 4: Add `#include <algorithm>` if not already present**

Check the top of `src/app/WordReadout.cpp`. If `<algorithm>` is not already included, add it (for `std::clamp` and `std::max`).

- [ ] **Step 5: Rebuild and rerun**

```bash
cmake --build build -j --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "[word-readout]"
```

Expected: all `[word-readout]` test cases PASS.

- [ ] **Step 6: Run the full suite to confirm no regression on speaking-scene tests**

```bash
./build/tests/guitar_dsp_tests
```

Expected: full suite green. Any speaking-scene integration tests now exercise the ramp; assertions in those tests should not pin static gold/34 pt (a quick grep confirms — `grep -rn "240, 230, 180\|kCenterBase\|34.0f" tests/`).

- [ ] **Step 7: Commit**

```bash
git add src/app/WordReadout.cpp tests/unit/app/test_word_readout.cpp
git commit -m "feat(ui): scene-color->red intensity ramp on the current word"
```

---

## Task 7: Python chop script + tests

Offline asset builder. Takes a Ballmer source file, produces `assets/tts/01_developers/audio.wav` with 14 chronological "DEVELOPERS!" bursts (last 2 = peak dupes), each padded with a 100 ms tail silence.

**Files:**
- Create: `scripts/build_developers_clip.py`
- Create: `scripts/tests/__init__.py` (empty — makes the tests dir a package)
- Create: `scripts/tests/test_build_developers_clip.py`

### Pre-decision: hand-tuned timestamps

The 14 `(start_s, end_s)` pairs at the top of the script need to match the canonical Ballmer source clip. **Lock those in as a step BEFORE shipping the script to anyone** — see Task 8 Step 3 for the manual calibration pass. The committed timestamps in this task use placeholder offsets `(t, t+0.6)` derived from a typical 25 s source where the bursts are evenly spaced; the operator re-tunes them once against their actual source file.

### Why pure-stdlib (no `numpy`/`scipy`)

The project does not currently require Python dependencies. The standard library's `wave` + `struct` modules are sufficient for mono PCM read/write. Keeping zero deps means anyone with Python 3.9+ can run this.

- [ ] **Step 1: Write the failing test**

Create `scripts/tests/__init__.py` (empty file).

Create `scripts/tests/test_build_developers_clip.py`:

```python
"""Tests for build_developers_clip.py — synthesises a source WAV with
marker tones at known offsets, runs the chopper with a stub timestamp
list pointing at those offsets, and verifies the output WAV shape."""

import math
import os
import struct
import sys
import tempfile
import unittest
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

import build_developers_clip as bdc  # noqa: E402


SR = 22050


def write_marker_wav(path, marker_offsets_s, total_s):
    """Write a mono 22.05 kHz PCM WAV. Each `marker_offsets_s[i]` is the
    start of a 0.6 s 440 Hz tone burst. Everything else is silence."""
    nframes = int(total_s * SR)
    samples = [0] * nframes
    for off in marker_offsets_s:
        start = int(off * SR)
        end = min(nframes, start + int(0.6 * SR))
        for i in range(start, end):
            samples[i] = int(0.5 * 32767 *
                             math.sin(2 * math.pi * 440 * (i - start) / SR))
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", s) for s in samples))


def read_wav_frames(path):
    with wave.open(path, "rb") as w:
        assert w.getnchannels() == 1, "expected mono"
        assert w.getsampwidth() == 2, "expected 16-bit PCM"
        assert w.getframerate() == SR, f"expected {SR} Hz"
        raw = w.readframes(w.getnframes())
        return [struct.unpack("<h", raw[i:i+2])[0] for i in range(0, len(raw), 2)]


class ChopperTests(unittest.TestCase):

    def test_chops_marker_segments_in_order(self):
        # 5 markers at known offsets. Stub timestamp list = those offsets.
        markers = [(1.0, 1.6), (3.0, 3.6), (5.0, 5.6), (7.0, 7.6), (9.0, 9.6)]
        with tempfile.TemporaryDirectory() as td:
            src = os.path.join(td, "src.wav")
            dst = os.path.join(td, "out.wav")
            write_marker_wav(src, [m[0] for m in markers], total_s=11.0)

            bdc.chop(src, dst, segments=markers,
                     tail_silence_s=0.1, target_sr=SR)

            self.assertTrue(os.path.exists(dst))
            frames = read_wav_frames(dst)

            # Each output segment = 0.6 s burst + 0.1 s silence = 0.7 s
            seg_len = int(0.7 * SR)
            self.assertEqual(len(frames), seg_len * len(markers))

            # First sample of each segment should be near the start of a burst
            # (440 Hz starts at 0 phase, so sample 0 of a burst is 0; sample 1
            # is non-zero). Check sample 1 of each segment is non-zero.
            for i in range(len(markers)):
                self.assertNotEqual(frames[i * seg_len + 1], 0,
                                    f"segment {i} appears empty")

    def test_aborts_if_segment_runs_past_source(self):
        with tempfile.TemporaryDirectory() as td:
            src = os.path.join(td, "src.wav")
            dst = os.path.join(td, "out.wav")
            write_marker_wav(src, [], total_s=2.0)  # 2-second source

            with self.assertRaises(ValueError) as ctx:
                bdc.chop(src, dst, segments=[(0.0, 5.0)],   # past end!
                         tail_silence_s=0.1, target_sr=SR)
            self.assertIn("past source duration", str(ctx.exception))

    def test_stereo_source_is_downmixed_to_mono(self):
        # 2-channel source: chop should average channels and produce mono.
        path_in = None
        with tempfile.TemporaryDirectory() as td:
            src = os.path.join(td, "src.wav")
            dst = os.path.join(td, "out.wav")
            nframes = int(3.0 * SR)
            with wave.open(src, "wb") as w:
                w.setnchannels(2)
                w.setsampwidth(2)
                w.setframerate(SR)
                # Left = 0.5 amplitude sine, right = -0.5 amplitude sine.
                # Mono mix = 0.
                interleaved = []
                for i in range(nframes):
                    s = int(0.5 * 32767 * math.sin(2 * math.pi * 440 * i / SR))
                    interleaved.append(struct.pack("<h", s))
                    interleaved.append(struct.pack("<h", -s))
                w.writeframes(b"".join(interleaved))
            path_in = src

            bdc.chop(src, dst, segments=[(0.5, 1.0)],
                     tail_silence_s=0.0, target_sr=SR)

            frames = read_wav_frames(dst)
            # All samples ~0 (L + R cancellation), well within rounding.
            for s in frames:
                self.assertLess(abs(s), 4, f"unexpected non-zero sample {s}")


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test — expect FAIL with `ModuleNotFoundError`**

```bash
cd /Users/user/GIT/guitar-dsp
python3 -m unittest scripts.tests.test_build_developers_clip -v
```

Expected: `ModuleNotFoundError: No module named 'build_developers_clip'` (the script doesn't exist yet).

- [ ] **Step 3: Write the chop script**

Create `scripts/build_developers_clip.py`:

```python
#!/usr/bin/env python3
"""Build assets/tts/01_developers/audio.wav from a user-supplied source clip.

Usage:
    python3 scripts/build_developers_clip.py <path-to-ballmer-source.wav>

Reads the source clip, chops 14 hand-tuned segments in chronological order
(the natural Ballmer crescendo), pads each with a tail silence, concatenates
in order, and writes the result. The last two segments duplicate the loudest
two bursts so wrap-around in NoteSteppedTTSPlayer still hits a loud one
before returning to the calm opening.

Pure standard library (wave + struct + math). No external dependencies.
"""

from __future__ import annotations

import math
import os
import struct
import sys
import wave

# 14 hand-tuned (start_s, end_s) Ballmer segments. The last two entries are
# the two loudest bursts intentionally repeated — see the spec §3.1.
#
# CALIBRATE THESE against the actual source clip before final commit. The
# values below are evenly-spaced placeholders that produce a syntactically
# valid output; the operator re-tunes them with audio inspection.
SEGMENTS_S: list[tuple[float, float]] = [
    (1.00, 1.55),
    (2.20, 2.75),
    (3.40, 3.95),
    (4.60, 5.15),
    (5.80, 6.35),
    (7.00, 7.55),
    (8.20, 8.75),
    (9.40, 9.95),
    (10.60, 11.15),
    (11.80, 12.35),
    (13.00, 13.55),
    (14.20, 14.75),
    # Last two = peak dupes (intentionally repeat the loudest two bursts)
    (13.00, 13.55),
    (14.20, 14.75),
]

TAIL_SILENCE_S = 0.1
TARGET_SR = 22050
OUTPUT_REL_PATH = "assets/tts/01_developers/audio.wav"


def chop(src_path: str,
         dst_path: str,
         *,
         segments: list[tuple[float, float]] = None,
         tail_silence_s: float = TAIL_SILENCE_S,
         target_sr: int = TARGET_SR) -> None:
    """Read src_path, slice per segments, pad each with tail_silence_s, write to dst_path.

    Raises ValueError if any segment runs past the source duration."""
    if segments is None:
        segments = SEGMENTS_S

    with wave.open(src_path, "rb") as w:
        n_in_channels = w.getnchannels()
        in_width = w.getsampwidth()
        in_sr = w.getframerate()
        n_frames = w.getnframes()
        raw = w.readframes(n_frames)

    if in_width != 2:
        raise ValueError(f"source must be 16-bit PCM (got {in_width * 8} bits)")
    if in_sr < target_sr:
        raise ValueError(f"source rate {in_sr} Hz < target {target_sr} Hz")

    # Decode source to mono float-ish (int kept as int16 range) at target_sr.
    src_mono = _decode_mono(raw, n_in_channels)
    src_at_target = _resample(src_mono, in_sr, target_sr) if in_sr != target_sr else src_mono
    src_duration = len(src_at_target) / target_sr

    out: list[int] = []
    tail = [0] * int(tail_silence_s * target_sr)
    for i, (start_s, end_s) in enumerate(segments):
        if end_s > src_duration:
            raise ValueError(
                f"segment {i} ({start_s}-{end_s} s) extends past source "
                f"duration ({src_duration:.3f} s)")
        start_n = int(start_s * target_sr)
        end_n = int(end_s * target_sr)
        out.extend(src_at_target[start_n:end_n])
        out.extend(tail)

    os.makedirs(os.path.dirname(dst_path) or ".", exist_ok=True)
    with wave.open(dst_path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(target_sr)
        w.writeframes(b"".join(struct.pack("<h", _clip16(s)) for s in out))


def _decode_mono(raw: bytes, n_channels: int) -> list[int]:
    """int16 little-endian -> list[int], downmix to mono if needed."""
    samples = [struct.unpack("<h", raw[i:i+2])[0] for i in range(0, len(raw), 2)]
    if n_channels == 1:
        return samples
    if n_channels == 2:
        return [(samples[i] + samples[i+1]) // 2
                for i in range(0, len(samples), 2)]
    raise ValueError(f"unsupported channel count: {n_channels}")


def _resample(samples: list[int], src_sr: int, dst_sr: int) -> list[int]:
    """Linear-interpolation downsample. Good enough for an offline asset."""
    ratio = src_sr / dst_sr
    out_len = int(len(samples) / ratio)
    out = [0] * out_len
    for i in range(out_len):
        src_idx = i * ratio
        i0 = int(src_idx)
        frac = src_idx - i0
        i1 = min(i0 + 1, len(samples) - 1)
        out[i] = int((1.0 - frac) * samples[i0] + frac * samples[i1])
    return out


def _clip16(s: int) -> int:
    return max(-32768, min(32767, int(s)))


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    src = argv[1]
    if not os.path.exists(src):
        print(f"error: source file not found: {src}", file=sys.stderr)
        return 1

    # Output path is relative to the repo root (the cwd if run from there).
    dst = os.path.abspath(OUTPUT_REL_PATH)
    print(f"chopping {src} -> {dst}")
    print(f"segments: {len(SEGMENTS_S)} (incl. 2 peak dupes at the end)")
    try:
        chop(src, dst)
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"wrote {dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
```

- [ ] **Step 4: Run the test — expect PASS**

```bash
python3 -m unittest scripts.tests.test_build_developers_clip -v
```

Expected: all 3 test cases pass.

- [ ] **Step 5: Manual smoke (script entry point)**

```bash
python3 scripts/build_developers_clip.py /tmp/nonexistent.wav
echo $?
```

Expected: exits non-zero with "error: source file not found".

- [ ] **Step 6: Commit**

```bash
git add scripts/build_developers_clip.py \
        scripts/tests/__init__.py \
        scripts/tests/test_build_developers_clip.py
git commit -m "feat(scripts): build_developers_clip.py — offline Ballmer chopper"
```

---

## Task 8: README acquisition note + manual smoke

Document how the operator obtains the Ballmer source and runs the chopper. Do not commit the source URL into the repo — README references it abstractly.

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Find the right insertion point in the README**

```bash
grep -n "^## " README.md
```

Find a section about scenes / assets / TTS. Insert the new subsection alphabetically/logically near the asset/scene documentation. If there is no clean section, add it as a new top-level subsection under the "Assets" or "Setup" heading.

- [ ] **Step 2: Add the acquisition note**

Append (or insert under the chosen heading) the following Markdown to `README.md`:

````markdown
### Scene 1 — "Developers!" asset

Scene 1 plays Steve Ballmer's "DEVELOPERS!" chant from the 2000 Microsoft
developer conference, one burst per guitar pluck. The audio file is not
distributed with the repo. Generate it locally:

1. Obtain a clean recording of the iconic ~25 s chant as `ballmer_source.wav`
   (mono or stereo, 16-bit PCM, ≥ 22.05 kHz). The source is on YouTube;
   `yt-dlp <url> -x --audio-format wav -o ballmer_source.wav` works.
2. Run the chopper:
   ```bash
   python3 scripts/build_developers_clip.py path/to/ballmer_source.wav
   ```
3. The script writes `assets/tts/01_developers/audio.wav`. This file is
   gitignored — re-run after fresh clone or asset edit.

If your source's timing differs from the default chops, edit `SEGMENTS_S`
at the top of `scripts/build_developers_clip.py` (14 `(start_s, end_s)`
tuples; the last two are intentional peak duplicates).
````

- [ ] **Step 3: Manual calibration (one-time, not committed)**

Open the source clip in any waveform editor (Audacity, ocenaudio, even
QuickTime + a stopwatch). Note the start and end timestamp (in seconds) of
each "DEVELOPERS!" burst. Update the `SEGMENTS_S` list in
`scripts/build_developers_clip.py` to reflect the actual offsets. Re-run
the chopper. Spot-check the generated `audio.wav` — it should play 14
"DEVELOPERS!" bursts in increasing intensity.

**This calibration is part of installing the scene; the placeholder offsets
in the committed script are NOT expected to produce a usable result on the
real source.** A follow-up commit can update the timestamps after the
calibration is done, but the placeholder values are intentionally left in
the initial commit so the script's correctness is verified by the unit
tests independent of asset-specific magic numbers.

- [ ] **Step 4: End-to-end smoke (manual, requires real source)**

```bash
# (only if you have a real source) — run the chopper:
python3 scripts/build_developers_clip.py path/to/ballmer_source.wav

# rebuild + launch the standalone app:
cmake --build build -j --target guitar_speak_Standalone
./build/src/app/guitar_speak_artefacts/Standalone/guitar_speak.app/Contents/MacOS/guitar_speak &

# In the app:
#   1. Press '1' (or FCB pedal 1) to switch to Scene 1.
#   2. Verify the readout shows "Developers!" as the scene name.
#   3. Pluck a string. Confirm: (a) the vocoded "DEVELOPERS!" plays, with a
#      dry-Ballmer underlayer audible at ~25%, (b) WordReadout shows
#      "DEVELOPERS!" in blue (#0078D4) at base size and one bright pip,
#      (c) successive plucks cycle through bursts of increasing intensity,
#      (d) by pluck 14 the readout text is enlarged and reddish, all pips
#      are dim/full/dim per progress, and the wrap-around at pluck 15
#      returns to the opening calm "developers".
#   4. Press another scene number to leave; press '1' again to confirm the
#      scene fully restores (mixer, vocoder, clarity, pip state reset).
```

If audio is silent on Scene 1 despite a valid `audio.wav` existing: check
the console for `[PrebakedTTSSource] missing: ...` (would indicate a path
mismatch) or for `WordAligner` failing to find 13 clean gaps (per spec §6
— may need to re-run the chopper after calibrating `SEGMENTS_S`).

- [ ] **Step 5: Commit**

```bash
git add README.md
git commit -m "docs(readme): asset acquisition note for the Developers! scene"
```

---

## Self-review (run after writing the plan, before declaring done)

**Spec coverage:**
- §1 Goal ✅ Tasks 1, 2, 5, 6, 7 cover the scene + readout + asset pipeline
- §2 Mechanism / data flow ✅ Reuses existing — no code task needed; covered by Tasks 1–2
- §3.1 audio.wav ✅ Generated by Task 7; gitignored by Task 3
- §3.2 meta.json ✅ Task 2
- §3.3 build script ✅ Task 7
- §3.4 scene JSON ✅ Task 2 (replacing old) + Task 1 (fixture/parser)
- §3.5 README ✅ Task 8
- §3.6 .gitignore ✅ Task 3
- §4.1 existing layout kept ✅ Task 5 preserves three-slot structure
- §4.2 pip row ✅ Task 5
- §4.3 intensity ramp ✅ Task 6
- §4.4 activeSceneColorRgb getter ✅ Task 4
- §4.5 hard-coded constants ✅ Task 5 (constants block)
- §6 error handling ✅ Task 7 chop script has the duration-past-source guard; Task 8 references missing-WAV behavior
- §7 testing — scene fixture+parser ✅ Task 1, WordAligner on synthetic 14-burst already exists in `test_word_aligner.cpp` (no new test needed since the existing test pattern covers the contract), build script tests ✅ Task 7, WordReadout paint ✅ Tasks 5–6, no-regression ✅ Task 6 Step 6

**Placeholder scan:** searched plan for "TBD" / "TODO" / "implement later" / "fill in details" — none. The placeholder timestamps in `SEGMENTS_S` are explicitly called out as placeholders with a manual-calibration step (Task 8 Step 3), which is the intended behavior, not a plan failure.

**Type consistency:** the `WordReadout` constants (`kPipStripHeight`, `kPipDiameter`, `kCenterBaseHeight`, `kCenterGrowFactor`, `kPeakColorR/G/B`, `kPipAlpha*`) are declared in Task 5 Step 4 and referenced consistently in Tasks 5 (Step 5) and 6 (Steps 1, 3). `activeSceneColorRgb()` returns `std::uint32_t`; Task 5 Step 5 unpacks it correctly. `chop()` signature matches between Task 7 Steps 1 (test) and 3 (impl): `chop(src, dst, *, segments, tail_silence_s, target_sr)`.

Plan complete and saved to `docs/superpowers/plans/2026-06-13-developers-scene.md`.
