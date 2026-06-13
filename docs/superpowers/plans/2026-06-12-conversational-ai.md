# Conversational AI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Spec:** [2026-06-12-conversational-ai-design.md](../specs/2026-06-12-conversational-ai-design.md)

**Goal:** Add a push-to-talk **mic → STT → LLM → existing TTS→vocoder** pipeline that lets the guitar voice AI replies in both standalone and the Logic Pro AU.

**Architecture:** New `src/ai/` directory holds an orchestrator (`ConversationEngine`) plus mockable interfaces for STT (`ITranscriber` → `WhisperTranscriber`), LLM (`ILlmClient` → `AnthropicClient` + `OllamaClient`), and HTTP (`IHttpTransport` → `JuceHttpTransport`). Mic input flows through a new `MicCapture` component (sidechain bus in AU, `AudioDeviceManager` in standalone). All async stages run on one background worker thread; `processBlock` only does RT-safe FIFO pushes. State machine returns to Idle on every error path.

**Tech Stack:** C++20, JUCE 7, Catch2, whisper.cpp (FetchContent), Anthropic API (cloud), Ollama HTTP (local).

**Project conventions (read before starting):**
- Namespace: `guitar_dsp::ai` for new directory; existing audio code uses `guitar_dsp::audio`.
- Test framework: Catch2 v3, `catch2/catch_test_macros.hpp` (see [tests/unit/audio/test_tts_prewarmer.cpp](../../../tests/unit/audio/test_tts_prewarmer.cpp) as canonical example of fake-driven unit tests).
- All new `.cpp` files in `src/ai/` must be added to a new static library `guitar_dsp_ai` in [src/CMakeLists.txt](../../../src/CMakeLists.txt).
- All new test files must be added to `guitar_dsp_tests` in [tests/CMakeLists.txt](../../../tests/CMakeLists.txt).
- Commit after every passing task. No batched commits.
- Use `cmake --build build --target guitar_dsp_tests && ./build/tests/guitar_dsp_tests "[tag]"` to run a tagged subset.

---

## Phase 0 — Setup

### Task 0.1: Create directory scaffolding + CMake plumbing

**Files:**
- Create: `src/ai/.gitkeep`
- Create: `tests/unit/ai/.gitkeep`
- Create: `tests/fixtures/ai/.gitkeep`
- Modify: `src/CMakeLists.txt` (append new library target)
- Modify: `tests/CMakeLists.txt` (append empty list, will fill in later tasks)

- [ ] **Step 1: Create directories**

```bash
mkdir -p src/ai tests/unit/ai tests/fixtures/ai
touch src/ai/.gitkeep tests/unit/ai/.gitkeep tests/fixtures/ai/.gitkeep
```

- [ ] **Step 2: Add empty `guitar_dsp_ai` library to `src/CMakeLists.txt`**

Append after the `guitar_dsp_scenes` block:

```cmake
# AI module — STT, LLM clients, conversation orchestration. Pure C++ + JUCE.
add_library(guitar_dsp_ai STATIC
    ai/.gitkeep  # placeholder; replaced as files arrive
)
set_target_properties(guitar_dsp_ai PROPERTIES LINKER_LANGUAGE CXX)

target_include_directories(guitar_dsp_ai PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(guitar_dsp_ai PUBLIC cxx_std_20)
target_link_libraries(guitar_dsp_ai
    PUBLIC
        juce::juce_core
        juce::juce_events
)
```

Then change the placeholder source to a real empty file:

```bash
echo "// placeholder — will hold registration code in later tasks" > src/ai/AiModule.cpp
```

Replace `ai/.gitkeep` with `ai/AiModule.cpp` in the `add_library` call.

- [ ] **Step 3: Verify clean build**

```bash
cmake --build build --target guitar_dsp_ai
```

Expected: `[100%] Built target guitar_dsp_ai`.

- [ ] **Step 4: Commit**

```bash
git add src/ai/ tests/unit/ai/ tests/fixtures/ai/ src/CMakeLists.txt
git commit -m "feat(ai): scaffold src/ai library and test directories"
```

---

### Task 0.2: Add fixture assets

**Files:**
- Create: `tests/fixtures/ai/hello_world.wav` (16 kHz mono, ~1s "hello world")
- Create: `tests/fixtures/ai/silence.wav` (16 kHz mono, 1s zeros)
- Create: `tests/fixtures/ai/anthropic_response_200.json`
- Create: `tests/fixtures/ai/anthropic_response_401.json`
- Create: `tests/fixtures/ai/ollama_chat_response_200.json`
- Create: `tests/fixtures/ai/ollama_tags_response.json`

- [ ] **Step 1: Generate silence.wav with sox or ffmpeg**

