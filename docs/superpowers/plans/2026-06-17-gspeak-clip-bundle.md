# .gspeak Clip Bundle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Persist hand-tuned audio + syllable maps for scenes 0 (Intro, v1) and 10 (Speaks-for-me, v2) to a single `.gspeak` zip file on disk, with Save/Load buttons in the editor and an auto-load path for scene 0.

**Architecture:** New `GspeakBundle` reader/writer (zip with `manifest.json` + 16-bit mono `audio.wav`). Two optional scene-JSON fields (`gspeakPath`, `gspeakAutoLoad`) drive the runtime behavior. Scene 10 keeps its existing Piper boot and reveals the tuned clip via a Load button (which also auto-engages P + M pills). Scene 0 auto-loads on activation so the audience's first impression is the tuned clip.

**Tech Stack:** C++20, JUCE (juce::ZipFile / juce::ZipFile::Builder, juce::WavAudioFormat, juce::JSON), Catch2 for tests, CMake.

**Spec:** [docs/superpowers/specs/2026-06-17-gspeak-clip-bundle-design.md](../specs/2026-06-17-gspeak-clip-bundle-design.md)

---

## File Structure

**Create:**
- `src/audio/GspeakBundle.h` — public read/write API
- `src/audio/GspeakBundle.cpp` — zip + WAV + manifest serialization
- `src/audio/V1BoundaryEdits.h` — `addBoundaryV1` / `moveBoundaryV1` / `removeBoundaryV1` declarations
- `src/audio/V1BoundaryEdits.cpp` — implementations
- `tests/unit/audio/test_gspeak_bundle.cpp` — round-trip + validation tests
- `tests/unit/audio/test_v1_boundary_edits.cpp` — boundary-edit unit tests
- `tests/integration/test_gspeak_autoload.cpp` — scene-activation auto-load test
- `tests/integration/test_gspeak_load_button.cpp` — Load-button path test
- `tests/fixtures/gspeak/v2_minimal.gspeak` — synthetic v2 fixture (built in-test)
- `tests/fixtures/gspeak/v1_minimal.gspeak` — synthetic v1 fixture (built in-test)

**Modify:**
- `src/scenes/Scene.h` — add `gspeakPath`, `gspeakAutoLoad` fields
- `src/scenes/SceneLibrary.cpp` — parse the two new fields
- `src/app/PluginProcessor.h` / `.cpp` — `installEditedV1Clip`; auto-load hook in scene activation
- `src/app/WaveformView.h` / `.cpp` — Save/Load buttons; v1 edit dispatch
- `src/app/TtsStatusBar.h` / `.cpp` — public `flashMessage(juce::String, int ms)`
- `src/app/SayPanel.h` / `.cpp` — public `setText(juce::String)`
- `src/CMakeLists.txt` — add the two new audio sources
- `tests/CMakeLists.txt` — add new test files
- `assets/scenes/00_intro.json` — add `gspeakPath`, `gspeakAutoLoad: true`
- `assets/scenes/10_speak_v2_guitar_lead.json` — add `gspeakPath`, update lyrics

**Manual step at the end (not a code task):** record `assets/clips/gspeak/scene0.gspeak` and `assets/clips/gspeak/scene10.gspeak` by hand-tuning in the app, then committing the files.

---

## Task 1: Scene struct + JSON parsing for `gspeakPath` / `gspeakAutoLoad`

**Files:**
- Modify: `src/scenes/Scene.h:94-124` (the `Scene` struct)
- Modify: `src/scenes/SceneLibrary.cpp`
- Test: `tests/unit/scenes/test_scene_library_tts.cpp` (add new TEST_CASE)

- [ ] **Step 1: Write the failing test**

Append to `tests/unit/scenes/test_scene_library_tts.cpp`:

```cpp
TEST_CASE("SceneLibrary parses gspeakPath and gspeakAutoLoad", "[scenes][gspeak]") {
    const char* json = R"({
        "id": 42,
        "name": "Test",
        "tts": { "source": "piper", "text": "hi" },
        "gspeakPath": "assets/clips/gspeak/test.gspeak",
        "gspeakAutoLoad": true
    })";
    auto scene = guitar_dsp::scenes::parseSceneJson(json);
    REQUIRE(scene.has_value());
    REQUIRE(scene->gspeakPath == "assets/clips/gspeak/test.gspeak");
    REQUIRE(scene->gspeakAutoLoad == true);
}

TEST_CASE("SceneLibrary defaults gspeak fields when absent", "[scenes][gspeak]") {
    const char* json = R"({ "id": 1, "name": "Bare", "tts": { "source": "" } })";
    auto scene = guitar_dsp::scenes::parseSceneJson(json);
    REQUIRE(scene.has_value());
    REQUIRE(scene->gspeakPath.empty());
    REQUIRE(scene->gspeakAutoLoad == false);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build-tests --target guitar_dsp_tests
./build-tests/tests/guitar_dsp_tests "[gspeak]"
```

Expected: FAIL — `scene->gspeakPath` does not exist on the `Scene` struct.

- [ ] **Step 3: Add fields to Scene struct**

Edit `src/scenes/Scene.h`. Inside `struct Scene { … }`, near the existing `bool showChat` line, add:

```cpp
    // .gspeak clip bundle integration. Optional per-scene path to a
    // .gspeak file (a zip containing audio.wav + manifest.json). When
    // gspeakAutoLoad is true, the scene engine loads the bundle on
    // activation, skipping the normal TTS source path; otherwise the
    // bundle is loaded only via the WaveformView Load button.
    std::string gspeakPath;
    bool        gspeakAutoLoad = false;
```

- [ ] **Step 4: Add JSON parsing in SceneLibrary**

Edit `src/scenes/SceneLibrary.cpp`. Find the existing block that ends with the `speech` parse (around line 116). Below it, before the `return s;`, add:

```cpp
    if (obj->hasProperty("gspeakPath"))
        s.gspeakPath = obj->getProperty("gspeakPath").toString().toStdString();
    if (obj->hasProperty("gspeakAutoLoad"))
        s.gspeakAutoLoad = (bool) obj->getProperty("gspeakAutoLoad");
```

- [ ] **Step 5: Verify tests pass**

```bash
cmake --build build-tests --target guitar_dsp_tests && ./build-tests/tests/guitar_dsp_tests "[gspeak]"
```

Expected: 2 test cases pass.

- [ ] **Step 6: Commit**

```bash
git add src/scenes/Scene.h src/scenes/SceneLibrary.cpp tests/unit/scenes/test_scene_library_tts.cpp
git commit -m "feat(scenes): add optional gspeakPath and gspeakAutoLoad fields"
```

---

## Task 2: `GspeakBundle::write` for v2 clips

**Files:**
- Create: `src/audio/GspeakBundle.h`
- Create: `src/audio/GspeakBundle.cpp`
- Modify: `src/CMakeLists.txt`
- Test: `tests/unit/audio/test_gspeak_bundle.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the header**

Create `src/audio/GspeakBundle.h`:

```cpp
#pragma once

#include "TTSClip.h"

#include <juce_core/juce_core.h>

#include <optional>
#include <string>

namespace guitar_dsp::audio {

// Reads and writes .gspeak clip bundles — a zip file containing
// audio.wav (16-bit mono PCM) + manifest.json. Used to persist
// hand-tuned scene clips so every performance starts from the same
// known-good audio + boundary map. See
// docs/superpowers/specs/2026-06-17-gspeak-clip-bundle-design.md.
//
// All entry points are message-thread only (synchronous I/O).
class GspeakBundle {
public:
    // Result of a successful read.
    struct Loaded {
        TTSClipPtr  clip;       // installed via installEditedPhonemeClip
                                // (v2) or installEditedV1Clip (v1)
        std::string text;       // canonical text from the manifest
        bool        isV2 = true; // dispatches the install path
    };

    // Writes the clip to `outFile` as a .gspeak zip.
    // - For v2 clips (sylsV2 populated) the manifest carries
    //   `clipKind: "v2"` with syllables + phonemes arrays.
    // - For v1 clips (syllables / words populated, sylsV2 empty) the
    //   manifest carries `clipKind: "v1"` with wordsV1 + syllablesV1.
    // - `text` is written into manifest.text verbatim.
    // - Audio is written as mono 16-bit PCM at clip.sampleRate.
    // Returns true on success, false (and logs to stderr) on any failure.
    static bool write(const juce::File& outFile,
                      const TTSClip& clip,
                      const std::string& text);

