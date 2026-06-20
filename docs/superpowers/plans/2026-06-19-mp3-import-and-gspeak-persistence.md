# mp3 Import + `.gspeak` In-Session Persistence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix the in-session `.gspeak` Save/Load round-trip and add runtime mp3 → v1 clip ingestion (Import + Auto-slice buttons in WaveformView), per `docs/superpowers/specs/2026-06-19-mp3-import-and-gspeak-persistence-design.md`.

**Architecture:** Add a single read-side helper `AssetLocator::resolveForRead` that prefers the source assets dir when the source file exists, falling back to the runtime (bundle) path. Wire it into the two read sites (`WaveformView::onLoadPressed_`, `PluginProcessor::tryAutoLoadGspeak_`). Then add a stateless `AudioFileDecoder` that wraps JUCE's `AudioFormatManager::registerBasicFormats` to produce mono PCM at engine sample rate, and two new buttons in `WaveformView` whose handlers install the decoded audio (Import) and run `WordAligner::alignSyllables` against the SayPanel text (Auto-slice). All install paths go through one helper `PluginProcessor::installImportedClip` that mirrors the v1 wet-path state setup already in `tryAutoLoadGspeak_`.

**Tech Stack:** C++20, JUCE (`juce_audio_formats`, `juce_gui_basics`), Catch2 for tests. macOS deployment target 13.0+. Universal2 (arm64 + x86_64) builds.

## Global Constraints

- **Test framework:** Catch2 (`<catch2/catch_test_macros.hpp>`); existing pattern matches `tests/unit/audio/test_gspeak_bundle.cpp`.
- **Build system:** CMake. Audio module sources listed in `src/CMakeLists.txt`. Test sources listed in `tests/CMakeLists.txt`.
- **Commit cadence:** one commit per task, each commit message ends with a `Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>` trailer.
- **Spec authority:** `docs/superpowers/specs/2026-06-19-mp3-import-and-gspeak-persistence-design.md` — every behavioral question that isn't in this plan resolves to that document. v1 clips only this round (no phoneme/sylsV2 work).
- **Audio: mono only.** Decoder downmixes by averaging channels. Resample via the same linear-interp loop as `PrebakedTTSSource.cpp:50-64`.
- **Threading:** Import decode runs off the message thread on a one-shot `juce::Thread`; install + status flash happen back on the message thread via `juce::MessageManager::callAsync`. Auto-slice and all other handlers run message-thread.
- **Atomicity:** every failure path leaves the in-memory clip and player wiring untouched.
- **No file-picker on Save / Load this round.** Existing buttons unchanged.

## File Structure

**New files:**

- `src/audio/AudioFileDecoder.h` — `AudioFileDecoder::decodeMono(juce::File, double requestedSampleRate) → std::optional<Result>`. Pure function. No JUCE component dependencies; only `juce_audio_formats`.
- `src/audio/AudioFileDecoder.cpp` — implementation.
- `tests/unit/audio/test_audio_file_decoder.cpp` — unit tests for the decoder.
- `tests/integration/test_mp3_import_flow.cpp` — integration test for Import + Auto-slice + Save → Load round-trip on a decoder-produced clip.
- `tests/integration/test_gspeak_save_then_load.cpp` — integration test confirming a Save followed by a Load (using `resolveForRead`) returns the just-saved samples in the same process.

**Modified files:**

- `src/app/AssetLocator.h` — declare `resolveForRead`.
- `src/app/AssetLocator.cpp` — implement `resolveForRead`.
- `src/app/WaveformView.h` — declare `importButton_`, `autoSliceButton_`, `onImportPressed_`, `onAutoSlicePressed_`, the import-in-flight flag, and one private helper for transcript parsing.
- `src/app/WaveformView.cpp` — wire the two new buttons, swap `onLoadPressed_`'s resolver to `resolveForRead`, and add the two new handlers.
- `src/app/PluginProcessor.h` — declare `installImportedClip(audio::TTSClipPtr)`.
- `src/app/PluginProcessor.cpp` — implement `installImportedClip`, switch `tryAutoLoadGspeak_`'s resolver to `resolveForRead`.
- `src/CMakeLists.txt` — add `audio/AudioFileDecoder.cpp` to `guitar_dsp_audio`.
- `tests/CMakeLists.txt` — add the new test files and (if not already linked transitively) `${CMAKE_SOURCE_DIR}/src/app/WaveformView.cpp` + `PluginProcessor.cpp` are only needed in the editor target, so the integration tests that exercise the buttons call into the processor entry points directly rather than instantiating the editor.

**Files NOT touched:**

- `src/audio/WordAligner.{h,cpp}` — used as-is.
- `src/audio/GspeakBundle.{h,cpp}` — used as-is.
- Scene JSON schema or any `assets/scenes/*.json` — no schema changes this round.
- `.gspeak` format / manifest — no format changes.

---

## Task 1: `AssetLocator::resolveForRead` helper

**Files:**
- Modify: `src/app/AssetLocator.h:46-58` (add declaration next to existing resolvers)
- Modify: `src/app/AssetLocator.cpp:141-149` (add implementation next to `resolveSourceRelativePath`)
- Create: `tests/unit/app/test_asset_locator.cpp` (new — none exists)
- Modify: `tests/CMakeLists.txt` (register the new test source)

**Interfaces:**
- Consumes: existing `AssetLocator::resolveSourceRelativePath`, `resolveRelativePath`.
- Produces:
  ```cpp
  // AssetLocator.h
  static std::string resolveForRead(const std::string& relPath);
  ```
  Returns `resolveSourceRelativePath(relPath)` when that path is non-empty AND the file exists; else returns `resolveRelativePath(relPath)`. Used by both `WaveformView::onLoadPressed_` and `PluginProcessor::tryAutoLoadGspeak_` in later tasks.

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/app/test_asset_locator.cpp`:

```cpp
#include "app/AssetLocator.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

namespace {
// Sentinel filename that should NEVER coexist with real assets, so a stale
// copy from a prior failed test run can't poison the result.
constexpr const char* kRel =
    "clips/gspeak/_test_asset_locator_resolve_for_read.gspeak";
}