```bash
ffmpeg -y -f lavfi -i "anullsrc=r=16000:cl=mono" -t 1 \
  tests/fixtures/ai/silence.wav
```

- [ ] **Step 2: Record / synthesize hello_world.wav**

For determinism, use `say` (macOS) + ffmpeg conversion:

```bash
say -v Alex "hello world" -o /tmp/hw.aiff
ffmpeg -y -i /tmp/hw.aiff -ar 16000 -ac 1 \
  tests/fixtures/ai/hello_world.wav
```

- [ ] **Step 3: Write fixture JSON files**

`tests/fixtures/ai/anthropic_response_200.json`:
```json
{
  "id": "msg_01",
  "type": "message",
  "role": "assistant",
  "content": [{"type": "text", "text": "I've been played by three guys."}],
  "model": "claude-haiku-4-5",
  "stop_reason": "end_turn",
  "usage": {"input_tokens": 12, "output_tokens": 8}
}
```

`tests/fixtures/ai/anthropic_response_401.json`:
```json
{"type": "error", "error": {"type": "authentication_error", "message": "invalid x-api-key"}}
```

`tests/fixtures/ai/ollama_chat_response_200.json`:
```json
{
  "model": "llama3.2:3b",
  "message": {"role": "assistant", "content": "I've been played by three guys."},
  "done": true,
  "total_duration": 1200000000
}
```

`tests/fixtures/ai/ollama_tags_response.json`:
```json
{"models": [
  {"name": "llama3.2:3b", "size": 2019393189},
  {"name": "qwen2.5:3b", "size": 1929393189}
]}
```

- [ ] **Step 4: Commit**

```bash
git add tests/fixtures/ai/
git commit -m "test(ai): add audio + JSON fixtures for AI tests"
```

---

## Phase 1 — Pure data layer

### Task 1.1: `ConversationBuffer`

**Files:**
- Create: `src/ai/ConversationBuffer.h`
- Create: `src/ai/ConversationBuffer.cpp`
- Create: `tests/unit/ai/test_conversation_buffer.cpp`

- [ ] **Step 1: Write the failing test**

`tests/unit/ai/test_conversation_buffer.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>
#include "ai/ConversationBuffer.h"

using guitar_dsp::ai::ConversationBuffer;
using guitar_dsp::ai::Message;

TEST_CASE("ConversationBuffer: append then snapshot returns messages in order",
          "[ai][buffer]") {
    ConversationBuffer b;
    b.append(Message::Role::User, "hi");
    b.append(Message::Role::Assistant, "hello");
    auto s = b.snapshot();
    REQUIRE(s.size() == 2);
    REQUIRE(s[0].role == Message::Role::User);
    REQUIRE(s[0].text == "hi");
    REQUIRE(s[1].role == Message::Role::Assistant);
    REQUIRE(s[1].text == "hello");
}

TEST_CASE("ConversationBuffer: truncates to last 10 messages",
          "[ai][buffer]") {
    ConversationBuffer b;
    for (int i = 0; i < 12; ++i)
        b.append(Message::Role::User, std::to_string(i));
    auto s = b.snapshot();
    REQUIRE(s.size() == 10);
    REQUIRE(s.front().text == "2");
    REQUIRE(s.back().text == "11");
}

TEST_CASE("ConversationBuffer: clear empties", "[ai][buffer]") {
    ConversationBuffer b;
    b.append(Message::Role::User, "hi");
    b.clear();
    REQUIRE(b.snapshot().empty());
}

TEST_CASE("ConversationBuffer: snapshot survives subsequent mutations",
          "[ai][buffer]") {
    ConversationBuffer b;
    b.append(Message::Role::User, "first");
    auto s = b.snapshot();
    b.append(Message::Role::Assistant, "second");
    REQUIRE(s.size() == 1);
    REQUIRE(s[0].text == "first");
}
```

Add `tests/unit/ai/test_conversation_buffer.cpp` to `guitar_dsp_tests` in `tests/CMakeLists.txt` and `guitar_dsp_ai` source list in `src/CMakeLists.txt` (`ai/ConversationBuffer.cpp`).

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build --target guitar_dsp_tests
```

Expected: compile error, `ai/ConversationBuffer.h: No such file`.

- [ ] **Step 3: Implement**

`src/ai/ConversationBuffer.h`:
```cpp
#pragma once
#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

namespace guitar_dsp::ai {

struct Message {
    enum class Role { User, Assistant };
    Role        role;
    std::string text;
};

class ConversationBuffer {
public:
    static constexpr std::size_t kMaxMessages = 10;

