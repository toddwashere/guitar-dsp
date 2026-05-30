# Phase 1: Foundation, Audio Passthrough, Test Harness — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a JUCE standalone macOS app that runs a guitar signal through an InputStage (DC block / noise gate / gain) and a Mixer (passthrough wet/dry/master) with full test coverage, a reusable test harness (`RealtimeSentinel`, `SyntheticGuitar`, `GoldenFile`), and CI running on every PR.

**Architecture:** CMake-driven JUCE project with two main targets — the standalone audio app (`guitar_dsp_app`) and a Catch2 test runner (`guitar_dsp_tests`). JUCE is vendored as a git submodule under `external/JUCE`; Catch2 v3 under `external/Catch2`. The app uses JUCE's `juce_add_plugin(... FORMATS Standalone ...)` so AU export remains a free option later. Audio modules are pure C++ classes with no JUCE dependency where possible (easier to unit-test).

**Tech Stack:** C++20, CMake 3.22+, JUCE 7.x or 8.x, Catch2 v3.x, macOS 13+, Apple Silicon primary.

**Reference spec:** [`docs/superpowers/specs/2026-05-29-while-my-guitar-gently-speaks-design.md`](../specs/2026-05-29-while-my-guitar-gently-speaks-design.md)

---

## Background for the implementing engineer

If you're new to this codebase, read these first:

- **JUCE basics**: JUCE is a C++ framework for audio applications. The audio thread runs `AudioProcessor::processBlock(AudioBuffer<float>&, MidiBuffer&)` continuously. You **never** allocate, lock, or do I/O in that method — those will cause audible glitches or hangs.
- **The standalone-plugin pattern**: We use `juce_add_plugin(... FORMATS Standalone)` rather than a custom `JUCEApplication` because it gives us a free standalone app + free future AU/VST3 export from the same source.
- **CMake + JUCE**: JUCE provides CMake helpers (`juce_add_plugin`, `juce_generate_juce_header`, etc.). You'll use these, not the legacy Projucer GUI.
- **Catch2 v3**: Header + library; uses `Catch2::Catch2WithMain` for an auto-`main()` and `catch_discover_tests()` for CTest integration.
- **Audio testing**: We never test against real hardware in CI. We feed known buffers to module methods and assert on the output buffers. The `SyntheticGuitar` harness generates these inputs.

---

## File structure (created across this plan)

```
guitar-dsp/
├── CMakeLists.txt                        (Task 4)
├── README.md                             (modified, Task 20)
├── .github/workflows/ci.yml              (Task 19)
├── .gitignore                            (Task 1)
├── external/
│   ├── JUCE/                             (submodule, Task 2)
│   └── Catch2/                           (submodule, Task 3)
├── src/
│   ├── app/
│   │   └── PluginProcessor.{h,cpp}       (Task 5, refined in Tasks 15/16)
│   ├── audio/
│   │   ├── InputStage.{h,cpp}            (Tasks 11-13)
│   │   ├── Mixer.{h,cpp}                 (Task 14)
│   │   └── AudioGraph.{h,cpp}            (Task 15)
│   └── CMakeLists.txt                    (Task 4)
├── tests/
│   ├── CMakeLists.txt                    (Task 7)
│   ├── unit/
│   │   └── audio/
│   │       ├── test_input_stage.cpp      (Tasks 11-13)
│   │       └── test_mixer.cpp            (Task 14)
│   ├── integration/
│   │   ├── test_passthrough_golden.cpp   (Task 17)
│   │   └── test_realtime_safety.cpp      (Task 18)
│   ├── fixtures/
│   │   ├── inputs/sine_440.wav           (Task 9)
│   │   └── expected/passthrough.wav      (Task 17)
│   └── harness/
│       ├── RealtimeSentinel.{h,cpp}      (Task 8)
│       ├── SyntheticGuitar.{h,cpp}       (Task 9)
│       └── GoldenFile.{h,cpp}            (Task 10)
└── docs/superpowers/
    ├── specs/2026-05-29-while-my-guitar-gently-speaks-design.md
    └── plans/2026-05-29-phase-1-foundation.md
```

---

## Task 1: Repository scaffolding

**Files:**
- Create: `.gitignore`
- Create: `src/` and `tests/` directory placeholders

- [ ] **Step 1: Create `.gitignore`**

Write to `.gitignore`:

```gitignore
# Build artifacts
build/
cmake-build-*/
*.dSYM/

# JUCE
JuceLibraryCode/
Builds/

# macOS
.DS_Store
*.swp

# IDEs
.vscode/
.idea/
*.xcodeproj/
*.xcworkspace/

# Catch2 ignore (we vendor it as submodule)
external/Catch2/build/

# Test artifacts
tests/output/
*.junit.xml

# Python (for tools/tts_prebake later)
__pycache__/
*.pyc
.venv/
venv/
```

- [ ] **Step 2: Create directory structure**

```bash
mkdir -p src/app src/audio tests/unit/audio tests/integration tests/fixtures/inputs tests/fixtures/expected tests/harness external
touch src/app/.gitkeep src/audio/.gitkeep tests/harness/.gitkeep tests/fixtures/inputs/.gitkeep tests/fixtures/expected/.gitkeep
```

- [ ] **Step 3: Commit**

```bash
git add .gitignore src/ tests/
git commit -m "chore: repository scaffolding and gitignore"
```

---

## Task 2: Vendor JUCE as git submodule

**Files:**
- Create: `external/JUCE/` (submodule)
- Modify: `.gitmodules`

- [ ] **Step 1: Add JUCE submodule**

```bash
git submodule add https://github.com/juce-framework/JUCE.git external/JUCE
cd external/JUCE && git checkout 8.0.4 && cd ../..
```

(8.0.4 is a known-good tagged release as of writing. Use the latest 8.x stable when running this.)

- [ ] **Step 2: Verify**

```bash
ls external/JUCE/CMakeLists.txt
```

Expected: file exists.

- [ ] **Step 3: Commit**

```bash
git add .gitmodules external/JUCE
git commit -m "chore: vendor JUCE 8.0.4 as git submodule"
```

---

## Task 3: Vendor Catch2 v3 as git submodule

**Files:**
- Create: `external/Catch2/` (submodule)
- Modify: `.gitmodules`

- [ ] **Step 1: Add Catch2 submodule**

```bash
git submodule add https://github.com/catchorg/Catch2.git external/Catch2
cd external/Catch2 && git checkout v3.7.1 && cd ../..
```

- [ ] **Step 2: Verify**

```bash
ls external/Catch2/CMakeLists.txt
```

Expected: file exists.

- [ ] **Step 3: Commit**

```bash
git add .gitmodules external/Catch2
git commit -m "chore: vendor Catch2 v3.7.1 as git submodule"
```

---

## Task 4: Top-level CMakeLists.txt

**Files:**
- Create: `CMakeLists.txt` (root)
- Create: `src/CMakeLists.txt`

- [ ] **Step 1: Write root CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.22)
project(guitar_dsp VERSION 0.1.0 LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# macOS deployment target — must support AVSpeechSynthesizer neural voices
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "macOS deployment target")

# Build the universal2 binary on macOS for portability
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()

# Optional sanitizers for debug builds
option(GUITAR_DSP_ASAN "Build with AddressSanitizer" OFF)
option(GUITAR_DSP_UBSAN "Build with UndefinedBehaviorSanitizer" OFF)

if(GUITAR_DSP_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

if(GUITAR_DSP_UBSAN)
    add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=undefined)
endif()

# Pull in JUCE
add_subdirectory(external/JUCE)

# Pull in Catch2
add_subdirectory(external/Catch2)

# Application source tree
add_subdirectory(src)

# Test tree
enable_testing()
add_subdirectory(tests)
```

- [ ] **Step 2: Write src/CMakeLists.txt (skeleton — fleshed out in later tasks)**

```cmake
# Audio module library — pure C++, no JUCE dependency.
# Modules added here as they are implemented in later tasks.
add_library(guitar_dsp_audio STATIC)

