# Phase 10 — Polish: pre-flight, fallback, headless safety

> Continuation of [2026-06-12-conversational-ai.md](2026-06-12-conversational-ai.md).

**Goal:** Make the AI feature solid for live demos — pre-flight validation, canned fallback for LLM errors, and explicit headless-safety tests so `auval`/`pluginval` keep passing.

---

### Task 10.1: Pre-flight validation in `AiSettingsPanel`

**Files:**
- Modify: `src/app/AiSettingsPanel.h`
- Modify: `src/app/AiSettingsPanel.cpp`
- Modify: `tests/unit/app/test_ai_settings_panel.cpp`

- [ ] **Step 1: Tests**

```cpp
TEST_CASE("AiSettingsPanel: model switch to Ollama validates running state",
          "[app][ui][settings][preflight]") {
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                 .getChildFile("aiset_preflight.xml");
    tmp.deleteFile();
    AppPreferences prefs{tmp};
    PersonaRegistry personas;
    FakeHttpTransport http;
    http.replies.push({0, "", "connect refused", {}});   // ollama down
    AiSettingsPanel p(prefs, personas, http);
    p.selectModel("ollama:llama3.2:3b");
    REQUIRE(p.modelStatusText().find("not running") != std::string::npos);
}

TEST_CASE("AiSettingsPanel: model switch to Anthropic without key shows warning",
          "[app][ui][settings][preflight]") {
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                 .getChildFile("aiset_preflight2.xml");
    tmp.deleteFile();
    AppPreferences prefs{tmp};
    PersonaRegistry personas;
    FakeHttpTransport http;
    AiSettingsPanel p(prefs, personas, http);
    p.selectModel("claude-haiku-4-5");
    REQUIRE(p.modelStatusText().find("missing") != std::string::npos);
}
```

- [ ] **Step 2: Implement `selectModel` + `modelStatusText`**

```cpp
// AiSettingsPanel.h
public:
    void selectModel(std::string modelId);
    std::string modelStatusText() const { return modelStatus_.getText().toStdString(); }

// AiSettingsPanel.cpp
void AiSettingsPanel::selectModel(std::string id) {
    if (id.rfind("ollama:", 0) == 0) {
        auto tag = id.substr(7);
        if (! ai::OllamaClient::isRunning(http_, prefs_.ollamaEndpoint())) {
            modelStatus_.setText("Ollama: not running (run `ollama serve`)",
                                 juce::dontSendNotification);
            return;
        }
        auto models = ai::OllamaClient::listInstalledModels(http_, prefs_.ollamaEndpoint());
        bool ok = std::find(models.begin(), models.end(), tag) != models.end();
        modelStatus_.setText(ok ? "Ollama: " + juce::String(tag) + " ready"
                                : "Ollama: model not pulled — run `ollama pull " + juce::String(tag) + "`",
                             juce::dontSendNotification);
    } else {
        modelStatus_.setText(prefs_.anthropicApiKey().empty()
                                ? "Anthropic: key missing"
                                : "Anthropic: key set ✓",
                             juce::dontSendNotification);
    }
}
```

Hook `modelBox_.onChange` to call `selectModel(...)` with the underlying id of the selected item.

- [ ] **Step 3: Run + commit**

```bash
./build/tests/guitar_dsp_tests "[app][ui][settings]"
git add src/app/AiSettingsPanel.{h,cpp} tests/unit/app/test_ai_settings_panel.cpp
git commit -m "feat(ui): pre-flight model validation in AiSettingsPanel"
```

---

### Task 10.2: Canned fallback on LLM error

**Files:**
- Modify: `src/ai/ConversationEngine.h` (add `setCannedFallback`)
- Modify: `src/ai/ConversationEngine.cpp`
- Modify: `tests/unit/ai/test_conversation_engine.cpp`

- [ ] **Step 1: Test**

```cpp
TEST_CASE("Engine: canned fallback fires LLM error path → still speaks",
          "[ai][engine][fallback]") {
    Harness h;
    h.llm.scriptedError = "rate limited";
    h.engine.setCannedFallbackEnabled(true);
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.endTurn();
    h.waitForState(ConversationEngine::State::Speaking);
    REQUIRE(h.spokenTexts.size() == 1);
    REQUIRE_FALSE(h.spokenTexts[0].empty());
    auto err = h.engine.lastError();
    REQUIRE(err.find("rate limited") != std::string::npos);   // still visible
}
```

- [ ] **Step 2: Implement**

```cpp
// ConversationEngine.h
public:
    void setCannedFallbackEnabled(bool enabled) { cannedFallback_ = enabled; }
private:
    std::atomic<bool> cannedFallback_ {false};
    static std::string pickCannedReply(int turnCount);
```