    void                 append(Message::Role role, std::string text);
    void                 clear();
    std::vector<Message> snapshot() const;

private:
    mutable std::mutex   mutex_;
    std::vector<Message> messages_;
};

} // namespace guitar_dsp::ai
```

`src/ai/ConversationBuffer.cpp`:
```cpp
#include "ai/ConversationBuffer.h"

namespace guitar_dsp::ai {

void ConversationBuffer::append(Message::Role role, std::string text) {
    std::lock_guard lk(mutex_);
    messages_.push_back({role, std::move(text)});
    if (messages_.size() > kMaxMessages)
        messages_.erase(messages_.begin(),
                        messages_.begin() + (messages_.size() - kMaxMessages));
}

void ConversationBuffer::clear() {
    std::lock_guard lk(mutex_);
    messages_.clear();
}

std::vector<Message> ConversationBuffer::snapshot() const {
    std::lock_guard lk(mutex_);
    return messages_;
}

} // namespace guitar_dsp::ai
```

- [ ] **Step 4: Run tests**

```bash
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests "[ai][buffer]"
```

Expected: 4 assertions pass.

- [ ] **Step 5: Commit**

```bash
git add src/ai/ConversationBuffer.{h,cpp} tests/unit/ai/test_conversation_buffer.cpp \
        src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(ai): add ConversationBuffer with last-10-message truncation"
```

---

### Task 1.2: `PersonaRegistry`

**Files:**
- Create: `src/ai/PersonaRegistry.h`
- Create: `src/ai/PersonaRegistry.cpp`
- Create: `tests/unit/ai/test_persona_registry.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "ai/ConversationBuffer.h"
#include "ai/PersonaRegistry.h"

using guitar_dsp::ai::ConversationBuffer;
using guitar_dsp::ai::Message;
using guitar_dsp::ai::PersonaId;
using guitar_dsp::ai::PersonaRegistry;

TEST_CASE("PersonaRegistry: every preset has a non-empty default with guardrail",
          "[ai][persona]") {
    for (auto id : { PersonaId::Interviewer, PersonaId::Snarky,
                     PersonaId::WeatheredGuitar, PersonaId::StudioEngineer,
                     PersonaId::CuriousAi, PersonaId::PlainAssistant }) {
        auto p = PersonaRegistry::defaultPromptFor(id);
        REQUIRE_FALSE(p.empty());
        REQUIRE(p.find("25 words") != std::string::npos);
        REQUIRE(p.find("No lists") != std::string::npos);
    }
}

TEST_CASE("PersonaRegistry: setCustomPrompt persists per-persona",
          "[ai][persona]") {
    PersonaRegistry r;
    r.setCustomPrompt(PersonaId::Snarky, "be especially snarky");
    REQUIRE(r.promptFor(PersonaId::Snarky) == "be especially snarky");
    REQUIRE(r.promptFor(PersonaId::Interviewer)
            == PersonaRegistry::defaultPromptFor(PersonaId::Interviewer));
}

TEST_CASE("PersonaRegistry: resetToDefault restores hardcoded text",
          "[ai][persona]") {
    PersonaRegistry r;
    r.setCustomPrompt(PersonaId::Interviewer, "edited");
    r.resetToDefault(PersonaId::Interviewer);
    REQUIRE(r.promptFor(PersonaId::Interviewer)
            == PersonaRegistry::defaultPromptFor(PersonaId::Interviewer));
}

TEST_CASE("PersonaRegistry: buildMessages puts system first, then history",
          "[ai][persona]") {
    PersonaRegistry r;
    ConversationBuffer b;
    b.append(Message::Role::User, "hi");
    b.append(Message::Role::Assistant, "hello");
    auto msgs = r.buildMessages(b, PersonaId::Interviewer);
    REQUIRE(msgs.size() == 3);  // system + 2 turns
    REQUIRE(msgs[0].role == Message::Role::System);
    REQUIRE(msgs[1].role == Message::Role::User);
    REQUIRE(msgs[1].text == "hi");
    REQUIRE(msgs[2].role == Message::Role::Assistant);
    REQUIRE(msgs[2].text == "hello");
}
```

Note: the test uses `Message::Role::System`. Add it to the enum in this task.

Update `Message` enum in `ConversationBuffer.h`:
```cpp
enum class Role { System, User, Assistant };
```

- [ ] **Step 2: Verify failure**

```bash
cmake --build build --target guitar_dsp_tests
```
Expected: `PersonaRegistry.h` not found.

- [ ] **Step 3: Implement**

`src/ai/PersonaRegistry.h`:
```cpp
#pragma once
#include "ai/ConversationBuffer.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace guitar_dsp::ai {

enum class PersonaId {
    Interviewer = 0,
    Snarky,
    WeatheredGuitar,
    StudioEngineer,
    CuriousAi,
    PlainAssistant,
};