target_include_directories(guitar_dsp_audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(guitar_dsp_audio PUBLIC cxx_std_20)

# Standalone application (declared in Task 5)
# The app target is added by app/CMakeLists.txt when it exists.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/CMakeLists.txt")
    add_subdirectory(app)
endif()
```

- [ ] **Step 3: Verify CMake configures**

```bash
cmake -B build -S . -G Ninja
```

Expected: configures successfully (may take a while as JUCE/Catch2 configure). No errors.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt src/CMakeLists.txt
git commit -m "build: root CMakeLists.txt with JUCE and Catch2 wiring"
```

---

## Task 5: Empty JUCE standalone app target

**Files:**
- Create: `src/app/CMakeLists.txt`
- Create: `src/app/PluginProcessor.h`
- Create: `src/app/PluginProcessor.cpp`
- Create: `src/app/PluginEditor.h`
- Create: `src/app/PluginEditor.cpp`

- [ ] **Step 1: Write `src/app/CMakeLists.txt`**

```cmake
juce_add_plugin(guitar_dsp_app
    PRODUCT_NAME "Guitar DSP"
    COMPANY_NAME "GuitarDSP"
    BUNDLE_ID com.guitardsp.app
    PLUGIN_MANUFACTURER_CODE GtDs
    PLUGIN_CODE GtAp
    FORMATS Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(guitar_dsp_app)

target_sources(guitar_dsp_app PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp
)

target_compile_definitions(guitar_dsp_app PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(guitar_dsp_app
    PRIVATE
        guitar_dsp_audio
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
```

- [ ] **Step 2: Write `src/app/PluginProcessor.h`**

```cpp
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace guitar_dsp {

class PluginProcessor : public juce::AudioProcessor {
public:
    PluginProcessor();
    ~PluginProcessor() override = default;

    // AudioProcessor overrides
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Guitar DSP"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
};

} // namespace guitar_dsp
```

- [ ] **Step 3: Write `src/app/PluginProcessor.cpp`**

```cpp
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace guitar_dsp {

PluginProcessor::PluginProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::mono(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

void PluginProcessor::prepareToPlay(double, int) {
    // No-op for now; real prepare logic arrives with AudioGraph in Task 15.
}

void PluginProcessor::releaseResources() {}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto inputs = layouts.getMainInputChannelSet();
    const auto outputs = layouts.getMainOutputChannelSet();
    if (inputs.isDisabled() || outputs.isDisabled())
        return false;
    return inputs == juce::AudioChannelSet::mono()
        && (outputs == juce::AudioChannelSet::mono()
            || outputs == juce::AudioChannelSet::stereo());
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    // Trivial mono->stereo passthrough; replaced by AudioGraph in Task 15.
    const auto numSamples = buffer.getNumSamples();
    const auto totalOut = getTotalNumOutputChannels();
    const auto totalIn = getTotalNumInputChannels();

    if (totalIn == 1 && totalOut == 2) {
        const float* in = buffer.getReadPointer(0);
        float* outL = buffer.getWritePointer(0);
        float* outR = buffer.getWritePointer(1);
        for (int i = 0; i < numSamples; ++i) {
            const float s = in[i];
            outL[i] = s;
            outR[i] = s;
        }
    }
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() {
    return new PluginEditor(*this);
}

} // namespace guitar_dsp

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new guitar_dsp::PluginProcessor();
}
```

- [ ] **Step 4: Write `src/app/PluginEditor.h`**

```cpp
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

namespace guitar_dsp {

class PluginEditor : public juce::AudioProcessorEditor {
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    PluginProcessor& processor_;
};

} // namespace guitar_dsp
```

- [ ] **Step 5: Write `src/app/PluginEditor.cpp`**

```cpp
#include "PluginEditor.h"

namespace guitar_dsp {

PluginEditor::PluginEditor(PluginProcessor& p)
    : juce::AudioProcessorEditor(&p), processor_(p) {
    setSize(800, 480);
    setResizable(true, true);
}

void PluginEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(20, 20, 28));
    g.setColour(juce::Colours::white);
    g.setFont(28.0f);
    g.drawFittedText("Guitar DSP — passthrough",
                     getLocalBounds(),
                     juce::Justification::centred,
                     1);
}

void PluginEditor::resized() {}

} // namespace guitar_dsp
```

- [ ] **Step 6: Build and run**

```bash
cmake --build build --target guitar_dsp_app_Standalone
```

Expected: builds without error. Binary at `build/src/app/guitar_dsp_app_artefacts/Standalone/Guitar DSP.app`.

```bash
open "build/src/app/guitar_dsp_app_artefacts/Standalone/Guitar DSP.app"
```

Expected: a dark window with "Guitar DSP — passthrough" centered. Audio menu → Audio/MIDI Settings should show your audio interface. Plugging a guitar in should pass through to the output (no processing yet, but the audio path works).

- [ ] **Step 7: Commit**

```bash
git add src/app/
git commit -m "feat(app): empty JUCE standalone app skeleton with passthrough"
```

---

## Task 6: PluginProcessor smoke test target (Catch2 hello world)

**Files:**
- Create: `tests/CMakeLists.txt`
- Create: `tests/unit/test_smoke.cpp`

- [ ] **Step 1: Write `tests/CMakeLists.txt`**

```cmake
add_executable(guitar_dsp_tests
    unit/test_smoke.cpp
)

target_link_libraries(guitar_dsp_tests PRIVATE
    guitar_dsp_audio
    Catch2::Catch2WithMain
)

target_include_directories(guitar_dsp_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

include(${CMAKE_SOURCE_DIR}/external/Catch2/extras/Catch.cmake)
catch_discover_tests(guitar_dsp_tests)
```

- [ ] **Step 2: Write `tests/unit/test_smoke.cpp`**

```cpp
#include <catch2/catch_test_macros.hpp>

TEST_CASE("smoke: arithmetic works", "[smoke]") {
    REQUIRE(2 + 2 == 4);
}
```

- [ ] **Step 3: Build and run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure
```

Expected: 1 test passes.

- [ ] **Step 4: Commit**

```bash
git add tests/CMakeLists.txt tests/unit/test_smoke.cpp
git commit -m "test: Catch2 test runner with smoke test"
```

---

## Task 7: RealtimeSentinel harness

**Files:**
- Create: `tests/harness/RealtimeSentinel.h`
- Create: `tests/harness/RealtimeSentinel.cpp`
- Create: `tests/unit/test_realtime_sentinel.cpp`
- Modify: `tests/CMakeLists.txt`

The `RealtimeSentinel` lets a test mark a thread as "the audio thread." If that thread allocates with `new`/`malloc` or acquires a `pthread_mutex_lock`, the sentinel records a violation. Tests then assert the violation count is zero.

We implement detection via thread-local flags plus operator overloading — we don't intercept system-wide `malloc` (too invasive); instead, we override `operator new` and `operator delete` at link-scope for test binaries only.

- [ ] **Step 1: Write the failing test first**

Create `tests/unit/test_realtime_sentinel.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "harness/RealtimeSentinel.h"

#include <thread>
#include <vector>

using guitar_dsp::tests::RealtimeSentinel;

TEST_CASE("sentinel: zero violations when no allocation occurs", "[harness]") {
    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();

    int x = 0;
    for (int i = 0; i < 1000; ++i) x += i;  // stack-only

    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}

TEST_CASE("sentinel: detects heap allocation on realtime thread", "[harness]") {
    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();

    {
        // Force a heap allocation in the marked region.
        auto* v = new std::vector<int>(16);
        delete v;
    }

    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() > 0);
}

TEST_CASE("sentinel: allocations on non-realtime thread do not count", "[harness]") {
    RealtimeSentinel sentinel;

    {
        auto* v = new std::vector<int>(16);
        delete v;
    }

    REQUIRE(sentinel.violations() == 0);
}
```

- [ ] **Step 2: Run to confirm it fails**

```bash
cmake --build build --target guitar_dsp_tests 2>&1 | head -20
```

Expected: compile error — `RealtimeSentinel` doesn't exist yet.

- [ ] **Step 3: Write `tests/harness/RealtimeSentinel.h`**

```cpp
#pragma once

#include <atomic>

namespace guitar_dsp::tests {

// Test-time sentinel for detecting heap allocations on the audio thread.
// Mark the current thread as realtime before exercising audio code; if any
// `operator new` / `operator delete` runs on that thread, the violation
// counter increments. Assert it stays at zero.
//
// Detection is process-wide because operator new is overridden globally in
// the test binary. Multiple RealtimeSentinel instances share state via the
// `violationCount()` static accessor.
class RealtimeSentinel {
public:
    RealtimeSentinel();
    ~RealtimeSentinel();

    void markCurrentThreadAsRealtime();
    void unmarkCurrentThreadAsRealtime();

    // Snapshot of violations recorded since this sentinel was constructed.
    std::size_t violations() const;

    // Process-wide hook used by overridden operator new/delete. Returns true
    // if the calling thread is currently marked as realtime.
    static bool isCurrentThreadRealtime();
    static void recordViolation();

private:
    std::size_t baseline_;
};

} // namespace guitar_dsp::tests
```

- [ ] **Step 4: Write `tests/harness/RealtimeSentinel.cpp`**

```cpp
#include "RealtimeSentinel.h"

#include <atomic>
#include <new>

namespace guitar_dsp::tests {

namespace {
    thread_local bool g_isRealtime = false;
    std::atomic<std::size_t> g_violationCount{0};
}

RealtimeSentinel::RealtimeSentinel()
    : baseline_(g_violationCount.load(std::memory_order_relaxed)) {}

RealtimeSentinel::~RealtimeSentinel() {
    // Defensive: ensure we leave no dangling realtime marker.
    g_isRealtime = false;
}

void RealtimeSentinel::markCurrentThreadAsRealtime() { g_isRealtime = true; }
void RealtimeSentinel::unmarkCurrentThreadAsRealtime() { g_isRealtime = false; }

std::size_t RealtimeSentinel::violations() const {
    return g_violationCount.load(std::memory_order_relaxed) - baseline_;
}

bool RealtimeSentinel::isCurrentThreadRealtime() { return g_isRealtime; }
void RealtimeSentinel::recordViolation() {
    g_violationCount.fetch_add(1, std::memory_order_relaxed);
}

} // namespace guitar_dsp::tests

// Global operator new/delete overrides — applied to the test binary only.
// We can't throw from `operator delete`, so we just record and return.

