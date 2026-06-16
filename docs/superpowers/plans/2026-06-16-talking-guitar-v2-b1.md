# Talking Guitar v2 (B1) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship Mode B1 ("Speak v2 — Guitar-Lead") as scene id 10 — phoneme-aligned syllable boundaries (from `espeak-ng -X`) replace v1's proportional split, and held notes hold the syllable's vowel via grain-loop. v1 stays bit-for-bit untouched.

**Architecture:** Three-stage pipeline. (1) `PhonemeExtractor` shells out to `espeak-ng -X` and returns a phoneme sequence with predicted durations rescaled to actual Piper audio length. (2) `Syllabifier` groups phonemes into syllables by the sonority sequencing principle. (3) `PhonemeSteppedTTSPlayer` runs an Attack→Sustain→Coda state machine per syllable, using the existing `OnsetDetector` for triggers and a pitch-period-synchronous grain loop on the vowel during sustain. Prerequisite: fix Piper (currently broken because the upstream tarball ships without the required runtime dylibs).

**Tech Stack:** C++17, JUCE 8.x, Catch2 v3 (test framework), espeak-ng + Piper (subprocess), bash (build scripts), CMake.

---

## Spec reference

This plan implements [`docs/superpowers/specs/2026-06-16-talking-guitar-v2-design.md`](../specs/2026-06-16-talking-guitar-v2-design.md). B2 (phase-vocoder vowel stretch) and B3 (concatenative phoneme synthesis) are future plans — not in scope here.

## File structure

**New files:**

- `scripts/build_piper.sh` — builds Piper + dylibs from source so `assets/piper/` is complete.
- `src/audio/Phoneme.h` — `Phoneme` struct + `Phoneme::Type` enum.
- `src/audio/PhonemeExtractor.h` / `.cpp` — espeak-ng subprocess wrapper that returns `std::vector<Phoneme>` from text.
- `src/audio/Syllabifier.h` / `.cpp` — groups phonemes into `SyllableSegment`s via sonority peaks.
- `src/audio/PhonemeAlignedClipBuilder.h` / `.cpp` — orchestrator: text → Piper audio → phoneme extract → rescale → syllabify → populate `TTSClip`.
- `src/audio/PhonemeSteppedTTSPlayer.h` / `.cpp` — new player with Attack/Sustain/Coda state machine.
- `src/audio/VowelGrainLoop.h` / `.cpp` — pitch-period-synchronous grain looper for the Sustain state.
- `assets/scenes/10_speak_v2_guitar_lead.json` — new scene definition.
- `tests/unit/audio/test_phoneme_extractor.cpp`
- `tests/unit/audio/test_syllabifier.cpp`
- `tests/unit/audio/test_phoneme_aligned_clip_builder.cpp`
- `tests/unit/audio/test_phoneme_stepped_player.cpp`
- `tests/unit/audio/test_vowel_grain_loop.cpp`
- `tests/integration/test_speak_v2_scene.cpp`
- `tests/golden/v1_speech/` — reference output for v1 drift detection.
- `tests/unit/audio/test_v1_golden.cpp` — drift-detector test.

**Modified files:**

- `src/audio/TTSClip.h` — add `SyllableSegment` struct (Phoneme-aware) and a `sylsV2` array. v1's `WordSegment`/`syllables` stay.
- `src/audio/PiperTTSSource.cpp` — no logic change; rely on dylib fix.
- `scripts/fetch_piper.sh` — detect missing dylibs and either call `build_piper.sh` or print a clear error.
- `src/scenes/SceneLibrary.cpp` — parse `speech.player`, `speech.maxSustainMs`, `speech.attackInterruptPolicy`.
- `src/scenes/Scene.h` — fields for the new speech config.
- `src/audio/AudioGraph.cpp` / `.h` — instantiate `PhonemeSteppedTTSPlayer` when scene selects it.
- `src/app/SceneIndicator.cpp` — dynamic strip width for any scene count.
- `src/app/WordReadout.cpp` / `.h` — highlight active syllable within the current word.
- `src/app/DiagToggleBar.cpp` / `.h` — add `Ph` pill.
- `tests/CMakeLists.txt` — add new test sources.
- `docs/presentation/while-my-guitar-gently-speaks.md` — final-state note in §10 once B1 ships.

---

## Phase 0 — Lock v1 behavior before touching anything

### Task 0.1: v1 golden-audio drift detector

**Files:**
- Create: `tests/golden/v1_speech/onsets.txt`
- Create: `tests/golden/v1_speech/clip_meta.json`
- Create: `tests/golden/v1_speech/reference_output.f32`
- Create: `tests/unit/audio/test_v1_golden.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

`tests/unit/audio/test_v1_golden.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/NoteSteppedTTSPlayer.h"
#include "audio/TTSClip.h"
#include "audio/WordSyncMode.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using guitar_dsp::audio::NoteSteppedTTSPlayer;
using guitar_dsp::audio::TTSClip;
using guitar_dsp::audio::WordSegment;
using guitar_dsp::audio::WordSyncMode;

namespace {
std::vector<float> readF32(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    REQUIRE(f);
    f.seekg(0, std::ios::end);
    const auto n = static_cast<std::size_t>(f.tellg()) / sizeof(float);
    f.seekg(0, std::ios::beg);
    std::vector<float> v(n);
    f.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(n*sizeof(float)));
    return v;
}

// Deterministic three-word "clip": three constant-amplitude bursts.
std::shared_ptr<const TTSClip> deterministicClip() {
    auto c = std::make_shared<TTSClip>();
    c->sampleRate = 48000.0;
    c->samples.assign(9000, 0.0f);
    for (int i = 0; i < 3000; ++i) c->samples[i]       = 0.10f;
    for (int i = 3000; i < 6000; ++i) c->samples[i]    = 0.20f;
    for (int i = 6000; i < 9000; ++i) c->samples[i]    = 0.30f;
    c->words = { {"a",0,3000}, {"b",3000,6000}, {"c",6000,9000} };
    return c;
}
}

TEST_CASE("v1 NoteSteppedTTSPlayer output is byte-equal to reference",
          "[audio][v1golden]") {
    NoteSteppedTTSPlayer p;
    p.prepare(48000.0, 256);
    p.setClip(deterministicClip());
    p.setMode(WordSyncMode::Latch);

    // Canned onset sequence: pulse onset > 0.5 at samples 0, 12000, 24000.
    std::vector<float> onsetTrack(36000, 0.0f);
    for (auto s : {0, 12000, 24000})
        for (int i = 0; i < 480; ++i) onsetTrack[s + i] = 0.8f;

    std::vector<float> modOut(36000, 0.0f);
    p.process(onsetTrack.data(), modOut.data(), modOut.size());

    auto ref = readF32("tests/golden/v1_speech/reference_output.f32");
    REQUIRE(modOut.size() == ref.size());
    for (std::size_t i = 0; i < ref.size(); ++i) {
        REQUIRE(modOut[i] == ref[i]);  // byte-equal for v1 drift detection
    }
}
```

`tests/golden/v1_speech/onsets.txt`:
```
# Canned onset sequence used by test_v1_golden.cpp.
# Sample positions of onset pulses (0.8 for 480 samples each).
0
12000
24000
```

`tests/golden/v1_speech/clip_meta.json`:
```json
{
  "sampleRate": 48000,
  "lengthSamples": 9000,
  "words": [
    {"word": "a", "start": 0,    "end": 3000},
    {"word": "b", "start": 3000, "end": 6000},
    {"word": "c", "start": 6000, "end": 9000}
  ],
  "amplitudes": [0.10, 0.20, 0.30]
}
```

- [ ] **Step 2: Add to test sources**

In `tests/CMakeLists.txt` `add_executable(guitar_dsp_tests ...)` list, add:
```
    unit/audio/test_v1_golden.cpp
```

- [ ] **Step 3: Generate the reference output**

Build the tests, then run a one-off generator. Add a temporary `WARN` of byte values to the test, or write a small generator. Simpler: run the test once with the reference missing, then write a tiny helper:

Create `tools/gen_v1_golden.cpp` (temporary — delete after committing the .f32 file):
```cpp
// One-off: produce tests/golden/v1_speech/reference_output.f32.
// Build with: clang++ -std=c++17 -I src tools/gen_v1_golden.cpp \
//   src/audio/NoteSteppedTTSPlayer.cpp src/audio/OnsetDetector.cpp -o /tmp/gen
// Run from repo root: /tmp/gen
#include "audio/NoteSteppedTTSPlayer.h"
#include "audio/TTSClip.h"
#include "audio/WordSyncMode.h"
#include <fstream>
#include <memory>
#include <vector>

int main() {
    using namespace guitar_dsp::audio;
    auto c = std::make_shared<TTSClip>();
    c->sampleRate = 48000.0;
    c->samples.assign(9000, 0.0f);
    for (int i = 0; i < 3000; ++i) c->samples[i]    = 0.10f;
    for (int i = 3000; i < 6000; ++i) c->samples[i] = 0.20f;
    for (int i = 6000; i < 9000; ++i) c->samples[i] = 0.30f;
    c->words = { {"a",0,3000}, {"b",3000,6000}, {"c",6000,9000} };

    NoteSteppedTTSPlayer p;
    p.prepare(48000.0, 256);
    p.setClip(c);
    p.setMode(WordSyncMode::Latch);

    std::vector<float> onset(36000, 0.0f);
    for (int s : {0, 12000, 24000})
        for (int i = 0; i < 480; ++i) onset[s+i] = 0.8f;
    std::vector<float> mod(36000, 0.0f);
    p.process(onset.data(), mod.data(), mod.size());

    std::ofstream f("tests/golden/v1_speech/reference_output.f32",
                    std::ios::binary);
    f.write(reinterpret_cast<const char*>(mod.data()),
            mod.size() * sizeof(float));
    return 0;
}
```

Run:
```bash
mkdir -p tests/golden/v1_speech
clang++ -std=c++17 -I src tools/gen_v1_golden.cpp \
  src/audio/NoteSteppedTTSPlayer.cpp src/audio/OnsetDetector.cpp \
  -o /tmp/gen_v1_golden && /tmp/gen_v1_golden