class PersonaRegistry {
public:
    static std::string defaultPromptFor(PersonaId);

    std::string promptFor(PersonaId) const;
    void        setCustomPrompt(PersonaId, std::string);
    void        resetToDefault(PersonaId);

    std::vector<Message> buildMessages(const ConversationBuffer&,
                                       PersonaId) const;

private:
    std::unordered_map<int, std::string> overrides_;
};

} // namespace guitar_dsp::ai
```

`src/ai/PersonaRegistry.cpp` — implement `defaultPromptFor` using prompts from Appendix A of the spec. Example for Interviewer:
```cpp
case PersonaId::Interviewer:
    return "You are an interviewer speaking through a guitar. "
           "The person in front of you is a guitarist who is about to play. "
           "Ask short, curious questions about the music, the player's history, "
           "and what they're feeling right now. Be warm but efficient. "
           "Reply in 1–2 sentences, max 25 words. No lists.";
```

Build remaining methods:
```cpp
std::string PersonaRegistry::promptFor(PersonaId id) const {
    auto it = overrides_.find(static_cast<int>(id));
    return it != overrides_.end() ? it->second : defaultPromptFor(id);
}

void PersonaRegistry::setCustomPrompt(PersonaId id, std::string p) {
    overrides_[static_cast<int>(id)] = std::move(p);
}

void PersonaRegistry::resetToDefault(PersonaId id) {
    overrides_.erase(static_cast<int>(id));
}

std::vector<Message> PersonaRegistry::buildMessages(
    const ConversationBuffer& buf, PersonaId id) const {
    std::vector<Message> out;
    out.push_back({Message::Role::System, promptFor(id)});
    for (auto& m : buf.snapshot()) out.push_back(m);
    return out;
}
```

- [ ] **Step 4: Run tests**

```bash
./build/tests/guitar_dsp_tests "[ai][persona]"
```

Expected: all assertions pass.

- [ ] **Step 5: Commit**

```bash
git add src/ai/PersonaRegistry.{h,cpp} src/ai/ConversationBuffer.h \
        tests/unit/ai/test_persona_registry.cpp \
        src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(ai): add PersonaRegistry with six presets + buildMessages"
```

---

### Task 1.3: `PluginState` extension + back-compat fixture test

**Files:**
- Modify: `src/app/PluginState.h`
- Modify: `src/app/PluginState.cpp`
- Modify: `tests/unit/app/test_plugin_state.cpp`
- Create: `tests/fixtures/ai/legacy_state_9734848.xml` (binary fixture of pre-AI state)

- [ ] **Step 1: Capture the current state format as a fixture**

```bash
git show 9734848:tests/fixtures/scenes/state.xml > \
  tests/fixtures/ai/legacy_state_9734848.xml 2>/dev/null || true
```

If that file doesn't exist in 9734848, instead serialize current `PluginState` to XML via a one-shot helper test (committed temporarily, then removed). Easier: load the current `PluginState` defaults to an XML string and pin it as the fixture by running this in a scratch test:

```cpp
TEST_CASE("Capture current state fixture", "[.capture]") {
    guitar_dsp::PluginState s;  // defaults
    auto xml = s.toXml();
    juce::File("tests/fixtures/ai/legacy_state_9734848.xml").replaceWithText(xml);
}
```

Run with `./build/tests/guitar_dsp_tests "[.capture]"` once, then delete the test.

- [ ] **Step 2: Write the failing test for the extended fields**

Append to `tests/unit/app/test_plugin_state.cpp`:

```cpp
TEST_CASE("PluginState: new AI fields have correct defaults",
          "[app][state][ai]") {
    guitar_dsp::PluginState s;
    REQUIRE(s.selectedModelId == "claude-haiku-4-5");
    REQUIRE(s.personaId == guitar_dsp::ai::PersonaId::Interviewer);
    REQUIRE(s.customPromptByPersona.empty());
    REQUIRE(s.maxSentences == 2);
    REQUIRE(s.maxWords == 25);
    REQUIRE(s.sttModelId == "whisper-base.en");
    REQUIRE(s.pttPedalId == 9);
    REQUIRE(s.clearChatPedalId == 10);
}

TEST_CASE("PluginState: legacy XML loads with defaults for new fields",
          "[app][state][ai]") {
    auto xml = juce::File("tests/fixtures/ai/legacy_state_9734848.xml")
                 .loadFileAsString();
    guitar_dsp::PluginState s = guitar_dsp::PluginState::fromXml(xml.toStdString());
    REQUIRE(s.personaId == guitar_dsp::ai::PersonaId::Interviewer);
    REQUIRE(s.selectedModelId == "claude-haiku-4-5");
}

