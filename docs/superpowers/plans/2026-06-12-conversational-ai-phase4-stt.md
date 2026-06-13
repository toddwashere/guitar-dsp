# Phase 4 — Speech-to-Text (whisper.cpp)

> Continuation of [2026-06-12-conversational-ai.md](2026-06-12-conversational-ai.md). Read it first.

**Goal:** Vendor whisper.cpp via CMake FetchContent, wrap it behind `ITranscriber`, ship the `ggml-base.en.bin` model in the bundle.

---

### Task 4.1: Vendor whisper.cpp via FetchContent

**Files:**
- Modify: `CMakeLists.txt` (top-level)
- Modify: `src/CMakeLists.txt`

- [ ] **Step 1: Add FetchContent block to top-level `CMakeLists.txt`**

After `find_package(JUCE ...)` block:

```cmake
include(FetchContent)

# whisper.cpp — used by guitar_dsp_ai.
set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
FetchContent_Declare(whisper_cpp
    GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
    GIT_TAG        v1.7.4
)
FetchContent_MakeAvailable(whisper_cpp)
```

- [ ] **Step 2: Link whisper into `guitar_dsp_ai`**

In `src/CMakeLists.txt`, extend the `guitar_dsp_ai` target_link_libraries:

```cmake
target_link_libraries(guitar_dsp_ai
    PUBLIC
        juce::juce_core
        juce::juce_events
    PRIVATE
        whisper
)
```

- [ ] **Step 3: Reconfigure & verify build (will fetch whisper.cpp)**

```bash
cmake -S . -B build
cmake --build build --target guitar_dsp_ai
```

Expected: whisper.cpp pulled, `[100%] Built target guitar_dsp_ai`. First run takes a few minutes.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt src/CMakeLists.txt
git commit -m "build(ai): vendor whisper.cpp v1.7.4 via FetchContent"
```

---

### Task 4.2: `ITranscriber` interface

**Files:**
- Create: `src/ai/ITranscriber.h`

- [ ] **Step 1: Header**

```cpp
#pragma once
#include <chrono>
#include <string>
#include <vector>

namespace guitar_dsp::ai {

class CancellationToken;

struct TranscriptionResult {
    std::string                text;
    std::string                language;
    std::chrono::milliseconds  latency {0};
    std::string                error;       // empty on success
};

class ITranscriber {
public:
    virtual ~ITranscriber() = default;
    virtual TranscriptionResult transcribe(const std::vector<float>& mono16k,
                                           CancellationToken* cancel = nullptr) = 0;
    virtual std::string modelName() const = 0;
};

} // namespace guitar_dsp::ai
```

- [ ] **Step 2: Commit**

```bash
git add src/ai/ITranscriber.h
git commit -m "feat(ai): add ITranscriber interface"
```

---

### Task 4.3: `WhisperTranscriber`

**Files:**
- Create: `src/ai/WhisperTranscriber.h`
- Create: `src/ai/WhisperTranscriber.cpp`
- Create: `tests/integration/test_whisper_transcriber.cpp`

Note: This is an **integration** test (real whisper.cpp + real fixture WAV). Add it to `guitar_dsp_tests` like the existing integration tests.

- [ ] **Step 1: Provision the model file at known path**

```bash
mkdir -p Resources/whisper
curl -L -o Resources/whisper/ggml-base.en.bin \
  https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin
```

Add `Resources/whisper/` to `.gitignore` (model is large; downloaded separately by setup script).

- [ ] **Step 2: Tests (real whisper.cpp)**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "ai/WhisperTranscriber.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

using guitar_dsp::ai::WhisperTranscriber;

namespace {
std::vector<float> loadMono16k(const char* path) {
    juce::AudioFormatManager mgr; mgr.registerBasicFormats();
    juce::File f(path);
    std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(f));
    REQUIRE(reader);
    juce::AudioBuffer<float> buf(1, (int)reader->lengthInSamples);
    reader->read(&buf, 0, (int)reader->lengthInSamples, 0, true, false);
    return std::vector<float>(buf.getReadPointer(0),
                              buf.getReadPointer(0) + buf.getNumSamples());
}
}

TEST_CASE("WhisperTranscriber: hello_world.wav transcribes to 'hello'",
          "[ai][stt][integration][.requires_model]") {
    juce::File model("Resources/whisper/ggml-base.en.bin");
    if (! model.existsAsFile()) {
        WARN("model not present; skipping");
        return;
    }
    WhisperTranscriber t{model};
    auto samples = loadMono16k("tests/fixtures/ai/hello_world.wav");
    auto r = t.transcribe(samples);
    REQUIRE(r.error.empty());
    auto lower = juce::String(r.text).toLowerCase().toStdString();
    REQUIRE(lower.find("hello") != std::string::npos);
}

TEST_CASE("WhisperTranscriber: silence returns empty text",
          "[ai][stt][integration][.requires_model]") {
    juce::File model("Resources/whisper/ggml-base.en.bin");
    if (! model.existsAsFile()) return;
    WhisperTranscriber t{model};
    auto samples = loadMono16k("tests/fixtures/ai/silence.wav");
    auto r = t.transcribe(samples);
    REQUIRE(r.error.empty());
    // whisper may emit "[BLANK_AUDIO]" or "" — accept either as no real content
    auto txt = juce::String(r.text).trim().toLowerCase().toStdString();
    REQUIRE((txt.empty() || txt.find("blank") != std::string::npos
                         || txt.find("[") == 0));
}

TEST_CASE("WhisperTranscriber: missing model file fails construction gracefully",
          "[ai][stt]") {
    juce::File bogus("/tmp/does_not_exist.bin");
    WhisperTranscriber t{bogus};
    auto r = t.transcribe(std::vector<float>(16000, 0.0f));
    REQUIRE(r.error.find("model") != std::string::npos);
}
```