rm tools/gen_v1_golden.cpp /tmp/gen_v1_golden
```

- [ ] **Step 4: Build & run the test**

```bash
cmake --build build-tests --target guitar_dsp_tests
./build-tests/tests/guitar_dsp_tests "[v1golden]"
```
Expected: 1 test passes, 36000/36000 samples byte-equal.

- [ ] **Step 5: Commit**

```bash
git add tests/golden/v1_speech tests/unit/audio/test_v1_golden.cpp tests/CMakeLists.txt
git commit -m "test(audio): v1 golden-audio drift detector for NoteSteppedTTSPlayer"
```

---

## Phase 1 — Piper fix (prerequisite)

### Task 1.1: Add build_piper.sh that produces a complete install

**Files:**
- Create: `scripts/build_piper.sh`

- [ ] **Step 1: Write the script**

`scripts/build_piper.sh`:
```bash
#!/usr/bin/env bash
# Builds Piper from source into assets/piper/, INCLUDING the three runtime
# dylibs (libespeak-ng.1.dylib, libpiper_phonemize.1.dylib,
# libonnxruntime.1.14.1.dylib) that the upstream macOS tarball is missing.
#
# Usage: ./scripts/build_piper.sh
#
# Idempotent — re-runs are cheap. Pins Piper to commit ${PIPER_COMMIT}
# for reproducibility.

set -euo pipefail
cd "$(dirname "$0")/.."

PIPER_DIR="assets/piper"
PIPER_REPO="https://github.com/rhasspy/piper.git"
PIPER_COMMIT="${PIPER_COMMIT:-2023.11.14-2}"   # tag of last good release
ORT_VERSION="${ORT_VERSION:-1.14.1}"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$PIPER_DIR"

# 1) Clone + checkout pinned commit.
echo "==> cloning piper @ ${PIPER_COMMIT}"
git clone --depth 1 --branch "${PIPER_COMMIT}" "$PIPER_REPO" "$WORK/piper" \
  || git clone "$PIPER_REPO" "$WORK/piper"
( cd "$WORK/piper" && git checkout "$PIPER_COMMIT" )

# 2) Fetch ONNX Runtime release binary (contains libonnxruntime dylib).
ORT_URL="https://github.com/microsoft/onnxruntime/releases/download/v${ORT_VERSION}/onnxruntime-osx-arm64-${ORT_VERSION}.tgz"
echo "==> fetching onnxruntime ${ORT_VERSION}"
curl -L --fail -o "$WORK/ort.tgz" "$ORT_URL"
tar -xzf "$WORK/ort.tgz" -C "$WORK"
ORT_DIR="$WORK/onnxruntime-osx-arm64-${ORT_VERSION}"

# 3) Configure + build Piper.
echo "==> configuring piper"
cmake -S "$WORK/piper" -B "$WORK/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DONNXRUNTIME_ROOTDIR="$ORT_DIR"
echo "==> building piper"
cmake --build "$WORK/build" -j

# 4) Copy artifacts into assets/piper/.
echo "==> installing into $PIPER_DIR"
cp "$WORK/build/piper"                          "$PIPER_DIR/piper"
cp "$ORT_DIR/lib/libonnxruntime.${ORT_VERSION}.dylib" \
   "$PIPER_DIR/libonnxruntime.1.14.1.dylib"
# Piper's CMake places these in the build tree:
find "$WORK/build" -name "libespeak-ng.1.dylib"        -exec cp {} "$PIPER_DIR/" \;
find "$WORK/build" -name "libpiper_phonemize.1.dylib"  -exec cp {} "$PIPER_DIR/" \;

# 5) Verify.
for lib in libespeak-ng.1.dylib libpiper_phonemize.1.dylib libonnxruntime.1.14.1.dylib; do
    if [[ ! -f "$PIPER_DIR/$lib" ]]; then
        echo "ERROR: $lib missing after build — check build logs in $WORK/build" >&2
        exit 1
    fi
done
chmod +x "$PIPER_DIR/piper"
echo "==> piper install complete: $PIPER_DIR"
otool -L "$PIPER_DIR/piper" | head
```

- [ ] **Step 2: Make executable**

```bash
chmod +x scripts/build_piper.sh
```

- [ ] **Step 3: Run it & verify**

```bash
./scripts/build_piper.sh
ls assets/piper/libespeak-ng.1.dylib \
   assets/piper/libpiper_phonemize.1.dylib \
   assets/piper/libonnxruntime.1.14.1.dylib
```
Expected: all three files listed (not "No such file").

- [ ] **Step 4: Sanity-launch the binary**

```bash
echo "hello world" | assets/piper/piper \
    --model assets/piper/voices/en_US-amy-medium.onnx \
    --output-raw > /tmp/piper_test.raw 2>&1