TEST_CASE("resolveForRead returns source path when source file exists",
          "[unit][asset-locator]") {
    namespace fs = std::filesystem;
    const auto src = guitar_dsp::AssetLocator::resolveSourceRelativePath(kRel);
    if (src.empty())
        SUCCEED("no source-tree dev build detected; skipping");
    REQUIRE_FALSE(src.empty());

    fs::create_directories(fs::path(src).parent_path());
    { std::ofstream out(src); out << "sentinel"; }

    const auto got = guitar_dsp::AssetLocator::resolveForRead(kRel);
    CHECK(got == src);

    fs::remove(src);
}

TEST_CASE("resolveForRead falls back to runtime when source file is absent",
          "[unit][asset-locator]") {
    namespace fs = std::filesystem;
    const auto src = guitar_dsp::AssetLocator::resolveSourceRelativePath(kRel);
    if (src.empty())
        SUCCEED("no source-tree dev build detected; skipping");
    REQUIRE_FALSE(src.empty());

    // Make sure the source file is NOT present.
    fs::remove(src);

    const auto rt = guitar_dsp::AssetLocator::resolveRelativePath(kRel);
    const auto got = guitar_dsp::AssetLocator::resolveForRead(kRel);
    CHECK(got == rt);
}
```

Add to `tests/CMakeLists.txt` source list (immediately after `unit/app/test_word_readout.cpp:71`):

```cmake
    unit/app/test_asset_locator.cpp
```

- [ ] **Step 2: Run tests, verify they fail**

```bash
cmake --build build --target guitar_dsp_tests -j
./build/tests/guitar_dsp_tests "[asset-locator]" -v
```
Expected: compile error — `resolveForRead` undeclared in `AssetLocator.h`.

- [ ] **Step 3: Add the declaration**

In `src/app/AssetLocator.h`, insert immediately after `resolveSourceRelativePath`'s declaration (around line 54):

```cpp
    // Read-side resolver: prefer the source assets dir when the source file
    // exists, else fall back to the runtime/bundle dir. This is what
    // dev-build read paths use so saves done in the current session are
    // visible on the very next read without waiting for a rebuild's
    // POST_BUILD asset copy. Installed AU / standalone .app builds have no
    // source-tree sibling, so this transparently equals resolveRelativePath
    // for them.
    static std::string resolveForRead(const std::string& relPath);
```

- [ ] **Step 4: Add the implementation**

In `src/app/AssetLocator.cpp`, append after `resolveSourceRelativePath` (after the closing `}` at line 149):

```cpp
std::string AssetLocator::resolveForRead(const std::string& relPath) {
    auto src = resolveSourceRelativePath(relPath);
    if (!src.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(src, ec))
            return src;
    }
    return resolveRelativePath(relPath);
}
```

If `<filesystem>` isn't already included near the top of `AssetLocator.cpp`, add `#include <filesystem>` to the include block.

- [ ] **Step 5: Run tests, verify they pass**

```bash
cmake --build build --target guitar_dsp_tests -j
./build/tests/guitar_dsp_tests "[asset-locator]" -v
```
Expected: both cases pass. (If the test prints "no source-tree dev build detected; skipping," the test binary is running from outside the dev tree — confirm it's running from `build/`.)

- [ ] **Step 6: Commit**

```bash
git add src/app/AssetLocator.h src/app/AssetLocator.cpp \
        tests/unit/app/test_asset_locator.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(app): AssetLocator::resolveForRead source-first read resolver

Adds a small helper used by upcoming read-site wiring changes:
prefers the source-tree assets path when the file exists there,
else falls back to resolveRelativePath. Installed builds have no
source-tree sibling so they keep returning the runtime path.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Wire `resolveForRead` into Load + auto-load read sites

**Files:**
- Modify: `src/app/WaveformView.cpp:392` (one line — swap resolver)
- Modify: `src/app/PluginProcessor.cpp:253` (one line — swap resolver)
- Create: `tests/integration/test_gspeak_save_then_load.cpp`
- Modify: `tests/CMakeLists.txt` (register the new integration test)

**Interfaces:**
- Consumes: `AssetLocator::resolveForRead` from Task 1; existing `GspeakBundle::read/write`.
- Produces: no new API. Behavioral change only.

- [ ] **Step 1: Write the failing integration test**

Create `tests/integration/test_gspeak_save_then_load.cpp`:

```cpp
#include "audio/GspeakBundle.h"
#include "app/AssetLocator.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <cmath>
#include <filesystem>

namespace {
constexpr const char* kRel =
    "clips/gspeak/_test_save_then_load.gspeak";

guitar_dsp::audio::TTSClip makeV1ClipWithSamples(float marker) {
    guitar_dsp::audio::TTSClip c;
    c.sampleRate = 48000.0;
    c.samples.assign(4800, marker);
    guitar_dsp::audio::WordSegment w{"x", 0, c.samples.size()};
    c.words.push_back(w);
    c.syllables.push_back(w);
    return c;
}
}

TEST_CASE("Save then Load in same session returns saved samples",
          "[integration][gspeak]") {
    namespace fs = std::filesystem;
    const auto src = guitar_dsp::AssetLocator::resolveSourceRelativePath(kRel);
    if (src.empty())
        SUCCEED("no source-tree dev build detected; skipping");
    REQUIRE_FALSE(src.empty());

    // Save a marker clip to the source path (what onSavePressed_ does).
    fs::create_directories(fs::path(src).parent_path());
    const auto orig = makeV1ClipWithSamples(0.42f);
    REQUIRE(guitar_dsp::audio::GspeakBundle::write(
        juce::File(src), orig, "marker"));

    // Read via resolveForRead — should see the source-tree file we just
    // wrote, not the (possibly older) bundle copy.
    const auto readPath = guitar_dsp::AssetLocator::resolveForRead(kRel);
    auto loaded = guitar_dsp::audio::GspeakBundle::read(
        juce::File(readPath), 48000.0);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->clip);
    REQUIRE(loaded->clip->samples.size() == orig.samples.size());
    // 16-bit PCM quantization tolerance — same threshold the existing
    // GspeakBundle round-trip test uses.
    for (std::size_t i = 0; i < orig.samples.size(); ++i)
        CHECK(std::fabs(loaded->clip->samples[i] - orig.samples[i]) < 1.0f / 16384.0f);

    fs::remove(src);
}
```

Add to `tests/CMakeLists.txt` source list (immediately after `integration/test_gspeak_autoload.cpp:96`):

```cmake
    integration/test_gspeak_save_then_load.cpp