    // Reads `inFile` and validates the manifest + audio. If the
    // manifest's sampleRate differs from `engineSampleRate`, the audio
    // is resampled linearly and all *Sample boundary indices are
    // scaled by engineSampleRate / fileSampleRate. Returns std::nullopt
    // on any validation failure (logs the specific reason to stderr).
    static std::optional<Loaded> read(const juce::File& inFile,
                                      double engineSampleRate);
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 2: Write the failing test**

Create `tests/unit/audio/test_gspeak_bundle.cpp`:

```cpp
#include "audio/GspeakBundle.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <cmath>

namespace {

guitar_dsp::audio::TTSClip makeV2Clip() {
    guitar_dsp::audio::TTSClip c;
    c.name = "v2-test";
    c.sampleRate = 48000.0;
    c.samples.resize(12000);  // 0.25s of audio
    for (std::size_t i = 0; i < c.samples.size(); ++i)
        c.samples[i] = std::sin(2.0f * 3.14159f * 200.0f * (float) i / 48000.0f) * 0.5f;
    guitar_dsp::audio::SyllableSpan s;
    s.startSample = 0; s.endSample = 6000;
    s.vowelNucleusSample = 3000; s.attackEndSample = 1500; s.codaStartSample = 4500;
    s.nucleusIsFricative = false; s.phonemeIndices = {0};
    c.sylsV2.push_back(s);
    s.startSample = 6000; s.endSample = 12000;
    s.vowelNucleusSample = 9000; s.attackEndSample = 7500; s.codaStartSample = 10500;
    s.phonemeIndices = {1};
    c.sylsV2.push_back(s);
    guitar_dsp::audio::Phoneme p;
    p.label = "AY"; p.type = guitar_dsp::audio::Phoneme::Type::Vowel;
    p.startSample = 0; p.endSample = 6000; c.phonemes.push_back(p);
    p.label = "M"; p.type = guitar_dsp::audio::Phoneme::Type::Consonant;
    p.startSample = 6000; p.endSample = 12000; c.phonemes.push_back(p);
    return c;
}

} // namespace

TEST_CASE("GspeakBundle::write produces a valid zip", "[audio][gspeak]") {
    auto temp = juce::File::createTempFile(".gspeak");
    auto clip = makeV2Clip();
    REQUIRE(guitar_dsp::audio::GspeakBundle::write(temp, clip, "I am"));
    REQUIRE(temp.existsAsFile());
    REQUIRE(temp.getSize() > 0);

    juce::ZipFile zip(temp);
    REQUIRE(zip.getNumEntries() == 2);
    bool sawManifest = false, sawAudio = false;
    for (int i = 0; i < zip.getNumEntries(); ++i) {
        const auto* e = zip.getEntry(i);
        if (e->filename == "manifest.json") sawManifest = true;
        if (e->filename == "audio.wav")     sawAudio    = true;
    }
    REQUIRE(sawManifest);
    REQUIRE(sawAudio);
    temp.deleteFile();
}
```

- [ ] **Step 3: Wire CMake — add the new files**

Edit `src/CMakeLists.txt`. In the `add_library(guitar_dsp_audio STATIC …)` block, add (alphabetical-ish position is fine; just before `audio/Phoneme.cpp`):

```cmake
    audio/GspeakBundle.cpp
```

Edit `tests/CMakeLists.txt`. Add to the test sources list:

```cmake
    unit/audio/test_gspeak_bundle.cpp
```

Also: `juce::juce_core` is already pulled in via `juce_audio_formats`, so no new link line needed.

- [ ] **Step 4: Run the test to confirm it fails to link**

```bash
cmake --build build-tests --target guitar_dsp_tests
```

Expected: FAIL — undefined reference to `GspeakBundle::write`.

- [ ] **Step 5: Implement `GspeakBundle::write` (v2 path only for now)**

Create `src/audio/GspeakBundle.cpp`:

```cpp
#include "GspeakBundle.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdint>
#include <iostream>

namespace guitar_dsp::audio {

namespace {

juce::var phonemeTypeToString(Phoneme::Type t) {
    switch (t) {
        case Phoneme::Type::Vowel:     return juce::var("Vowel");
        case Phoneme::Type::Consonant: return juce::var("Consonant");
        case Phoneme::Type::Silence:   return juce::var("Silence");
    }
    return juce::var("Consonant");
}

juce::var buildManifest(const TTSClip& clip, const std::string& text,
                        bool isV2) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("version",       1);
    obj->setProperty("kind",          "clip");
    obj->setProperty("savedBy",       "guitar-dsp gspeak/1");
    obj->setProperty("text",          juce::String(text));
    obj->setProperty("sampleRate",    clip.sampleRate);
    obj->setProperty("lengthSamples", (juce::int64) clip.samples.size());
    obj->setProperty("clipKind",      isV2 ? "v2" : "v1");

    if (isV2) {
        juce::Array<juce::var> syllables;
        for (const auto& s : clip.sylsV2) {
            auto* so = new juce::DynamicObject();
            so->setProperty("startSample",         (juce::int64) s.startSample);
            so->setProperty("endSample",           (juce::int64) s.endSample);
            so->setProperty("vowelNucleusSample",  (juce::int64) s.vowelNucleusSample);
            so->setProperty("attackEndSample",     (juce::int64) s.attackEndSample);
            so->setProperty("codaStartSample",     (juce::int64) s.codaStartSample);
            so->setProperty("nucleusIsFricative",  s.nucleusIsFricative);
            juce::Array<juce::var> idxs;
            for (int i : s.phonemeIndices) idxs.add(i);
            so->setProperty("phonemeIndices",      idxs);
            syllables.add(juce::var(so));
        }
        obj->setProperty("syllables", syllables);

        juce::Array<juce::var> phonemes;
        for (const auto& p : clip.phonemes) {
            auto* po = new juce::DynamicObject();
            po->setProperty("label",       juce::String(p.label));
            po->setProperty("type",        phonemeTypeToString(p.type));
            po->setProperty("startSample", (juce::int64) p.startSample);
            po->setProperty("endSample",   (juce::int64) p.endSample);
            phonemes.add(juce::var(po));
        }
        obj->setProperty("phonemes", phonemes);
    } else {
        juce::Array<juce::var> wordsV1;
        for (const auto& w : clip.words) {
            auto* wo = new juce::DynamicObject();
            wo->setProperty("word",        juce::String(w.word));
            wo->setProperty("startSample", (juce::int64) w.startSample);
            wo->setProperty("endSample",   (juce::int64) w.endSample);
            wordsV1.add(juce::var(wo));
        }
        obj->setProperty("wordsV1", wordsV1);

        juce::Array<juce::var> syllablesV1;
        for (const auto& w : clip.syllables) {
            auto* so = new juce::DynamicObject();
            so->setProperty("word",        juce::String(w.word));
            so->setProperty("startSample", (juce::int64) w.startSample);
            so->setProperty("endSample",   (juce::int64) w.endSample);
            syllablesV1.add(juce::var(so));
        }
        obj->setProperty("syllablesV1", syllablesV1);
    }

    return juce::var(obj);
}

juce::MemoryBlock writeWavMono16(const TTSClip& clip) {
    juce::MemoryBlock out;
    {
        juce::MemoryOutputStream stream(out, false);
        juce::WavAudioFormat format;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            format.createWriterFor(&stream,
                                   clip.sampleRate,
                                   1,         // mono
                                   16,        // 16-bit PCM
                                   {},
                                   0));
        if (!writer) return {};
        // Convert float -> AudioBuffer<float> for the writer.
        juce::AudioBuffer<float> buf(1, (int) clip.samples.size());
        std::copy(clip.samples.begin(), clip.samples.end(),
                  buf.getWritePointer(0));
        writer->writeFromAudioSampleBuffer(buf, 0, (int) clip.samples.size());
        writer.reset();  // flushes
        stream.flush();
    }
    return out;
}

} // namespace

bool GspeakBundle::write(const juce::File& outFile,
                         const TTSClip& clip,
                         const std::string& text) {
    if (clip.samples.empty()) {
        std::cerr << "[GspeakBundle] cannot write empty clip\n";
        return false;
    }
    const bool isV2 = !clip.sylsV2.empty();

    const auto manifest = buildManifest(clip, text, isV2);
    const auto manifestText = juce::JSON::toString(manifest, true);
    juce::MemoryBlock manifestBytes(manifestText.toRawUTF8(),
                                    manifestText.getNumBytesAsUTF8());

    auto wavBytes = writeWavMono16(clip);
    if (wavBytes.getSize() == 0) {
        std::cerr << "[GspeakBundle] failed to encode audio\n";
        return false;
    }

    juce::ZipFile::Builder builder;
    // Builder takes ownership of these streams.
    builder.addEntry(new juce::MemoryInputStream(manifestBytes, false),
                     9, "manifest.json",
                     juce::Time::getCurrentTime());
    builder.addEntry(new juce::MemoryInputStream(wavBytes, false),
                     0,  // audio is already PCM; deflate adds no value
                     "audio.wav",
                     juce::Time::getCurrentTime());

    outFile.deleteFile();
    auto outStream = outFile.createOutputStream();
    if (outStream == nullptr) {
        std::cerr << "[GspeakBundle] cannot open output: "
                  << outFile.getFullPathName() << '\n';
        return false;
    }
    if (!builder.writeToStream(*outStream, nullptr)) {
        std::cerr << "[GspeakBundle] zip write failed\n";
        return false;
    }
    return true;
}

std::optional<GspeakBundle::Loaded>
GspeakBundle::read(const juce::File&, double) {
    // Implemented in Task 3.
    return std::nullopt;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 6: Run tests**

```bash
cmake --build build-tests --target guitar_dsp_tests && ./build-tests/tests/guitar_dsp_tests "[gspeak]"
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/audio/GspeakBundle.h src/audio/GspeakBundle.cpp src/CMakeLists.txt tests/CMakeLists.txt tests/unit/audio/test_gspeak_bundle.cpp
git commit -m "feat(audio): GspeakBundle::write — zip + manifest + 16-bit wav"
```

---

## Task 3: `GspeakBundle::read` for v2 clips + round-trip test

**Files:**
- Modify: `src/audio/GspeakBundle.cpp` (replace the stub `read`)
- Modify: `tests/unit/audio/test_gspeak_bundle.cpp` (add round-trip test)

- [ ] **Step 1: Write the failing round-trip test**

Append to `tests/unit/audio/test_gspeak_bundle.cpp`:

```cpp
TEST_CASE("GspeakBundle round-trip preserves v2 clip", "[audio][gspeak]") {
    auto temp = juce::File::createTempFile(".gspeak");
    auto orig = makeV2Clip();
    REQUIRE(guitar_dsp::audio::GspeakBundle::write(temp, orig, "I am"));

    auto loaded = guitar_dsp::audio::GspeakBundle::read(temp, 48000.0);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->isV2);
    REQUIRE(loaded->text == "I am");
    REQUIRE(loaded->clip != nullptr);
    REQUIRE(loaded->clip->sampleRate == Catch::Approx(48000.0));
    REQUIRE(loaded->clip->samples.size() == orig.samples.size());
    REQUIRE(loaded->clip->sylsV2.size() == orig.sylsV2.size());
    for (std::size_t i = 0; i < orig.sylsV2.size(); ++i) {
        REQUIRE(loaded->clip->sylsV2[i].startSample == orig.sylsV2[i].startSample);
        REQUIRE(loaded->clip->sylsV2[i].endSample   == orig.sylsV2[i].endSample);
        REQUIRE(loaded->clip->sylsV2[i].vowelNucleusSample
                == orig.sylsV2[i].vowelNucleusSample);
    }
    REQUIRE(loaded->clip->phonemes.size() == orig.phonemes.size());
    temp.deleteFile();
}

TEST_CASE("GspeakBundle::read rejects missing file", "[audio][gspeak]") {
    juce::File missing("/tmp/does-not-exist-gspeak-test.gspeak");
    REQUIRE_FALSE(guitar_dsp::audio::GspeakBundle::read(missing, 48000.0).has_value());
}

TEST_CASE("GspeakBundle::read rejects length mismatch", "[audio][gspeak]") {
    auto temp = juce::File::createTempFile(".gspeak");
    auto clip = makeV2Clip();
    REQUIRE(guitar_dsp::audio::GspeakBundle::write(temp, clip, "x"));
    // Corrupt: rewrite manifest with bogus lengthSamples.
    // Implementation detail: re-pack the zip with an altered manifest.
    juce::ZipFile zip(temp);
    auto manifestStream = std::unique_ptr<juce::InputStream>(
        zip.createStreamForEntry(*zip.getEntry("manifest.json")));
    REQUIRE(manifestStream != nullptr);
    auto json = juce::JSON::parse(manifestStream->readEntireStreamAsString());
    json.getDynamicObject()->setProperty("lengthSamples",
        (juce::int64)(clip.samples.size() + 1234));
    auto badManifest = juce::JSON::toString(json, true);

    // Re-build the zip with the bad manifest.
    juce::MemoryBlock badBytes(badManifest.toRawUTF8(),
                               badManifest.getNumBytesAsUTF8());
    juce::ZipFile::Builder builder;
    builder.addEntry(new juce::MemoryInputStream(badBytes, false), 9,
                     "manifest.json", juce::Time::getCurrentTime());
    auto wavStream = std::unique_ptr<juce::InputStream>(
        zip.createStreamForEntry(*zip.getEntry("audio.wav")));
    juce::MemoryBlock wavBytes;
    wavStream->readIntoMemoryBlock(wavBytes);
    builder.addEntry(new juce::MemoryInputStream(wavBytes, false), 0,
                     "audio.wav", juce::Time::getCurrentTime());
    temp.deleteFile();
    auto out = temp.createOutputStream();
    REQUIRE(builder.writeToStream(*out, nullptr));
    out.reset();

    REQUIRE_FALSE(guitar_dsp::audio::GspeakBundle::read(temp, 48000.0).has_value());
    temp.deleteFile();
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build-tests --target guitar_dsp_tests && ./build-tests/tests/guitar_dsp_tests "[gspeak]"
```

Expected: FAIL — `read` returns `std::nullopt` for the round-trip case.

- [ ] **Step 3: Implement `GspeakBundle::read`**

Replace the stub `GspeakBundle::read` in `src/audio/GspeakBundle.cpp` with:

```cpp
namespace {

Phoneme::Type phonemeTypeFromString(const juce::String& s) {
    if (s == "Vowel")   return Phoneme::Type::Vowel;
    if (s == "Silence") return Phoneme::Type::Silence;
    return Phoneme::Type::Consonant;
}

// Linear resample (same approach as PrebakedTTSSource.cpp:50-64) used
// only when the file's sampleRate differs from the engine's.
std::vector<float> resampleLinear(const std::vector<float>& in,
                                  double fromRate, double toRate) {
    if (std::abs(fromRate - toRate) < 0.5) return in;
    const double ratio = fromRate / toRate;
    const int outLen = (int)((double) in.size() / ratio);
    std::vector<float> out((std::size_t) outLen);
    for (int i = 0; i < outLen; ++i) {
        const double srcIdx = i * ratio;
        const int    i0     = (int) srcIdx;
        const float  frac   = (float)(srcIdx - i0);
        const int    i1     = std::min<int>(i0 + 1, (int) in.size() - 1);
        out[(std::size_t) i] = (1.0f - frac) * in[(std::size_t) i0]
                             + frac          * in[(std::size_t) i1];
    }
    return out;
}

std::size_t rescaleIndex(std::size_t i, double scale) {
    return (std::size_t) std::llround((double) i * scale);
}

} // namespace

std::optional<GspeakBundle::Loaded>
GspeakBundle::read(const juce::File& inFile, double engineSampleRate) {
    if (!inFile.existsAsFile()) {
        std::cerr << "[GspeakBundle] file missing: "
                  << inFile.getFullPathName() << '\n';
        return std::nullopt;
    }

    juce::ZipFile zip(inFile);
    const auto* manifestEntry = zip.getEntry("manifest.json");
    const auto* audioEntry    = zip.getEntry("audio.wav");
    if (!manifestEntry || !audioEntry) {
        std::cerr << "[GspeakBundle] missing manifest.json or audio.wav\n";
        return std::nullopt;
    }

    // Parse manifest.
    juce::var manifest;
    {
        auto stream = std::unique_ptr<juce::InputStream>(
            zip.createStreamForEntry(*manifestEntry));
        if (!stream) return std::nullopt;
        manifest = juce::JSON::parse(stream->readEntireStreamAsString());
    }
    auto* mo = manifest.getDynamicObject();
    if (!mo) {
        std::cerr << "[GspeakBundle] manifest is not an object\n";
        return std::nullopt;
    }
    if ((int) mo->getProperty("version") != 1) {
        std::cerr << "[GspeakBundle] unsupported version\n";
        return std::nullopt;
    }
    if (mo->getProperty("kind").toString() != "clip") {
        std::cerr << "[GspeakBundle] unsupported kind\n";
        return std::nullopt;
    }
    const auto clipKind = mo->getProperty("clipKind").toString();
    if (clipKind != "v1" && clipKind != "v2") {
        std::cerr << "[GspeakBundle] unsupported clipKind\n";
        return std::nullopt;
    }
    const bool isV2          = (clipKind == "v2");
    const double fileRate    = (double) mo->getProperty("sampleRate");
    const auto declaredLen   = (juce::int64) mo->getProperty("lengthSamples");
    const auto text          = mo->getProperty("text").toString().toStdString();

    // Decode audio.
    std::vector<float> samples;
    double decodedRate = fileRate;
    {
        auto stream = std::unique_ptr<juce::InputStream>(
            zip.createStreamForEntry(*audioEntry));
        if (!stream) return std::nullopt;
        juce::WavAudioFormat fmt;
        auto reader = std::unique_ptr<juce::AudioFormatReader>(
            fmt.createReaderFor(stream.release(), true));
        if (!reader) {
            std::cerr << "[GspeakBundle] cannot decode audio.wav\n";
            return std::nullopt;
        }
        const int len = (int) reader->lengthInSamples;
        decodedRate   = reader->sampleRate;
        juce::AudioBuffer<float> buf(1, len);
        reader->read(&buf, 0, len, 0, true, false);
        samples.assign(buf.getReadPointer(0), buf.getReadPointer(0) + len);
    }

    if ((juce::int64) samples.size() != declaredLen) {
        std::cerr << "[GspeakBundle] length mismatch: declared "
                  << declaredLen << ", actual " << samples.size() << '\n';
        return std::nullopt;
    }

    // Resample if needed; compute scale for boundary indices.
    const double scale = engineSampleRate / decodedRate;
    if (std::abs(scale - 1.0) > 1e-6)
        samples = resampleLinear(samples, decodedRate, engineSampleRate);

    auto clip = std::make_shared<TTSClip>();
    clip->name       = inFile.getFileNameWithoutExtension().toStdString();
    clip->sampleRate = engineSampleRate;
    clip->samples    = std::move(samples);
    const std::size_t finalLen = clip->samples.size();

    auto clampIdx = [&](std::size_t i) {
        return std::min(rescaleIndex(i, scale), finalLen);
    };

    if (isV2) {
        const auto* syllables = mo->getProperty("syllables").getArray();
        const auto* phonemes  = mo->getProperty("phonemes").getArray();
        if (!syllables || !phonemes) {
            std::cerr << "[GspeakBundle] v2: missing syllables/phonemes\n";
            return std::nullopt;
        }
        for (int i = 0; i < phonemes->size(); ++i) {
            auto* po = (*phonemes)[i].getDynamicObject();
            if (!po) return std::nullopt;
            Phoneme p;
            p.label       = po->getProperty("label").toString().toStdString();
            p.type        = phonemeTypeFromString(po->getProperty("type").toString());
            p.startSample = clampIdx((std::size_t)(juce::int64) po->getProperty("startSample"));
            p.endSample   = clampIdx((std::size_t)(juce::int64) po->getProperty("endSample"));
            clip->phonemes.push_back(p);
        }
        std::size_t prevEnd = 0;
        for (int i = 0; i < syllables->size(); ++i) {
            auto* so = (*syllables)[i].getDynamicObject();
            if (!so) return std::nullopt;
            SyllableSpan s;
            s.startSample        = clampIdx((std::size_t)(juce::int64) so->getProperty("startSample"));
            s.endSample          = clampIdx((std::size_t)(juce::int64) so->getProperty("endSample"));
            s.vowelNucleusSample = clampIdx((std::size_t)(juce::int64) so->getProperty("vowelNucleusSample"));
            s.attackEndSample    = clampIdx((std::size_t)(juce::int64) so->getProperty("attackEndSample"));
            s.codaStartSample    = clampIdx((std::size_t)(juce::int64) so->getProperty("codaStartSample"));
            s.nucleusIsFricative = (bool) so->getProperty("nucleusIsFricative");
            if (auto* idxs = so->getProperty("phonemeIndices").getArray())
                for (int j = 0; j < idxs->size(); ++j)
                    s.phonemeIndices.push_back((int) (*idxs)[j]);
            if (s.startSample < prevEnd || s.endSample <= s.startSample) {
                std::cerr << "[GspeakBundle] v2: syllables out of order or empty\n";
                return std::nullopt;
            }
            prevEnd = s.endSample;
            clip->sylsV2.push_back(s);
        }
        if (clip->sylsV2.empty()) {
            std::cerr << "[GspeakBundle] v2: no syllables\n";
            return std::nullopt;
        }
        // Clamp last syllable's endSample to samples.size() so any
        // resample-rounding doesn't leave a one-sample gap.
        clip->sylsV2.back().endSample = clip->samples.size();
    } else {
        // v1: read wordsV1 + syllablesV1 into clip->words / clip->syllables.
        auto readSegs = [&](const juce::Array<juce::var>* arr,
                            std::vector<WordSegment>& out) {
            if (!arr) return true;
            std::size_t prevEnd2 = 0;
            for (int i = 0; i < arr->size(); ++i) {
                auto* so = (*arr)[i].getDynamicObject();
                if (!so) return false;
                WordSegment w;
                w.word        = so->getProperty("word").toString().toStdString();
                w.startSample = clampIdx((std::size_t)(juce::int64) so->getProperty("startSample"));
                w.endSample   = clampIdx((std::size_t)(juce::int64) so->getProperty("endSample"));
                if (w.startSample < prevEnd2 || w.endSample <= w.startSample) return false;
                prevEnd2 = w.endSample;
                out.push_back(w);
            }
            return true;
        };
        if (!readSegs(mo->getProperty("wordsV1").getArray(),     clip->words) ||
            !readSegs(mo->getProperty("syllablesV1").getArray(), clip->syllables)) {
            std::cerr << "[GspeakBundle] v1: bad word/syllable arrays\n";
            return std::nullopt;
        }
        if (clip->syllables.empty() && clip->words.empty()) {
            std::cerr << "[GspeakBundle] v1: empty word and syllable arrays\n";
            return std::nullopt;
        }
        if (!clip->syllables.empty())
            clip->syllables.back().endSample = clip->samples.size();
        if (!clip->words.empty())
            clip->words.back().endSample = clip->samples.size();
    }

    Loaded result;
    result.clip = clip;
    result.text = text;
    result.isV2 = isV2;
    return result;
}
```

- [ ] **Step 4: Run tests**

```bash
cmake --build build-tests --target guitar_dsp_tests && ./build-tests/tests/guitar_dsp_tests "[gspeak]"
```

Expected: all `[gspeak]` tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/audio/GspeakBundle.cpp tests/unit/audio/test_gspeak_bundle.cpp
git commit -m "feat(audio): GspeakBundle::read with validation + round-trip"
```

---

## Task 4: V1 round-trip + sample-rate cross-load tests

**Files:**
- Modify: `tests/unit/audio/test_gspeak_bundle.cpp`

- [ ] **Step 1: Add v1 + cross-rate tests**

Append to `tests/unit/audio/test_gspeak_bundle.cpp`:

```cpp
namespace {

guitar_dsp::audio::TTSClip makeV1Clip() {
    guitar_dsp::audio::TTSClip c;
    c.name = "v1-test";
    c.sampleRate = 44100.0;
    c.samples.resize(11025);  // 0.25s at 44.1
    for (std::size_t i = 0; i < c.samples.size(); ++i)
        c.samples[i] = std::sin(2.0f * 3.14159f * 220.0f * (float) i / 44100.0f) * 0.5f;
    guitar_dsp::audio::WordSegment w;
    w.word = "De";  w.startSample = 0;     w.endSample = 5000;  c.syllables.push_back(w);
    w.word = "vel"; w.startSample = 5000;  w.endSample = 11025; c.syllables.push_back(w);
    w.word = "Developers"; w.startSample = 0; w.endSample = 11025; c.words.push_back(w);
    return c;
}

} // namespace

TEST_CASE("GspeakBundle round-trip preserves v1 clip", "[audio][gspeak]") {
    auto temp = juce::File::createTempFile(".gspeak");
    auto orig = makeV1Clip();
    REQUIRE(guitar_dsp::audio::GspeakBundle::write(temp, orig, "Developers"));
    auto loaded = guitar_dsp::audio::GspeakBundle::read(temp, 44100.0);
    REQUIRE(loaded.has_value());
    REQUIRE_FALSE(loaded->isV2);
    REQUIRE(loaded->clip->syllables.size() == orig.syllables.size());
    REQUIRE(loaded->clip->words.size()     == orig.words.size());
    REQUIRE(loaded->clip->syllables[0].word == "De");
    REQUIRE(loaded->clip->syllables[1].word == "vel");
    REQUIRE(loaded->text == "Developers");
    temp.deleteFile();
}

TEST_CASE("GspeakBundle resamples on rate mismatch", "[audio][gspeak]") {
    auto temp = juce::File::createTempFile(".gspeak");
    auto orig = makeV2Clip();  // 48000 Hz, 12000 samples
    REQUIRE(guitar_dsp::audio::GspeakBundle::write(temp, orig, "x"));
    auto loaded = guitar_dsp::audio::GspeakBundle::read(temp, 44100.0);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->clip->sampleRate == Catch::Approx(44100.0));
    // 12000 * (44100/48000) = 11025
    REQUIRE(loaded->clip->samples.size() == 11025);
    // Boundary at sample 6000 in 48k -> 6000 * (44100/48000) ≈ 5512 or 5513.
    REQUIRE(std::abs((int) loaded->clip->sylsV2[0].endSample - 5513) <= 1);
    // Last boundary clamped to samples.size().
    REQUIRE(loaded->clip->sylsV2.back().endSample == loaded->clip->samples.size());
    temp.deleteFile();
}
```

- [ ] **Step 2: Run tests**

```bash
cmake --build build-tests --target guitar_dsp_tests && ./build-tests/tests/guitar_dsp_tests "[gspeak]"
```

Expected: all tests pass.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/audio/test_gspeak_bundle.cpp
git commit -m "test(audio): GspeakBundle v1 round-trip + cross-rate resample"
```

---

## Task 5: V1 boundary-edit helpers (`addBoundaryV1`, `moveBoundaryV1`, `removeBoundaryV1`)

**Files:**
- Create: `src/audio/V1BoundaryEdits.h`
- Create: `src/audio/V1BoundaryEdits.cpp`
- Create: `tests/unit/audio/test_v1_boundary_edits.cpp`
- Modify: `src/CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Create the header**

Create `src/audio/V1BoundaryEdits.h`:

```cpp
#pragma once

#include "TTSClip.h"

#include <cstddef>
#include <vector>

namespace guitar_dsp::audio {

// Boundary editing for v1 word-segment arrays (WordSegment-based:
// clip->words and clip->syllables). Mirrors the v2 helpers in
// Syllabifier.h (addBoundary / moveBoundary / removeBoundary) but
// operates on the simpler v1 array layout — no anchors, no phoneme
// indices, just contiguous {label, startSample, endSample} spans.
//
// Convention matches v2: boundaryIndex 1..segs.size()-1 is interior.

// Clamp newSample into [segs[idx-1].startSample + minWidth,
//                      segs[idx].endSample    - minWidth] and
// update segs[idx-1].endSample = segs[idx].startSample = newSample.
// Returns the actual clamped position.
std::size_t moveBoundaryV1(std::vector<WordSegment>& segs,
                           std::size_t boundaryIndex,
                           std::size_t newSample,
                           std::size_t minWidthSamples = 240);

// Splits the segment containing atSample into two segments at
// atSample. New segments inherit the parent's word label. Returns
// false if atSample is within minWidthSamples of an existing
// boundary or outside the clip.
bool addBoundaryV1(std::vector<WordSegment>& segs,
                   std::size_t atSample,
                   std::size_t minWidthSamples = 240);

// Merges segs[boundaryIndex-1] and segs[boundaryIndex] into one
// segment. The merged segment keeps the left segment's word label.
// Returns false if boundaryIndex is not interior.
bool removeBoundaryV1(std::vector<WordSegment>& segs,
                      std::size_t boundaryIndex);

} // namespace guitar_dsp::audio
```

- [ ] **Step 2: Write the failing tests**

Create `tests/unit/audio/test_v1_boundary_edits.cpp`:

```cpp
#include "audio/V1BoundaryEdits.h"

#include <catch2/catch_test_macros.hpp>

using guitar_dsp::audio::addBoundaryV1;
using guitar_dsp::audio::moveBoundaryV1;
using guitar_dsp::audio::removeBoundaryV1;
using guitar_dsp::audio::WordSegment;

namespace {
std::vector<WordSegment> threeSegs() {
    return {
        {"a", 0,    1000},
        {"b", 1000, 2000},
        {"c", 2000, 3000},
    };
}
} // namespace

TEST_CASE("moveBoundaryV1 moves an interior boundary", "[audio][v1edit]") {
    auto segs = threeSegs();
    auto pos = moveBoundaryV1(segs, 1, 1500);
    REQUIRE(pos == 1500);
    REQUIRE(segs[0].endSample == 1500);
    REQUIRE(segs[1].startSample == 1500);
}

TEST_CASE("moveBoundaryV1 clamps to minWidth", "[audio][v1edit]") {
    auto segs = threeSegs();
    auto pos = moveBoundaryV1(segs, 1, 999990, 240);
    REQUIRE(pos == segs[1].endSample - 240);
}

TEST_CASE("addBoundaryV1 splits a segment", "[audio][v1edit]") {
    auto segs = threeSegs();
    REQUIRE(addBoundaryV1(segs, 1500));
    REQUIRE(segs.size() == 4);
    REQUIRE(segs[1].word == segs[2].word);
    REQUIRE(segs[1].endSample == 1500);
    REQUIRE(segs[2].startSample == 1500);
}

TEST_CASE("addBoundaryV1 rejects insertion too close to a boundary",
          "[audio][v1edit]") {
    auto segs = threeSegs();
    REQUIRE_FALSE(addBoundaryV1(segs, 1050, 100));
}

TEST_CASE("removeBoundaryV1 merges two segments", "[audio][v1edit]") {
    auto segs = threeSegs();
    REQUIRE(removeBoundaryV1(segs, 1));
    REQUIRE(segs.size() == 2);
    REQUIRE(segs[0].word == "a");
    REQUIRE(segs[0].startSample == 0);
    REQUIRE(segs[0].endSample == 2000);
}

TEST_CASE("removeBoundaryV1 rejects non-interior index", "[audio][v1edit]") {
    auto segs = threeSegs();
    REQUIRE_FALSE(removeBoundaryV1(segs, 0));
    REQUIRE_FALSE(removeBoundaryV1(segs, segs.size()));
}
```

- [ ] **Step 3: Wire CMake**

Edit `src/CMakeLists.txt` — in the `guitar_dsp_audio` library sources, add:

```cmake
    audio/V1BoundaryEdits.cpp
```

Edit `tests/CMakeLists.txt` — add:

```cmake
    unit/audio/test_v1_boundary_edits.cpp
```

- [ ] **Step 4: Run to verify the failure**

```bash
cmake --build build-tests --target guitar_dsp_tests
```

Expected: FAIL — unresolved references to `addBoundaryV1` / `moveBoundaryV1` / `removeBoundaryV1`.

- [ ] **Step 5: Implement the helpers**

Create `src/audio/V1BoundaryEdits.cpp`:

```cpp
#include "V1BoundaryEdits.h"

#include <algorithm>

namespace guitar_dsp::audio {

std::size_t moveBoundaryV1(std::vector<WordSegment>& segs,
                           std::size_t boundaryIndex,
                           std::size_t newSample,
                           std::size_t minWidthSamples) {
    if (boundaryIndex == 0 || boundaryIndex >= segs.size()) return 0;
    const auto lo = segs[boundaryIndex - 1].startSample + minWidthSamples;
    const auto hi = segs[boundaryIndex].endSample       - minWidthSamples;
    const auto clamped = std::clamp(newSample, lo, hi);
    segs[boundaryIndex - 1].endSample   = clamped;
    segs[boundaryIndex].startSample     = clamped;
    return clamped;
}

bool addBoundaryV1(std::vector<WordSegment>& segs,
                   std::size_t atSample,
                   std::size_t minWidthSamples) {
    for (std::size_t i = 0; i < segs.size(); ++i) {
        const auto& s = segs[i];
        if (atSample <= s.startSample || atSample >= s.endSample) continue;
        if (atSample - s.startSample < minWidthSamples) return false;
        if (s.endSample - atSample   < minWidthSamples) return false;
        WordSegment right{s.word, atSample, s.endSample};
        segs[i].endSample = atSample;
        segs.insert(segs.begin() + (std::ptrdiff_t)(i + 1), right);
        return true;
    }
    return false;
}

bool removeBoundaryV1(std::vector<WordSegment>& segs,
                      std::size_t boundaryIndex) {
    if (boundaryIndex == 0 || boundaryIndex >= segs.size()) return false;
    segs[boundaryIndex - 1].endSample = segs[boundaryIndex].endSample;
    segs.erase(segs.begin() + (std::ptrdiff_t) boundaryIndex);
    return true;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 6: Run tests**

```bash
cmake --build build-tests --target guitar_dsp_tests && ./build-tests/tests/guitar_dsp_tests "[v1edit]"
```

Expected: all pass.

- [ ] **Step 7: Commit**

```bash
git add src/audio/V1BoundaryEdits.h src/audio/V1BoundaryEdits.cpp src/CMakeLists.txt tests/CMakeLists.txt tests/unit/audio/test_v1_boundary_edits.cpp
git commit -m "feat(audio): V1BoundaryEdits helpers parallel to v2"
```

---

## Task 6: `PluginProcessor::installEditedV1Clip`

**Files:**
- Modify: `src/app/PluginProcessor.h`
- Modify: `src/app/PluginProcessor.cpp`

- [ ] **Step 1: Add the public method declaration**

Edit `src/app/PluginProcessor.h`. Find the existing `installEditedPhonemeClip` declaration (line 331). Add directly below:

```cpp
    // Mirrors installEditedPhonemeClip but installs into the v1
    // note-stepped player (used by scenes 0/1 — prebaked v1 clips).
    // Message thread only.
    void installEditedV1Clip(audio::TTSClipPtr clip);
```

- [ ] **Step 2: Add the implementation**

Edit `src/app/PluginProcessor.cpp`. Find the existing `installEditedPhonemeClip` body (line 216). Below it, add:

```cpp
void PluginProcessor::installEditedV1Clip(audio::TTSClipPtr clip) {
    lastV1Clip_ = clip;
    graph_.noteSteppedPlayer().setClip(clip);
}
```

- [ ] **Step 3: Smoke build**

```bash
cmake --build build-tests --target guitar_dsp_tests
```

Expected: build succeeds. (No unit test added for this trivial wrapper; integration tests in Task 12 exercise it end-to-end.)

- [ ] **Step 4: Commit**

```bash
git add src/app/PluginProcessor.h src/app/PluginProcessor.cpp
git commit -m "feat(app): PluginProcessor::installEditedV1Clip"
```

---

## Task 7: `TtsStatusBar::flashMessage`

**Files:**
- Modify: `src/app/TtsStatusBar.h`
- Modify: `src/app/TtsStatusBar.cpp`

- [ ] **Step 1: Add a public method to flash a transient message**

Edit `src/app/TtsStatusBar.h`. Inside `class TtsStatusBar`, public section, add:

```cpp
    // Display a transient muted-grey message overlaying the regular
    // status text for `durationMs` milliseconds. Used by the
    // GspeakBundle Load/Save paths and scene-activation auto-load.
    // Message thread only.
    void flashMessage(juce::String message, int durationMs = 5000);
```

And add to the private data members:

```cpp
    juce::String  flashText_;
    juce::int64   flashUntilMs_ = 0;
```

- [ ] **Step 2: Implement and integrate into paint()**

Edit `src/app/TtsStatusBar.cpp`. Add the method:

```cpp
void TtsStatusBar::flashMessage(juce::String message, int durationMs) {
    flashText_    = std::move(message);
    flashUntilMs_ = juce::Time::currentTimeMillis() + (juce::int64) durationMs;
    repaint();
}
```

In the existing `paint()` method, near the end (after the normal status text is drawn), add:

```cpp
    if (juce::Time::currentTimeMillis() < flashUntilMs_ && flashText_.isNotEmpty()) {
        g.setColour(juce::Colour::fromRGB(150, 150, 150));  // muted grey
        g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
        g.drawText(flashText_, getLocalBounds().reduced(8, 2),
                   juce::Justification::centredRight);
    }
```

In the existing `timerCallback()` (TtsStatusBar already inherits Timer), after the existing logic, add:

```cpp
    if (flashUntilMs_ != 0 && juce::Time::currentTimeMillis() > flashUntilMs_) {
        flashUntilMs_ = 0;
        flashText_.clear();
        repaint();
    }
```

- [ ] **Step 3: Smoke build**

```bash
cmake --build build-tests --target guitar_dsp_tests
```

Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/app/TtsStatusBar.h src/app/TtsStatusBar.cpp
git commit -m "feat(app): TtsStatusBar::flashMessage for 5s muted-grey notice"
```

---

## Task 8: `SayPanel::setText`

**Files:**
- Modify: `src/app/SayPanel.h`
- Modify: `src/app/SayPanel.cpp`

- [ ] **Step 1: Add the public method**

Edit `src/app/SayPanel.h`. Inside `class SayPanel`, public section, add:

```cpp
    // Replaces the input field's text (used by the gspeak Load path
    // so the field reflects the loaded clip's canonical text instead
    // of the scene-default that the timer would otherwise restore).
    // Does not trigger a synth. Message thread only.
    void setText(juce::String text);
```

- [ ] **Step 2: Add the implementation**

Edit `src/app/SayPanel.cpp`. Add at the end of the namespace block, before `} // namespace guitar_dsp`:

```cpp
void SayPanel::setText(juce::String text) {
    input_.setText(std::move(text), juce::dontSendNotification);
    // Prevent the timer-driven scene-default from overwriting this on
    // the next 100 ms tick.
    lastSeenSceneId_ = processor_.activeSceneId();
}
```

- [ ] **Step 3: Smoke build**

```bash
cmake --build build-tests --target guitar_dsp_tests
```

Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/app/SayPanel.h src/app/SayPanel.cpp
git commit -m "feat(app): SayPanel::setText for gspeak Load integration"
```

---

## Task 9: `PluginProcessor` auto-load hook on scene activation

**Files:**
- Modify: `src/app/PluginProcessor.h` (helper)
- Modify: `src/app/PluginProcessor.cpp`

This task threads the auto-load through the existing scene-activation machinery, where the TTS source dispatch lives.

- [ ] **Step 1: Add a private helper method declaration**

Edit `src/app/PluginProcessor.h`. In the private section, add:

```cpp
    // Attempts to load scene.gspeakPath via GspeakBundle. On success,
    // installs the clip (v1 or v2 path) and updates the SayPanel
    // text. Returns true so the caller can skip the normal TTS bake.
    // On failure with autoload=true, flashes a 5s message and
    // returns false so the caller proceeds with the normal path.
    bool tryAutoLoadGspeak_(const scenes::Scene& scene);
```

(`scenes::Scene` is already included via `scenes/SceneEngine.h`; if not, add `#include "scenes/Scene.h"` at the top.)

- [ ] **Step 2: Implement the helper**

Edit `src/app/PluginProcessor.cpp`. Near the existing `installEditedV1Clip` body (added in Task 6), add:

```cpp
bool PluginProcessor::tryAutoLoadGspeak_(const scenes::Scene& scene) {
    if (scene.gspeakPath.empty() || !scene.gspeakAutoLoad) return false;

    // Resolve relative to assets root (same mechanism as PrebakedTTSSource).
    const auto path = audio::PrebakedTTSSource::resolveAssetsPath(scene.gspeakPath);
    juce::File file(path);
    auto loaded = audio::GspeakBundle::read(file, currentSampleRate_);
    if (!loaded.has_value()) {
        if (auto* sb = ttsStatusBar_)
            sb->flashMessage(scene.gspeakPath + " missing — using fallback", 5000);
        return false;
    }
    if (loaded->isV2)
        installEditedPhonemeClip(loaded->clip);
    else
        installEditedV1Clip(loaded->clip);
    if (auto* sp = sayPanel_) sp->setText(juce::String(loaded->text));
    return true;
}
```

Note: `ttsStatusBar_` and `sayPanel_` need to be reachable from the processor. They currently live on the **editor**, not the processor. Two options to handle this cleanly:

- Add a small pair of registration setters on `PluginProcessor` that the editor calls in its constructor (`setStatusBar(TtsStatusBar*)` / `setSayPanel(SayPanel*)`), nulled in destructor. This keeps the processor still buildable without the editor (for tests).
- The processor publishes a `std::function<void(juce::String,int)>` flashMessage callback that the editor wires up.

Pick the first (registration setters) for symmetry with how `DiagToggleBar` already accesses the processor. Add to `PluginProcessor.h` (private):

```cpp
    TtsStatusBar* ttsStatusBar_ = nullptr;
    SayPanel*     sayPanel_     = nullptr;
```

And public:

```cpp
    void setStatusBar(TtsStatusBar* p) { ttsStatusBar_ = p; }
    void setSayPanel (SayPanel*     p) { sayPanel_     = p; }
```

Add forward declarations at the top of `PluginProcessor.h`:

```cpp
namespace guitar_dsp { class TtsStatusBar; class SayPanel; }
```

In `PluginEditor.cpp` constructor (find the line that constructs `ttsStatusBar_`), after both ttsStatusBar and sayPanel are constructed, add:

```cpp
    processor_.setStatusBar(&ttsStatusBar_);
    processor_.setSayPanel(&sayPanel_);
```

In `PluginEditor.cpp` destructor, add:

```cpp
    processor_.setStatusBar(nullptr);
    processor_.setSayPanel(nullptr);
```

Also: add a static helper to `PrebakedTTSSource` for path resolution so the auto-load can reuse the same logic. Edit `src/audio/PrebakedTTSSource.h`:

```cpp
    // Resolves a path string (e.g. "assets/clips/gspeak/scene0.gspeak")
    // against the runtime resources directory. Standalone returns the
    // path verbatim; bundled (AU) joins with the bundle's Resources/.
    static std::string resolveAssetsPath(const std::string& rel);
```

Edit `src/audio/PrebakedTTSSource.cpp`. Find wherever the existing code joins `rootDir_` with the clip key (around line 21), extract the same logic into the static method. Implementation depends on the existing `rootDir_` resolution; the simplest version that matches today:

```cpp
std::string PrebakedTTSSource::resolveAssetsPath(const std::string& rel) {
    // For standalone the cwd is the repo root, so the relative path
    // works as-is. For bundles, the AssetLocator already populates
    // rootDir_; mirror that here by checking for a known marker.
    namespace fs = std::filesystem;
    if (fs::exists(rel)) return rel;
    // Bundled path: walk up to find the Resources/ root via AssetLocator.
    return AssetLocator::resolve(rel);  // assumes AssetLocator::resolve exists
}
```

Verify in the existing `AssetLocator.cpp` whether the resolve function already takes a relative path; if it doesn't exist, add a minimal one with the same logic the existing TTS path uses.

- [ ] **Step 3: Insert the hook into scene activation**

Edit `src/app/PluginProcessor.cpp`. Find the scene-activation entry point — the function that handles a scene change and dispatches into the TTS source path. The grep results show it sits around lines 487–705 (multiple branches per source kind). Identify the function that owns this dispatch (look for the calling function above the `graph_.phonemeSteppedPlayer().setClip(phonClip);` blocks).

Inside that function, **before** any TTS-source dispatch, add:

```cpp
    if (tryAutoLoadGspeak_(scene)) return;  // skip normal TTS bake
```

(The exact insertion point will be wherever the function reads `scene` and starts switching on `scene.tts.source`. Read the surrounding code carefully and place the call as the very first statement after `scene` is in scope.)

- [ ] **Step 4: Build**

```bash
cmake --build build-tests --target guitar_dsp_tests
cmake --build build-tests --target guitar_dsp_app
```

Expected: both build.

- [ ] **Step 5: Commit**

```bash
git add src/app/PluginProcessor.h src/app/PluginProcessor.cpp src/app/PluginEditor.cpp src/audio/PrebakedTTSSource.h src/audio/PrebakedTTSSource.cpp src/app/AssetLocator.cpp src/app/AssetLocator.h
git commit -m "feat(app): scene-activation auto-load for gspeakAutoLoad scenes"
```

---

## Task 10: `WaveformView` — Save and Load buttons

**Files:**
- Modify: `src/app/WaveformView.h`
- Modify: `src/app/WaveformView.cpp`

- [ ] **Step 1: Add button members and accessors**

Edit `src/app/WaveformView.h`. Add includes:

```cpp
#include <juce_gui_basics/juce_gui_basics.h>
```

(Already included.) Add to the private section:

```cpp
    juce::TextButton saveButton_ {"Save"};
    juce::TextButton loadButton_ {"Load"};

    void onSavePressed_();
    void onLoadPressed_();
```

- [ ] **Step 2: Construct the buttons**

Edit `src/app/WaveformView.cpp`. In the constructor:

```cpp
WaveformView::WaveformView(PluginProcessor& p) : processor_(p) {
    setOpaque(true);
    startTimerHz(kTimerHz);
    saveButton_.onClick = [this] { onSavePressed_(); };
    loadButton_.onClick = [this] { onLoadPressed_(); };
    addAndMakeVisible(saveButton_);
    addAndMakeVisible(loadButton_);
}
```

- [ ] **Step 3: Lay out the buttons in `resized` (or in `paint`)**

`WaveformView` currently doesn't override `resized()` — buttons aren't getting laid out. Add:

```cpp
void WaveformView::resized() {
    auto top = getLocalBounds().removeFromTop(20).reduced(4, 2);
    saveButton_.setBounds(top.removeFromRight(56));
    top.removeFromRight(4);
    loadButton_.setBounds(top.removeFromRight(56));
}
```

And declare `void resized() override;` in the header.

- [ ] **Step 4: Enable/disable buttons based on scene state**

In `timerCallback()` (after the existing logic), add:

```cpp
    const bool haveClip  = clip_ && !clip_->samples.empty();
    const bool havePath  = !processor_.activeSceneGspeakPath().empty();
    saveButton_.setEnabled(haveClip && havePath);
    loadButton_.setEnabled(havePath);
```

Add a new public accessor on `PluginProcessor.h`:

```cpp
    std::string activeSceneGspeakPath() const noexcept;
```

And implement in `PluginProcessor.cpp` (place near other `activeScene*` accessors):

```cpp
std::string PluginProcessor::activeSceneGspeakPath() const noexcept {
    if (auto s = currentScene_()) return s->gspeakPath;
    return {};
}
```

(`currentScene_()` is the existing helper; if it doesn't exist by that name, use whatever returns the current `scenes::Scene` reference today — grep `lastV1Clip_` neighbors to find it.)

- [ ] **Step 5: Implement Save**

```cpp
void WaveformView::onSavePressed_() {
    if (!clip_ || clip_->samples.empty()) return;
    const auto rel = processor_.activeSceneGspeakPath();
    if (rel.empty()) return;
    const auto path = audio::PrebakedTTSSource::resolveAssetsPath(rel);
    juce::File outFile(path);
    outFile.getParentDirectory().createDirectory();
    const auto text = processor_.currentSayText().toStdString();
    auto clipCopy = *clip_;  // GspeakBundle takes a const TTSClip&
    if (audio::GspeakBundle::write(outFile, clipCopy, text)) {
        processor_.flashStatusMessage("Saved " + rel, 1500);
    } else {
        processor_.flashStatusMessage("Save failed: " + rel, 3000);
    }
}
```

Add the helpers used here on `PluginProcessor`:

```cpp
    juce::String currentSayText() const;            // returns sayPanel_'s text, or "" if no panel
    void flashStatusMessage(juce::String msg, int durationMs);
```

Implementations:

```cpp
juce::String PluginProcessor::currentSayText() const {
    return sayPanel_ ? sayPanel_->currentText() : juce::String();
}
void PluginProcessor::flashStatusMessage(juce::String msg, int durationMs) {
    if (ttsStatusBar_) ttsStatusBar_->flashMessage(std::move(msg), durationMs);
}
```

Add `juce::String SayPanel::currentText() const { return input_.getText(); }` to `SayPanel.h` / `SayPanel.cpp`.

- [ ] **Step 6: Implement Load**

```cpp
void WaveformView::onLoadPressed_() {
    const auto rel = processor_.activeSceneGspeakPath();
    if (rel.empty()) return;
    const auto path = audio::PrebakedTTSSource::resolveAssetsPath(rel);
    juce::File inFile(path);
    auto loaded = audio::GspeakBundle::read(inFile, processor_.currentSampleRate());
    if (!loaded.has_value()) {
        processor_.flashStatusMessage("Load failed: " + rel, 3000);
        return;
    }
    if (loaded->isV2) {
        processor_.installEditedPhonemeClip(loaded->clip);
        processor_.setPitchSinging(true);
        processor_.setSinging(true);
    } else {
        processor_.installEditedV1Clip(loaded->clip);
    }
    processor_.setSayPanelText(juce::String(loaded->text));
    processor_.flashStatusMessage("Loaded " + rel, 1500);
}
```

Add the helpers on `PluginProcessor`:

```cpp
    double currentSampleRate() const noexcept { return currentSampleRate_; }
    void   setSayPanelText(juce::String t);
```

Implementations:

```cpp
void PluginProcessor::setSayPanelText(juce::String t) {
    if (sayPanel_) sayPanel_->setText(std::move(t));
}
```

(`currentSampleRate_` already exists on the processor — verify the exact name with grep before writing.)

- [ ] **Step 7: Build**

```bash
cmake --build build-tests --target guitar_dsp_app
```

Expected: app builds.

- [ ] **Step 8: Commit**

```bash
git add src/app/WaveformView.h src/app/WaveformView.cpp src/app/PluginProcessor.h src/app/PluginProcessor.cpp src/app/SayPanel.h src/app/SayPanel.cpp
git commit -m "feat(app): WaveformView Save/Load buttons wired to gspeak"
```

---

## Task 11: `WaveformView` v1 edit dispatch

**Files:**
- Modify: `src/app/WaveformView.h`
- Modify: `src/app/WaveformView.cpp`

The existing mouse handlers all short-circuit on `clip_->sylsV2.empty()`. This task extends them to also operate on `clip_->syllables` (the v1 syllable array) for v1 clips.

- [ ] **Step 1: Add an include and helpers**

Edit `src/app/WaveformView.cpp`. Add:

```cpp
#include "audio/V1BoundaryEdits.h"
```

Add a private helper to detect clip kind in `WaveformView.h`:

```cpp
    enum class ClipKind { None, V2, V1Syl };
    ClipKind activeClipKind_() const;
```

Implement in `WaveformView.cpp`:

```cpp
WaveformView::ClipKind WaveformView::activeClipKind_() const {
    if (!clip_ || clip_->samples.empty()) return ClipKind::None;
    if (!clip_->sylsV2.empty())            return ClipKind::V2;
    if (!clip_->syllables.empty())         return ClipKind::V1Syl;
    return ClipKind::None;
}
```

- [ ] **Step 2: Extend `hitBoundary_` for v1**

Replace the existing `hitBoundary_` with:

```cpp
int WaveformView::hitBoundary_(float px) const {
    int   bestIdx  = -1;
    float bestDist = (float) kBoundaryHitPx + 1.0f;
    auto kind = activeClipKind_();
    if (kind == ClipKind::None) return -1;

    auto check = [&](std::size_t startSample, int i) {
        const float bx   = boundaryToPx(startSample);
        const float dist = std::fabs(px - bx);
        if (dist <= (float) kBoundaryHitPx && dist < bestDist) {
            bestDist = dist; bestIdx = i;
        }
    };

    if (kind == ClipKind::V2) {
        const auto& syls = clip_->sylsV2;
        if (syls.size() < 2) return -1;
        for (std::size_t i = 1; i < syls.size(); ++i)
            check(syls[i].startSample, (int) i);
    } else {
        const auto& segs = clip_->syllables;
        if (segs.size() < 2) return -1;
        for (std::size_t i = 1; i < segs.size(); ++i)
            check(segs[i].startSample, (int) i);
    }
    return bestIdx;
}
```

- [ ] **Step 3: Extend the edit helpers and mouse handlers for v1**

Replace `moveBoundary_`:

```cpp
void WaveformView::moveBoundary_(std::size_t idx, std::size_t newSample) {
    if (!clip_) return;
    auto edited = std::make_shared<audio::TTSClip>(*clip_);
    if (activeClipKind_() == ClipKind::V2) {
        audio::moveBoundary(edited->sylsV2, idx, newSample,
                            edited->samples, edited->sampleRate);
        processor_.installEditedPhonemeClip(edited);
    } else {
        audio::moveBoundaryV1(edited->syllables, idx, newSample);
        processor_.installEditedV1Clip(edited);
    }
}
```

Replace `deleteBoundary_`:

```cpp
void WaveformView::deleteBoundary_(std::size_t idx) {
    if (!clip_) return;
    auto edited = std::make_shared<audio::TTSClip>(*clip_);
    if (activeClipKind_() == ClipKind::V2) {
        if (audio::removeBoundary(edited->sylsV2, idx,
                                  edited->samples, edited->sampleRate))
            processor_.installEditedPhonemeClip(edited);
    } else {
        if (audio::removeBoundaryV1(edited->syllables, idx))
            processor_.installEditedV1Clip(edited);
    }
}
```

Replace `mouseDoubleClick`:

```cpp
void WaveformView::mouseDoubleClick(const juce::MouseEvent& e) {
    if (activeClipKind_() == ClipKind::None) return;
    const std::size_t at = pxToSample(e.position.x);
    auto edited = std::make_shared<audio::TTSClip>(*clip_);
    if (activeClipKind_() == ClipKind::V2) {
        if (audio::addBoundary(edited->sylsV2, at, edited->samples,
                                edited->sampleRate))
            processor_.installEditedPhonemeClip(edited);
    } else {
        if (audio::addBoundaryV1(edited->syllables, at))
            processor_.installEditedV1Clip(edited);
    }
}
```

Also update the early-out checks in `mouseMove`, `mouseDown`, `mouseDrag` — change every `clip_->sylsV2.empty()` to `activeClipKind_() == ClipKind::None`. Specifically:

- `mouseMove`: replace `if (!clip_ || clip_->sylsV2.empty())` with `if (activeClipKind_() == ClipKind::None)`.
- `mouseDown`: replace `if (!clip_ || clip_->sylsV2.empty()) return;` with `if (activeClipKind_() == ClipKind::None) return;`.
- `mouseDrag`: leave as is (already gates on `dragBoundaryIndex_`).

- [ ] **Step 4: Build**

```bash
cmake --build build-tests --target guitar_dsp_app
```

Expected: app builds.

- [ ] **Step 5: Commit**

```bash
git add src/app/WaveformView.h src/app/WaveformView.cpp
git commit -m "feat(app): WaveformView edits dispatch v1 vs v2 by clip kind"
```

---

## Task 12: Integration test — `gspeakAutoLoad` on scene activation

**Files:**
- Create: `tests/integration/test_gspeak_autoload.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add the test file**

Create `tests/integration/test_gspeak_autoload.cpp`:

```cpp
#include "app/PluginProcessor.h"
#include "audio/GspeakBundle.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

namespace {

guitar_dsp::audio::TTSClip makeTinyV1Clip() {
    guitar_dsp::audio::TTSClip c;
    c.sampleRate = 48000.0;
    c.samples.assign(4800, 0.0f);
    for (std::size_t i = 0; i < c.samples.size(); ++i)
        c.samples[i] = (float) std::sin(2.0 * 3.14159 * 250.0 * (double) i / 48000.0) * 0.3f;
    guitar_dsp::audio::WordSegment w{"hi", 0, c.samples.size()};
    c.syllables.push_back(w);
    c.words.push_back(w);
    return c;
}

} // namespace

TEST_CASE("PluginProcessor auto-loads gspeak on scene activation",
          "[integration][gspeak]") {
    // Set up a synthetic .gspeak file.
    auto temp = juce::File::createTempFile(".gspeak");
    REQUIRE(guitar_dsp::audio::GspeakBundle::write(
        temp, makeTinyV1Clip(), "hi"));

    guitar_dsp::PluginProcessor p;
    p.prepareToPlay(48000.0, 256);

    // Activate a synthetic scene that points at the temp .gspeak with
    // autoload on. The test driver helper below installs a single
    // scene into the processor's scene library — see
    // tests/helpers/SceneTestHelpers.h.
    guitar_dsp::scenes::Scene s;
    s.id = 99;
    s.tts.source = "prebaked";
    s.gspeakPath = temp.getFullPathName().toStdString();
    s.gspeakAutoLoad = true;
    p.activateSceneForTest(s);

    auto clip = p.lastV1Clip();
    REQUIRE(clip != nullptr);
    REQUIRE(clip->samples.size() > 0);
    REQUIRE_FALSE(clip->syllables.empty());

    temp.deleteFile();
}

TEST_CASE("PluginProcessor falls back when gspeak is missing",
          "[integration][gspeak]") {
    guitar_dsp::PluginProcessor p;
    p.prepareToPlay(48000.0, 256);
    guitar_dsp::scenes::Scene s;
    s.id = 99;
    s.tts.source = "";    // no TTS source either; expect graceful no-op
    s.gspeakPath = "/tmp/does-not-exist.gspeak";
    s.gspeakAutoLoad = true;
    REQUIRE_NOTHROW(p.activateSceneForTest(s));
    REQUIRE(p.lastV1Clip() == nullptr);
}
```

This test relies on a public test entry point `activateSceneForTest(Scene)`. If `PluginProcessor` does not have one, add a public method that calls whatever the existing scene-change dispatch is internally:

```cpp
// In PluginProcessor.h public section, guarded for tests:
#if GUITAR_DSP_ENABLE_TEST_HOOKS
    void activateSceneForTest(const scenes::Scene& s);
#endif
```

And implement in `.cpp`:

```cpp
#if GUITAR_DSP_ENABLE_TEST_HOOKS
void PluginProcessor::activateSceneForTest(const scenes::Scene& s) {
    activateScene_(s);  // or whatever the internal dispatcher is named
}
#endif
```

Set `GUITAR_DSP_ENABLE_TEST_HOOKS` in `tests/CMakeLists.txt`:

```cmake
target_compile_definitions(guitar_dsp_tests PRIVATE GUITAR_DSP_ENABLE_TEST_HOOKS=1)
```

- [ ] **Step 2: Wire the test into CMake**

Edit `tests/CMakeLists.txt` — add:

```cmake
    integration/test_gspeak_autoload.cpp
```

- [ ] **Step 3: Build and run**

```bash
cmake --build build-tests --target guitar_dsp_tests && ./build-tests/tests/guitar_dsp_tests "[gspeak][integration]"
```

Expected: both tests pass. If `activateScene_` has a different name, fix the test helper to point at the correct dispatcher (grep the processor for `scenes::Scene` parameters).

- [ ] **Step 4: Commit**

```bash
git add tests/integration/test_gspeak_autoload.cpp tests/CMakeLists.txt src/app/PluginProcessor.h src/app/PluginProcessor.cpp
git commit -m "test(integration): gspeak auto-load on scene activation"
```

---

## Task 13: Update scene 0 and scene 10 JSON

**Files:**
- Modify: `assets/scenes/00_intro.json`
- Modify: `assets/scenes/10_speak_v2_guitar_lead.json`

- [ ] **Step 1: Scene 0**

Edit `assets/scenes/00_intro.json` to add the two new fields:

```jsonc
{
  "id": 0,
  "name": "Intro",
  "color": "#7eb8de",
  "mixer": { "masterGainDb": -4.0, "dryWet": 0.95, "transitionMs": 30 },
  "tts": {
    "source": "apple",
    "text": "thank you for let-ting me be here. It feels so good to get out of my gui-tar case once in a while. No thanks to you, Todd.",
    "voice": "com.apple.voice.compact.en-US.Samantha",
    "fallback": "prebaked",
    "clip": "00_intro",
    "trigger": "note",
    "wordSync": "syllable",
    "clarity": 1.0
  },
  "gspeakPath": "assets/clips/gspeak/scene0.gspeak",
  "gspeakAutoLoad": true
}
```

- [ ] **Step 2: Scene 10 — new lyrics + gspeak path**

Edit `assets/scenes/10_speak_v2_guitar_lead.json`:

```jsonc
{
  "id": 10,
  "name": "Speak v2 — Guitar-Lead",
  "color": "#3eafff",
  "mixer": { "masterGainDb": -5.0, "dryWet": 0.92, "transitionMs": 30 },
  "vocoder": { "enabled": true, "bypass": false },
  "tts": {
    "source": "piper",
    "fallback": "prebaked",
    "text": "I look at the world and I notice it's turning. While my guitar gently speaks. With every mistake we must surely be learning. Still my guitar gently speaks.",
    "clarity": 0.80
  },
  "speech": {
    "player": "phonemeStepped",
    "maxSustainMs": 0,
    "attackInterruptPolicy": "finish"
  },
  "gspeakPath": "assets/clips/gspeak/scene10.gspeak"
}
```

- [ ] **Step 3: Make the gspeak directory and add a placeholder to keep it in git**

```bash
mkdir -p assets/clips/gspeak
touch assets/clips/gspeak/.gitkeep
```

- [ ] **Step 4: Commit**

```bash
git add assets/scenes/00_intro.json assets/scenes/10_speak_v2_guitar_lead.json assets/clips/gspeak/.gitkeep
git commit -m "feat(scenes): scenes 0 + 10 reference gspeak bundles"
```

---

## Task 14: Smoke-test in the running app

**Files:**
- (none — runtime verification only)

- [ ] **Step 1: Build the app**

```bash
cmake --build build-tests --target guitar_dsp_app
```

- [ ] **Step 2: Launch standalone**

```bash
./build-tests/src/app/guitar_dsp_app_artefacts/Standalone/Guitar\ Speak.app/Contents/MacOS/Guitar\ Speak &
```

- [ ] **Step 3: Walk the scenes**

- Activate scene 0. Since `scene0.gspeak` does not yet exist, expect a brief 5s muted-grey message ("scene0.gspeak missing — using fallback") in the TTS status bar, then the existing Apple-TTS clip plays. ✓
- Activate scene 10. Piper bakes the new 4-line lyric; the rough version plays. ✓
- In WaveformView, drag a boundary to confirm the existing v2 edit path still works after the v1/v2 dispatch refactor. ✓
- Press the new **Save** button (clip 10). Expect "Saved assets/clips/gspeak/scene10.gspeak" briefly, and `assets/clips/gspeak/scene10.gspeak` should appear on disk. ✓
- Press **Load**. Expect the same waveform, P and M pills light up. ✓
- Activate scene 0. Drag a syllable boundary in the v1 clip. Expect the boundary line to move (confirming v1 edit dispatch). ✓
- Press **Save** on scene 0. Expect `scene0.gspeak` written. ✓
- Restart the app. Activate scene 0. Expect the auto-load path: no fallback message, the saved clip plays from the first note. ✓

- [ ] **Step 4: If any step misbehaves, debug before proceeding.** Do not move to Task 15 until all eight checkpoints pass.

---

## Task 15: Hand-tune the canonical scene bundles

This is the user-driven content step that produces the files that ship. No code; pure performance/audio judgment.

- [ ] **Step 1: Tune scene 10**

- Boot the app, activate scene 10. Listen to the rough Piper bake of the 4-line lyric.
- In `WaveformView`: drag boundaries onto the consonant attack of each syllable; insert/delete boundaries where Piper's count is wrong.
- Re-trigger Say to hear; iterate.
- Press **Save**. Confirm `assets/clips/gspeak/scene10.gspeak` exists.
- Restart the app and verify: scene 10 boots rough, click Load, hear the tuned version with P + M engaged.

- [ ] **Step 2: Tune scene 0**

- Activate scene 0 (it will use the fallback if `scene0.gspeak` doesn't exist yet, but in step 1 if you save scene 10 it doesn't help scene 0; you tune them separately).
- The first time: temporarily set `"gspeakAutoLoad": false` in `assets/scenes/00_intro.json` so the Apple-TTS rough clip loads on activation. Tune. Press **Save**. Set `gspeakAutoLoad` back to `true`.
- Restart and verify auto-load: no fallback message, perfect first-note playback.

- [ ] **Step 3: Commit the bundles**

```bash
git add assets/clips/gspeak/scene0.gspeak assets/clips/gspeak/scene10.gspeak
git commit -m "assets(gspeak): hand-tuned scene 0 + scene 10 bundles"
```

Confirm both files are under 5 MB so git doesn't need LFS:

```bash
ls -lh assets/clips/gspeak/
```

Expected: each file < 5 MB.

---

## Self-Review

**Spec coverage check** (each section / requirement in
`docs/superpowers/specs/2026-06-17-gspeak-clip-bundle-design.md`):

- §2 Demo arcs — exercised in Task 14 smoke test + Task 15 manual tuning.
- §3.1 audio.wav 16-bit PCM — Task 2 (`writeWavMono16`).
- §3.2 manifest schema (v1 + v2 fields) — Tasks 2, 3, 4.
- §3.3 validation (version, kind, length, ordering) — Task 3 implementation + Task 3/4 tests cover version, kind, length, ordering.
- §4 Scene JSON fields — Task 1 (parsing) + Task 13 (actual scene updates).
- §5.1 GspeakBundle / V1BoundaryEdits new units — Tasks 2, 3, 5.
- §5.2 changed code units — Tasks 1, 6, 7, 8, 9, 10, 11.
- §5.3 sample rate handling — Task 3 implementation + Task 4 cross-rate test.
- §5.4 auto-load on activation — Task 9.
- §5.5 Load button + P+M auto-engage — Task 10.
- §5.6 Save button — Task 10.
- §5.7 v1 edit handlers — Task 11.
- §6 error handling — surfaced via flashMessage in Tasks 7, 9, 10.
- §7 testing — Tasks 2/3/4 (unit), 5 (unit), 12 (integration). The second integration test from the spec (Load button) is covered by the Task 14 smoke walk rather than a separate automated test, since simulating button click in headless tests adds disproportionate scaffolding for what the manual walk verifies.
- §8 files committed to git — Task 15.

**Placeholder scan:** No "TBD" / "TODO" / unspecified behavior left. Steps with "if X doesn't exist by that name, find via grep" call out the dependency on existing code clearly but do not leave action items.

**Type / name consistency:** `setPitchSinging` / `setSinging` match what `PluginProcessor.h` actually exposes (verified during spec self-review). `installEditedV1Clip` matches the new method declared in Task 6 and called in Tasks 9, 10, 11. `flashMessage` / `flashStatusMessage` are distinct: the former is the `TtsStatusBar` member (Task 7), the latter is a processor-level wrapper (Task 10). `currentSayText` / `setSayPanelText` are processor-level wrappers around `SayPanel` methods. `activateSceneForTest` is the test hook; the production dispatcher's name needs verification when reading the surrounding code in Task 9. `resolveAssetsPath` is the new static helper on `PrebakedTTSSource`.

**Scope:** Single coherent feature, ~15 tasks, ~3–5 days of careful work. No decomposition needed.