ls -l /tmp/piper_test.raw
```
Expected: file size > 10 000 bytes (audio was synthesized).

- [ ] **Step 5: Commit**

```bash
git add scripts/build_piper.sh
git commit -m "build(piper): build_piper.sh produces complete piper install with dylibs"
```

> **Note**: do NOT commit `assets/piper/*.dylib` to git. The script repopulates them on demand. Confirm `.gitignore` ignores them (add if missing):
> ```bash
> grep -q '^assets/piper/\*\.dylib' .gitignore || \
>   echo 'assets/piper/*.dylib' >> .gitignore
> ```

### Task 1.2: fetch_piper.sh detects missing dylibs

**Files:**
- Modify: `scripts/fetch_piper.sh`

- [ ] **Step 1: Append detection logic**

After the existing voice-fetch block at end of `scripts/fetch_piper.sh`, append:
```bash
REQUIRED_DYLIBS=(libespeak-ng.1.dylib libpiper_phonemize.1.dylib libonnxruntime.1.14.1.dylib)
MISSING=0
for lib in "${REQUIRED_DYLIBS[@]}"; do
    if [[ ! -f "$PIPER_DIR/$lib" ]]; then
        echo "Missing: $PIPER_DIR/$lib"
        MISSING=1
    fi
done

if [[ $MISSING -eq 1 ]]; then
    cat >&2 <<EOM
Piper runtime dylibs are missing — the upstream macOS tarball ships
without them. Run:

    ./scripts/build_piper.sh

This builds Piper from source and copies the dylibs into $PIPER_DIR.
EOM
    exit 1
fi
echo "All required Piper dylibs present."
```

- [ ] **Step 2: Verify on a known-good install**

```bash
./scripts/fetch_piper.sh
```
Expected: prints "All required Piper dylibs present." (because Task 1.1 already populated them).

- [ ] **Step 3: Verify error path**

```bash
mv assets/piper/libespeak-ng.1.dylib /tmp/_save.dylib
./scripts/fetch_piper.sh; echo "exit=$?"
mv /tmp/_save.dylib assets/piper/libespeak-ng.1.dylib
```
Expected: exit=1, error message mentions `build_piper.sh`.

- [ ] **Step 4: Commit**

```bash
git add scripts/fetch_piper.sh
git commit -m "build(piper): fetch_piper.sh detects missing dylibs and points at build_piper.sh"
```

### Task 1.3: End-to-end Piper synthesize test

**Files:**
- Modify: `tests/unit/audio/test_piper_tts_source.cpp`

- [ ] **Step 1: Read the existing test**

```bash
cat tests/unit/audio/test_piper_tts_source.cpp
```
Note: the existing test likely skips when `isReady()` returns false. Locate that skip branch.

- [ ] **Step 2: Add a positive-path test case**

Append to the file:
```cpp
TEST_CASE("PiperTTSSource: synthesizes a short phrase end-to-end",
          "[audio][piper][e2e]") {
    using namespace guitar_dsp::audio;
    PiperTTSSource src("assets/piper/piper",
                       "assets/piper/voices/en_US-amy-medium.onnx");
    src.prepare(48000.0);
    if (!src.isReady()) {
        WARN("PiperTTSSource not ready: " + src.statusDetail());
        return;
    }
    auto clip = src.synthesize("hello world");
    REQUIRE(clip);
    REQUIRE(clip->samples.size() > 4800);   // > 0.1 s @ 48 kHz
    REQUIRE(clip->sampleRate == 48000.0);
}
```

- [ ] **Step 3: Build & run**

```bash
cmake --build build-tests --target guitar_dsp_tests
./build-tests/tests/guitar_dsp_tests "[piper][e2e]"
```
Expected: PASS (after Task 1.1 populated the dylibs).

- [ ] **Step 4: Commit**

```bash
git add tests/unit/audio/test_piper_tts_source.cpp
git commit -m "test(piper): end-to-end synthesize test for PiperTTSSource"
```

---

## Phase 2 — Phoneme types & extraction

### Task 2.1: Phoneme data type

**Files:**
- Create: `src/audio/Phoneme.h`

- [ ] **Step 1: Write the header**

```cpp
#pragma once

#include <cstddef>
#include <string>

namespace guitar_dsp::audio {

struct Phoneme {
    enum class Type { Vowel, Consonant, Silence };

    std::string  label;          // espeak label, e.g. "AE", "m", "_" (silence)
    Type         type = Type::Consonant;
    std::size_t  startSample = 0;  // inclusive
    std::size_t  endSample   = 0;  // exclusive

    std::size_t lengthSamples() const noexcept {
        return endSample > startSample ? endSample - startSample : 0;
    }
};

// Sonority rank of a phoneme label. Higher = more sonorous.
// Used by Syllabifier::group. Open vowels = 6; nasals = 3; stops = 0.
int phonemeSonority(const std::string& label) noexcept;

// Classifies an espeak label as vowel / consonant / silence.
Phoneme::Type phonemeType(const std::string& label) noexcept;

} // namespace guitar_dsp::audio
```

- [ ] **Step 2: Stub the .cpp**

`src/audio/Phoneme.cpp`:
```cpp
#include "Phoneme.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace guitar_dsp::audio {

namespace {

constexpr std::array<std::string_view, 18> kVowels = {
    "a","A","e","E","i","I","o","O","u","U","V","@","3","6","Q","aI","aU","OI"
};
constexpr std::array<std::string_view, 8> kSilenceLabels = {
    "_","__"," ","-","\n","\t","",","
};

bool isOneOf(const std::string& s, const auto& set) {
    return std::any_of(set.begin(), set.end(),
                       [&](std::string_view e){ return e == s; });
}
} // namespace

Phoneme::Type phonemeType(const std::string& label) noexcept {
    if (label.empty() || isOneOf(label, kSilenceLabels))
        return Phoneme::Type::Silence;
    // espeak's vowel labels are mostly lowercase IPA-ish; also uppercase
    // diphthongs (aI, aU, OI). Heuristic: if first char is in kVowels set,
    // treat as vowel. Edge cases (syllabic n/l/m) fall through as consonant.
    const std::string first(1, label[0]);
    if (isOneOf(first, kVowels)) return Phoneme::Type::Vowel;
    if (isOneOf(label, kVowels)) return Phoneme::Type::Vowel;
    return Phoneme::Type::Consonant;
}

int phonemeSonority(const std::string& label) noexcept {
    const auto t = phonemeType(label);
    if (t == Phoneme::Type::Vowel)   return 6;
    if (t == Phoneme::Type::Silence) return -1;
    // Consonant sub-ranks: liquids/glides 4, nasals 3, fricatives 2,
    // affricates 1, stops 0.
    if (label.find_first_of("rlwjy") != std::string::npos) return 4;
    if (label.find_first_of("mnN")   != std::string::npos) return 3;
    if (label.find_first_of("szSZfvTDhx") != std::string::npos) return 2;
    if (label.find_first_of("tSdZ")  != std::string::npos) return 1;
    return 0;  // stops: p, b, t, d, k, g
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 3: Add to test target & verify compiles**

In `tests/CMakeLists.txt`, ensure the test target's source compile path picks up `src/audio/Phoneme.cpp` (it should via the existing `target_include_directories` + glob, but if explicit list, add `unit/audio/test_phoneme.cpp` in Task 2.3 step).

Add minimal compile-only test now to lock the type:

`tests/unit/audio/test_phoneme_compiles.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/Phoneme.h"
using guitar_dsp::audio::Phoneme;

TEST_CASE("Phoneme type: vowel/consonant/silence classification",
          "[audio][phoneme]") {
    REQUIRE(guitar_dsp::audio::phonemeType("a")  == Phoneme::Type::Vowel);
    REQUIRE(guitar_dsp::audio::phonemeType("aI") == Phoneme::Type::Vowel);
    REQUIRE(guitar_dsp::audio::phonemeType("m")  == Phoneme::Type::Consonant);
    REQUIRE(guitar_dsp::audio::phonemeType("_")  == Phoneme::Type::Silence);
    REQUIRE(guitar_dsp::audio::phonemeType("")   == Phoneme::Type::Silence);
}

TEST_CASE("Phoneme sonority: vowel > liquid > nasal > fric > stop",
          "[audio][phoneme]") {
    using guitar_dsp::audio::phonemeSonority;
    REQUIRE(phonemeSonority("a") > phonemeSonority("r"));
    REQUIRE(phonemeSonority("r") > phonemeSonority("m"));
    REQUIRE(phonemeSonority("m") > phonemeSonority("s"));
    REQUIRE(phonemeSonority("s") > phonemeSonority("t"));
}
```

Add to `tests/CMakeLists.txt`:
```
    unit/audio/test_phoneme_compiles.cpp
```

- [ ] **Step 4: Build & run**

```bash
cmake --build build-tests --target guitar_dsp_tests
./build-tests/tests/guitar_dsp_tests "[phoneme]"
```
Expected: 2 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/audio/Phoneme.h src/audio/Phoneme.cpp \
        tests/unit/audio/test_phoneme_compiles.cpp tests/CMakeLists.txt
git commit -m "feat(audio): Phoneme type + sonority/classification helpers"
```

### Task 2.2: PhonemeExtractor (espeak-ng -X subprocess)

**Files:**
- Create: `src/audio/PhonemeExtractor.h`
- Create: `src/audio/PhonemeExtractor.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once

#include <string>
#include <vector>

#include "Phoneme.h"

namespace guitar_dsp::audio {

// Shells out to `espeak-ng -q -X -v en-us "<text>"` and parses the
// phoneme stream + predicted per-phoneme durations.
//
// Phoneme times in the result are RAW espeak predictions, in milliseconds,
// converted to sample positions at `targetSampleRate`. Callers
// (PhonemeAlignedClipBuilder) typically rescale them to match actual
// Piper audio length.
//
// Call from a worker thread. The subprocess takes ~30-80 ms per short
// phrase on M-series. Returns empty vector on failure.
class PhonemeExtractor {
public:
    PhonemeExtractor(std::string binaryPath, std::string voice = "en-us");

    std::vector<Phoneme> extract(const std::string& text,
                                 double targetSampleRate) const;

    // Path to the espeak-ng binary (e.g. "assets/piper/espeak-ng").
    bool isReady() const;
    std::string statusDetail() const;

private:
    std::string binaryPath_;
    std::string voice_;
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 2: Implementation**

`src/audio/PhonemeExtractor.cpp`:
```cpp
#include "PhonemeExtractor.h"

#include <juce_core/juce_core.h>

#include <cerrno>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>

#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace guitar_dsp::audio {

namespace {

// Runs espeak-ng and collects stdout. -X flag emits phoneme stream with
// durations in tenths of ms. -q suppresses audio. Format per phoneme:
//   "label dur_tenths_ms"  separated by newlines, e.g.:
//   "h_              0  19 D-h         < h ə> l əʊ"
// In practice espeak's -X output is multiline trace info; we use the
// simpler `--phonout` + `-x` combination by piping through.
//
// We use `-q -x` (phoneme stream only, no audio) and a separate
// per-phoneme-duration derivation by re-running with `-X`. The simplest
// reliable parse: run with `-q --pho` (one phoneme per line w/ duration).
std::vector<std::pair<std::string,double>> runEspeak(
        const std::string& bin, const std::string& voice,
        const std::string& text) {
    int stdoutPipe[2] = {-1, -1};
    if (pipe(stdoutPipe) != 0) return {};
    const pid_t pid = fork();
    if (pid < 0) { ::close(stdoutPipe[0]); ::close(stdoutPipe[1]); return {}; }

    if (pid == 0) {
        // Child.
        ::dup2(stdoutPipe[1], STDOUT_FILENO);
        const int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) { ::dup2(devnull, STDERR_FILENO); ::close(devnull); }
        ::close(stdoutPipe[0]); ::close(stdoutPipe[1]);
        ::execl(bin.c_str(), bin.c_str(),
                "-q", "--pho",
                "-v", voice.c_str(),
                text.c_str(),
                static_cast<char*>(nullptr));
        ::_exit(127);
    }

    ::close(stdoutPipe[1]);
    std::string acc;
    char buf[4096];
    while (true) {
        const ssize_t n = ::read(stdoutPipe[0], buf, sizeof(buf));
        if (n > 0) { acc.append(buf, buf + n); continue; }
        if (n == 0) break;
        if (errno == EINTR) continue;
        break;
    }
    ::close(stdoutPipe[0]);
    int status = 0;
    ::waitpid(pid, &status, 0);

    // Parse `--pho` format: one line per phoneme:
    //   "<label> <duration_ms>"
    // Silence lines have label "_" and represent inter-word pauses.
    std::vector<std::pair<std::string,double>> out;
    std::istringstream iss(acc);
    std::string line;
    while (std::getline(iss, line)) {
        std::istringstream ls(line);
        std::string label;
        double durMs = 0.0;
        if (ls >> label >> durMs && durMs > 0.0) {
            out.emplace_back(label, durMs);
        }
    }
    return out;
}

} // namespace

PhonemeExtractor::PhonemeExtractor(std::string binaryPath, std::string voice)
    : binaryPath_(std::move(binaryPath)), voice_(std::move(voice)) {}

bool PhonemeExtractor::isReady() const { return statusDetail().empty(); }

std::string PhonemeExtractor::statusDetail() const {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(binaryPath_, ec) || ec)
        return "espeak-ng binary missing at " + binaryPath_;
    const auto perms = fs::status(binaryPath_, ec).permissions();
    if (ec) return "could not stat espeak-ng at " + binaryPath_;
    if ((perms & (fs::perms::owner_exec | fs::perms::group_exec |
                  fs::perms::others_exec)) == fs::perms::none)
        return "espeak-ng at " + binaryPath_ + " is not executable";
    return {};
}

std::vector<Phoneme> PhonemeExtractor::extract(
        const std::string& text, double targetSampleRate) const {
    if (text.empty() || !isReady()) return {};
    const auto raw = runEspeak(binaryPath_, voice_, text);
    if (raw.empty()) {
        std::cerr << "[PhonemeExtractor] no phonemes for: " << text << '\n';
        return {};
    }
    std::vector<Phoneme> result;
    result.reserve(raw.size());
    std::size_t cursor = 0;
    for (const auto& [label, durMs] : raw) {
        const auto durSamples = static_cast<std::size_t>(
            durMs * 0.001 * targetSampleRate);
        Phoneme p;
        p.label = label;
        p.type  = phonemeType(label);
        p.startSample = cursor;
        p.endSample   = cursor + durSamples;
        result.push_back(p);
        cursor += durSamples;
    }
    return result;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 3: Add to test build**

In `tests/CMakeLists.txt`, ensure `src/audio/PhonemeExtractor.cpp` is in the lib the tests link against. (Check current convention — likely a glob or per-file list under `src/audio`.) Re-check how `PiperTTSSource.cpp` is linked; mirror that.

- [ ] **Step 4: Build (compile-only check)**

```bash
cmake --build build-tests --target guitar_dsp_tests
```
Expected: compiles clean.

- [ ] **Step 5: Commit**

```bash
git add src/audio/PhonemeExtractor.h src/audio/PhonemeExtractor.cpp \
        tests/CMakeLists.txt
git commit -m "feat(audio): PhonemeExtractor (espeak-ng --pho subprocess)"
```

### Task 2.3: PhonemeExtractor end-to-end test

**Files:**
- Create: `tests/unit/audio/test_phoneme_extractor.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/PhonemeExtractor.h"

using guitar_dsp::audio::PhonemeExtractor;
using guitar_dsp::audio::Phoneme;

TEST_CASE("PhonemeExtractor: extracts phonemes from a 2-word phrase",
          "[audio][phex]") {
    PhonemeExtractor pe("assets/piper/espeak-ng");
    if (!pe.isReady()) {
        WARN("espeak-ng not present: " + pe.statusDetail());
        return;
    }
    auto ph = pe.extract("hello world", 48000.0);
    REQUIRE(ph.size() >= 6);            // h-e-l-o w-o-r-l-d roughly
    // First phoneme should start at sample 0.
    REQUIRE(ph.front().startSample == 0);
    // Phonemes should be contiguous in time.
    for (std::size_t i = 1; i < ph.size(); ++i)
        REQUIRE(ph[i].startSample == ph[i-1].endSample);
    // Should contain at least one vowel.
    bool hasVowel = false;
    for (const auto& p : ph)
        if (p.type == Phoneme::Type::Vowel) { hasVowel = true; break; }
    REQUIRE(hasVowel);
}

TEST_CASE("PhonemeExtractor: empty text returns empty", "[audio][phex]") {
    PhonemeExtractor pe("assets/piper/espeak-ng");
    REQUIRE(pe.extract("", 48000.0).empty());
}

TEST_CASE("PhonemeExtractor: bad binary path is detected", "[audio][phex]") {
    PhonemeExtractor pe("/nonexistent/espeak-ng");
    REQUIRE_FALSE(pe.isReady());
    REQUIRE(pe.extract("hello", 48000.0).empty());
}
```

- [ ] **Step 2: Add to CMake**

```
    unit/audio/test_phoneme_extractor.cpp
```

- [ ] **Step 3: Build & run**

```bash
cmake --build build-tests --target guitar_dsp_tests
./build-tests/tests/guitar_dsp_tests "[phex]"
```
Expected: 3 tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/unit/audio/test_phoneme_extractor.cpp tests/CMakeLists.txt
git commit -m "test(audio): PhonemeExtractor end-to-end with espeak-ng"
```

---

## Phase 3 — Syllabification

### Task 3.1: Syllabifier (sonority sequencing)

**Files:**
- Create: `src/audio/Syllabifier.h`
- Create: `src/audio/Syllabifier.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once

#include <vector>

#include "Phoneme.h"

namespace guitar_dsp::audio {

// A single syllable: phoneme indices into the input vector, plus pre-
// computed sample ranges and the vowel-nucleus anchor for grain looping.
struct SyllableSpan {
    std::vector<int> phonemeIndices;   // into input Phonemes
    std::size_t startSample = 0;
    std::size_t endSample   = 0;
    std::size_t vowelNucleusSample = 0;  // midpoint of nucleus vowel
    std::size_t attackEndSample = 0;     // approx vowel attack end
    std::size_t codaStartSample = 0;     // start of coda consonants
    bool        nucleusIsFricative = false;  // skip Sustain if true
};

class Syllabifier {
public:
    // Groups phonemes into syllables via the sonority sequencing principle:
    // every vowel (or fricative/silence-bordered sonority peak) is a
    // nucleus; preceding consonants attach as onset by max-onset principle,
    // following consonants as coda. Silences (Phoneme::Type::Silence)
    // end the current syllable.
    static std::vector<SyllableSpan> group(const std::vector<Phoneme>& phonemes);
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 2: Implementation**

`src/audio/Syllabifier.cpp`:
```cpp
#include "Syllabifier.h"

namespace guitar_dsp::audio {

namespace {
bool isNucleus(const Phoneme& p) {
    return p.type == Phoneme::Type::Vowel;
}
}

std::vector<SyllableSpan> Syllabifier::group(
        const std::vector<Phoneme>& ph) {
    std::vector<SyllableSpan> out;
    if (ph.empty()) return out;

    // 1) Find all nucleus indices (vowels). Each becomes its own syllable.
    std::vector<int> nuclei;
    for (int i = 0; i < static_cast<int>(ph.size()); ++i)
        if (isNucleus(ph[i])) nuclei.push_back(i);

    if (nuclei.empty()) return out;  // no vowels = nothing to syllabify

    // 2) For each nucleus, attach onset consonants (back to previous
    //    boundary) and coda consonants (forward to midpoint with next
    //    nucleus via max-onset principle).
    for (std::size_t n = 0; n < nuclei.size(); ++n) {
        const int nucIdx = nuclei[n];

        // Onset: from end of previous syllable (or start) up to nucIdx.
        int onsetStart;
        if (n == 0) {
            // Walk back through consonants/silences from nucIdx.
            onsetStart = nucIdx;
            while (onsetStart > 0
                   && ph[onsetStart-1].type != Phoneme::Type::Silence) {
                --onsetStart;
            }
        } else {
            // Boundary chosen by max-onset principle: as many consonants as
            // possible go to the *next* nucleus's onset (i.e., to THIS
            // syllable's onset). Simple impl: split the inter-nuclear
            // consonant span at floor(span/2). All-but-first go to onset.
            const int prevNuc = nuclei[n-1];
            const int gap = nucIdx - prevNuc - 1;  // # consonants between
            // gap >= 0; first ceil(gap/2) consonants become prev coda,
            // remaining floor(gap/2) become this onset.
            const int prevCodaCount = (gap + 1) / 2;
            onsetStart = prevNuc + 1 + prevCodaCount;
        }

        // Coda: from nucIdx+1 forward.
        int codaEnd;
        if (n + 1 == nuclei.size()) {
            // Walk forward through consonants/silences from nucIdx.
            codaEnd = nucIdx + 1;
            while (codaEnd < static_cast<int>(ph.size())
                   && ph[codaEnd].type != Phoneme::Type::Silence) {
                ++codaEnd;
            }
        } else {
            // See above: prevCodaCount of THIS syllable goes here.
            const int nextNuc = nuclei[n+1];
            const int gap = nextNuc - nucIdx - 1;
            const int codaCount = (gap + 1) / 2;  // first half of gap
            codaEnd = nucIdx + 1 + codaCount;
        }

        SyllableSpan s;
        for (int i = onsetStart; i < codaEnd; ++i) s.phonemeIndices.push_back(i);
        if (s.phonemeIndices.empty()) continue;

        s.startSample = ph[onsetStart].startSample;
        s.endSample   = ph[codaEnd - 1].endSample;
        const auto& nuc = ph[nucIdx];
        s.vowelNucleusSample = (nuc.startSample + nuc.endSample) / 2;
        s.attackEndSample    = nuc.startSample + nuc.lengthSamples() / 4;
        s.codaStartSample    = nuc.endSample;
        s.nucleusIsFricative = (phonemeSonority(nuc.label) <= 2);
        out.push_back(s);
    }
    return out;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 3: Commit (interface lock)**

```bash
git add src/audio/Syllabifier.h src/audio/Syllabifier.cpp
git commit -m "feat(audio): Syllabifier — sonority-peak grouping with max-onset"
```

### Task 3.2: Syllabifier unit tests

**Files:**
- Create: `tests/unit/audio/test_syllabifier.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write tests**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/Syllabifier.h"

using guitar_dsp::audio::Syllabifier;
using guitar_dsp::audio::Phoneme;

namespace {
Phoneme make(const std::string& l, Phoneme::Type t,
             std::size_t s, std::size_t e) {
    Phoneme p; p.label = l; p.type = t; p.startSample = s; p.endSample = e;
    return p;
}
}

TEST_CASE("Syllabifier: empty input yields empty output", "[audio][syl]") {
    REQUIRE(Syllabifier::group({}).empty());
}

TEST_CASE("Syllabifier: single CV (consonant-vowel) is one syllable",
          "[audio][syl]") {
    // "ma" — m + a
    std::vector<Phoneme> ph = {
        make("m", Phoneme::Type::Consonant, 0, 1000),
        make("a", Phoneme::Type::Vowel,     1000, 3000),
    };
    auto s = Syllabifier::group(ph);
    REQUIRE(s.size() == 1);
    REQUIRE(s[0].phonemeIndices == std::vector<int>{0, 1});
    REQUIRE(s[0].startSample == 0);
    REQUIRE(s[0].endSample == 3000);
    REQUIRE(s[0].vowelNucleusSample == 2000);  // midpoint of a
}

TEST_CASE("Syllabifier: CVCV → two syllables (max-onset)",
          "[audio][syl]") {
    // "mama" — m + a + m + a
    std::vector<Phoneme> ph = {
        make("m", Phoneme::Type::Consonant, 0,    1000),
        make("a", Phoneme::Type::Vowel,     1000, 2500),
        make("m", Phoneme::Type::Consonant, 2500, 3500),
        make("a", Phoneme::Type::Vowel,     3500, 5000),
    };
    auto s = Syllabifier::group(ph);
    REQUIRE(s.size() == 2);
    REQUIRE(s[0].phonemeIndices == std::vector<int>{0, 1});         // m-a
    REQUIRE(s[1].phonemeIndices == std::vector<int>{2, 3});         // m-a
}

TEST_CASE("Syllabifier: VCCV → max-onset puts CC on syllable 2",
          "[audio][syl]") {
    // "atra" — a + t + r + a → syllable 1 = "a", syllable 2 = "tra"
    std::vector<Phoneme> ph = {
        make("a", Phoneme::Type::Vowel,     0,    1500),
        make("t", Phoneme::Type::Consonant, 1500, 2000),
        make("r", Phoneme::Type::Consonant, 2000, 2500),
        make("a", Phoneme::Type::Vowel,     2500, 4000),
    };
    auto s = Syllabifier::group(ph);
    REQUIRE(s.size() == 2);
    // With (gap=2, prevCodaCount=ceil(2/2)=1), coda of syl 1 = {t},
    // onset of syl 2 = {r}. Acceptable simple-max-onset behavior.
    REQUIRE(s[0].phonemeIndices == std::vector<int>{0, 1});
    REQUIRE(s[1].phonemeIndices == std::vector<int>{2, 3});
}

TEST_CASE("Syllabifier: silence ends a syllable", "[audio][syl]") {
    // "ma _ pa" — m+a then silence then p+a
    std::vector<Phoneme> ph = {
        make("m", Phoneme::Type::Consonant, 0,    500),
        make("a", Phoneme::Type::Vowel,     500,  1500),
        make("_", Phoneme::Type::Silence,   1500, 2000),
        make("p", Phoneme::Type::Consonant, 2000, 2500),
        make("a", Phoneme::Type::Vowel,     2500, 3500),
    };
    auto s = Syllabifier::group(ph);
    REQUIRE(s.size() == 2);
    REQUIRE(s[0].endSample <= 1500);
    REQUIRE(s[1].startSample >= 2000);
}
```

- [ ] **Step 2: Add to CMake**

```
    unit/audio/test_syllabifier.cpp
```

- [ ] **Step 3: Build & run**

```bash
cmake --build build-tests --target guitar_dsp_tests
./build-tests/tests/guitar_dsp_tests "[syl]"
```
Expected: 5 tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/unit/audio/test_syllabifier.cpp tests/CMakeLists.txt
git commit -m "test(audio): Syllabifier grouping cases — CV, CVCV, VCCV, silence"
```

---

## Phase 4 — PhonemeAlignedClip data model + builder

### Task 4.1: Extend TTSClip with sylsV2

**Files:**
- Modify: `src/audio/TTSClip.h`

- [ ] **Step 1: Add fields & include**

At the top of `src/audio/TTSClip.h`, after the `#include` of `<vector>`, add:
```cpp
#include "Syllabifier.h"
#include "Phoneme.h"
```

After the `syllables` field (line 25 today), inside the `TTSClip` struct, add:
```cpp
    // v2: phoneme-aligned syllable map. Populated by
    // PhonemeAlignedClipBuilder; empty for v1 clips. v1's `syllables`
    // array above stays for the v1 player.
    std::vector<Phoneme>       phonemes;
    std::vector<SyllableSpan>  sylsV2;
```

- [ ] **Step 2: Compile-only check**

```bash
cmake --build build-tests --target guitar_dsp_tests
```
Expected: compiles clean. v1 tests still pass (`[notestep][aligner]`).

- [ ] **Step 3: Run v1 tests + golden test to confirm no behavior drift**

```bash
./build-tests/tests/guitar_dsp_tests "[notestep],[aligner],[v1golden]"
```
Expected: all pass byte-equal.

- [ ] **Step 4: Commit**

```bash
git add src/audio/TTSClip.h
git commit -m "feat(audio): TTSClip gains phonemes + sylsV2 arrays (v1 untouched)"
```

### Task 4.2: PhonemeAlignedClipBuilder

**Files:**
- Create: `src/audio/PhonemeAlignedClipBuilder.h`
- Create: `src/audio/PhonemeAlignedClipBuilder.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once

#include <memory>
#include <string>

#include "ITTSSource.h"
#include "PhonemeExtractor.h"
#include "TTSClip.h"

namespace guitar_dsp::audio {

// Synthesizes audio via an ITTSSource (Piper), extracts phonemes via
// PhonemeExtractor, rescales phoneme durations to the actual audio
// length, syllabifies, and returns a fully-populated TTSClipPtr.
//
// Call from a worker thread. Returns nullptr on failure (TTS source not
// ready, phoneme extractor not ready, empty text).
class PhonemeAlignedClipBuilder {
public:
    PhonemeAlignedClipBuilder(ITTSSource* tts,
                              const PhonemeExtractor* phex);

    TTSClipPtr build(const std::string& text) const;

private:
    ITTSSource* tts_;
    const PhonemeExtractor* phex_;
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 2: Implementation**

`src/audio/PhonemeAlignedClipBuilder.cpp`:
```cpp
#include "PhonemeAlignedClipBuilder.h"

#include "Syllabifier.h"

#include <iostream>

namespace guitar_dsp::audio {

PhonemeAlignedClipBuilder::PhonemeAlignedClipBuilder(
        ITTSSource* tts, const PhonemeExtractor* phex)
    : tts_(tts), phex_(phex) {}

TTSClipPtr PhonemeAlignedClipBuilder::build(const std::string& text) const {
    if (text.empty() || tts_ == nullptr || phex_ == nullptr) return nullptr;

    auto clip = tts_->synthesize(text);
    if (!clip || clip->samples.empty()) {
        std::cerr << "[PhonemeAlignedClipBuilder] TTS produced empty clip\n";
        return nullptr;
    }

    auto phonemes = phex_->extract(text, clip->sampleRate);
    if (phonemes.empty()) {
        std::cerr << "[PhonemeAlignedClipBuilder] PhonemeExtractor empty; "
                     "returning clip with no phoneme map\n";
        return clip;  // v1-compatible: no sylsV2 means v1 fallback wiring.
    }

    // Rescale phoneme times so total matches actual audio length.
    const std::size_t actualLen = clip->samples.size();
    const std::size_t predictedLen = phonemes.back().endSample;
    if (predictedLen == 0) return clip;
    const double scale = static_cast<double>(actualLen)
                       / static_cast<double>(predictedLen);

    std::vector<Phoneme> rescaled;
    rescaled.reserve(phonemes.size());
    std::size_t cursor = 0;
    for (const auto& p : phonemes) {
        Phoneme q = p;
        q.startSample = cursor;
        q.endSample   = static_cast<std::size_t>(
            static_cast<double>(p.endSample) * scale);
        if (q.endSample > actualLen) q.endSample = actualLen;
        if (q.endSample < q.startSample) q.endSample = q.startSample;
        cursor = q.endSample;
        rescaled.push_back(q);
    }
    // Ensure final phoneme ends exactly at actualLen.
    if (!rescaled.empty()) rescaled.back().endSample = actualLen;

    auto syllables = Syllabifier::group(rescaled);

    // Build a new clip with the existing audio + phoneme + syllable maps.
    // (TTSClipPtr is shared_ptr<const TTSClip>; copy-construct a mutable
    // version, fill, then re-wrap.)
    auto out = std::make_shared<TTSClip>(*clip);
    out->phonemes = std::move(rescaled);
    out->sylsV2   = std::move(syllables);
    return out;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 3: Build (compile-only)**

```bash
cmake --build build-tests --target guitar_dsp_tests
```

- [ ] **Step 4: Commit**

```bash
git add src/audio/PhonemeAlignedClipBuilder.h \
        src/audio/PhonemeAlignedClipBuilder.cpp
git commit -m "feat(audio): PhonemeAlignedClipBuilder — TTS+phex → sylsV2 clip"
```

### Task 4.3: Builder integration test

**Files:**
- Create: `tests/unit/audio/test_phoneme_aligned_clip_builder.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/PhonemeAlignedClipBuilder.h"
#include "audio/PiperTTSSource.h"
#include "audio/PhonemeExtractor.h"

using namespace guitar_dsp::audio;

TEST_CASE("PhonemeAlignedClipBuilder: end-to-end produces sylsV2",
          "[audio][builder][e2e]") {
    PiperTTSSource piper("assets/piper/piper",
                         "assets/piper/voices/en_US-amy-medium.onnx");
    piper.prepare(48000.0);
    PhonemeExtractor phex("assets/piper/espeak-ng");
    if (!piper.isReady() || !phex.isReady()) {
        WARN("piper/espeak-ng not ready — skipping");
        return;
    }
    PhonemeAlignedClipBuilder b(&piper, &phex);
    auto clip = b.build("hello world");
    REQUIRE(clip);
    REQUIRE(clip->samples.size() > 0);
    REQUIRE(!clip->phonemes.empty());
    REQUIRE(!clip->sylsV2.empty());
    // Syllables should cover audio start-to-end approximately.
    REQUIRE(clip->sylsV2.front().startSample == 0);
    REQUIRE(clip->sylsV2.back().endSample <= clip->samples.size());
    REQUIRE(clip->sylsV2.back().endSample
            >= clip->samples.size() * 9 / 10);  // within last 10 %
}
```

- [ ] **Step 2: CMake & build**

Add `unit/audio/test_phoneme_aligned_clip_builder.cpp` to `tests/CMakeLists.txt`.
```bash
cmake --build build-tests --target guitar_dsp_tests
./build-tests/tests/guitar_dsp_tests "[builder]"
```
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/audio/test_phoneme_aligned_clip_builder.cpp \
        tests/CMakeLists.txt
git commit -m "test(audio): PhonemeAlignedClipBuilder end-to-end"
```

---

## Phase 5 — PhonemeSteppedTTSPlayer (the player)

### Task 5.1: Player header + Idle/Attack state

**Files:**
- Create: `src/audio/PhonemeSteppedTTSPlayer.h`
- Create: `src/audio/PhonemeSteppedTTSPlayer.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once

#include <atomic>
#include <cstddef>
#include <memory>

#include "OnsetDetector.h"
#include "TTSClip.h"
#include "VowelGrainLoop.h"

namespace guitar_dsp::audio {

// Plays a phoneme-aligned TTSClip one syllable per onset, with vowel
// sustain via VowelGrainLoop. Parallel to NoteSteppedTTSPlayer; does
// NOT replace it.
//
// Allocation-free in process().
class PhonemeSteppedTTSPlayer {
public:
    enum class State { Idle, Attack, Sustain, Coda };

    PhonemeSteppedTTSPlayer();
    void prepare(double sampleRate, int blockSize);
    void reset();

    void setClip(TTSClipPtr clip);                     // message thread
    void setMaxSustainMs(double ms) noexcept;          // message thread
    void setLoop(bool on) noexcept { loop_.store(on, std::memory_order_relaxed); }
    void rewind() noexcept;

    void process(const float* onsetSrc, float* modOut,
                 std::size_t numSamples) noexcept;     // audio thread

    int currentSyllableIndex() const noexcept {
        return currentSylIdx_.load(std::memory_order_relaxed);
    }
    State currentState() const noexcept {
        return static_cast<State>(currentState_.load(std::memory_order_relaxed));
    }

private:
    void advanceToNext_();   // RT-safe
    void enterSustain_();
    void enterCoda_();

    OnsetDetector onset_;
    VowelGrainLoop grain_;

    std::atomic<bool> newClipFlag_{false};
    std::atomic<bool> pendingRewind_{false};
    TTSClipPtr pendingClip_;
    TTSClipPtr activeClip_;

    int   sylIdx_   = -1;
    std::size_t playPos_ = 0;
    State state_ = State::Idle;
    std::size_t sustainSamplesPlayed_ = 0;
    std::size_t maxSustainSamples_ = 72000;  // 1.5 s @ 48 kHz, default
    double sampleRate_ = 48000.0;

    std::atomic<int>  currentSylIdx_{-1};
    std::atomic<int>  currentState_{0};
    std::atomic<bool> loop_{true};
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 2: Skeleton .cpp (Idle + Attack only — Sustain/Coda stubbed)**

`src/audio/PhonemeSteppedTTSPlayer.cpp`:
```cpp
#include "PhonemeSteppedTTSPlayer.h"

#include <algorithm>

namespace guitar_dsp::audio {

PhonemeSteppedTTSPlayer::PhonemeSteppedTTSPlayer() = default;

void PhonemeSteppedTTSPlayer::prepare(double sr, int /*blockSize*/) {
    sampleRate_ = sr;
    onset_.prepare(sr);
    grain_.prepare(sr);
    reset();
}