TEST_CASE("PluginState: round-trip preserves AI fields",
          "[app][state][ai]") {
    guitar_dsp::PluginState s;
    s.personaId = guitar_dsp::ai::PersonaId::Snarky;
    s.customPromptByPersona[guitar_dsp::ai::PersonaId::Snarky]
        = "be brutal, but witty";
    s.selectedModelId = "ollama:llama3.2:3b";
    s.maxWords = 30;
    auto xml = s.toXml();
    auto s2  = guitar_dsp::PluginState::fromXml(xml);
    REQUIRE(s2.personaId == guitar_dsp::ai::PersonaId::Snarky);
    REQUIRE(s2.customPromptByPersona.at(guitar_dsp::ai::PersonaId::Snarky)
            == "be brutal, but witty");
    REQUIRE(s2.selectedModelId == "ollama:llama3.2:3b");
    REQUIRE(s2.maxWords == 30);
}
```

- [ ] **Step 3: Extend `PluginState`**

In `src/app/PluginState.h`, add:
```cpp
#include "ai/PersonaRegistry.h"
#include <map>

struct PluginState {
    // ... existing fields ...
    std::string                                selectedModelId = "claude-haiku-4-5";
    ai::PersonaId                              personaId       = ai::PersonaId::Interviewer;
    std::map<ai::PersonaId, std::string>       customPromptByPersona;
    int                                        maxSentences    = 2;
    int                                        maxWords        = 25;
    std::string                                sttModelId      = "whisper-base.en";
    int                                        pttPedalId      = 9;
    int                                        clearChatPedalId = 10;
};
```

In `PluginState.cpp`, extend `toXml` / `fromXml` to read+write the new attributes. Use `XmlElement::getStringAttribute(name, defaultValue)` so missing attributes load as defaults — that's what makes the legacy file pass.

- [ ] **Step 4: Run tests**

```bash
./build/tests/guitar_dsp_tests "[app][state]"
```

Expected: all pass including existing state tests.

- [ ] **Step 5: Commit**

```bash
git add src/app/PluginState.{h,cpp} tests/unit/app/test_plugin_state.cpp \
        tests/fixtures/ai/legacy_state_9734848.xml
git commit -m "feat(state): extend PluginState with AI fields; backward-compatible"
```

---

### Task 1.4: `AppPreferences` (PropertiesFile wrapper for API key)

**Files:**
- Create: `src/ai/AppPreferences.h`
- Create: `src/ai/AppPreferences.cpp`
- Create: `tests/unit/ai/test_app_preferences.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "ai/AppPreferences.h"
#include <juce_core/juce_core.h>

using guitar_dsp::ai::AppPreferences;

TEST_CASE("AppPreferences: round-trip API key", "[ai][prefs]") {
    auto tmp = juce::File::getSpecialLocation(
                  juce::File::tempDirectory).getChildFile("prefs_test.xml");
    tmp.deleteFile();
    {
        AppPreferences p{tmp};
        p.setAnthropicApiKey("sk-ant-test-xyz");
        REQUIRE(p.anthropicApiKey() == "sk-ant-test-xyz");
    }
    AppPreferences p2{tmp};
    REQUIRE(p2.anthropicApiKey() == "sk-ant-test-xyz");
    tmp.deleteFile();
}

TEST_CASE("AppPreferences: env-var fallback when no file value",
          "[ai][prefs]") {
    auto tmp = juce::File::getSpecialLocation(
                  juce::File::tempDirectory).getChildFile("prefs_env.xml");
    tmp.deleteFile();
    setenv("ANTHROPIC_API_KEY", "sk-ant-from-env", 1);
    AppPreferences p{tmp};
    REQUIRE(p.anthropicApiKey() == "sk-ant-from-env");
    unsetenv("ANTHROPIC_API_KEY");
}

TEST_CASE("AppPreferences: stored key wins over env-var",
          "[ai][prefs]") {
    auto tmp = juce::File::getSpecialLocation(
                  juce::File::tempDirectory).getChildFile("prefs_both.xml");
    tmp.deleteFile();
    {
        AppPreferences p{tmp};
        p.setAnthropicApiKey("sk-ant-from-file");
    }
    setenv("ANTHROPIC_API_KEY", "sk-ant-from-env", 1);
    AppPreferences p2{tmp};
    REQUIRE(p2.anthropicApiKey() == "sk-ant-from-file");
    unsetenv("ANTHROPIC_API_KEY");
    tmp.deleteFile();
}
```

- [ ] **Step 2: Implement**

`src/ai/AppPreferences.h`:
```cpp
#pragma once
#include <juce_core/juce_core.h>
#include <string>