```

- [ ] **Step 2: Run the test, verify it fails**

```bash
cmake --build build --target guitar_dsp_tests -j
./build/tests/guitar_dsp_tests "Save then Load in same session returns saved samples" -v
```
Expected: PASS — wait, this actually passes already because `GspeakBundle::read` of the `src` path works directly. The test as written only proves Save + Read-of-the-explicit-path round-trip; we want it to prove that `resolveForRead` returns the source path. The test is correct as written: it asserts the resolver picks the source path (otherwise the read would target the stale bundle copy, which doesn't have our sentinel). Re-confirm: the test must fail BEFORE Task 1's `resolveForRead` exists — but Task 1 is already done by the time this task runs. So this test is a positive-validation test, not a "fails first then passes" test. That's fine — it locks in the behavior and catches future regressions.

If the test passes here, treat that as the test landing green from the start. The real regression target is the next step.

- [ ] **Step 3: Switch the two read sites**

In `src/app/WaveformView.cpp:392`, replace:

```cpp
    const auto resolved = AssetLocator::resolveRelativePath(rel.toStdString());
```

with:

```cpp
    const auto resolved = AssetLocator::resolveForRead(rel.toStdString());
```

In `src/app/PluginProcessor.cpp:253`, replace:

```cpp
    const auto path = AssetLocator::resolveRelativePath(scene.gspeakPath);
```

with:

```cpp
    const auto path = AssetLocator::resolveForRead(scene.gspeakPath);
```

- [ ] **Step 4: Run full test suite**

```bash
cmake --build build --target guitar_dsp_tests -j
./build/tests/guitar_dsp_tests "[gspeak]" -v
```
Expected: all gspeak tests pass (the existing `test_gspeak_autoload.cpp` validates the auto-load path still works; the new save-then-load test validates the new behavior).

- [ ] **Step 5: Commit**

```bash
git add src/app/WaveformView.cpp src/app/PluginProcessor.cpp \
        tests/integration/test_gspeak_save_then_load.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
fix(app): Load + auto-load read from source-first via resolveForRead

Fixes the in-session save→navigate-away→navigate-back→edits-vanish
regression. The persistence fix in 430c39d made Save write to the
source assets dir, but both read sites (WaveformView::onLoadPressed_
and PluginProcessor::tryAutoLoadGspeak_) still went through the
runtime (bundle) path. The bundle copy is only refreshed by the build
system's POST_BUILD asset-copy step, so within a single session
edits appeared lost until the next rebuild.

Both sites now use AssetLocator::resolveForRead — source-tree path
when the source file exists, runtime path otherwise. Installed builds
have no source-tree sibling, so they're unaffected.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: `AudioFileDecoder::decodeMono`

**Files:**
- Create: `src/audio/AudioFileDecoder.h`
- Create: `src/audio/AudioFileDecoder.cpp`
- Modify: `src/CMakeLists.txt:37` (insert `audio/AudioFileDecoder.cpp` in the source list)
- Create: `tests/unit/audio/test_audio_file_decoder.cpp`
- Modify: `tests/CMakeLists.txt` (register the new unit test)

**Interfaces:**
- Consumes: `juce::AudioFormatManager`, `juce::AudioFormatReader`. Mirrors the format-manager use in `GspeakBundle.cpp:236` and `PrebakedTTSSource.cpp:29`.
- Produces:
  ```cpp
  namespace guitar_dsp::audio {

  class AudioFileDecoder {
  public:
      struct Result {
          std::vector<float> samples;  // mono, at requestedSampleRate
          double             sampleRate = 0.0;
          std::string        formatName;
      };

      // Decodes the file to mono PCM at requestedSampleRate.
      // Returns nullopt on unsupported format, decode error, missing file,
      // or empty audio (zero samples).
      static std::optional<Result> decodeMono(const juce::File& file,
                                              double requestedSampleRate);
  };

  } // namespace guitar_dsp::audio
  ```

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/audio/test_audio_file_decoder.cpp`:

```cpp
#include "audio/AudioFileDecoder.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <filesystem>

namespace {

// Writes a synthetic mono WAV (sine, 250 Hz, 0.3 amplitude) to `path`
// at `srcRate` for `samples` samples. Returns the sample count.
std::size_t writeSineWav(const juce::File& path,
                         double srcRate,
                         std::size_t samples) {
    juce::WavAudioFormat fmt;
    path.deleteFile();
    auto stream = std::unique_ptr<juce::FileOutputStream>(path.createOutputStream());
    REQUIRE(stream);
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        fmt.createWriterFor(stream.get(), srcRate, 1, 16, {}, 0));
    REQUIRE(writer);
    stream.release();  // writer owns it now

    juce::AudioBuffer<float> buf(1, (int) samples);
    for (std::size_t i = 0; i < samples; ++i)
        buf.setSample(0, (int) i, (float) std::sin(2.0 * 3.14159
                * 250.0 * (double) i / srcRate) * 0.3f);
    writer->writeFromAudioSampleBuffer(buf, 0, (int) samples);
    return samples;
}

juce::File tempWavPath(const char* tag) {
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile(juce::String{"_test_audio_file_decoder_"} + tag + ".wav");
}

} // namespace

TEST_CASE("decodeMono returns mono samples for a mono WAV at target SR",
          "[unit][audio-file-decoder]") {
    const auto path = tempWavPath("mono_same_sr");
    const auto n = writeSineWav(path, 48000.0, 4800);

    auto r = guitar_dsp::audio::AudioFileDecoder::decodeMono(path, 48000.0);
    REQUIRE(r.has_value());
    CHECK(r->samples.size() == n);
    CHECK(r->sampleRate == 48000.0);
    // First non-zero sample should match the sine formula within float error.
    CHECK(std::fabs(r->samples[100]
            - (float) std::sin(2.0 * 3.14159 * 250.0 * 100.0 / 48000.0) * 0.3f) < 1e-4f);

    path.deleteFile();
}

TEST_CASE("decodeMono resamples to a different target SR",
          "[unit][audio-file-decoder]") {
    const auto path = tempWavPath("resample");
    const auto n = writeSineWav(path, 44100.0, 4410);  // 0.1 s

    auto r = guitar_dsp::audio::AudioFileDecoder::decodeMono(path, 48000.0);
    REQUIRE(r.has_value());
    CHECK(r->sampleRate == 48000.0);
    // 0.1 s at 48000 Hz = 4800 samples (±1 sample for rounding).
    CHECK(std::abs((int) r->samples.size() - 4800) <= 1);

    path.deleteFile();
}