```cpp
// ConversationEngine.cpp — in runEndTurn(), where LLM error currently sets State::Error:
if (! reply.error.empty()) {
    if (cannedFallback_.load()) {
        const std::string canned = pickCannedReply((int)buf_->snapshot().size());
        { std::lock_guard lk(errorMutex_); lastError_ = reply.error; }
        buf_->append(Message::Role::Assistant, canned);
        state_.store(State::Speaking);
        say_(canned);
        return;
    }
    { std::lock_guard lk(errorMutex_); lastError_ = reply.error; }
    state_.store(State::Error); return;
}

// implementation
std::string ConversationEngine::pickCannedReply(int n) {
    static const char* pool[] = {
        "Hmm, let me think.",
        "Say that again?",
        "Hard to say.",
        "I'm not sure yet."
    };
    return pool[n % 4];
}
```

- [ ] **Step 3: Wire to settings**

In `PluginProcessor`, after constructing `engine_`:
```cpp
engine_->setCannedFallbackEnabled(prefs_->cannedFallbackOnLlmError());
```

In `AiSettingsPanel`, add a checkbox bound to `prefs_.setCannedFallbackOnLlmError(...)` (and call `engine_->setCannedFallbackEnabled(...)` when toggled — pass a callback into the panel from the editor).

- [ ] **Step 4: Run + commit**

```bash
./build/tests/guitar_dsp_tests "[ai][engine][fallback]"
git add src/ai/ConversationEngine.{h,cpp} src/app/PluginProcessor.cpp \
        src/app/AiSettingsPanel.{h,cpp} tests/unit/ai/test_conversation_engine.cpp
git commit -m "feat(ai): canned-fallback option for LLM errors (off by default)"
```

---

### Task 10.3: Headless-safety integration test

**Files:**
- Create: `tests/integration/test_headless_safety_ai.cpp`

- [ ] **Step 1: Test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "app/PluginProcessor.h"
#include <thread>

using guitar_dsp::PluginProcessor;

TEST_CASE("Headless safety: processor constructs without model file",
          "[app][headless][integration]") {
    // Simulate the model file being absent — point AssetLocator at a bogus dir
    setenv("GUITAR_SPEAK_ASSETS_DIR", "/tmp/nonexistent_guitar_speak_assets", 1);
    PluginProcessor p;                       // must not throw / crash
    SUCCEED();
    unsetenv("GUITAR_SPEAK_ASSETS_DIR");
}

TEST_CASE("Headless safety: AI feature off-path leaves audio chain unaffected",
          "[app][headless][integration]") {
    PluginProcessor p;
    juce::AudioProcessor::BusesLayout l;
    l.inputBuses.add(juce::AudioChannelSet::stereo());
    l.outputBuses.add(juce::AudioChannelSet::stereo());
    p.setBusesLayout(l);
    p.prepareToPlay(48000.0, 512);

    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    for (int i = 0; i < 10; ++i) p.processBlock(buf, midi);
    SUCCEED();
}
```

The `AssetLocator` must honor `GUITAR_SPEAK_ASSETS_DIR` (or your equivalent test hook) — if it doesn't yet, add a fallback there as part of this task.

- [ ] **Step 2: Run + commit**

```bash
./build/tests/guitar_dsp_tests "[app][headless]"
git add tests/integration/test_headless_safety_ai.cpp tests/CMakeLists.txt \
        src/app/AssetLocator.cpp
git commit -m "test(ai): headless safety — processor + processBlock survive missing model"
```

---

### Task 10.4: Standalone mic permission (Info.plist)

**Files:**
- Modify: top-level `CMakeLists.txt` (JUCE plugin/standalone Info.plist injection)

- [ ] **Step 1: Add `NSMicrophoneUsageDescription`**

Find the existing `juce_add_plugin(...)` block — add or extend:
```cmake
juce_add_plugin(GuitarSpeak
    ...
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT
      "Guitar Speak listens to your voice to power the conversational-AI feature."
)
```

(JUCE generates the matching Info.plist key automatically.)

- [ ] **Step 2: Build + verify**

```bash
cmake --build build --target GuitarSpeak_Standalone
grep NSMicrophoneUsageDescription \
  build/.../Guitar\ Speak.app/Contents/Info.plist
```

Expected: key + the description string appear.

- [ ] **Step 3: Commit**

```bash
git add CMakeLists.txt
git commit -m "build(standalone): add NSMicrophoneUsageDescription for AI mic capture"
```

---

## Phase 10 checkpoint — green.