namespace guitar_dsp::ai {

class AppPreferences {
public:
    explicit AppPreferences(juce::File path);

    std::string anthropicApiKey() const;        // file > env-var > ""
    void        setAnthropicApiKey(std::string);

    std::string ollamaEndpoint() const;         // default "http://localhost:11434"
    void        setOllamaEndpoint(std::string);

    bool        cannedFallbackOnLlmError() const;
    void        setCannedFallbackOnLlmError(bool);

    static juce::File defaultPath();            // ~/Library/.../settings.xml

private:
    void load();
    void save() const;

    juce::File   path_;
    juce::XmlElement xml_ {"GuitarSpeakPrefs"};
};

} // namespace guitar_dsp::ai
```

`src/ai/AppPreferences.cpp`:
```cpp
#include "ai/AppPreferences.h"
#include <cstdlib>

namespace guitar_dsp::ai {

AppPreferences::AppPreferences(juce::File path) : path_(std::move(path)) {
    load();
}

void AppPreferences::load() {
    if (! path_.existsAsFile()) return;
    if (auto parsed = juce::parseXML(path_)) xml_ = *parsed;
}

void AppPreferences::save() const {
    path_.getParentDirectory().createDirectory();
    xml_.writeTo(path_);
}

std::string AppPreferences::anthropicApiKey() const {
    auto stored = xml_.getStringAttribute("anthropic_api_key", "").toStdString();
    if (! stored.empty()) return stored;
    if (const char* env = std::getenv("ANTHROPIC_API_KEY")) return env;
    return "";
}

void AppPreferences::setAnthropicApiKey(std::string k) {
    xml_.setAttribute("anthropic_api_key", juce::String(k));
    save();
}

std::string AppPreferences::ollamaEndpoint() const {
    return xml_.getStringAttribute("ollama_endpoint", "http://localhost:11434")
              .toStdString();
}

void AppPreferences::setOllamaEndpoint(std::string e) {
    xml_.setAttribute("ollama_endpoint", juce::String(e));
    save();
}

bool AppPreferences::cannedFallbackOnLlmError() const {
    return xml_.getBoolAttribute("canned_fallback_on_llm_error", false);
}

void AppPreferences::setCannedFallbackOnLlmError(bool b) {
    xml_.setAttribute("canned_fallback_on_llm_error", b);
    save();
}

juce::File AppPreferences::defaultPath() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
             .getChildFile("Todd B Fisher/Guitar Speak/settings.xml");
}

} // namespace guitar_dsp::ai
```

- [ ] **Step 3: Run tests**

```bash
./build/tests/guitar_dsp_tests "[ai][prefs]"
```

- [ ] **Step 4: Commit**

```bash
git add src/ai/AppPreferences.{h,cpp} tests/unit/ai/test_app_preferences.cpp \
        src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(ai): add AppPreferences with API-key persistence + env-var fallback"
```

---

## Phase 1 checkpoint

Run the full suite:
```bash
cmake --build build --target guitar_dsp_tests
./build/tests/guitar_dsp_tests
```

All tests green. Pure-data layer is complete and fully unit-testable.

---

## Phase 2 — HTTP transport

### Task 2.1: `IHttpTransport` + `FakeHttpTransport`

**Files:**
- Create: `src/ai/IHttpTransport.h`
- Create: `tests/unit/ai/FakeHttpTransport.h` (test helper, header-only)

- [ ] **Step 1: Write `IHttpTransport.h`**

```cpp
#pragma once
#include <chrono>
#include <map>
#include <string>

namespace guitar_dsp::ai {

struct HttpResponse {
    int         status = 0;     // 0 = transport error (no HTTP response)
    std::string body;
    std::string error;          // populated when status==0 or parse failed
    std::map<std::string,std::string> headers;
};

class CancellationToken;

class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    virtual HttpResponse post(const std::string& url,
                              const std::map<std::string,std::string>& headers,
                              const std::string& body,
                              std::chrono::milliseconds timeout,
                              CancellationToken* cancel = nullptr) = 0;
    virtual HttpResponse get (const std::string& url,
                              std::chrono::milliseconds timeout,
                              CancellationToken* cancel = nullptr) = 0;
};

} // namespace guitar_dsp::ai
```

- [ ] **Step 2: Write `FakeHttpTransport.h` (test helper)**

```cpp
#pragma once
#include "ai/IHttpTransport.h"
#include <queue>
#include <vector>

namespace guitar_dsp::ai::test {

struct RecordedCall {
    std::string method;     // "POST" or "GET"
    std::string url;
    std::map<std::string,std::string> headers;
    std::string body;
};

class FakeHttpTransport : public IHttpTransport {
public:
    std::vector<RecordedCall>   calls;
    std::queue<HttpResponse>    replies;