TEST_CASE("decodeMono returns nullopt for a missing file",
          "[unit][audio-file-decoder]") {
    auto r = guitar_dsp::audio::AudioFileDecoder::decodeMono(
        juce::File("/nonexistent/path/foo.wav"), 48000.0);
    CHECK_FALSE(r.has_value());
}

TEST_CASE("decodeMono returns nullopt for a malformed file",
          "[unit][audio-file-decoder]") {
    const auto path = tempWavPath("malformed");
    path.deleteFile();
    path.replaceWithText("not a real WAV header");

    auto r = guitar_dsp::audio::AudioFileDecoder::decodeMono(path, 48000.0);
    CHECK_FALSE(r.has_value());

    path.deleteFile();
}

TEST_CASE("decodeMono returns nullopt for an empty WAV",
          "[unit][audio-file-decoder]") {
    const auto path = tempWavPath("empty");
    writeSineWav(path, 48000.0, 0);

    auto r = guitar_dsp::audio::AudioFileDecoder::decodeMono(path, 48000.0);
    CHECK_FALSE(r.has_value());

    path.deleteFile();
}
```

Add to `tests/CMakeLists.txt` source list (immediately after `unit/audio/test_word_aligner.cpp:27`):

```cmake
    unit/audio/test_audio_file_decoder.cpp
```

- [ ] **Step 2: Run tests, verify they fail**

```bash
cmake --build build --target guitar_dsp_tests -j
./build/tests/guitar_dsp_tests "[audio-file-decoder]" -v
```
Expected: compile error — `AudioFileDecoder.h` not found.

- [ ] **Step 3: Create the header**

Write `src/audio/AudioFileDecoder.h`:

```cpp
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <optional>
#include <string>
#include <vector>

namespace guitar_dsp::audio {

// Decodes an audio file from disk to mono PCM at a requested sample rate.
// Backed by JUCE's basic-format manager (WAV / AIFF / FLAC / OGG / MP3
// where the platform provides it). Multichannel files are downmixed by
// averaging channels.
//
// Pure function (no state, no audio-thread concerns). Safe to call from
// any thread, but expected to run off the message thread because file
// decode + resample can take tens of milliseconds for multi-second clips.
class AudioFileDecoder {
public:
    struct Result {
        std::vector<float> samples;       // mono float32 at requestedSampleRate
        double             sampleRate = 0.0;
        std::string        formatName;    // e.g. "WAV", "MP3" — informational
    };