- [ ] **Step 3: Implement**

`src/ai/WhisperTranscriber.h`:
```cpp
#pragma once
#include "ai/ITranscriber.h"
#include <juce_core/juce_core.h>
#include <mutex>

struct whisper_context;

namespace guitar_dsp::ai {

class WhisperTranscriber : public ITranscriber {
public:
    explicit WhisperTranscriber(juce::File modelFile);
    ~WhisperTranscriber() override;

    TranscriptionResult transcribe(const std::vector<float>& mono16k,
                                   CancellationToken* cancel) override;
    std::string modelName() const override { return modelName_; }

private:
    juce::File          modelFile_;
    std::string         modelName_;
    whisper_context*    ctx_  {nullptr};
    std::mutex          mutex_;
};

} // namespace
```

`src/ai/WhisperTranscriber.cpp`:
```cpp
#include "ai/WhisperTranscriber.h"
#include "ai/CancellationToken.h"
#include <whisper.h>

namespace guitar_dsp::ai {

WhisperTranscriber::WhisperTranscriber(juce::File f)
    : modelFile_(std::move(f)),
      modelName_(modelFile_.getFileNameWithoutExtension().toStdString()) {
    if (! modelFile_.existsAsFile()) return;
    whisper_context_params cp = whisper_context_default_params();
    ctx_ = whisper_init_from_file_with_params(
        modelFile_.getFullPathName().toRawUTF8(), cp);
}

WhisperTranscriber::~WhisperTranscriber() {
    if (ctx_) whisper_free(ctx_);
}

TranscriptionResult WhisperTranscriber::transcribe(
    const std::vector<float>& mono16k, CancellationToken* cancel) {
    TranscriptionResult r;
    if (! ctx_) { r.error = "model not loaded"; return r; }
    if (cancel && cancel->isCancelled()) { r.error = "cancelled"; return r; }

    auto start = std::chrono::steady_clock::now();
    std::lock_guard lk(mutex_);

    whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    p.language        = "en";
    p.print_progress  = false;
    p.print_timestamps = false;
    p.no_context      = true;
    p.single_segment  = false;

    try {
        if (whisper_full(ctx_, p, mono16k.data(), (int)mono16k.size()) != 0) {
            r.error = "whisper_full failed";
            return r;
        }
    } catch (...) { r.error = "whisper exception"; return r; }

    const int n = whisper_full_n_segments(ctx_);
    std::string out;
    for (int i = 0; i < n; ++i) out += whisper_full_get_segment_text(ctx_, i);
    r.text = out;
    r.language = "en";
    r.latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - start);
    return r;
}

} // namespace
```

- [ ] **Step 4: Add `.gitignore` entry for the model**

```bash
echo "Resources/whisper/*.bin" >> .gitignore
```

- [ ] **Step 5: Run tests + commit**

```bash
./build/tests/guitar_dsp_tests "[ai][stt]"
git add src/ai/WhisperTranscriber.{h,cpp} \
        tests/integration/test_whisper_transcriber.cpp \
        src/CMakeLists.txt tests/CMakeLists.txt .gitignore
git commit -m "feat(ai): WhisperTranscriber with graceful missing-model handling"
```

---

### Task 4.4: Add whisper model to bundle copy step

**Files:**
- Modify: `CMakeLists.txt` (top-level — wherever `COPY_PLUGIN_AFTER_BUILD` assets are listed)

- [ ] **Step 1: Locate existing asset copy block**

```bash
grep -n "COPY_PLUGIN_AFTER_BUILD\|Resources/" CMakeLists.txt src/CMakeLists.txt
```

Find the block that copies TTS assets (`Resources/voices`, `Resources/piper`) into the bundle.

- [ ] **Step 2: Add the whisper model directory**

Adjacent to the existing voice asset copy commands, add:

```cmake
# Copy whisper model into the standalone .app and the AU .component
foreach(_t IN ITEMS GuitarSpeak_Standalone GuitarSpeak_AU)
    if (TARGET ${_t})
        add_custom_command(TARGET ${_t} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/Resources/whisper"
                "$<TARGET_BUNDLE_CONTENT_DIR:${_t}>/Resources/whisper"
            COMMENT "Copying whisper model into ${_t} bundle")
    endif()
endforeach()
```

Adapt target names to match the existing standalone/AU target names; check the existing copy blocks for the exact names.

- [ ] **Step 3: Build + verify**

```bash
cmake --build build --target GuitarSpeak_Standalone
find build -name "ggml-base.en.bin" -path "*Resources/whisper*"
```

Expected: the model file appears inside the standalone bundle.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit -m "build(ai): copy whisper model into Standalone + AU bundles"
```

---

## Phase 4 checkpoint — green.

Document for the user (or setup script): how to download `ggml-base.en.bin`. Add this to `docs/au-logic-setup.md` in Phase 11.