    HttpResponse post(const std::string& url,
                      const std::map<std::string,std::string>& h,
                      const std::string& body,
                      std::chrono::milliseconds,
                      CancellationToken*) override {
        calls.push_back({"POST", url, h, body});
        return nextReply();
    }
    HttpResponse get(const std::string& url,
                     std::chrono::milliseconds,
                     CancellationToken*) override {
        calls.push_back({"GET", url, {}, ""});
        return nextReply();
    }

private:
    HttpResponse nextReply() {
        if (replies.empty()) return HttpResponse{0, "", "no scripted reply", {}};
        auto r = replies.front(); replies.pop();
        return r;
    }
};

} // namespace guitar_dsp::ai::test
```

- [ ] **Step 3: Verify it compiles**

```bash
cmake --build build --target guitar_dsp_tests
```

No tests yet — just compilation. Header inclusion will be exercised by the LLM client tests.

- [ ] **Step 4: Commit**

```bash
git add src/ai/IHttpTransport.h tests/unit/ai/FakeHttpTransport.h \
        src/CMakeLists.txt
git commit -m "feat(ai): add IHttpTransport seam + FakeHttpTransport test helper"
```

---

### Task 2.2: `CancellationToken`

**Files:**
- Create: `src/ai/CancellationToken.h`
- Create: `src/ai/CancellationToken.cpp`
- Create: `tests/unit/ai/test_cancellation_token.cpp`

- [ ] **Step 1: Test**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "ai/CancellationToken.h"

using guitar_dsp::ai::CancellationToken;

TEST_CASE("CancellationToken: cancel makes isCancelled true",
          "[ai][cancel]") {
    CancellationToken t;
    REQUIRE_FALSE(t.isCancelled());
    t.cancel();
    REQUIRE(t.isCancelled());
}

TEST_CASE("CancellationToken: reset clears", "[ai][cancel]") {
    CancellationToken t;
    t.cancel();
    t.reset();
    REQUIRE_FALSE(t.isCancelled());
}
```

- [ ] **Step 2: Implement**

```cpp
// src/ai/CancellationToken.h
#pragma once
#include <atomic>
namespace guitar_dsp::ai {
class CancellationToken {
public:
    void cancel() noexcept       { flag_.store(true, std::memory_order_release); }
    void reset() noexcept        { flag_.store(false, std::memory_order_release); }
    bool isCancelled() const noexcept { return flag_.load(std::memory_order_acquire); }
private:
    std::atomic<bool> flag_{false};
};
} // namespace
```

`CancellationToken.cpp` is empty (header-only impl); skip the .cpp file. Update `src/CMakeLists.txt` accordingly (do not add a .cpp).

- [ ] **Step 3: Run tests + commit**

```bash
./build/tests/guitar_dsp_tests "[ai][cancel]"
git add src/ai/CancellationToken.h tests/unit/ai/test_cancellation_token.cpp \
        tests/CMakeLists.txt
git commit -m "feat(ai): add CancellationToken atomic flag"
```

---

### Task 2.3: `JuceHttpTransport`

**Files:**
- Create: `src/ai/JuceHttpTransport.h`
- Create: `src/ai/JuceHttpTransport.cpp`
- Create: `tests/unit/ai/test_juce_http_transport.cpp`

- [ ] **Step 1: Test (smoke + cancellation only — no real network)**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "ai/JuceHttpTransport.h"
#include "ai/CancellationToken.h"
#include <thread>

using guitar_dsp::ai::JuceHttpTransport;
using guitar_dsp::ai::CancellationToken;

TEST_CASE("JuceHttpTransport: invalid URL returns transport error, no crash",
          "[ai][http]") {
    JuceHttpTransport t;
    auto r = t.get("http://invalid.invalid.invalid:1/", std::chrono::seconds{1});
    REQUIRE(r.status == 0);
    REQUIRE_FALSE(r.error.empty());
}

TEST_CASE("JuceHttpTransport: pre-cancelled token returns immediately",
          "[ai][http]") {
    JuceHttpTransport t;
    CancellationToken c;
    c.cancel();
    auto r = t.get("http://example.com/", std::chrono::seconds{10}, &c);
    REQUIRE(r.status == 0);
    REQUIRE(r.error == "cancelled");
}
```

- [ ] **Step 2: Implement using `juce::URL` + `WebInputStream`**

`src/ai/JuceHttpTransport.h`:
```cpp
#pragma once
#include "ai/IHttpTransport.h"
namespace guitar_dsp::ai {
class JuceHttpTransport : public IHttpTransport {
public:
    HttpResponse post(const std::string& url,
                      const std::map<std::string,std::string>& headers,
                      const std::string& body,
                      std::chrono::milliseconds timeout,
                      CancellationToken* cancel) override;
    HttpResponse get (const std::string& url,
                      std::chrono::milliseconds timeout,
                      CancellationToken* cancel) override;
};
} // namespace
```

`src/ai/JuceHttpTransport.cpp`:
```cpp
#include "ai/JuceHttpTransport.h"
#include "ai/CancellationToken.h"
#include <juce_core/juce_core.h>