void* operator new(std::size_t size) {
    if (guitar_dsp::tests::RealtimeSentinel::isCurrentThreadRealtime()) {
        guitar_dsp::tests::RealtimeSentinel::recordViolation();
    }
    void* p = std::malloc(size);
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void operator delete(void* p) noexcept {
    if (guitar_dsp::tests::RealtimeSentinel::isCurrentThreadRealtime()) {
        guitar_dsp::tests::RealtimeSentinel::recordViolation();
    }
    std::free(p);
}

void operator delete[](void* p) noexcept {
    ::operator delete(p);
}

void operator delete(void* p, std::size_t) noexcept { ::operator delete(p); }
void operator delete[](void* p, std::size_t) noexcept { ::operator delete[](p); }
```

- [ ] **Step 5: Add harness to `tests/CMakeLists.txt`**

Replace the `add_executable` call in `tests/CMakeLists.txt`:

```cmake
add_executable(guitar_dsp_tests
    unit/test_smoke.cpp
    unit/test_realtime_sentinel.cpp
    harness/RealtimeSentinel.cpp
)
```

- [ ] **Step 6: Build and run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R sentinel
```

Expected: 3 sentinel tests pass.

- [ ] **Step 7: Commit**

```bash
git add tests/harness/RealtimeSentinel.{h,cpp} tests/unit/test_realtime_sentinel.cpp tests/CMakeLists.txt
git commit -m "test: RealtimeSentinel harness for audio-thread allocation detection"
```

---

## Task 8: SyntheticGuitar harness

**Files:**
- Create: `tests/harness/SyntheticGuitar.h`
- Create: `tests/harness/SyntheticGuitar.cpp`
- Create: `tests/unit/test_synthetic_guitar.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/test_synthetic_guitar.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "harness/SyntheticGuitar.h"

#include <vector>
#include <cmath>

using Catch::Matchers::WithinAbs;
using guitar_dsp::tests::SyntheticGuitar;

TEST_CASE("synthetic guitar: sine has expected peak", "[harness]") {
    SyntheticGuitar gen{48000.0};
    std::vector<float> buf(4800);
    gen.sine(440.0f, 0.5f, buf.data(), buf.size());

    float peak = 0.0f;
    for (float s : buf) peak = std::max(peak, std::abs(s));
    REQUIRE_THAT(peak, WithinAbs(0.5f, 1e-3f));
}

TEST_CASE("synthetic guitar: silence is exactly zero", "[harness]") {
    SyntheticGuitar gen{48000.0};
    std::vector<float> buf(1024, 0.123f);
    gen.silence(buf.data(), buf.size());
    for (float s : buf) REQUIRE(s == 0.0f);
}

TEST_CASE("synthetic guitar: sweep starts at f0 and ends near f1", "[harness]") {
    SyntheticGuitar gen{48000.0};
    std::vector<float> buf(48000);
    gen.sweep(100.0f, 1000.0f, 1.0f, buf.data(), buf.size());
    // Just check it's non-silent and bounded.
    float peak = 0.0f;
    for (float s : buf) peak = std::max(peak, std::abs(s));
    REQUIRE(peak > 0.5f);
    REQUIRE(peak <= 1.0f);
}
```

- [ ] **Step 2: Run to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests 2>&1 | head -10
```

Expected: compile error.

- [ ] **Step 3: Write `tests/harness/SyntheticGuitar.h`**

```cpp
#pragma once

#include <cstddef>

namespace guitar_dsp::tests {

// Deterministic test-signal generators used by unit and integration tests.
// All methods write `numSamples` mono samples into the provided buffer.
class SyntheticGuitar {
public:
    explicit SyntheticGuitar(double sampleRate);

    void silence(float* buffer, std::size_t numSamples);
    void dc(float value, float* buffer, std::size_t numSamples);
    void sine(float frequencyHz, float amplitude,
              float* buffer, std::size_t numSamples);
    void sweep(float startHz, float endHz, float amplitude,
               float* buffer, std::size_t numSamples);

    // Karplus-Strong plucked-string for golden-file tests.
    void pluck(float frequencyHz, float decaySeconds, float amplitude,
               float* buffer, std::size_t numSamples);

private:
    double sampleRate_;
    double phase_ = 0.0;
};

} // namespace guitar_dsp::tests
```

- [ ] **Step 4: Write `tests/harness/SyntheticGuitar.cpp`**

```cpp
#include "SyntheticGuitar.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace guitar_dsp::tests {

namespace {
    constexpr double kTwoPi = 6.28318530717958647692;
}

SyntheticGuitar::SyntheticGuitar(double sampleRate) : sampleRate_(sampleRate) {}

void SyntheticGuitar::silence(float* buffer, std::size_t numSamples) {
    std::memset(buffer, 0, numSamples * sizeof(float));
}

void SyntheticGuitar::dc(float value, float* buffer, std::size_t numSamples) {
    for (std::size_t i = 0; i < numSamples; ++i) buffer[i] = value;
}

void SyntheticGuitar::sine(float frequencyHz, float amplitude,
                           float* buffer, std::size_t numSamples) {
    const double inc = kTwoPi * frequencyHz / sampleRate_;
    for (std::size_t i = 0; i < numSamples; ++i) {
        buffer[i] = static_cast<float>(amplitude * std::sin(phase_));
        phase_ += inc;
        if (phase_ > kTwoPi) phase_ -= kTwoPi;
    }
}

void SyntheticGuitar::sweep(float startHz, float endHz, float amplitude,
                            float* buffer, std::size_t numSamples) {
    // Linear frequency sweep.
    double localPhase = 0.0;
    for (std::size_t i = 0; i < numSamples; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(numSamples);
        const double freq = startHz + (endHz - startHz) * t;
        localPhase += kTwoPi * freq / sampleRate_;
        if (localPhase > kTwoPi) localPhase -= kTwoPi;
        buffer[i] = static_cast<float>(amplitude * std::sin(localPhase));
    }
}

void SyntheticGuitar::pluck(float frequencyHz, float decaySeconds, float amplitude,
                            float* buffer, std::size_t numSamples) {
    // Karplus-Strong with simple 2-tap lowpass feedback.
    const int delayLen = std::max(2, static_cast<int>(sampleRate_ / frequencyHz));
    std::vector<float> line(static_cast<std::size_t>(delayLen));
    // Initial noise burst.
    for (auto& s : line) s = amplitude * (2.0f * (std::rand() / float(RAND_MAX)) - 1.0f);

    const float decayFactor = static_cast<float>(
        std::pow(0.5, 1.0 / (decaySeconds * sampleRate_ / delayLen)));

    std::size_t idx = 0;
    for (std::size_t i = 0; i < numSamples; ++i) {
        const float out = line[idx];
        const std::size_t nextIdx = (idx + 1) % line.size();
        const float averaged = 0.5f * (line[idx] + line[nextIdx]) * decayFactor;
        line[idx] = averaged;
        idx = nextIdx;
        buffer[i] = out;
    }
}

} // namespace guitar_dsp::tests
```

- [ ] **Step 5: Update `tests/CMakeLists.txt`**

Add the new files to `add_executable`:

```cmake
add_executable(guitar_dsp_tests
    unit/test_smoke.cpp
    unit/test_realtime_sentinel.cpp
    unit/test_synthetic_guitar.cpp
    harness/RealtimeSentinel.cpp
    harness/SyntheticGuitar.cpp
)
```

- [ ] **Step 6: Build and run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R "synthetic guitar"
```

Expected: 3 tests pass.

- [ ] **Step 7: Commit**

```bash
git add tests/harness/SyntheticGuitar.{h,cpp} tests/unit/test_synthetic_guitar.cpp tests/CMakeLists.txt
git commit -m "test: SyntheticGuitar input generators for DSP tests"
```

---

## Task 9: Generate a fixed input WAV fixture

**Files:**
- Create: `tests/fixtures/inputs/sine_440.wav` (binary, generated by a script)
- Create: `tools/fixtures/generate_sine_440.cpp`
- Modify: `tests/CMakeLists.txt`

We generate fixtures from source rather than checking in opaque binaries blind. A small CLI tool produces the WAV; the WAV is committed for stability.

- [ ] **Step 1: Write the generator**

Create `tools/fixtures/CMakeLists.txt`:

```cmake
add_executable(generate_sine_440 generate_sine_440.cpp)
target_link_libraries(generate_sine_440 PRIVATE juce::juce_audio_formats)
target_compile_features(generate_sine_440 PRIVATE cxx_std_20)
```

Add to root `CMakeLists.txt` near the bottom (before `add_subdirectory(tests)`):

```cmake
add_subdirectory(tools/fixtures)
```

Create `tools/fixtures/generate_sine_440.cpp`:

```cpp
#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: generate_sine_440 <output.wav>\n";
        return 1;
    }

    constexpr double sr = 48000.0;
    constexpr double freq = 440.0;
    constexpr double durSec = 2.0;
    const int numSamples = static_cast<int>(sr * durSec);

    juce::AudioBuffer<float> buffer(1, numSamples);
    float* data = buffer.getWritePointer(0);
    const double inc = 2.0 * 3.14159265358979323846 * freq / sr;
    double phase = 0.0;
    for (int i = 0; i < numSamples; ++i) {
        data[i] = static_cast<float>(0.5 * std::sin(phase));
        phase += inc;
    }

    juce::File outFile(argv[1]);
    outFile.deleteFile();
    juce::WavAudioFormat format;
    auto stream = std::unique_ptr<juce::FileOutputStream>(outFile.createOutputStream().release());
    if (!stream) {
        std::cerr << "Failed to open output file\n";
        return 1;
    }
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        format.createWriterFor(stream.get(), sr, 1, 24, {}, 0));
    if (!writer) {
        std::cerr << "Failed to create writer\n";
        return 1;
    }
    stream.release();  // writer takes ownership
    writer->writeFromAudioSampleBuffer(buffer, 0, numSamples);
    return 0;
}
```

- [ ] **Step 2: Build and run the generator**

```bash
cmake --build build --target generate_sine_440
./build/tools/fixtures/generate_sine_440 tests/fixtures/inputs/sine_440.wav
```

Expected: `tests/fixtures/inputs/sine_440.wav` exists, ~280 KB.

- [ ] **Step 3: Verify with file**

```bash
file tests/fixtures/inputs/sine_440.wav
```

Expected: `RIFF (little-endian) data, WAVE audio, Microsoft PCM, 24 bit, mono 48000 Hz`.

- [ ] **Step 4: Commit**

```bash
git add tools/fixtures/ tests/fixtures/inputs/sine_440.wav CMakeLists.txt
git commit -m "test: fixture generator and sine_440 input WAV"
```

---

## Task 10: GoldenFile harness