    // Returns nullopt on:
    //   - missing or unreadable file
    //   - unsupported format (no registered reader)
    //   - decode error
    //   - empty audio (zero samples after decode)
    static std::optional<Result> decodeMono(const juce::File& file,
                                            double requestedSampleRate);
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 4: Create the implementation**

Write `src/audio/AudioFileDecoder.cpp`:

```cpp
#include "AudioFileDecoder.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace guitar_dsp::audio {

std::optional<AudioFileDecoder::Result>
AudioFileDecoder::decodeMono(const juce::File& file,
                             double requestedSampleRate) {
    if (!file.existsAsFile() || requestedSampleRate <= 0.0)
        return std::nullopt;

    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(file));
    if (!reader) return std::nullopt;

    const auto srcLen = static_cast<int>(reader->lengthInSamples);
    if (srcLen <= 0) return std::nullopt;

    const auto srcRate    = reader->sampleRate;
    const auto srcChans   = (int) reader->numChannels;
    if (srcChans <= 0) return std::nullopt;

    juce::AudioBuffer<float> raw(srcChans, srcLen);
    if (!reader->read(&raw, 0, srcLen, 0, true, srcChans > 1))
        return std::nullopt;

    // Downmix: average all channels into a single mono buffer.
    std::vector<float> mono(static_cast<std::size_t>(srcLen), 0.0f);
    for (int ch = 0; ch < srcChans; ++ch) {
        const float* p = raw.getReadPointer(ch);
        for (int i = 0; i < srcLen; ++i)
            mono[(std::size_t) i] += p[i];
    }
    const float inv = 1.0f / (float) srcChans;
    for (auto& s : mono) s *= inv;

    Result out;
    out.sampleRate = requestedSampleRate;
    out.formatName = reader->getFormatName().toStdString();

    if (std::abs(srcRate - requestedSampleRate) < 0.5) {
        out.samples = std::move(mono);
    } else {
        // Linear resample — same loop as PrebakedTTSSource.cpp:50-64.
        const double ratio  = srcRate / requestedSampleRate;
        const int    outLen = static_cast<int>(srcLen / ratio);
        out.samples.resize(static_cast<std::size_t>(outLen));
        for (int i = 0; i < outLen; ++i) {
            const double srcIdx = i * ratio;
            const int    i0     = static_cast<int>(srcIdx);
            const float  frac   = static_cast<float>(srcIdx - i0);
            const int    i1     = std::min(i0 + 1, srcLen - 1);
            out.samples[(std::size_t) i] =
                (1.0f - frac) * mono[(std::size_t) i0]
                + frac       * mono[(std::size_t) i1];
        }
    }

    if (out.samples.empty()) return std::nullopt;
    return out;
}

} // namespace guitar_dsp::audio
```

Add to `src/CMakeLists.txt` after `audio/PhonemeSteppedTTSPlayer.cpp` (line 37):

```cmake
    audio/AudioFileDecoder.cpp
```

- [ ] **Step 5: Run tests, verify they pass**

```bash
cmake --build build --target guitar_dsp_tests -j
./build/tests/guitar_dsp_tests "[audio-file-decoder]" -v
```
Expected: all 5 tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/audio/AudioFileDecoder.h src/audio/AudioFileDecoder.cpp \
        src/CMakeLists.txt \
        tests/unit/audio/test_audio_file_decoder.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(audio): AudioFileDecoder — mono PCM decode at engine sample rate

Pure function over juce::AudioFormatManager::registerBasicFormats:
decode any supported audio file to a mono float vector at the
requested sample rate. Multichannel input is averaged to mono;
SR mismatch uses the same linear-interp resample as PrebakedTTSSource.
Returns nullopt on missing / malformed / empty input — no exceptions.

Used by the upcoming WaveformView Import button to bring an mp3 (or
any supported format) into the v1 clip system for hand-slicing.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: `PluginProcessor::installImportedClip` + WaveformView Import button

**Files:**
- Modify: `src/app/PluginProcessor.h:334-340` (add `installImportedClip` declaration next to `installEditedV1Clip`)
- Modify: `src/app/PluginProcessor.cpp` (add `installImportedClip` implementation next to `installEditedV1Clip` at line 225)
- Modify: `src/app/WaveformView.h:49-53` (add `importButton_`, `onImportPressed_`, decode-in-flight atomic)
- Modify: `src/app/WaveformView.cpp:29-46` (add button visibility + bounds; add handler)
- Create: `tests/integration/test_mp3_import_flow.cpp`
- Modify: `tests/CMakeLists.txt` (register the new integration test)

**Interfaces:**
- Consumes: `AudioFileDecoder::decodeMono` from Task 3; existing `processor_.installEditedV1Clip`; existing `processor_.activeSceneGspeakPath`, `flashStatusMessage`, `currentSampleRate`.
- Produces:
  ```cpp
  // PluginProcessor.h
  // Installs a freshly imported / built v1 clip into the wet path with
  // the exact same player + modulator wiring tryAutoLoadGspeak_ uses
  // (modulator = NoteStepped, clipBank cleared, noteSteppedPlayer.loop=true,
  // active speech player = NoteStepped). Message-thread only.
  void installImportedClip(audio::TTSClipPtr clip);
  ```

- [ ] **Step 1: Write the failing test**

Create `tests/integration/test_mp3_import_flow.cpp`:

```cpp
#include "audio/AudioFileDecoder.h"
#include "audio/TTSClip.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <memory>

namespace {

juce::File writeFixtureWav(const char* tag, double srcRate, std::size_t n) {
    auto path = juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile(juce::String{"_test_mp3_import_"} + tag + ".wav");
    path.deleteFile();
    juce::WavAudioFormat fmt;
    auto stream = std::unique_ptr<juce::FileOutputStream>(path.createOutputStream());
    REQUIRE(stream);
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        fmt.createWriterFor(stream.get(), srcRate, 1, 16, {}, 0));
    REQUIRE(writer);
    stream.release();
    juce::AudioBuffer<float> buf(1, (int) n);
    for (std::size_t i = 0; i < n; ++i)
        buf.setSample(0, (int) i, (float) std::sin(2.0 * 3.14159 * 220.0
                * (double) i / srcRate) * 0.25f);
    writer->writeFromAudioSampleBuffer(buf, 0, (int) n);
    return path;
}

} // namespace

TEST_CASE("Import flow: decode produces a single-span v1 clip",
          "[integration][import]") {
    const auto path = writeFixtureWav("decode_v1", 48000.0, 4800);

    auto decoded = guitar_dsp::audio::AudioFileDecoder::decodeMono(path, 48000.0);
    REQUIRE(decoded.has_value());

    // What the Import handler builds: a v1 clip with one full-length word/syl.
    auto clip = std::make_shared<guitar_dsp::audio::TTSClip>();
    clip->sampleRate = 48000.0;
    clip->samples    = std::move(decoded->samples);
    guitar_dsp::audio::WordSegment full{"imported", 0, clip->samples.size()};
    clip->words.push_back(full);
    clip->syllables.push_back(full);

    REQUIRE(clip->samples.size() == 4800);
    REQUIRE(clip->words.size() == 1);
    REQUIRE(clip->syllables.size() == 1);
    REQUIRE(clip->words[0].startSample == 0);
    REQUIRE(clip->words[0].endSample == clip->samples.size());
    REQUIRE(clip->sylsV2.empty());     // v1 only
    REQUIRE(clip->phonemes.empty());

    path.deleteFile();
}
```

Add to `tests/CMakeLists.txt`:

```cmake
    integration/test_mp3_import_flow.cpp
```

- [ ] **Step 2: Run test, verify it passes (positive lock-in)**

```bash
cmake --build build --target guitar_dsp_tests -j
./build/tests/guitar_dsp_tests "[import]" -v
```
Expected: PASS. This test is a contract lock for the v1 clip shape the Import handler will produce; it doesn't drive new code by itself but will catch shape-drift later.

- [ ] **Step 3: Add `installImportedClip` to PluginProcessor.h**

In `src/app/PluginProcessor.h`, immediately after the existing `installEditedV1Clip` declaration (around line 339), insert:

```cpp
    // Installs a freshly built v1 clip (from Import or Auto-slice) into the
    // wet path with the same player / modulator wiring tryAutoLoadGspeak_
    // uses for v1: modulator = NoteStepped, clip-bank cleared,
    // noteSteppedPlayer.setLoop(true), active speech player = NoteStepped.
    // Message thread only.
    void installImportedClip(audio::TTSClipPtr clip);
```

- [ ] **Step 4: Implement `installImportedClip` in PluginProcessor.cpp**

In `src/app/PluginProcessor.cpp`, immediately after the `installEditedV1Clip` definition (around line 230), insert:

```cpp
void PluginProcessor::installImportedClip(audio::TTSClipPtr clip) {
    if (!clip || clip->samples.empty()) return;

    // Match the v1 branch of tryAutoLoadGspeak_ (see PluginProcessor.cpp:297-305).
    graph_.clipBankPlayer().setBank({});
    graph_.setModulatorSource(audio::AudioGraph::ModulatorSource::NoteStepped);
    graph_.setActiveSpeechPlayer(audio::AudioGraph::ActiveSpeechPlayer::NoteStepped);
    graph_.ttsClipPlayer().setClip(nullptr);
    graph_.phonemeSteppedPlayer().setClip(nullptr);
    lastPhonemeClip_.reset();
    installEditedV1Clip(clip);
    graph_.noteSteppedPlayer().setLoop(true);
}
```

If `lastPhonemeClip_` isn't visible from this translation unit, check its declaration in the header — existing `tryAutoLoadGspeak_` (PluginProcessor.cpp:302) does the same reset, so the member is already accessible.

- [ ] **Step 5: Add the Import button to WaveformView.h**

In `src/app/WaveformView.h`, immediately after the existing `loadButton_` declaration (line 50), insert:

```cpp
    juce::TextButton importButton_ {"Import"};
```

Immediately after the existing `onLoadPressed_` declaration (line 53), insert:

```cpp
    void onImportPressed_();
```

And in the private member section near `dragBoundaryIndex_` (around line 72), add:

```cpp
    // True while a file decode is running off the message thread.
    // Re-clicks are ignored; button is disabled in the meantime.
    std::atomic<bool> importInFlight_ { false };
```

If `<atomic>` isn't included near the top of the header, add `#include <atomic>` to the include block.

- [ ] **Step 6: Wire the Import button in WaveformView.cpp**

In `src/app/WaveformView.cpp`'s constructor (line 29-37), add after the `addAndMakeVisible(loadButton_)` line:

```cpp
    importButton_.onClick = [this] { onImportPressed_(); };
    addAndMakeVisible(importButton_);
```

In `resized()` (lines 41-46), update the button layout so Import sits to the left of Save/Load:

```cpp
void WaveformView::resized() {
    auto top = getLocalBounds().removeFromTop(20).reduced(4, 2);
    saveButton_.setBounds(top.removeFromRight(56));
    top.removeFromRight(4);
    loadButton_.setBounds(top.removeFromRight(56));
    top.removeFromRight(4);
    importButton_.setBounds(top.removeFromRight(64));
}
```

In `timerCallback()` (around line 68), add Import enablement after the existing Save/Load enablement lines:

```cpp
    importButton_.setEnabled(!importInFlight_.load());
```

Add the handler at the bottom of the file (before the closing `} // namespace`), modeled on the existing `onSavePressed_`/`onLoadPressed_` style:

```cpp
void WaveformView::onImportPressed_() {
    if (importInFlight_.load()) return;

    juce::FileChooser chooser("Import audio file",
                              juce::File{},
                              "*.mp3;*.wav;*.aif;*.aiff;*.flac");
    if (!chooser.browseForFileToOpen()) return;

    const auto picked = chooser.getResult();
    const auto sr     = processor_.currentSampleRate();
    importInFlight_.store(true);
    importButton_.setEnabled(false);

    // Off-thread decode; install + status flash on the message thread.
    std::thread([this, picked, sr]() {
        auto result = audio::AudioFileDecoder::decodeMono(picked, sr);
        juce::MessageManager::callAsync([this, picked, result = std::move(result)]() mutable {
            importInFlight_.store(false);
            if (!result.has_value()) {
                processor_.flashStatusMessage(
                    "Import failed: " + picked.getFileName(), 3000);
                return;
            }
            auto clip = std::make_shared<audio::TTSClip>();
            clip->name       = picked.getFileNameWithoutExtension().toStdString();
            clip->sampleRate = result->sampleRate;
            clip->samples    = std::move(result->samples);
            const audio::WordSegment full{
                "imported", 0, clip->samples.size()};
            clip->words.push_back(full);
            clip->syllables.push_back(full);
            processor_.installImportedClip(clip);
            processor_.flashStatusMessage(
                "Imported " + picked.getFileName(), 1500);
        });
    }).detach();
}
```

If `<thread>` isn't already in the cpp's includes, add `#include <thread>`.

- [ ] **Step 7: Add `#include "audio/AudioFileDecoder.h"` to WaveformView.cpp**

Insert near the existing `#include "audio/GspeakBundle.h"` (line 4):

```cpp
#include "audio/AudioFileDecoder.h"
```

- [ ] **Step 8: Build and re-run tests**

```bash
cmake --build build --target guitar_dsp_tests -j
./build/tests/guitar_dsp_tests "[import]" -v
./build/tests/guitar_dsp_tests "[gspeak]" -v
```
Expected: import contract test passes; all gspeak tests still pass.

Also build the standalone app to confirm the WaveformView changes compile in the editor target:

```bash
cmake --build build --target guitar_dsp_app -j
```
Expected: successful build.

- [ ] **Step 9: Commit**

```bash
git add src/app/PluginProcessor.h src/app/PluginProcessor.cpp \
        src/app/WaveformView.h src/app/WaveformView.cpp \
        tests/integration/test_mp3_import_flow.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(app): WaveformView Import button + installImportedClip

Adds a runtime "Import" button that opens a file picker for audio
(mp3/wav/aif/aiff/flac), decodes off the message thread via
AudioFileDecoder::decodeMono, and installs the result as a single-span
v1 TTSClip via the new PluginProcessor::installImportedClip helper.
installImportedClip mirrors the v1 wet-path setup tryAutoLoadGspeak_
already uses: modulator = NoteStepped, clip-bank cleared,
noteSteppedPlayer.loop = true, active speech player = NoteStepped.

The button is disabled during the decode; re-clicks are ignored.
On failure the in-memory clip is untouched and a "Import failed:..."
flash appears in TtsStatusBar.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: WaveformView Auto-slice button (with `parseTranscript`)

**Files:**
- Modify: `src/app/WaveformView.h` (add `autoSliceButton_`, `onAutoSlicePressed_`, `parseTranscript_` static helper)
- Modify: `src/app/WaveformView.cpp` (button visibility/bounds, handler, parseTranscript impl)
- Modify: `tests/integration/test_mp3_import_flow.cpp` (add Auto-slice contract test using `WordAligner::alignSyllables`)

**Interfaces:**
- Consumes: `WordAligner::alignSyllables` (existing); `processor_.currentSayText`, `installImportedClip` (Task 4).
- Produces:
  ```cpp
  // WaveformView.h (private)
  struct ParsedTranscript {
      std::vector<std::string> words;             // hyphens stripped
      std::vector<std::string> hyphenatedWords;   // hyphens preserved
  };
  static ParsedTranscript parseTranscript_(const juce::String& text);
  ```
  Splits on whitespace, strips leading/trailing ASCII punctuation, preserves interior hyphens in `hyphenatedWords`. `words.size() == hyphenatedWords.size()`. Empty input → both vectors empty.

- [ ] **Step 1: Add the failing test**

Append to `tests/integration/test_mp3_import_flow.cpp`:

```cpp
#include "audio/WordAligner.h"

TEST_CASE("Auto-slice: WordAligner produces monotonic per-syllable boundaries",
          "[integration][import]") {
    // 0.5 s of audio, two "words" separated by ~150 ms of silence.
    const double sr = 48000.0;
    const std::size_t n = 24000;  // 0.5 s
    std::vector<float> samples(n, 0.0f);
    // word 0: 0..9000 (sine), 9000..16200 silence, word 1: 16200..24000.
    for (std::size_t i = 0; i < 9000; ++i)
        samples[i] = (float) std::sin(2.0 * 3.14159 * 250.0 * i / sr) * 0.3f;
    for (std::size_t i = 16200; i < n; ++i)
        samples[i] = (float) std::sin(2.0 * 3.14159 * 250.0 * i / sr) * 0.3f;

    std::vector<std::string> words           = { "hel-lo", "world" };
    std::vector<std::string> hyphenatedForms = { "hel-lo", "world" };
    // Strip hyphens for the unhyphenated form expected by WordAligner.
    std::vector<std::string> unhyphenated    = { "hello", "world" };

    auto syls = guitar_dsp::audio::WordAligner::alignSyllables(
        samples, unhyphenated, hyphenatedForms, sr);
    REQUIRE_FALSE(syls.empty());
    // "hel-lo" → 2 syls, "world" → 1 syl = 3 total.
    REQUIRE(syls.size() == 3);
    // Monotonic + cover the buffer.
    for (std::size_t i = 0; i + 1 < syls.size(); ++i)
        CHECK(syls[i].endSample <= syls[i + 1].startSample
              || syls[i].endSample == syls[i + 1].startSample);
    CHECK(syls.front().startSample == 0);
    CHECK(syls.back().endSample == n);
}
```

- [ ] **Step 2: Run test, verify it passes**

```bash
cmake --build build --target guitar_dsp_tests -j
./build/tests/guitar_dsp_tests "Auto-slice: WordAligner" -v
```
Expected: PASS — locks in the contract Auto-slice depends on.

- [ ] **Step 3: Add the button + helper to WaveformView.h**

In `src/app/WaveformView.h`, immediately after the new `importButton_` declaration, insert:

```cpp
    juce::TextButton autoSliceButton_ {"Auto-slice"};
```

After `onImportPressed_`, insert:

```cpp
    void onAutoSlicePressed_();