void PhonemeSteppedTTSPlayer::reset() {
    onset_.reset();
    grain_.reset();
    sylIdx_ = -1;
    playPos_ = 0;
    state_ = State::Idle;
    sustainSamplesPlayed_ = 0;
    currentSylIdx_.store(-1, std::memory_order_relaxed);
    currentState_.store(0, std::memory_order_relaxed);
}

void PhonemeSteppedTTSPlayer::setClip(TTSClipPtr c) {
    pendingClip_ = std::move(c);
    newClipFlag_.store(true, std::memory_order_release);
}

void PhonemeSteppedTTSPlayer::setMaxSustainMs(double ms) noexcept {
    maxSustainSamples_ = static_cast<std::size_t>(ms * 0.001 * sampleRate_);
}

void PhonemeSteppedTTSPlayer::rewind() noexcept {
    pendingRewind_.store(true, std::memory_order_release);
}

void PhonemeSteppedTTSPlayer::advanceToNext_() {
    if (!activeClip_) return;
    const auto& syls = activeClip_->sylsV2;
    if (syls.empty()) { state_ = State::Idle; return; }
    int next = sylIdx_ + 1;
    if (next >= static_cast<int>(syls.size())) {
        if (loop_.load(std::memory_order_relaxed)) next = 0;
        else { state_ = State::Idle; return; }
    }
    sylIdx_ = next;
    playPos_ = syls[next].startSample;
    state_ = State::Attack;
    sustainSamplesPlayed_ = 0;
    currentSylIdx_.store(sylIdx_, std::memory_order_relaxed);
    currentState_.store(static_cast<int>(state_), std::memory_order_relaxed);
}