**Files:**
- Create: `tests/harness/GoldenFile.h`
- Create: `tests/harness/GoldenFile.cpp`
- Create: `tests/unit/test_golden_file.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/test_golden_file.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "harness/GoldenFile.h"

#include <filesystem>

using guitar_dsp::tests::GoldenFile;

TEST_CASE("golden: identical buffers match bit-exact", "[harness]") {
    std::vector<float> a(1024, 0.5f);
    std::vector<float> b(1024, 0.5f);
    auto diff = GoldenFile::compareBuffers(a.data(), b.data(), a.size());
    REQUIRE(diff.maxAbsDiff == 0.0f);
    REQUIRE(diff.numDifferingSamples == 0);
}

TEST_CASE("golden: differing buffers report diff", "[harness]") {
    std::vector<float> a(8, 0.0f);
    std::vector<float> b(8, 0.0f);
    b[3] = 0.25f;
    auto diff = GoldenFile::compareBuffers(a.data(), b.data(), a.size());
    REQUIRE(diff.maxAbsDiff == 0.25f);
    REQUIRE(diff.numDifferingSamples == 1);
}

TEST_CASE("golden: round-trip write-then-read preserves data", "[harness]") {
    const auto path = std::filesystem::temp_directory_path() / "golden_roundtrip.wav";
    std::vector<float> data(2048);
    for (size_t i = 0; i < data.size(); ++i) data[i] = std::sin(0.01f * i);

    GoldenFile::writeMonoWav(path.string(), 48000.0, data.data(), data.size());
    auto readBack = GoldenFile::readMonoWav(path.string());

    REQUIRE(readBack.sampleRate == 48000.0);
    REQUIRE(readBack.samples.size() == data.size());
    auto diff = GoldenFile::compareBuffers(data.data(), readBack.samples.data(), data.size());
    // 24-bit WAV → tolerance ~1/2^23.
    REQUIRE(diff.maxAbsDiff < 1.5e-7f);
}
```

- [ ] **Step 2: Run to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests 2>&1 | head -10
```

Expected: compile error.

- [ ] **Step 3: Write `tests/harness/GoldenFile.h`**

```cpp
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace guitar_dsp::tests {

class GoldenFile {
public:
    struct WavData {
        double sampleRate = 0.0;
        std::vector<float> samples;
    };

    struct DiffResult {
        float maxAbsDiff = 0.0f;
        std::size_t numDifferingSamples = 0;
        std::size_t firstDifferingIndex = 0;
    };

    // Writes a 24-bit mono WAV at the given sample rate.
    static void writeMonoWav(const std::string& path,
                             double sampleRate,
                             const float* samples,
                             std::size_t numSamples);

    // Reads a mono WAV (any bit depth). Throws std::runtime_error on failure.
    static WavData readMonoWav(const std::string& path);

    // Element-wise compare two buffers. `tolerance` is absolute; pass 0 for
    // bit-exact comparison.
    static DiffResult compareBuffers(const float* a,
                                     const float* b,
                                     std::size_t numSamples,
                                     float tolerance = 0.0f);

    // Render a buffer + compare against a golden WAV on disk. If env var
    // `GUITAR_DSP_REGENERATE_GOLDENS=1` is set, writes the new buffer as the
    // golden instead of comparing.
    static DiffResult assertMatchesGolden(const std::string& goldenPath,
                                          double sampleRate,
                                          const float* samples,
                                          std::size_t numSamples,
                                          float tolerance = 0.0f);
};

} // namespace guitar_dsp::tests
```

- [ ] **Step 4: Write `tests/harness/GoldenFile.cpp`**

```cpp
#include "GoldenFile.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace guitar_dsp::tests {

void GoldenFile::writeMonoWav(const std::string& path,
                              double sampleRate,
                              const float* samples,
                              std::size_t numSamples) {
    juce::File outFile(path);
    outFile.deleteFile();
    juce::WavAudioFormat format;
    auto stream = std::unique_ptr<juce::FileOutputStream>(outFile.createOutputStream().release());
    if (!stream) throw std::runtime_error("GoldenFile: cannot open " + path);

    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        format.createWriterFor(stream.get(), sampleRate, 1, 24, {}, 0));
    if (!writer) throw std::runtime_error("GoldenFile: cannot create writer for " + path);
    stream.release();

    juce::AudioBuffer<float> buffer(1, static_cast<int>(numSamples));
    std::memcpy(buffer.getWritePointer(0), samples, numSamples * sizeof(float));
    if (!writer->writeFromAudioSampleBuffer(buffer, 0, static_cast<int>(numSamples)))
        throw std::runtime_error("GoldenFile: write failed for " + path);
}

GoldenFile::WavData GoldenFile::readMonoWav(const std::string& path) {
    juce::File inFile(path);
    if (!inFile.existsAsFile()) throw std::runtime_error("GoldenFile: missing " + path);

    juce::WavAudioFormat format;
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        format.createReaderFor(inFile.createInputStream().release(), true));
    if (!reader) throw std::runtime_error("GoldenFile: cannot read " + path);

    WavData out;
    out.sampleRate = reader->sampleRate;
    out.samples.resize(static_cast<std::size_t>(reader->lengthInSamples));

    juce::AudioBuffer<float> buffer(1, static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, false);
    std::memcpy(out.samples.data(), buffer.getReadPointer(0), out.samples.size() * sizeof(float));
    return out;
}

GoldenFile::DiffResult GoldenFile::compareBuffers(const float* a,
                                                  const float* b,
                                                  std::size_t numSamples,
                                                  float tolerance) {
    DiffResult result;
    bool foundFirst = false;
    for (std::size_t i = 0; i < numSamples; ++i) {
        const float d = std::abs(a[i] - b[i]);
        if (d > tolerance) {
            if (!foundFirst) { result.firstDifferingIndex = i; foundFirst = true; }
            ++result.numDifferingSamples;
            if (d > result.maxAbsDiff) result.maxAbsDiff = d;
        }
    }
    return result;
}

GoldenFile::DiffResult GoldenFile::assertMatchesGolden(const std::string& goldenPath,
                                                      double sampleRate,
                                                      const float* samples,
                                                      std::size_t numSamples,
                                                      float tolerance) {
    const char* regen = std::getenv("GUITAR_DSP_REGENERATE_GOLDENS");
    if (regen && std::string(regen) == "1") {
        writeMonoWav(goldenPath, sampleRate, samples, numSamples);
        return DiffResult{};
    }
    auto golden = readMonoWav(goldenPath);
    if (golden.samples.size() != numSamples)
        throw std::runtime_error("GoldenFile: length mismatch for " + goldenPath);
    return compareBuffers(golden.samples.data(), samples, numSamples, tolerance);
}

} // namespace guitar_dsp::tests
```

- [ ] **Step 5: Update `tests/CMakeLists.txt`**

```cmake
add_executable(guitar_dsp_tests
    unit/test_smoke.cpp
    unit/test_realtime_sentinel.cpp
    unit/test_synthetic_guitar.cpp
    unit/test_golden_file.cpp
    harness/RealtimeSentinel.cpp
    harness/SyntheticGuitar.cpp
    harness/GoldenFile.cpp
)

target_link_libraries(guitar_dsp_tests PRIVATE
    guitar_dsp_audio
    juce::juce_audio_formats
    Catch2::Catch2WithMain
)
```

- [ ] **Step 6: Build and run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R golden
```

Expected: 3 tests pass.

- [ ] **Step 7: Commit**

```bash
git add tests/harness/GoldenFile.{h,cpp} tests/unit/test_golden_file.cpp tests/CMakeLists.txt
git commit -m "test: GoldenFile harness for WAV diff + regeneration"
```

---

## Task 11: InputStage — DC blocker

**Files:**
- Create: `src/audio/InputStage.h`
- Create: `src/audio/InputStage.cpp`
- Create: `tests/unit/audio/test_input_stage.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/audio/test_input_stage.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "audio/InputStage.h"
#include "harness/SyntheticGuitar.h"

#include <vector>

using Catch::Matchers::WithinAbs;
using guitar_dsp::audio::InputStage;
using guitar_dsp::tests::SyntheticGuitar;

TEST_CASE("InputStage: DC offset is removed", "[audio][input_stage]") {
    InputStage stage;
    stage.prepare(48000.0, 1024);
    stage.setNoiseGateThreshold(-200.0f);  // disable gate
    stage.setInputGainDb(0.0f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> buf(48000);
    gen.dc(0.5f, buf.data(), buf.size());

    stage.process(buf.data(), buf.data(), buf.size());

    // After settling, the DC offset should be near zero.
    float meanLast = 0.0f;
    for (size_t i = buf.size() - 1024; i < buf.size(); ++i) meanLast += buf[i];
    meanLast /= 1024.0f;
    REQUIRE_THAT(meanLast, WithinAbs(0.0f, 1e-3f));
}

TEST_CASE("InputStage: AC signal preserved through DC blocker", "[audio][input_stage]") {
    InputStage stage;
    stage.prepare(48000.0, 1024);
    stage.setNoiseGateThreshold(-200.0f);
    stage.setInputGainDb(0.0f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000), out(48000);
    gen.sine(440.0f, 0.5f, in.data(), in.size());

    std::copy(in.begin(), in.end(), out.begin());
    stage.process(out.data(), out.data(), out.size());

    // Peak after settling should still be approximately 0.5.
    float peak = 0.0f;
    for (size_t i = out.size() / 2; i < out.size(); ++i) peak = std::max(peak, std::abs(out[i]));
    REQUIRE_THAT(peak, WithinAbs(0.5f, 0.05f));
}
```

- [ ] **Step 2: Run to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests 2>&1 | head -10
```

Expected: `InputStage.h not found`.

- [ ] **Step 3: Write `src/audio/InputStage.h`**

```cpp
#pragma once

#include <cstddef>