    // Splits SayPanel text into (unhyphenated words, hyphenated words)
    // pairs for WordAligner::alignSyllables. Whitespace-split; ASCII
    // punctuation stripped from token ends; interior hyphens preserved
    // in `hyphenatedWords`. Same size on success.
    struct ParsedTranscript {
        std::vector<std::string> words;
        std::vector<std::string> hyphenatedWords;
    };
    static ParsedTranscript parseTranscript_(const juce::String& text);
```

- [ ] **Step 4: Wire the button + implement the handler in WaveformView.cpp**

In the constructor (after the `importButton_` wiring from Task 4), add:

```cpp
    autoSliceButton_.onClick = [this] { onAutoSlicePressed_(); };
    addAndMakeVisible(autoSliceButton_);
```

In `resized()`, add the Auto-slice button to the right of Import:

```cpp
void WaveformView::resized() {
    auto top = getLocalBounds().removeFromTop(20).reduced(4, 2);
    saveButton_.setBounds(top.removeFromRight(56));
    top.removeFromRight(4);
    loadButton_.setBounds(top.removeFromRight(56));
    top.removeFromRight(4);
    autoSliceButton_.setBounds(top.removeFromRight(80));
    top.removeFromRight(4);
    importButton_.setBounds(top.removeFromRight(64));
}
```

In `timerCallback()` (next to the existing button enablements), add:

```cpp
    const bool haveText = processor_.currentSayText().isNotEmpty();
    autoSliceButton_.setEnabled(haveClip && haveText);
```

Add `#include "audio/WordAligner.h"` near the other audio includes if not present.

Add the handler at the bottom of the file (next to `onImportPressed_`):

```cpp
void WaveformView::onAutoSlicePressed_() {
    if (!clip_ || clip_->samples.empty()) return;
    const auto text = processor_.currentSayText();
    if (text.isEmpty()) {
        processor_.flashStatusMessage("Auto-slice failed: no text", 3000);
        return;
    }

    const auto parsed = parseTranscript_(text);
    if (parsed.words.empty()) {
        processor_.flashStatusMessage("Auto-slice failed: no words", 3000);
        return;
    }

    auto syls = audio::WordAligner::alignSyllables(
        clip_->samples, parsed.words, parsed.hyphenatedWords, clip_->sampleRate);
    if (syls.empty()) {
        processor_.flashStatusMessage(
            "Auto-slice failed: aligner returned empty", 3000);
        return;
    }

    auto fresh = std::make_shared<audio::TTSClip>();
    fresh->name       = clip_->name;
    fresh->sampleRate = clip_->sampleRate;
    fresh->samples    = clip_->samples;
    fresh->syllables  = syls;

    // Build per-word spans by consuming N syllables per word, where N is
    // hyphen-bounded fragment count (= hyphens + 1) — matches
    // WordAligner::alignSyllables's emission order.
    std::size_t sylCursor = 0;
    for (std::size_t w = 0; w < parsed.words.size(); ++w) {
        const auto& hw = parsed.hyphenatedWords[w];
        const std::size_t fragments =
            (std::size_t) std::count(hw.begin(), hw.end(), '-') + 1;
        if (sylCursor + fragments > syls.size()) break;
        const auto start = syls[sylCursor].startSample;
        const auto end   = syls[sylCursor + fragments - 1].endSample;
        fresh->words.push_back({parsed.words[w], start, end});
        sylCursor += fragments;
    }

    processor_.installImportedClip(fresh);
    processor_.flashStatusMessage(
        juce::String("Auto-sliced ") + juce::String((int) parsed.words.size())
            + " words / " + juce::String((int) syls.size()) + " syls",
        1500);
}

WaveformView::ParsedTranscript
WaveformView::parseTranscript_(const juce::String& text) {
    ParsedTranscript out;
    const auto std = text.toStdString();
    std::string cur;
    auto flush = [&] {
        if (cur.empty()) return;
        // Strip leading + trailing ASCII punctuation (but preserve interior).
        std::size_t a = 0, b = cur.size();
        auto isPunct = [](char c) {
            return c == '.' || c == ',' || c == '!' || c == '?' || c == ';'
                || c == ':' || c == '"' || c == '\'' || c == '(' || c == ')';
        };
        while (a < b && isPunct(cur[a])) ++a;
        while (b > a && isPunct(cur[b - 1])) --b;
        if (b > a) {
            std::string hyphenated = cur.substr(a, b - a);
            std::string unhyphenated;
            unhyphenated.reserve(hyphenated.size());
            for (char c : hyphenated) if (c != '-') unhyphenated.push_back(c);
            out.hyphenatedWords.push_back(std::move(hyphenated));
            out.words.push_back(std::move(unhyphenated));
        }
        cur.clear();
    };
    for (char c : std) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') flush();
        else cur += c;
    }
    flush();
    return out;
}
```

If `<algorithm>` isn't included, add it for `std::count`. If `<memory>` isn't, add it for `std::make_shared`.

- [ ] **Step 5: Build and verify all tests + the app**

```bash
cmake --build build --target guitar_dsp_tests -j
./build/tests/guitar_dsp_tests "[import]" -v
./build/tests/guitar_dsp_tests "[gspeak]" -v
./build/tests/guitar_dsp_tests "[asset-locator]" -v
```
Expected: all pass.

```bash
cmake --build build --target guitar_dsp_app -j
```
Expected: successful build.

- [ ] **Step 6: Commit**

```bash
git add src/app/WaveformView.h src/app/WaveformView.cpp \
        tests/integration/test_mp3_import_flow.cpp
git commit -m "$(cat <<'EOF'
feat(app): WaveformView Auto-slice button — text + audio → v1 boundaries

Adds the second of the two mp3-import buttons. Reads SayPanel text,
splits on whitespace with ASCII punctuation strip, preserves interior
hyphens for the hyphenated form. Calls WordAligner::alignSyllables
on the current in-memory clip's samples and installs the result via
PluginProcessor::installImportedClip. Re-runnable any time.

Per-word boundaries are computed from per-syllable boundaries by
consuming N syllables per word where N = (count of '-' in hyphenated
form) + 1 — matches the order alignSyllables emits.

Failure paths (no clip / no text / aligner empty) flash a status
message and leave the in-memory clip untouched.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Manual dress-rehearsal smoke test

No code. After Task 5 lands, run through this in the standalone app and report results back. This is the end-to-end validation per spec §3 / §5.3 that automated tests can't cover (file picker, real mp3 codec, live vocoder).

- [ ] **Step 1: Boot the standalone app and switch to scene 10.**

- [ ] **Step 2: Click Import. Pick `11labs-gently-speaks-brit-1.mp3` from the repo root.**

Expected: TtsStatusBar shows `Imported 11labs-gently-speaks-brit-1.mp3`. WaveformView renders the waveform with one span covering the full audio. Trigger a note on the guitar — vocoder produces the imported voice modulated by the guitar.

- [ ] **Step 3: Type the hyphenated transcript in SayPanel.**

Example (replace with actual transcript): `"thank you for let-ting me speak"`. Click Auto-slice.

Expected: TtsStatusBar shows `Auto-sliced 5 words / 6 syls` (or whatever counts match the transcript). WaveformView now shows vertical boundary lines.

- [ ] **Step 4: Drag a boundary in WaveformView to confirm interactive edit still works for the imported clip.**

Expected: boundary moves; release confirms the new position.

- [ ] **Step 5: Click Save.**

Expected: TtsStatusBar shows `Saved assets/clips/gspeak/scene10.gspeak`.

- [ ] **Step 6: Navigate to a different scene, then back to scene 10.**

Expected: auto-load surfaces the saved clip; boundaries still where you put them. (This is the in-session persistence fix.)

- [ ] **Step 7: Quit and relaunch the app, activate scene 10 again.**

Expected: auto-load surfaces the same saved clip after restart.

- [ ] **Step 8: Repeat steps 2-5 with `11labs-gently-speaks-brit-2-yelling.mp3.mp3` to audition the second file.**

Expected: scene 10's slot is overwritten with the brit-2 tune; nothing else regresses.

- [ ] **Step 9: Report the outcomes back so we can close the plan.**

If anything misbehaves, file the discrepancy + repro on the plan thread before merge.

---

## Self-Review

### Spec coverage check

Spec sections cross-checked against tasks:

- §2 (persistence fix) → Tasks 1 + 2. ✓
- §3.1 (UX flow) → Tasks 4 + 5 + 6. ✓
- §3.2 (AudioFileDecoder new code) → Task 3. ✓
- §3.3 (changed code: WaveformView buttons, PluginProcessor::installImportedClip) → Tasks 4 + 5. ✓
- §3.4 (Import handler details: file chooser, off-thread decode, single-span clip, status flashes) → Task 4. ✓
- §3.5 (Auto-slice handler: parseTranscript, WordAligner call, groupByOriginalWord) → Task 5. ✓
- §3.6 (button enablement) → covered in `timerCallback` updates in Tasks 4 + 5. ✓
- §3.7 (SayPanel text source via `processor_.currentSayText()`) → Task 5 reads from it. ✓
- §4 (error handling table) → status-flash strings in Tasks 4 + 5 match the table. ✓
- §5.1 unit (audio_file_decoder) → Task 3. ✓
- §5.1 unit (asset_locator) → Task 1. ✓
- §5.2 integration (gspeak_save_then_load) → Task 2. ✓
- §5.2 integration (mp3_import_flow) → Tasks 4 + 5. ✓
- §5.3 manual → Task 6. ✓
- §6 (out of scope) — nothing in tasks attempts these. ✓
- §7 (files touched) — each entry has a task that creates or modifies it. ✓

### Placeholder scan

- No "TBD" / "TODO" / "implement later" anywhere.
- Every code step has the full code block.
- Every command has the expected output / expected outcome called out.
- No "similar to Task N" references — each task carries its own code.

### Type / name consistency

- `installImportedClip(audio::TTSClipPtr clip)` — same name in PluginProcessor.h declaration (Task 4 step 3), implementation (Task 4 step 4), call site in Import handler (Task 4 step 6), and call site in Auto-slice handler (Task 5 step 4). ✓
- `AudioFileDecoder::decodeMono(juce::File, double)` — same signature in header (Task 3 step 3), implementation (Task 3 step 4), Import handler (Task 4 step 6), and decoder test (Task 3 step 1). ✓
- `AudioFileDecoder::Result` fields: `samples`, `sampleRate`, `formatName` — same in header and implementation. ✓
- `AssetLocator::resolveForRead(const std::string&)` — same in declaration (Task 1), implementation (Task 1), and both call sites (Task 2). ✓
- `ParsedTranscript { words, hyphenatedWords }` — same name + fields in declaration and use. ✓
- `importInFlight_` — declared in header (Task 4 step 5), used in handler + `timerCallback` (Task 4 step 6). ✓