namespace guitar_dsp::ai {

namespace {
HttpResponse run(const juce::URL& url, bool post,
                 const std::map<std::string,std::string>& headers,
                 std::chrono::milliseconds timeout,
                 CancellationToken* cancel) {
    if (cancel && cancel->isCancelled())
        return {0, "", "cancelled", {}};

    juce::StringPairArray respHeaders;
    int statusCode = 0;
    juce::String allHeaders;
    for (auto& [k, v] : headers) allHeaders << juce::String(k) << ": " << juce::String(v) << "\r\n";

    auto opts = juce::URL::InputStreamOptions(
                    post ? juce::URL::ParameterHandling::inPostData
                         : juce::URL::ParameterHandling::inAddress)
                  .withExtraHeaders(allHeaders)
                  .withConnectionTimeoutMs(static_cast<int>(timeout.count()))
                  .withResponseHeaders(&respHeaders)
                  .withStatusCode(&statusCode);

    auto stream = url.createInputStream(opts);
    if (! stream) return {0, "", "connect failed", {}};

    juce::MemoryBlock buf;
    while (! stream->isExhausted()) {
        if (cancel && cancel->isCancelled()) return {0, "", "cancelled", {}};
        char chunk[4096];
        auto n = stream->read(chunk, sizeof(chunk));
        if (n <= 0) break;
        buf.append(chunk, static_cast<size_t>(n));
    }

    std::map<std::string,std::string> hOut;
    for (auto& k : respHeaders.getAllKeys())
        hOut[k.toStdString()] = respHeaders[k].toStdString();

    return {statusCode, std::string(static_cast<const char*>(buf.getData()), buf.getSize()),
            "", hOut};
}
}

HttpResponse JuceHttpTransport::get(const std::string& u,
                                    std::chrono::milliseconds t,
                                    CancellationToken* c) {
    juce::URL url{juce::String(u)};
    return run(url, false, {}, t, c);
}

HttpResponse JuceHttpTransport::post(const std::string& u,
                                     const std::map<std::string,std::string>& h,
                                     const std::string& body,
                                     std::chrono::milliseconds t,
                                     CancellationToken* c) {
    juce::URL url = juce::URL{juce::String(u)}
                      .withPOSTData(juce::String(body));
    return run(url, true, h, t, c);
}

} // namespace
```

- [ ] **Step 3: Run + commit**

```bash
./build/tests/guitar_dsp_tests "[ai][http]"
git add src/ai/JuceHttpTransport.{h,cpp} tests/unit/ai/test_juce_http_transport.cpp \
        src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(ai): add JuceHttpTransport with cancellation support"
```

---

## Phase 2 checkpoint — green.

---

The plan continues in subsequent files for Phases 3–11. To keep this document
manageable, the remaining phases (LLM clients, STT, mic capture, orchestrator,
UI, wiring, pedals, polish, docs) are written as task-by-task sections in:

- [2026-06-12-conversational-ai-phase3-llm.md](2026-06-12-conversational-ai-phase3-llm.md)
- [2026-06-12-conversational-ai-phase4-stt.md](2026-06-12-conversational-ai-phase4-stt.md)
- [2026-06-12-conversational-ai-phase5-mic.md](2026-06-12-conversational-ai-phase5-mic.md)
- [2026-06-12-conversational-ai-phase6-engine.md](2026-06-12-conversational-ai-phase6-engine.md)
- [2026-06-12-conversational-ai-phase7-ui.md](2026-06-12-conversational-ai-phase7-ui.md)
- [2026-06-12-conversational-ai-phase8-wiring.md](2026-06-12-conversational-ai-phase8-wiring.md)
- [2026-06-12-conversational-ai-phase9-pedals.md](2026-06-12-conversational-ai-phase9-pedals.md)
- [2026-06-12-conversational-ai-phase10-polish.md](2026-06-12-conversational-ai-phase10-polish.md)
- [2026-06-12-conversational-ai-phase11-docs.md](2026-06-12-conversational-ai-phase11-docs.md)

Each follow-on file is self-contained: it lists files-to-touch, has full
test code and full implementation code per task, and ends with a "Phase N
checkpoint — green" line. Read them in order.