namespace guitar_dsp::audio {

// Front-of-chain processing for the live guitar signal.
// Provides DC removal (essential for any AC-coupled input), a noise gate
// (suppresses background hum / amp hiss during silence), and input gain
// trim. Operates in-place on mono float buffers.
class InputStage {
public:
    InputStage();
    ~InputStage();

    void prepare(double sampleRate, int blockSize);
    void reset();

    // Process numSamples in/out. `in` and `out` may alias.
    void process(const float* in, float* out, std::size_t numSamples);

    // Threshold in dBFS below which the gate closes. Set very low (e.g.
    // -200) to effectively disable the gate.
    void setNoiseGateThreshold(float thresholdDb);
    void setInputGainDb(float gainDb);

private:
    double sampleRate_ = 48000.0;

    // DC blocker (one-pole high-pass at ~10 Hz).
    float dcBlockerXPrev_ = 0.0f;
    float dcBlockerYPrev_ = 0.0f;
    float dcBlockerR_ = 0.0f;

    // Noise gate (peak envelope follower + soft gain control).
    float gateThresholdLin_ = 0.0f;
    float gateEnvelope_ = 0.0f;
    float gateAttackCoef_ = 0.0f;
    float gateReleaseCoef_ = 0.0f;
    float gateCurrentGain_ = 1.0f;

    // Input gain.
    float inputGainLin_ = 1.0f;