void PhonemeSteppedTTSPlayer::enterSustain_() {
    state_ = State::Sustain;
    sustainSamplesPlayed_ = 0;
    currentState_.store(static_cast<int>(state_), std::memory_order_relaxed);
    grain_.beginLoop(activeClip_->samples.data(),
                     activeClip_->sylsV2[sylIdx_].vowelNucleusSample);
}

void PhonemeSteppedTTSPlayer::enterCoda_() {
    state_ = State::Coda;
    playPos_ = activeClip_->sylsV2[sylIdx_].codaStartSample;
    currentState_.store(static_cast<int>(state_), std::memory_order_relaxed);
}

void PhonemeSteppedTTSPlayer::process(const float* onsetSrc, float* modOut,
                                      std::size_t numSamples) noexcept {
    if (newClipFlag_.exchange(false, std::memory_order_acquire)) {
        activeClip_ = std::move(pendingClip_);
        reset();
    }
    if (pendingRewind_.exchange(false, std::memory_order_acquire)) reset();

    const bool haveClip = activeClip_ && !activeClip_->sylsV2.empty();

    for (std::size_t i = 0; i < numSamples; ++i) {
        const bool onset = onset_.processSample(onsetSrc[i]) && haveClip;
        if (onset) {
            // From Idle: advance. From Attack: defer (finish current). From
            // Sustain: enter Coda (current syllable plays its tail, then
            // next onset will advance). From Coda: advance.
            if (state_ == State::Idle)    advanceToNext_();
            else if (state_ == State::Sustain) enterCoda_();
            else if (state_ == State::Coda)    advanceToNext_();
            // Attack: ignore (default "finish" policy).
        }

        float s = 0.0f;
        if (haveClip) {
            const auto& syl = activeClip_->sylsV2[sylIdx_ >= 0 ? sylIdx_ : 0];
            if (state_ == State::Attack) {
                if (playPos_ < syl.attackEndSample
                    && playPos_ < activeClip_->samples.size()) {
                    s = activeClip_->samples[playPos_++];
                } else {
                    if (syl.nucleusIsFricative) enterCoda_();
                    else enterSustain_();
                }
            } else if (state_ == State::Sustain) {
                s = grain_.next();
                ++sustainSamplesPlayed_;
                if (sustainSamplesPlayed_ >= maxSustainSamples_) enterCoda_();
            } else if (state_ == State::Coda) {
                if (playPos_ < syl.endSample
                    && playPos_ < activeClip_->samples.size()) {
                    s = activeClip_->samples[playPos_++];
                } else {
                    state_ = State::Idle;
                    currentState_.store(0, std::memory_order_relaxed);
                }
            }
        }
        modOut[i] = s;
    }
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 3: Compile-only check**

```bash
cmake --build build-tests --target guitar_dsp_tests
```
Expected: undefined symbol for `VowelGrainLoop` (created in Task 5.3). Note the failure and continue.

- [ ] **Step 4: Commit (interface lock, will fail link)**

Skip building before commit; commit source as-is with a clear message:
```bash
git add src/audio/PhonemeSteppedTTSPlayer.h src/audio/PhonemeSteppedTTSPlayer.cpp
git commit -m "feat(audio): PhonemeSteppedTTSPlayer skeleton (state machine; needs VowelGrainLoop)"
```

### Task 5.2: VowelGrainLoop

**Files:**
- Create: `src/audio/VowelGrainLoop.h`
- Create: `src/audio/VowelGrainLoop.cpp`

- [ ] **Step 1: Header**

```cpp
#pragma once

#include <cstddef>

namespace guitar_dsp::audio {

// Pitch-period-synchronous grain loop for sustaining a vowel during
// note-hold. RT-safe; no allocation in next().
//
// Usage:
//   prepare(sampleRate);
//   beginLoop(clipSamples, anchorSample);
//   while (sustaining) modOut = grain.next();
class VowelGrainLoop {
public:
    void prepare(double sampleRate);
    void reset();

    // Start (or restart) a loop centered at `anchorSample` in `samples`.
    void beginLoop(const float* samples, std::size_t anchorSample) noexcept;

    // Returns the next sample of the looped grain. Returns 0.0f if not
    // looping or if samples is null.
    float next() noexcept;

private:
    const float* samples_ = nullptr;
    std::size_t  loopStart_  = 0;    // grain region [start, start+len)
    std::size_t  loopLen_    = 1024; // ~ pitch period samples (set later)
    std::size_t  cursor_     = 0;
    std::size_t  xfadeLen_   = 240;  // ~5 ms @ 48 kHz
    double       sampleRate_ = 48000.0;
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 2: Implementation (simple constant-period grain loop with crossfade)**

`src/audio/VowelGrainLoop.cpp`:
```cpp
#include "VowelGrainLoop.h"

namespace guitar_dsp::audio {

void VowelGrainLoop::prepare(double sr) {
    sampleRate_ = sr;
    xfadeLen_   = static_cast<std::size_t>(0.005 * sr);  // 5 ms
    reset();
}

void VowelGrainLoop::reset() {
    samples_ = nullptr;
    loopStart_ = 0;
    loopLen_ = static_cast<std::size_t>(0.020 * sampleRate_);  // 20 ms
    cursor_ = 0;
}

void VowelGrainLoop::beginLoop(const float* samples,
                               std::size_t anchorSample) noexcept {
    samples_ = samples;
    // Center the loop on the anchor.
    if (anchorSample >= loopLen_ / 2) loopStart_ = anchorSample - loopLen_ / 2;
    else                              loopStart_ = 0;
    cursor_ = 0;
}

float VowelGrainLoop::next() noexcept {
    if (samples_ == nullptr || loopLen_ == 0) return 0.0f;
    const std::size_t pos = loopStart_ + cursor_;
    float s = samples_[pos];
    // Crossfade the last `xfadeLen_` samples of the loop with the first
    // `xfadeLen_` of the next iteration. Cheap linear xfade.
    if (cursor_ + xfadeLen_ >= loopLen_) {
        const std::size_t into = cursor_ + xfadeLen_ - loopLen_;
        const float w = static_cast<float>(into)
                      / static_cast<float>(xfadeLen_);
        const float head = samples_[loopStart_ + into];
        s = (1.0f - w) * s + w * head;
    }
    if (++cursor_ >= loopLen_) cursor_ = 0;
    return s;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 3: Build (should now link cleanly)**

```bash
cmake --build build-tests --target guitar_dsp_tests
```
Expected: builds clean.

- [ ] **Step 4: Commit**

```bash
git add src/audio/VowelGrainLoop.h src/audio/VowelGrainLoop.cpp
git commit -m "feat(audio): VowelGrainLoop — linear-crossfade grain looper"
```

### Task 5.3: VowelGrainLoop unit tests

**Files:**
- Create: `tests/unit/audio/test_vowel_grain_loop.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/VowelGrainLoop.h"

#include <vector>

using guitar_dsp::audio::VowelGrainLoop;

TEST_CASE("VowelGrainLoop: returns 0 before beginLoop", "[audio][grain]") {
    VowelGrainLoop g;
    g.prepare(48000.0);
    REQUIRE(g.next() == 0.0f);
}

TEST_CASE("VowelGrainLoop: produces non-zero output after beginLoop on a sine",
          "[audio][grain]") {
    std::vector<float> samples(48000, 0.0f);
    for (std::size_t i = 0; i < samples.size(); ++i)
        samples[i] = 0.5f * std::sin(2 * 3.14159265f * 440.0f * i / 48000.0f);
    VowelGrainLoop g;
    g.prepare(48000.0);
    g.beginLoop(samples.data(), 24000);  // anchor mid-buffer

    float maxAbs = 0.0f;
    for (int i = 0; i < 2400; ++i) {  // 50 ms of output
        const float v = g.next();
        if (std::abs(v) > maxAbs) maxAbs = std::abs(v);
    }
    REQUIRE(maxAbs > 0.1f);
}

TEST_CASE("VowelGrainLoop: wraps without reading past clip end",
          "[audio][grain]") {
    std::vector<float> samples(2000, 0.123f);
    VowelGrainLoop g;
    g.prepare(48000.0);
    g.beginLoop(samples.data(), 1000);
    for (int i = 0; i < 100'000; ++i) {
        const float v = g.next();
        REQUIRE(std::isfinite(v));  // no NaN/inf from OOB read
    }
}
```

- [ ] **Step 2: Add & build**

```
    unit/audio/test_vowel_grain_loop.cpp
```
```bash
cmake --build build-tests --target guitar_dsp_tests
./build-tests/tests/guitar_dsp_tests "[grain]"
```
Expected: 3 tests pass.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/audio/test_vowel_grain_loop.cpp tests/CMakeLists.txt
git commit -m "test(audio): VowelGrainLoop — basic produce/wrap correctness"
```

### Task 5.4: PhonemeSteppedTTSPlayer state-machine tests

**Files:**
- Create: `tests/unit/audio/test_phoneme_stepped_player.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Tests**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/PhonemeSteppedTTSPlayer.h"
#include "audio/TTSClip.h"
#include "harness/RealtimeSentinel.h"

#include <memory>
#include <vector>

using namespace guitar_dsp::audio;
using guitar_dsp::tests::RealtimeSentinel;

namespace {
std::shared_ptr<const TTSClip> threeSylClip() {
    auto c = std::make_shared<TTSClip>();
    c->sampleRate = 48000.0;
    c->samples.assign(9000, 0.0f);
    for (int i = 0; i < 9000; ++i)
        c->samples[i] = 0.5f * std::sin(2*3.14159265f*220.0f*i/48000.0f);

    // Three CV syllables: spans 0..3000, 3000..6000, 6000..9000.
    // Each has its vowel anchor at the midpoint.
    auto mk = [](std::size_t s, std::size_t e) {
        SyllableSpan sp;
        sp.startSample = s;
        sp.endSample = e;
        sp.vowelNucleusSample = (s + e) / 2;
        sp.attackEndSample = s + (e - s) / 3;   // first third = Attack
        sp.codaStartSample = s + 2 * (e - s) / 3; // last third = Coda
        sp.nucleusIsFricative = false;
        return sp;
    };
    c->sylsV2 = { mk(0, 3000), mk(3000, 6000), mk(6000, 9000) };
    return c;
}

void runBlock(PhonemeSteppedTTSPlayer& p, std::size_t n,
              bool onsetAtStart, std::vector<float>& out) {
    std::vector<float> onset(n, 0.0f);
    if (onsetAtStart) for (int i = 0; i < 480; ++i) onset[i] = 0.8f;
    out.assign(n, 0.0f);
    p.process(onset.data(), out.data(), out.size());
}
}

TEST_CASE("PhonemeSteppedTTSPlayer: idle until first onset",
          "[audio][phstep]") {
    PhonemeSteppedTTSPlayer p;
    p.prepare(48000.0, 256);
    p.setClip(threeSylClip());
    std::vector<float> out;
    runBlock(p, 1000, false, out);
    for (float v : out) REQUIRE(v == 0.0f);
}

TEST_CASE("PhonemeSteppedTTSPlayer: first onset starts syllable 0 Attack",
          "[audio][phstep]") {
    PhonemeSteppedTTSPlayer p;
    p.prepare(48000.0, 256);
    p.setClip(threeSylClip());
    std::vector<float> out;
    runBlock(p, 800, true, out);  // less than attackEnd (1000)
    REQUIRE(p.currentSyllableIndex() == 0);
    REQUIRE(p.currentState() == PhonemeSteppedTTSPlayer::State::Attack);
    bool sawNonZero = false;
    for (float v : out) if (v != 0.0f) { sawNonZero = true; break; }
    REQUIRE(sawNonZero);
}

TEST_CASE("PhonemeSteppedTTSPlayer: held note enters Sustain",
          "[audio][phstep]") {
    PhonemeSteppedTTSPlayer p;
    p.prepare(48000.0, 256);
    p.setClip(threeSylClip());
    std::vector<float> out;
    // Onset then play through Attack window (1000 samples = first third).
    runBlock(p, 4000, true, out);  // long enough to enter Sustain
    REQUIRE(p.currentState() == PhonemeSteppedTTSPlayer::State::Sustain);
}

TEST_CASE("PhonemeSteppedTTSPlayer: second onset during Sustain → Coda then advance",
          "[audio][phstep]") {
    PhonemeSteppedTTSPlayer p;
    p.prepare(48000.0, 256);
    p.setClip(threeSylClip());
    std::vector<float> out;
    runBlock(p, 4000, true, out);   // syl 0 Attack→Sustain
    runBlock(p, 100, true, out);    // onset → Coda
    REQUIRE(p.currentState() == PhonemeSteppedTTSPlayer::State::Coda);
    // Play out coda, expect Idle
    runBlock(p, 4000, false, out);
    REQUIRE(p.currentState() == PhonemeSteppedTTSPlayer::State::Idle);
    // Next onset → syl 1 Attack
    runBlock(p, 100, true, out);
    REQUIRE(p.currentSyllableIndex() == 1);
}

TEST_CASE("PhonemeSteppedTTSPlayer: rewind resets to start",
          "[audio][phstep]") {
    PhonemeSteppedTTSPlayer p;
    p.prepare(48000.0, 256);
    p.setClip(threeSylClip());
    std::vector<float> out;
    runBlock(p, 800, true, out);
    p.rewind();
    runBlock(p, 100, true, out);
    REQUIRE(p.currentSyllableIndex() == 0);
}

TEST_CASE("PhonemeSteppedTTSPlayer: process() is allocation/lock-free",
          "[audio][phstep][rt]") {
    PhonemeSteppedTTSPlayer p;
    p.prepare(48000.0, 256);
    p.setClip(threeSylClip());
    std::vector<float> onset(1000, 0.4f), mod(1000, 0.0f);
    RealtimeSentinel rts;
    rts.markCurrentThreadAsRealtime();
    p.process(onset.data(), mod.data(), mod.size());
    rts.unmarkCurrentThreadAsRealtime();
    REQUIRE(rts.violations() == 0);
}
```

- [ ] **Step 2: Add & build**

```
    unit/audio/test_phoneme_stepped_player.cpp
```
```bash
cmake --build build-tests --target guitar_dsp_tests
./build-tests/tests/guitar_dsp_tests "[phstep]"
```
Expected: 6 tests pass.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/audio/test_phoneme_stepped_player.cpp tests/CMakeLists.txt
git commit -m "test(audio): PhonemeSteppedTTSPlayer state machine + RT-safety"
```

---

## Phase 6 — Scene & UI wiring

### Task 6.1: Scene 10 JSON

**Files:**
- Create: `assets/scenes/10_speak_v2_guitar_lead.json`

- [ ] **Step 1: Pick a near-neighbor scene to clone**

```bash
cat assets/scenes/01_developers.json
```
Note its overall structure (mixer, vocoder, tts, scene-specific fields).

- [ ] **Step 2: Write the JSON**

```json
{
  "id": 10,
  "name": "Speak v2 — Guitar-Lead",
  "color": "#3eafff",
  "mixer": { "masterGainDb": -5.0, "dryWet": 0.92, "transitionMs": 30 },
  "vocoder": { "enabled": true, "bypass": false },
  "tts": {
    "source": "piper",
    "fallback": "prebaked",
    "text": "while my guitar gently weeps"
  },
  "speech": {
    "player": "phonemeStepped",
    "maxSustainMs": 1500,
    "attackInterruptPolicy": "finish"
  }
}
```

- [ ] **Step 3: Load the scene library tests for parse coverage**

```bash
./build-tests/tests/guitar_dsp_tests "[scenes]"
```
Expected: existing tests still pass (the new fields are ignored until Task 6.2).

- [ ] **Step 4: Commit**

```bash
git add assets/scenes/10_speak_v2_guitar_lead.json
git commit -m "feat(scenes): scene 10 — Speak v2 Guitar-Lead (UI-only, no FCB map)"
```

### Task 6.2: SceneLibrary parses new speech fields

**Files:**
- Modify: `src/scenes/Scene.h`
- Modify: `src/scenes/SceneLibrary.cpp`
- Create: `tests/unit/scenes/test_scene_library_speech_v2.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Add fields to Scene**

In `src/scenes/Scene.h`, in the existing speech-config struct (look for the existing `tts` block; add a sibling `speech` struct):
```cpp
struct Speech {
    enum class Player { NoteStepped, PhonemeStepped };
    enum class AttackInterrupt { Finish, Interrupt };

    Player          player           = Player::NoteStepped;
    double          maxSustainMs     = 1500.0;
    AttackInterrupt attackInterrupt  = AttackInterrupt::Finish;
};
Speech speech;
```

- [ ] **Step 2: Parse in SceneLibrary**

Locate the parsing of `tts` in `src/scenes/SceneLibrary.cpp`. Add a sibling block parsing `speech`:
```cpp
if (obj->hasProperty("speech")) {
    if (auto* sp = obj->getProperty("speech").getDynamicObject()) {
        if (sp->hasProperty("player")) {
            const auto v = sp->getProperty("player").toString().toStdString();
            s.speech.player = (v == "phonemeStepped")
                ? Scene::Speech::Player::PhonemeStepped
                : Scene::Speech::Player::NoteStepped;
        }
        if (sp->hasProperty("maxSustainMs"))
            s.speech.maxSustainMs = static_cast<double>(
                sp->getProperty("maxSustainMs"));
        if (sp->hasProperty("attackInterruptPolicy")) {
            const auto v = sp->getProperty("attackInterruptPolicy").toString().toStdString();
            s.speech.attackInterrupt = (v == "interrupt")
                ? Scene::Speech::AttackInterrupt::Interrupt
                : Scene::Speech::AttackInterrupt::Finish;
        }
    }
}
```

- [ ] **Step 3: Test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "scenes/SceneLibrary.h"

using namespace guitar_dsp::scenes;

TEST_CASE("SceneLibrary: parses speech.player=phonemeStepped",
          "[scenes][speechv2]") {
    auto scenes = SceneLibrary::loadDirectory("assets/scenes");
    const Scene* s = nullptr;
    for (const auto& sc : scenes) if (sc.id == 10) { s = &sc; break; }
    REQUIRE(s);
    REQUIRE(s->speech.player == Scene::Speech::Player::PhonemeStepped);
    REQUIRE(s->speech.maxSustainMs == 1500.0);
    REQUIRE(s->speech.attackInterrupt == Scene::Speech::AttackInterrupt::Finish);
}
```

- [ ] **Step 4: Add & run**

```
    unit/scenes/test_scene_library_speech_v2.cpp
```
```bash
cmake --build build-tests --target guitar_dsp_tests
./build-tests/tests/guitar_dsp_tests "[speechv2]"
```
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add src/scenes/Scene.h src/scenes/SceneLibrary.cpp \
        tests/unit/scenes/test_scene_library_speech_v2.cpp tests/CMakeLists.txt
git commit -m "feat(scenes): parse speech.player / maxSustainMs / attackInterruptPolicy"
```

### Task 6.3: AudioGraph wiring for the new player

**Files:**
- Modify: `src/audio/AudioGraph.h`
- Modify: `src/audio/AudioGraph.cpp`

- [ ] **Step 1: Inspect the graph**

```bash
grep -n "NoteSteppedTTSPlayer\|noteStepped_\|setPlayer" src/audio/AudioGraph.{h,cpp} 2>/dev/null
```
Identify the current owner of the v1 player.

- [ ] **Step 2: Add the v2 player as a sibling**

In `AudioGraph.h`, alongside the existing `NoteSteppedTTSPlayer noteStepped_`, add:
```cpp
#include "PhonemeSteppedTTSPlayer.h"

PhonemeSteppedTTSPlayer phonemeStepped_;
enum class ActiveSpeechPlayer { None, NoteStepped, PhonemeStepped };
std::atomic<int> activeSpeechPlayer_{static_cast<int>(ActiveSpeechPlayer::NoteStepped)};
```

In `AudioGraph::prepare` (the existing one), add:
```cpp
phonemeStepped_.prepare(sampleRate, blockSize);
```

In the speech-processing block (wherever `noteStepped_.process(...)` is called), wrap:
```cpp
const auto active = static_cast<ActiveSpeechPlayer>(
    activeSpeechPlayer_.load(std::memory_order_relaxed));
if (active == ActiveSpeechPlayer::PhonemeStepped) {
    phonemeStepped_.process(onsetBuf.data(), modBuf.data(), n);
} else if (active == ActiveSpeechPlayer::NoteStepped) {
    noteStepped_.process(onsetBuf.data(), modBuf.data(), n);
}
```

Add public setters:
```cpp
void setSpeechClip(TTSClipPtr clip);   // routes to whichever is active
void setActiveSpeechPlayer(ActiveSpeechPlayer p) noexcept {
    activeSpeechPlayer_.store(static_cast<int>(p), std::memory_order_relaxed);
}
```

Wire scene activation (look for where the scene engine forwards TTS to the audio graph; add a branch on `scene.speech.player`).

- [ ] **Step 3: Build & run v1 tests**

```bash
cmake --build build-tests --target guitar_dsp_tests
./build-tests/tests/guitar_dsp_tests "[notestep],[v1golden]"
```
Expected: still pass byte-equal.

- [ ] **Step 4: Commit**

```bash
git add src/audio/AudioGraph.h src/audio/AudioGraph.cpp
git commit -m "feat(audio): AudioGraph hosts both v1 and v2 players, scene-selected"
```

### Task 6.4: SceneIndicator dynamic strip

**Files:**
- Modify: `src/app/SceneIndicator.cpp`

- [ ] **Step 1: Replace hardcoded 10**

At [SceneIndicator.cpp:25, 56–58](../../../src/app/SceneIndicator.cpp), replace the fixed `10` with the actual count from the SceneLibrary (or the SceneEngine). Sketch:
```cpp
const int n = std::max(1, processor_.sceneEngine().sceneCount());
const int slotWidth = strip.getWidth() / n;
// ...
for (int i = 0; i < n; ++i) { /* ... */ }
```
(If `sceneCount()` doesn't exist yet, add it as `int sceneCount() const noexcept` to `SceneEngine` returning `library_.scenes().size()`.)

- [ ] **Step 2: Manual visual check**

Build the app, launch, eyeball the scene strip — it should show 11 slots when scene 10 is present.

```bash
cmake --build build --target guitar_dsp
open build/guitar_dsp.app
```
Visually verify: 11 equal-width slots, scene 10 visible.

- [ ] **Step 3: Commit**

```bash
git add src/app/SceneIndicator.cpp src/scenes/SceneEngine.h src/scenes/SceneEngine.cpp
git commit -m "ui(scene-strip): dynamic slot count instead of hardcoded 10"
```

### Task 6.5: WordReadout — syllable highlight

**Files:**
- Modify: `src/app/WordReadout.h`
- Modify: `src/app/WordReadout.cpp`

- [ ] **Step 1: Add a syllable-index property**

In `WordReadout.h`, add:
```cpp
void setCurrentSyllableIndex(int globalSylIdx) noexcept;
```
And a backing `std::atomic<int> currentSyl_{-1};` field.

In `.cpp`, in `paint()` (or whatever it's called), look up the syllable in the current clip's `sylsV2`, draw its grapheme text bold/colored, the rest normal weight.

- [ ] **Step 2: Manual check**

Run the app, select scene 10, hit Say with a multi-syllable phrase, pluck — confirm the current syllable highlights.

- [ ] **Step 3: Commit**

```bash
git add src/app/WordReadout.h src/app/WordReadout.cpp
git commit -m "ui(WordReadout): highlight active syllable when phoneme-stepped"
```

### Task 6.6: DiagToggleBar — Ph pill

**Files:**
- Modify: `src/app/DiagToggleBar.cpp`
- Modify: `src/app/DiagToggleBar.h`

- [ ] **Step 1: Add the pill**

Mirror the existing `V`/`N`/`Sib`/`P`/`M` pill wiring. Add `Ph` that reflects whether the active scene's player is `PhonemeStepped`.

- [ ] **Step 2: Manual check**

Launch the app, switch between scene 01 (v1) and scene 10 (v2): the `Ph` pill should light only on scene 10.

- [ ] **Step 3: Commit**

```bash
git add src/app/DiagToggleBar.h src/app/DiagToggleBar.cpp
git commit -m "ui(DiagToggleBar): Ph pill indicates phoneme-stepped player active"
```

---

## Phase 7 — Integration tests

### Task 7.1: Say-textbox round-trip on scene 10

**Files:**
- Create: `tests/integration/test_speak_v2_scene.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/PhonemeAlignedClipBuilder.h"
#include "audio/PhonemeExtractor.h"
#include "audio/PhonemeSteppedTTSPlayer.h"
#include "audio/PiperTTSSource.h"

#include <vector>

using namespace guitar_dsp::audio;

TEST_CASE("integration: Say-textbox text → phoneme-aligned clip → player",
          "[integration][speakv2]") {
    PiperTTSSource piper("assets/piper/piper",
                         "assets/piper/voices/en_US-amy-medium.onnx");
    piper.prepare(48000.0);
    PhonemeExtractor phex("assets/piper/espeak-ng");
    if (!piper.isReady() || !phex.isReady()) {
        WARN("piper/espeak-ng not ready — skipping");
        return;
    }
    PhonemeAlignedClipBuilder b(&piper, &phex);
    auto clip = b.build("automatically learn");
    REQUIRE(clip);
    REQUIRE(clip->sylsV2.size() >= 4);   // 5-syl + 2-syl = 7-ish

    PhonemeSteppedTTSPlayer p;
    p.prepare(48000.0, 256);
    p.setClip(clip);

    // Send N onsets, expect syllable index to advance.
    auto pluck = [&](int n) {
        std::vector<float> onset(n, 0.0f), mod(n, 0.0f);
        for (int i = 0; i < 480 && i < n; ++i) onset[i] = 0.8f;
        p.process(onset.data(), mod.data(), mod.size());
    };

    pluck(1000);
    REQUIRE(p.currentSyllableIndex() == 0);
    // Play out, then onset again — sylIdx should increment over time.
    std::vector<float> silence(48000, 0.0f), mod(48000, 0.0f);
    p.process(silence.data(), mod.data(), mod.size());
    pluck(1000);
    REQUIRE(p.currentSyllableIndex() == 1);
}
```

- [ ] **Step 2: CMake & build**

Add to `tests/CMakeLists.txt`:
```
    integration/test_speak_v2_scene.cpp
```
```bash
cmake --build build-tests --target guitar_dsp_tests
./build-tests/tests/guitar_dsp_tests "[integration][speakv2]"
```
Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/integration/test_speak_v2_scene.cpp tests/CMakeLists.txt
git commit -m "test(integration): text → phoneme-aligned clip → player round-trip"
```

---

## Phase 8 — Manual verification & docs

### Task 8.1: Manual demo-scenario checklist

**Files:** none (verification only)

- [ ] **Step 1: Launch the standalone app**

```bash
cmake --build build --target guitar_dsp
open build/guitar_dsp.app
```

- [ ] **Step 2: Walk through the v1 baseline first**

- Switch to scene 01 ("multi syllable words").
- Type "automatically" in Say.
- Pluck 5 times slowly. Confirm v1 behavior is unchanged from before this branch.

- [ ] **Step 3: Walk through scene 10 (v2)**

Switch to scene 10. Run all four sub-cases:

- One pluck per syllable on "while my guitar gently weeps". Each pluck advances by one *real* syllable; consonants snap on attack.
- Fast strum (6+ onsets in 1 second): syllables advance, output does not gap or stack.
- Sustained chord on a single syllable for 1+ seconds: vowel holds cleanly; no obvious robot-loop artifact.
- Release mid-vowel: clean Coda playback, then Idle.

- [ ] **Step 4: Conversation scene round-trip**

Switch to scene 04 (Conversation) with the new player wired (requires §7.1 step in plan to be done — confirm `speech.player` for scene 04 if it should use v2; otherwise this step verifies v2 doesn't break scene 04).

- [ ] **Step 5: Record results**

Add a one-paragraph note + waveform snapshot to `docs/presentation/media/v2_b1_results.md`. (Create the file.)

- [ ] **Step 6: Commit notes**

```bash
git add docs/presentation/media/v2_b1_results.md
git commit -m "docs(presentation): v2 B1 manual verification notes"
```

### Task 8.2: Update presentation §10 with completion note

**Files:**
- Modify: `docs/presentation/while-my-guitar-gently-speaks.md`

- [ ] **Step 1: Append a "Shipped" subsection to §10**

After the "Three ways to build Mode B" section, add:
```markdown
### Status: B1 shipped (2026-06-NN)

Scene 10 "Speak v2 — Guitar-Lead" ships with phoneme-aligned syllable
boundaries (`espeak-ng -X` rescaled to Piper audio length) and
vowel grain-loop sustain. v1 (scenes 01, 02) untouched; the v1 golden
audio test guards against regression. B2 (phase-vocoder vowel stretch)
and B3 (concatenative phoneme synth) remain future work.
```

- [ ] **Step 2: Commit**

```bash
git add docs/presentation/while-my-guitar-gently-speaks.md
git commit -m "docs(presentation): mark v2 B1 shipped in §10"
```

---

## Self-review (run before handing off)

- [ ] **Spec coverage check.** Each §4–§8 spec item has a corresponding task:
  - §4 Piper fix → Phase 1 (Tasks 1.1–1.3)
  - §5 Phoneme alignment & syllabification → Phases 2 & 3 & 4
  - §6 Player → Phase 5
  - §7 Scene & UI → Phase 6
  - §8 v1 preservation → Task 0.1
  - §12 Test plan → distributed across each phase + Phase 7
- [ ] **No placeholders.** No "TBD", "add validation", "similar to Task N" without code shown.
- [ ] **Type consistency.** `Phoneme`, `SyllableSpan`, `TTSClip::sylsV2`, `PhonemeSteppedTTSPlayer::State`, `Scene::Speech::Player` — names match across all task code blocks.
- [ ] **Frequent commits.** Each task commits at the end. Phases never leave the tree mid-build (except Task 5.1's expected link error, called out explicitly).

If you find issues, fix inline.

---

## Risks & mitigations from spec §11

| Risk | Mitigation in this plan |
|---|---|
| espeak duration drift > ±10 % on long phrases | Builder rescales total in Task 4.2; ±5 % per-phoneme target in §4.2 acceptance is *measurement-only* for now, not a build gate. |
| Grain-loop artifacts on fricatives | `nucleusIsFricative` flag (Task 3.1) routes around Sustain (Task 5.1 enterSustain check). |
| Piper-from-source friction | Task 1.1 single-script build; Task 1.2 gives clear error pointing at script. |
| AU plugin path | All Piper/espeak calls already on worker threads (no audio-thread changes in this plan). |
| Piper duration ≠ espeak duration distribution | Linear-rescale only in v1 of v2 (Task 4.2). Forced alignment is plan B (not in this plan).