    void recomputeCoefficients();
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 4: Write `src/audio/InputStage.cpp`**

```cpp
#include "InputStage.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

namespace {
    constexpr float kEpsilon = 1.0e-9f;

    float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }
}

InputStage::InputStage() {
    setNoiseGateThreshold(-60.0f);
    setInputGainDb(0.0f);
}

InputStage::~InputStage() = default;

void InputStage::prepare(double sampleRate, int /*blockSize*/) {
    sampleRate_ = sampleRate;
    recomputeCoefficients();
    reset();
}

void InputStage::reset() {
    dcBlockerXPrev_ = 0.0f;
    dcBlockerYPrev_ = 0.0f;
    gateEnvelope_ = 0.0f;
    gateCurrentGain_ = 1.0f;
}

void InputStage::recomputeCoefficients() {
    // DC blocker: y[n] = x[n] - x[n-1] + R*y[n-1]; R ≈ 1 - (2π*fc/fs).
    // fc = 10 Hz.
    constexpr float fc = 10.0f;
    dcBlockerR_ = 1.0f - (6.28318530717958647692f * fc /
                          static_cast<float>(sampleRate_));

    // Gate: 5 ms attack, 80 ms release.
    const float attackTime = 0.005f;
    const float releaseTime = 0.080f;
    gateAttackCoef_ = std::exp(-1.0f / (attackTime * static_cast<float>(sampleRate_)));
    gateReleaseCoef_ = std::exp(-1.0f / (releaseTime * static_cast<float>(sampleRate_)));
}

void InputStage::setNoiseGateThreshold(float thresholdDb) {
    gateThresholdLin_ = dbToLinear(thresholdDb);
}

void InputStage::setInputGainDb(float gainDb) {
    inputGainLin_ = dbToLinear(gainDb);
}

void InputStage::process(const float* in, float* out, std::size_t numSamples) {
    for (std::size_t i = 0; i < numSamples; ++i) {
        // 1. Input gain.
        float x = in[i] * inputGainLin_;

        // 2. DC blocker.
        const float y = x - dcBlockerXPrev_ + dcBlockerR_ * dcBlockerYPrev_;
        dcBlockerXPrev_ = x;
        dcBlockerYPrev_ = y;
        x = y;

        // 3. Noise gate (peak envelope follower, hard threshold for now).
        const float absX = std::abs(x);
        const float coef = (absX > gateEnvelope_) ? gateAttackCoef_ : gateReleaseCoef_;
        gateEnvelope_ = coef * gateEnvelope_ + (1.0f - coef) * absX;

        const float targetGain = (gateEnvelope_ > gateThresholdLin_) ? 1.0f : 0.0f;
        gateCurrentGain_ = gateCurrentGain_ * 0.99f + targetGain * 0.01f;

        out[i] = x * gateCurrentGain_;
    }
    // Denormal guard.
    if (std::abs(dcBlockerYPrev_) < kEpsilon) dcBlockerYPrev_ = 0.0f;
    if (gateEnvelope_ < kEpsilon) gateEnvelope_ = 0.0f;
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 5: Update `src/CMakeLists.txt`**

```cmake
add_library(guitar_dsp_audio STATIC
    audio/InputStage.cpp
)

target_include_directories(guitar_dsp_audio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(guitar_dsp_audio PUBLIC cxx_std_20)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/CMakeLists.txt")
    add_subdirectory(app)
endif()
```

- [ ] **Step 6: Update `tests/CMakeLists.txt` to include the new test**

```cmake
add_executable(guitar_dsp_tests
    unit/test_smoke.cpp
    unit/test_realtime_sentinel.cpp
    unit/test_synthetic_guitar.cpp
    unit/test_golden_file.cpp
    unit/audio/test_input_stage.cpp
    harness/RealtimeSentinel.cpp
    harness/SyntheticGuitar.cpp
    harness/GoldenFile.cpp
)
```

- [ ] **Step 7: Build and run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R InputStage
```

Expected: 2 tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/audio/InputStage.{h,cpp} src/CMakeLists.txt tests/unit/audio/test_input_stage.cpp tests/CMakeLists.txt
git commit -m "feat(audio): InputStage with DC blocker, noise gate, input gain"
```

---

## Task 12: InputStage — noise gate behavior tests

**Files:**
- Modify: `tests/unit/audio/test_input_stage.cpp`

The implementation from Task 11 already includes the noise gate; this task adds explicit tests for its behavior so we have regression coverage.

- [ ] **Step 1: Append failing tests to `tests/unit/audio/test_input_stage.cpp`**

Append to the file:

```cpp
TEST_CASE("InputStage: gate closes on silence", "[audio][input_stage]") {
    InputStage stage;
    stage.prepare(48000.0, 1024);
    stage.setNoiseGateThreshold(-40.0f);  // close on quiet
    stage.setInputGainDb(0.0f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> buf(48000 * 2);
    // First half: loud sine; second half: very quiet sine (below threshold).
    gen.sine(440.0f, 0.5f, buf.data(), 48000);
    gen.sine(440.0f, 0.001f, buf.data() + 48000, 48000);

    stage.process(buf.data(), buf.data(), buf.size());

    // RMS of the gated region (last 1024 samples after 100 ms release).
    float sumSq = 0.0f;
    for (std::size_t i = buf.size() - 1024; i < buf.size(); ++i) sumSq += buf[i] * buf[i];
    const float rms = std::sqrt(sumSq / 1024.0f);
    REQUIRE(rms < 1e-3f);
}

TEST_CASE("InputStage: gate opens on loud signal", "[audio][input_stage]") {
    InputStage stage;
    stage.prepare(48000.0, 1024);
    stage.setNoiseGateThreshold(-40.0f);
    stage.setInputGainDb(0.0f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> buf(48000);
    gen.sine(440.0f, 0.5f, buf.data(), buf.size());

    stage.process(buf.data(), buf.data(), buf.size());

    // After settling, peak should be near 0.5 — gate is fully open.
    float peak = 0.0f;
    for (std::size_t i = buf.size() - 4800; i < buf.size(); ++i) peak = std::max(peak, std::abs(buf[i]));
    REQUIRE(peak > 0.4f);
}

TEST_CASE("InputStage: input gain scales output", "[audio][input_stage]") {
    InputStage stage;
    stage.prepare(48000.0, 1024);
    stage.setNoiseGateThreshold(-200.0f);
    stage.setInputGainDb(6.0f);  // ~2x

    SyntheticGuitar gen{48000.0};
    std::vector<float> buf(4800);
    gen.sine(440.0f, 0.25f, buf.data(), buf.size());

    stage.process(buf.data(), buf.data(), buf.size());

    float peak = 0.0f;
    for (std::size_t i = buf.size() / 2; i < buf.size(); ++i) peak = std::max(peak, std::abs(buf[i]));
    // 0.25 * 10^(6/20) ≈ 0.498
    REQUIRE_THAT(peak, WithinAbs(0.498f, 0.02f));
}
```

- [ ] **Step 2: Run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R InputStage
```

Expected: 5 tests pass.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/audio/test_input_stage.cpp
git commit -m "test(audio): noise gate and input gain coverage for InputStage"
```

---

## Task 13: InputStage — real-time-safety test

**Files:**
- Modify: `tests/unit/audio/test_input_stage.cpp`

- [ ] **Step 1: Append the realtime-safety test**

Append to `tests/unit/audio/test_input_stage.cpp`:

```cpp
#include "harness/RealtimeSentinel.h"
using guitar_dsp::tests::RealtimeSentinel;

TEST_CASE("InputStage: zero allocations on audio thread", "[audio][input_stage][realtime]") {
    InputStage stage;
    stage.prepare(48000.0, 512);
    stage.setNoiseGateThreshold(-40.0f);
    stage.setInputGainDb(3.0f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> buf(512);

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();

    // Process 10 seconds of audio in 512-sample blocks.
    for (int i = 0; i < 938; ++i) {
        gen.sine(440.0f, 0.5f, buf.data(), buf.size());
        stage.process(buf.data(), buf.data(), buf.size());
    }

    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
```

- [ ] **Step 2: Run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R "InputStage.*realtime"
```

Expected: passes with zero violations.

- [ ] **Step 3: Commit**

```bash
git add tests/unit/audio/test_input_stage.cpp
git commit -m "test(audio): realtime-safety regression test for InputStage"
```

---

## Task 14: Mixer module

**Files:**
- Create: `src/audio/Mixer.h`
- Create: `src/audio/Mixer.cpp`
- Create: `tests/unit/audio/test_mixer.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing tests**

Create `tests/unit/audio/test_mixer.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "audio/Mixer.h"
#include "harness/SyntheticGuitar.h"
#include "harness/RealtimeSentinel.h"

#include <vector>

using Catch::Matchers::WithinAbs;
using guitar_dsp::audio::Mixer;
using guitar_dsp::tests::SyntheticGuitar;
using guitar_dsp::tests::RealtimeSentinel;

TEST_CASE("Mixer: dry=1 wet=0 is bit-exact passthrough", "[audio][mixer]") {
    Mixer mixer;
    mixer.prepare(48000.0, 512);
    mixer.setDryWet(0.0f);  // 0 = fully dry
    mixer.setMasterGainDb(0.0f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> dry(512), wet(512, 0.0f), out(512);
    gen.sine(440.0f, 0.4f, dry.data(), dry.size());

    mixer.process(dry.data(), wet.data(), out.data(), out.size());

    for (std::size_t i = 0; i < out.size(); ++i)
        REQUIRE(out[i] == dry[i]);
}

TEST_CASE("Mixer: dry=0 wet=1 is bit-exact wet", "[audio][mixer]") {
    Mixer mixer;
    mixer.prepare(48000.0, 512);
    mixer.setDryWet(1.0f);  // 1 = fully wet
    mixer.setMasterGainDb(0.0f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> dry(512, 0.0f), wet(512), out(512);
    gen.sine(440.0f, 0.4f, wet.data(), wet.size());

    mixer.process(dry.data(), wet.data(), out.data(), out.size());

    for (std::size_t i = 0; i < out.size(); ++i)
        REQUIRE(out[i] == wet[i]);
}

TEST_CASE("Mixer: master gain scales output", "[audio][mixer]") {
    Mixer mixer;
    mixer.prepare(48000.0, 512);
    mixer.setDryWet(0.0f);
    mixer.setMasterGainDb(-6.0f);  // ~0.5x

    SyntheticGuitar gen{48000.0};
    std::vector<float> dry(512), wet(512, 0.0f), out(512);
    gen.dc(1.0f, dry.data(), dry.size());

    mixer.process(dry.data(), wet.data(), out.data(), out.size());

    // After settling, output should be ~0.5.
    REQUIRE_THAT(out[511], WithinAbs(0.501f, 0.01f));
}

TEST_CASE("Mixer: zero allocations on audio thread", "[audio][mixer][realtime]") {
    Mixer mixer;
    mixer.prepare(48000.0, 512);
    mixer.setDryWet(0.5f);
    mixer.setMasterGainDb(0.0f);

    SyntheticGuitar gen{48000.0};
    std::vector<float> dry(512), wet(512), out(512);
    gen.sine(440.0f, 0.4f, dry.data(), dry.size());
    gen.sine(220.0f, 0.4f, wet.data(), wet.size());

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int i = 0; i < 938; ++i)
        mixer.process(dry.data(), wet.data(), out.data(), out.size());
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
```

- [ ] **Step 2: Run to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests 2>&1 | head -10
```

Expected: `Mixer.h not found`.

- [ ] **Step 3: Write `src/audio/Mixer.h`**

```cpp
#pragma once

#include <cstddef>

namespace guitar_dsp::audio {

// Two-input mixer: dry input + wet input, blended by a single dry/wet
// control (0.0 = fully dry, 1.0 = fully wet, equal-power crossfade in
// between), then scaled by a master gain. Parameters are smoothed over
// `rampSamples` to avoid zipper noise on parameter changes.
class Mixer {
public:
    Mixer();

    void prepare(double sampleRate, int blockSize);
    void reset();

    // dryWet: 0..1. Equal-power crossfade.
    void setDryWet(float dryWet);
    void setMasterGainDb(float db);
    void setRampMs(float ms);

    void process(const float* dry,
                 const float* wet,
                 float* out,
                 std::size_t numSamples);

private:
    double sampleRate_ = 48000.0;

    float targetDryGain_ = 1.0f;
    float targetWetGain_ = 0.0f;
    float currentDryGain_ = 1.0f;
    float currentWetGain_ = 0.0f;

    float targetMasterGain_ = 1.0f;
    float currentMasterGain_ = 1.0f;

    float rampCoef_ = 0.0f;
    float rampMs_ = 5.0f;

    void recomputeCoefficients();
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 4: Write `src/audio/Mixer.cpp`**

```cpp
#include "Mixer.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

namespace {
    constexpr float kPiOverTwo = 1.57079632679489661923f;
    float dbToLinear(float db) { return std::pow(10.0f, db / 20.0f); }
}

Mixer::Mixer() = default;

void Mixer::prepare(double sampleRate, int /*blockSize*/) {
    sampleRate_ = sampleRate;
    recomputeCoefficients();
    reset();
}

void Mixer::reset() {
    currentDryGain_ = targetDryGain_;
    currentWetGain_ = targetWetGain_;
    currentMasterGain_ = targetMasterGain_;
}

void Mixer::setRampMs(float ms) {
    rampMs_ = std::max(0.1f, ms);
    recomputeCoefficients();
}

void Mixer::recomputeCoefficients() {
    const float t = rampMs_ * 0.001f;
    rampCoef_ = std::exp(-1.0f / (t * static_cast<float>(sampleRate_)));
}

void Mixer::setDryWet(float dryWet) {
    dryWet = std::clamp(dryWet, 0.0f, 1.0f);
    targetDryGain_ = std::cos(dryWet * kPiOverTwo);
    targetWetGain_ = std::sin(dryWet * kPiOverTwo);
}

void Mixer::setMasterGainDb(float db) {
    targetMasterGain_ = dbToLinear(db);
}

void Mixer::process(const float* dry,
                    const float* wet,
                    float* out,
                    std::size_t numSamples) {
    for (std::size_t i = 0; i < numSamples; ++i) {
        currentDryGain_ = rampCoef_ * currentDryGain_ + (1.0f - rampCoef_) * targetDryGain_;
        currentWetGain_ = rampCoef_ * currentWetGain_ + (1.0f - rampCoef_) * targetWetGain_;
        currentMasterGain_ = rampCoef_ * currentMasterGain_ + (1.0f - rampCoef_) * targetMasterGain_;

        const float mixed = dry[i] * currentDryGain_ + wet[i] * currentWetGain_;
        out[i] = mixed * currentMasterGain_;
    }
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 5: Update `src/CMakeLists.txt`**

```cmake
add_library(guitar_dsp_audio STATIC
    audio/InputStage.cpp
    audio/Mixer.cpp
)
```

- [ ] **Step 6: Update `tests/CMakeLists.txt`**

```cmake
add_executable(guitar_dsp_tests
    unit/test_smoke.cpp
    unit/test_realtime_sentinel.cpp
    unit/test_synthetic_guitar.cpp
    unit/test_golden_file.cpp
    unit/audio/test_input_stage.cpp
    unit/audio/test_mixer.cpp
    harness/RealtimeSentinel.cpp
    harness/SyntheticGuitar.cpp
    harness/GoldenFile.cpp
)
```

- [ ] **Step 7: Build and run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R Mixer
```

Expected: 4 tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/audio/Mixer.{h,cpp} src/CMakeLists.txt tests/unit/audio/test_mixer.cpp tests/CMakeLists.txt
git commit -m "feat(audio): Mixer with equal-power dry/wet and ramped master gain"
```

---

## Task 15: AudioGraph wiring

**Files:**
- Create: `src/audio/AudioGraph.h`
- Create: `src/audio/AudioGraph.cpp`
- Create: `tests/unit/audio/test_audio_graph.cpp`
- Modify: `src/CMakeLists.txt`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/unit/audio/test_audio_graph.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/AudioGraph.h"
#include "harness/SyntheticGuitar.h"
#include "harness/RealtimeSentinel.h"

#include <vector>

using guitar_dsp::audio::AudioGraph;
using guitar_dsp::tests::SyntheticGuitar;
using guitar_dsp::tests::RealtimeSentinel;

TEST_CASE("AudioGraph: produces non-silent output for sine input", "[audio][graph]") {
    AudioGraph graph;
    graph.prepare(48000.0, 512);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(512), out(512);
    gen.sine(440.0f, 0.4f, in.data(), in.size());

    graph.process(in.data(), out.data(), out.size());

    float peak = 0.0f;
    for (float s : out) peak = std::max(peak, std::abs(s));
    REQUIRE(peak > 0.05f);
}

TEST_CASE("AudioGraph: silence in -> silence out", "[audio][graph]") {
    AudioGraph graph;
    graph.prepare(48000.0, 512);

    std::vector<float> in(512, 0.0f), out(512);
    graph.process(in.data(), out.data(), out.size());

    for (float s : out) REQUIRE(std::abs(s) < 1e-3f);
}

TEST_CASE("AudioGraph: zero allocations on audio thread", "[audio][graph][realtime]") {
    AudioGraph graph;
    graph.prepare(48000.0, 512);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(512), out(512);
    gen.sine(440.0f, 0.4f, in.data(), in.size());

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int i = 0; i < 938; ++i)
        graph.process(in.data(), out.data(), out.size());
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
```

- [ ] **Step 2: Run to confirm failure**

```bash
cmake --build build --target guitar_dsp_tests 2>&1 | head -10
```

Expected: `AudioGraph.h not found`.

- [ ] **Step 3: Write `src/audio/AudioGraph.h`**

```cpp
#pragma once

#include <cstddef>
#include <vector>

#include "InputStage.h"
#include "Mixer.h"

namespace guitar_dsp::audio {

// The top-level audio processing graph. In Phase 1 this is just:
//   InputStage -> (passthrough as both dry and wet) -> Mixer -> Output
// Subsequent phases insert the Instrument Carousel and Vocoder branches
// between InputStage and Mixer. All buffers used internally are sized at
// prepare() time; processing is allocation-free.
class AudioGraph {
public:
    AudioGraph();

    void prepare(double sampleRate, int blockSize);
    void reset();

    void process(const float* in, float* out, std::size_t numSamples);

    InputStage& input() { return inputStage_; }
    Mixer& mixer() { return mixer_; }

private:
    InputStage inputStage_;
    Mixer mixer_;

    std::vector<float> postInputBuffer_;
    std::vector<float> wetBuffer_;
};

} // namespace guitar_dsp::audio
```

- [ ] **Step 4: Write `src/audio/AudioGraph.cpp`**

```cpp
#include "AudioGraph.h"

#include <algorithm>

namespace guitar_dsp::audio {

AudioGraph::AudioGraph() = default;

void AudioGraph::prepare(double sampleRate, int blockSize) {
    inputStage_.prepare(sampleRate, blockSize);
    mixer_.prepare(sampleRate, blockSize);

    postInputBuffer_.assign(static_cast<std::size_t>(blockSize), 0.0f);
    wetBuffer_.assign(static_cast<std::size_t>(blockSize), 0.0f);

    // Default mixer to fully dry until later phases install branches.
    mixer_.setDryWet(0.0f);
    mixer_.setMasterGainDb(0.0f);
    mixer_.reset();
}

void AudioGraph::reset() {
    inputStage_.reset();
    mixer_.reset();
    std::fill(postInputBuffer_.begin(), postInputBuffer_.end(), 0.0f);
    std::fill(wetBuffer_.begin(), wetBuffer_.end(), 0.0f);
}

void AudioGraph::process(const float* in, float* out, std::size_t numSamples) {
    // Guard against caller passing a larger block than we prepared for.
    // Truncate rather than allocate. Tests should always call prepare()
    // with the expected max block size.
    numSamples = std::min(numSamples, postInputBuffer_.size());

    inputStage_.process(in, postInputBuffer_.data(), numSamples);
    // Wet path is silent in Phase 1; subsequent phases populate it.
    mixer_.process(postInputBuffer_.data(), wetBuffer_.data(), out, numSamples);
}

} // namespace guitar_dsp::audio
```

- [ ] **Step 5: Update `src/CMakeLists.txt`**

```cmake
add_library(guitar_dsp_audio STATIC
    audio/InputStage.cpp
    audio/Mixer.cpp
    audio/AudioGraph.cpp
)
```

- [ ] **Step 6: Update `tests/CMakeLists.txt`**

```cmake
add_executable(guitar_dsp_tests
    unit/test_smoke.cpp
    unit/test_realtime_sentinel.cpp
    unit/test_synthetic_guitar.cpp
    unit/test_golden_file.cpp
    unit/audio/test_input_stage.cpp
    unit/audio/test_mixer.cpp
    unit/audio/test_audio_graph.cpp
    harness/RealtimeSentinel.cpp
    harness/SyntheticGuitar.cpp
    harness/GoldenFile.cpp
)
```

- [ ] **Step 7: Build and run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R AudioGraph
```

Expected: 3 tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/audio/AudioGraph.{h,cpp} src/CMakeLists.txt tests/unit/audio/test_audio_graph.cpp tests/CMakeLists.txt
git commit -m "feat(audio): AudioGraph wiring InputStage and Mixer"
```

---

## Task 16: Wire AudioGraph into PluginProcessor

**Files:**
- Modify: `src/app/PluginProcessor.h`
- Modify: `src/app/PluginProcessor.cpp`

- [ ] **Step 1: Update `src/app/PluginProcessor.h`**

Add an include and a member:

```cpp
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "audio/AudioGraph.h"

namespace guitar_dsp {

class PluginProcessor : public juce::AudioProcessor {
public:
    PluginProcessor();
    ~PluginProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Guitar DSP"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    audio::AudioGraph graph_;
    std::vector<float> monoScratch_;
};

} // namespace guitar_dsp
```

- [ ] **Step 2: Update `src/app/PluginProcessor.cpp`**

```cpp
#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>

namespace guitar_dsp {

PluginProcessor::PluginProcessor()
    : juce::AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::mono(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    graph_.prepare(sampleRate, samplesPerBlock);
    monoScratch_.assign(static_cast<std::size_t>(samplesPerBlock), 0.0f);
}

void PluginProcessor::releaseResources() {
    graph_.reset();
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto inputs = layouts.getMainInputChannelSet();
    const auto outputs = layouts.getMainOutputChannelSet();
    if (inputs.isDisabled() || outputs.isDisabled()) return false;
    return inputs == juce::AudioChannelSet::mono()
        && (outputs == juce::AudioChannelSet::mono()
            || outputs == juce::AudioChannelSet::stereo());
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int totalOut = getTotalNumOutputChannels();
    if (totalOut == 0) return;

    // Grow scratch only if buffer is larger than expected; this happens
    // off the hot path only when the host changes block size at runtime.
    if (monoScratch_.size() < static_cast<std::size_t>(numSamples)) {
        monoScratch_.assign(static_cast<std::size_t>(numSamples), 0.0f);
        graph_.prepare(getSampleRate(), numSamples);
    }

    const float* in = buffer.getReadPointer(0);
    graph_.process(in, monoScratch_.data(), static_cast<std::size_t>(numSamples));

    for (int ch = 0; ch < totalOut; ++ch) {
        float* out = buffer.getWritePointer(ch);
        std::memcpy(out, monoScratch_.data(), sizeof(float) * static_cast<std::size_t>(numSamples));
    }
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() {
    return new PluginEditor(*this);
}

} // namespace guitar_dsp

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new guitar_dsp::PluginProcessor();
}
```

- [ ] **Step 3: Build and run the app**

```bash
cmake --build build --target guitar_dsp_app_Standalone
open "build/src/app/guitar_dsp_app_artefacts/Standalone/Guitar DSP.app"
```

Expected: app launches; plug in a guitar; signal passes through with the noise gate and DC blocker engaged. No glitches.

- [ ] **Step 4: Commit**

```bash
git add src/app/PluginProcessor.{h,cpp}
git commit -m "feat(app): wire AudioGraph into PluginProcessor"
```

---

## Task 17: Golden-file passthrough integration test

**Files:**
- Create: `tests/integration/test_passthrough_golden.cpp`
- Create: `tests/fixtures/expected/passthrough_sine_440.wav` (generated by test)
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/integration/test_passthrough_golden.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/AudioGraph.h"
#include "harness/GoldenFile.h"

#include <filesystem>
#include <vector>

using guitar_dsp::audio::AudioGraph;
using guitar_dsp::tests::GoldenFile;

namespace {
std::string findFixturePath(const std::string& relative) {
    // Walk up from the current dir until we find the fixtures folder.
    auto p = std::filesystem::current_path();
    for (int i = 0; i < 6; ++i) {
        auto candidate = p / "tests" / "fixtures" / relative;
        if (std::filesystem::exists(candidate.parent_path())) return candidate.string();
        p = p.parent_path();
    }
    throw std::runtime_error("Could not locate tests/fixtures from " + std::filesystem::current_path().string());
}
}

TEST_CASE("integration: AudioGraph passthrough golden file", "[integration][golden]") {
    const auto inputPath = findFixturePath("inputs/sine_440.wav");
    const auto goldenPath = findFixturePath("expected/passthrough_sine_440.wav");

    auto input = GoldenFile::readMonoWav(inputPath);
    REQUIRE(input.sampleRate == 48000.0);

    AudioGraph graph;
    graph.prepare(input.sampleRate, 512);

    std::vector<float> output(input.samples.size());

    // Process in 512-sample blocks.
    constexpr std::size_t block = 512;
    for (std::size_t i = 0; i < input.samples.size(); i += block) {
        const std::size_t n = std::min(block, input.samples.size() - i);
        graph.process(input.samples.data() + i, output.data() + i, n);
    }

    // Tolerance accommodates filter group delay + DC blocker settling.
    auto diff = GoldenFile::assertMatchesGolden(goldenPath,
                                                input.sampleRate,
                                                output.data(),
                                                output.size(),
                                                /*tolerance=*/1e-4f);
    INFO("max abs diff: " << diff.maxAbsDiff
         << ", differing samples: " << diff.numDifferingSamples);
    REQUIRE(diff.maxAbsDiff < 1e-4f);
}
```

- [ ] **Step 2: Update `tests/CMakeLists.txt`**

```cmake
add_executable(guitar_dsp_tests
    unit/test_smoke.cpp
    unit/test_realtime_sentinel.cpp
    unit/test_synthetic_guitar.cpp
    unit/test_golden_file.cpp
    unit/audio/test_input_stage.cpp
    unit/audio/test_mixer.cpp
    unit/audio/test_audio_graph.cpp
    integration/test_passthrough_golden.cpp
    harness/RealtimeSentinel.cpp
    harness/SyntheticGuitar.cpp
    harness/GoldenFile.cpp
)
```

- [ ] **Step 3: First run will fail because the golden doesn't exist yet; regenerate it**

```bash
cmake --build build --target guitar_dsp_tests
GUITAR_DSP_REGENERATE_GOLDENS=1 ctest --test-dir build --output-on-failure -R "passthrough golden"
```

Expected: test runs, writes `tests/fixtures/expected/passthrough_sine_440.wav`, passes.

- [ ] **Step 4: Confirm the golden was written and the test now passes without regeneration**

```bash
ls -lh tests/fixtures/expected/passthrough_sine_440.wav
ctest --test-dir build --output-on-failure -R "passthrough golden"
```

Expected: file exists ~280 KB; test passes.

- [ ] **Step 5: Commit**

```bash
git add tests/integration/test_passthrough_golden.cpp tests/fixtures/expected/passthrough_sine_440.wav tests/CMakeLists.txt
git commit -m "test(integration): golden-file passthrough regression test"
```

---

## Task 18: 60-second realtime-safety integration test

**Files:**
- Create: `tests/integration/test_realtime_safety.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the test**

Create `tests/integration/test_realtime_safety.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "audio/AudioGraph.h"
#include "harness/RealtimeSentinel.h"
#include "harness/SyntheticGuitar.h"

#include <vector>

using guitar_dsp::audio::AudioGraph;
using guitar_dsp::tests::RealtimeSentinel;
using guitar_dsp::tests::SyntheticGuitar;

TEST_CASE("integration: 60 s of audio thread activity is allocation-free", "[integration][realtime][slow]") {
    constexpr double sr = 48000.0;
    constexpr std::size_t block = 512;
    constexpr int blocks = static_cast<int>(60.0 * sr / block);  // ~5625 blocks

    AudioGraph graph;
    graph.prepare(sr, static_cast<int>(block));

    SyntheticGuitar gen{sr};
    std::vector<float> in(block), out(block);

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int i = 0; i < blocks; ++i) {
        // Vary input pattern so we hit different code paths.
        if (i % 100 == 0) gen.silence(in.data(), in.size());
        else if (i % 50 == 0) gen.sweep(80.0f, 8000.0f, 0.4f, in.data(), in.size());
        else gen.sine(440.0f, 0.4f, in.data(), in.size());

        graph.process(in.data(), out.data(), in.size());
    }
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
```

- [ ] **Step 2: Update `tests/CMakeLists.txt`**

```cmake
add_executable(guitar_dsp_tests
    unit/test_smoke.cpp
    unit/test_realtime_sentinel.cpp
    unit/test_synthetic_guitar.cpp
    unit/test_golden_file.cpp
    unit/audio/test_input_stage.cpp
    unit/audio/test_mixer.cpp
    unit/audio/test_audio_graph.cpp
    integration/test_passthrough_golden.cpp
    integration/test_realtime_safety.cpp
    harness/RealtimeSentinel.cpp
    harness/SyntheticGuitar.cpp
    harness/GoldenFile.cpp
)
```

- [ ] **Step 3: Run**

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure -R "60 s"
```

Expected: passes in a few seconds; zero violations.

- [ ] **Step 4: Commit**

```bash
git add tests/integration/test_realtime_safety.cpp tests/CMakeLists.txt
git commit -m "test(integration): 60s realtime-safety test for AudioGraph"
```

---

## Task 19: GitHub Actions CI

**Files:**
- Create: `.github/workflows/ci.yml`

- [ ] **Step 1: Write `.github/workflows/ci.yml`**

```yaml
name: ci

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  build-and-test:
    runs-on: macos-14
    strategy:
      fail-fast: false
      matrix:
        build_type: [Debug, Release]

    steps:
      - name: Checkout (with submodules)
        uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install Ninja
        run: brew install ninja

      - name: Configure
        run: |
          cmake -B build -S . -G Ninja \
            -DCMAKE_BUILD_TYPE=${{ matrix.build_type }} \
            $([ "${{ matrix.build_type }}" = "Debug" ] && echo "-DGUITAR_DSP_ASAN=ON -DGUITAR_DSP_UBSAN=ON")

      - name: Build app
        run: cmake --build build --target guitar_dsp_app_Standalone

      - name: Build tests
        run: cmake --build build --target guitar_dsp_tests

      - name: Run tests
        run: ctest --test-dir build --output-on-failure --timeout 300
```

- [ ] **Step 2: Commit and push to a feature branch to verify CI**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: GitHub Actions workflow for macOS build + test"
```

- [ ] **Step 3: Push and observe**

```bash
git push origin HEAD
```

Then check the Actions tab on GitHub. The first run may surface platform-specific issues (e.g., Xcode version, JUCE 8 requirements). If it fails, iterate on `ci.yml` until both Debug+ASan and Release pass.

---

## Task 20: README — build and run instructions

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Replace `README.md` contents**

```markdown
# guitar-dsp

Standalone macOS audio app used as the live instrument for the tech-conference talk *While My Guitar Gently Speaks*. Live guitar audio is processed into transformed timbres and into "speech" via a vocoder driven by text-to-speech, with scene switching via a Behringer FCB1010 MIDI foot controller.

See [`docs/superpowers/specs/2026-05-29-while-my-guitar-gently-speaks-design.md`](docs/superpowers/specs/2026-05-29-while-my-guitar-gently-speaks-design.md) for the full design.

## Build

Requirements: macOS 13+, Xcode 15+, CMake 3.22+, Ninja.

```bash
git clone --recurse-submodules <repo-url>
cd guitar-dsp
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target guitar_dsp_app_Standalone
open "build/src/app/guitar_dsp_app_artefacts/Standalone/Guitar DSP.app"
```

## Run tests

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure
```

To regenerate golden-file fixtures after an intentional DSP change:

```bash
GUITAR_DSP_REGENERATE_GOLDENS=1 ctest --test-dir build --output-on-failure -R golden
```

## Project status

This branch implements **Phase 1: Foundation, Audio Passthrough, Test Harness**. Subsequent phases (see plans directory) add MIDI/scenes, the vocoder + TTS sources, the Instrument Carousel, and the visualizer.
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "docs: README with build/run/test instructions"
```

---

## Subsequent phase plans (preview)

These are written as separate plans following Phase 1's pattern. They are listed here so the engineer sees the roadmap; each gets its own detailed plan document when it is the next phase to execute.

- **Phase 2: Scene system + MIDI** — `Scene` struct, `SceneEngine` with lock-free param snapshots, JSON loader, hot reload; `MidiRouter` + `FCB1010Mapping`; keyboard fallback (number keys 1–0). Ships: app with 10 selectable scenes (placeholder DSP) driven by FCB1010 or keyboard.

- **Phase 3: Vocoder + TTS sources** — `IVocoder` interface, `ChannelVocoder` (24-band Bark-scale, sibilance noise injection); `ITTSSource` interface + three implementations (`PrebakedTTSSource`, `LiveTTSSource` via Obj-C++ AVSpeechSynthesizer, `PiperTTSSource` via bundled subprocess); `TTSClipPlayer`, `TTSPrewarmer`; Python `tools/tts_prebake/` pipeline. Ships: speaking-guitar scenes work end-to-end with all three TTS paths and fallback chain.

- **Phase 4: Instrument Carousel** — chain of DSP stages (pitch shifter, octaver, formant shifter, multimode filter, bit crusher, comb, tube saturation, chorus, reverb); 5 instrument presets (organ, piano-ish, synth lead, 8-bit, choir). Ships: scenes 1–5 from the spec sound like their target instruments.

- **Phase 5: Visualization** — JUCE OpenGL `VisualizerView` (spectrogram), karaoke text overlay synced to `TTSClipPlayer` position, `HUDView` (scene name, pedal meters, MIDI activity LED, VU). Ships: the on-stage visual experience the audience sees.

- **Phase 6: Hardening + pre-conference** — golden-file renders for all 10 scenes (Tier 3 test #15); audio-thread benchmark (#16); 10-minute CI soak (#17); Python pre-bake pytest (#18); 4-hour manual soak; latency measurement; intelligibility verification; dress rehearsal pass. Ships: v1 cleared for first conference.

---

## Self-review

**Spec coverage** (skimming the spec § by §):
- §1 Overview, §3 Demo arc — Phase 1 doesn't implement scenes (Phase 2) but lays the audio path.
- §4 Threading model / audio graph — Phase 1 builds the audio thread structure + `AudioGraph` (Task 15).
- §5 DSP — Phase 1 builds `InputStage` and `Mixer` only. Carousel (§5.1), vocoder (§5.2), `IVocoder` (§5.3) are Phase 3.
- §6 TTS sources — entirely Phase 3.
- §7 Scenes — entirely Phase 2.
- §8 MIDI / FCB1010 — entirely Phase 2.
- §9 Visualization — entirely Phase 5.
- §10 File layout — Phase 1 implements the `external/`, `src/app/`, `src/audio/`, `tests/` skeleton; subsequent phases fill in their respective subtrees.
- §11 Risks — Phase 1 mitigates risk #1 (audio dropouts: 64-sample test budget enforced) and #6 (TTS source failure: not applicable yet) via the harness. Other risks unblock as their feature lands.
- §12 Testing strategy — Phase 1 establishes the framework (Catch2 v3), harness (`RealtimeSentinel`, `SyntheticGuitar`, `GoldenFile`), and the CI pipeline. Tier 1 tests #1 (realtime safety) and partial #4 (passthrough golden) ship in Phase 1. Other Tier 1 tests arrive with their feature in later phases.
- §13 v2 roadmap — unaffected; Phase 1 doesn't touch v2 surface.

**Placeholder scan**: All steps include exact file contents, paths, commands, and expected output. No "TBD" / "implement later" remaining.

**Type consistency**: 
- `InputStage` API: `prepare`, `reset`, `process(in, out, n)`, `setNoiseGateThreshold(db)`, `setInputGainDb(db)` — consistent across header, .cpp, and all test files.
- `Mixer` API: `prepare`, `reset`, `process(dry, wet, out, n)`, `setDryWet(0..1)`, `setMasterGainDb(db)`, `setRampMs(ms)` — consistent.
- `AudioGraph` API: `prepare`, `reset`, `process(in, out, n)`, `input()`, `mixer()` — consistent.
- `RealtimeSentinel` API: `markCurrentThreadAsRealtime`, `unmarkCurrentThreadAsRealtime`, `violations()` — consistent.
- `GoldenFile` API: `writeMonoWav`, `readMonoWav`, `compareBuffers`, `assertMatchesGolden` — consistent.

---

## Execution handoff

Plan complete and saved to `docs/superpowers/plans/2026-05-29-phase-1-foundation.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
