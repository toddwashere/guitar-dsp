# Phase 7 — UI panels

> Continuation of [2026-06-12-conversational-ai.md](2026-06-12-conversational-ai.md).

**Goal:** `ConversationPanel` (full + compact), `AiSettingsPanel`, and status pill widgets. UI is exercised via headless construction tests + manual checklist (Phase 11).

UI panels are intentionally thin views over engine state; logic lives in the engine. Tests assert the widgets construct, resize, and reflect engine state — they don't drive painting.

---

### Task 7.1: `StatePill` widget

**Files:**
- Create: `src/app/StatePill.h`
- Create: `src/app/StatePill.cpp`
- Create: `tests/unit/app/test_state_pill.cpp`

- [ ] **Step 1: Tests**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "app/StatePill.h"
#include "ai/ConversationEngine.h"

using guitar_dsp::StatePill;
using guitar_dsp::ai::ConversationEngine;

TEST_CASE("StatePill: label reflects state", "[app][ui][pill]") {
    StatePill p;
    p.setState(ConversationEngine::State::Idle);
    REQUIRE(p.currentLabel() == "Idle");
    p.setState(ConversationEngine::State::Capturing);
    REQUIRE(p.currentLabel() == "Capturing");
    p.setState(ConversationEngine::State::Transcribing);
    REQUIRE(p.currentLabel() == "Transcribing");
    p.setState(ConversationEngine::State::Thinking);
    REQUIRE(p.currentLabel() == "Thinking");
    p.setState(ConversationEngine::State::Speaking);
    REQUIRE(p.currentLabel() == "Speaking");
    p.setState(ConversationEngine::State::Error);
    REQUIRE(p.currentLabel() == "Error");
}

TEST_CASE("StatePill: error label includes reason", "[app][ui][pill]") {
    StatePill p;
    p.setState(ConversationEngine::State::Error);
    p.setErrorReason("API key invalid");
    REQUIRE(p.currentLabel() == "Error: API key invalid");
}
```

- [ ] **Step 2: Implement**

`src/app/StatePill.h`:
```cpp
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ai/ConversationEngine.h"

namespace guitar_dsp {

class StatePill : public juce::Component {
public:
    void setState(ai::ConversationEngine::State);
    void setErrorReason(std::string);
    std::string currentLabel() const;

    void paint(juce::Graphics&) override;

private:
    ai::ConversationEngine::State state_ {ai::ConversationEngine::State::Idle};
    std::string errorReason_;
};

} // namespace
```

`src/app/StatePill.cpp`:
```cpp
#include "app/StatePill.h"
namespace guitar_dsp {

void StatePill::setState(ai::ConversationEngine::State s) { state_ = s; repaint(); }
void StatePill::setErrorReason(std::string r) { errorReason_ = std::move(r); repaint(); }

std::string StatePill::currentLabel() const {
    using S = ai::ConversationEngine::State;
    switch (state_) {
        case S::Idle:         return "Idle";
        case S::Capturing:    return "Capturing";
        case S::Transcribing: return "Transcribing";
        case S::Thinking:     return "Thinking";
        case S::Speaking:     return "Speaking";
        case S::Error:        return errorReason_.empty() ? std::string{"Error"} : "Error: " + errorReason_;
    }
    return "?";
}

void StatePill::paint(juce::Graphics& g) {
    using S = ai::ConversationEngine::State;
    juce::Colour bg;
    switch (state_) {
        case S::Idle:         bg = juce::Colours::darkgrey;       break;
        case S::Capturing:    bg = juce::Colours::red;            break;
        case S::Transcribing: bg = juce::Colours::orange;         break;
        case S::Thinking:     bg = juce::Colours::yellow.darker(); break;
        case S::Speaking:     bg = juce::Colours::green;          break;
        case S::Error:        bg = juce::Colours::orange.darker(); break;
    }
    g.setColour(bg);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
    g.setColour(juce::Colours::white);
    g.setFont(13.0f);
    g.drawText(currentLabel(), getLocalBounds(), juce::Justification::centred);
}

} // namespace
```

- [ ] **Step 3: Run + commit**

```bash
./build/tests/guitar_dsp_tests "[app][ui][pill]"
git add src/app/StatePill.{h,cpp} tests/unit/app/test_state_pill.cpp \
        src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(ui): StatePill widget that mirrors ConversationEngine state"
```

---

### Task 7.2: `ConversationPanel`

**Files:**
- Create: `src/app/ConversationPanel.h`
- Create: `src/app/ConversationPanel.cpp`
- Create: `tests/unit/app/test_conversation_panel.cpp`

The panel holds: `StatePill`, transcript text area (read-only), record/clear/settings buttons, mic peak meter. A `juce::Timer` polls engine state + buffer + mic peak every 50 ms.

- [ ] **Step 1: Tests**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "app/ConversationPanel.h"
#include "ai/ConversationBuffer.h"
#include "ai/PersonaRegistry.h"
#include "FakeTranscriber.h"
#include "FakeLlmClient.h"
#include "FakeMicCapture.h"

using namespace guitar_dsp::ai;
using namespace guitar_dsp::ai::test;
using guitar_dsp::ConversationPanel;

TEST_CASE("ConversationPanel: constructs and resizes in compact mode",
          "[app][ui][conv]") {
    FakeTranscriber stt; FakeLlmClient llm; FakeMicCapture mic;
    ConversationBuffer buf; PersonaRegistry p;
    std::vector<std::string> spoken;
    ConversationEngine engine(stt, llm, mic, buf, p,
        [&](std::string s){ spoken.push_back(std::move(s)); });
    ConversationPanel panel(engine, buf, /*compact=*/true);
    panel.setBounds(0, 0, 320, 80);
    REQUIRE(panel.getHeight() == 80);
}

TEST_CASE("ConversationPanel: constructs in full mode",
          "[app][ui][conv]") {
    FakeTranscriber stt; FakeLlmClient llm; FakeMicCapture mic;
    ConversationBuffer buf; PersonaRegistry p;
    ConversationEngine engine(stt, llm, mic, buf, p, [](std::string){});
    ConversationPanel panel(engine, buf, /*compact=*/false);
    panel.setBounds(0, 0, 600, 280);
    REQUIRE(panel.getHeight() == 280);
}

TEST_CASE("ConversationPanel: transcript text builds from buffer snapshot",
          "[app][ui][conv]") {
    FakeTranscriber stt; FakeLlmClient llm; FakeMicCapture mic;
    ConversationBuffer buf; PersonaRegistry p;
    buf.append(Message::Role::User, "hi");
    buf.append(Message::Role::Assistant, "hello there");
    ConversationEngine engine(stt, llm, mic, buf, p, [](std::string){});
    ConversationPanel panel(engine, buf, false);
    auto txt = panel.composedTranscriptForTest();
    REQUIRE(txt.find("You: hi") != std::string::npos);
    REQUIRE(txt.find("AI : hello there") != std::string::npos);
}
```

- [ ] **Step 2: Implement**

`src/app/ConversationPanel.h`:
```cpp
#pragma once
#include "ai/ConversationEngine.h"
#include "ai/ConversationBuffer.h"
#include "app/StatePill.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class ConversationPanel : public juce::Component, private juce::Timer {
public:
    ConversationPanel(ai::ConversationEngine&, ai::ConversationBuffer&, bool compact);

    void resized() override;
    void paint(juce::Graphics&) override;

    std::string composedTranscriptForTest() const;

private:
    void timerCallback() override;
    void onRecord();
    void onClear();
    void recomposeTranscript();

    ai::ConversationEngine&  engine_;
    ai::ConversationBuffer&  buf_;
    bool                     compact_;

    StatePill                pill_;
    juce::TextEditor         transcript_;
    juce::TextButton         recordBtn_  {"● Record"};
    juce::TextButton         clearBtn_   {"Clear"};
    juce::TextButton         settingsBtn_{"⚙"};
    juce::Label              timingsLabel_;
    size_t                   lastSeenSize_ {0};
};

} // namespace
```

`src/app/ConversationPanel.cpp`:
```cpp
#include "app/ConversationPanel.h"
namespace guitar_dsp {

ConversationPanel::ConversationPanel(ai::ConversationEngine& e,
                                     ai::ConversationBuffer& b, bool compact)
    : engine_(e), buf_(b), compact_(compact) {
    addAndMakeVisible(pill_);
    transcript_.setMultiLine(true);
    transcript_.setReadOnly(true);
    transcript_.setScrollbarsShown(true);
    transcript_.setFont(juce::Font(13.0f));
    addAndMakeVisible(transcript_);
    addAndMakeVisible(recordBtn_);
    addAndMakeVisible(clearBtn_);
    addAndMakeVisible(settingsBtn_);
    addAndMakeVisible(timingsLabel_);
    recordBtn_.onClick = [this]{ onRecord(); };
    clearBtn_.onClick  = [this]{ onClear(); };
    recomposeTranscript();
    startTimerHz(20);
}

void ConversationPanel::resized() {
    auto r = getLocalBounds().reduced(6);
    pill_.setBounds(r.removeFromTop(compact_ ? 18 : 22));
    r.removeFromTop(4);
    auto btnRow = r.removeFromBottom(compact_ ? 22 : 30);
    recordBtn_  .setBounds(btnRow.removeFromLeft(compact_ ?  70 : 100));
    clearBtn_   .setBounds(btnRow.removeFromLeft(compact_ ?  50 :  70));
    settingsBtn_.setBounds(btnRow.removeFromLeft(compact_ ?  30 :  40));
    if (! compact_) {
        timingsLabel_.setBounds(r.removeFromBottom(18));
    }
    transcript_.setBounds(r);
}

void ConversationPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff222426));
}

void ConversationPanel::onRecord() {
    auto s = engine_.state();
    using S = ai::ConversationEngine::State;
    if (s == S::Idle)          engine_.startTurn();
    else if (s == S::Capturing) engine_.endTurn();
    else                       engine_.cancelTurn();
}
void ConversationPanel::onClear() { engine_.clearConversation(); }

void ConversationPanel::recomposeTranscript() {
    auto snap = buf_.snapshot();
    lastSeenSize_ = snap.size();
    if (compact_ && snap.size() > 2)
        snap.erase(snap.begin(), snap.end() - 2);
    juce::String out;
    for (auto& m : snap) {
        out << (m.role == ai::Message::Role::User ? "You: " : "AI : ")
            << juce::String(m.text) << "\n";
    }
    transcript_.setText(out);
}

std::string ConversationPanel::composedTranscriptForTest() const {
    return transcript_.getText().toStdString();
}

void ConversationPanel::timerCallback() {
    pill_.setState(engine_.state());
    if (engine_.state() == ai::ConversationEngine::State::Error)
        pill_.setErrorReason(engine_.lastError());
    if (buf_.snapshot().size() != lastSeenSize_) recomposeTranscript();
    auto t = engine_.lastTimings();
    if (t.tts.count() > 0) {
        timingsLabel_.setText(
            "STT " + juce::String(t.stt.count()/1000.0, 1) + "s · "
            "LLM " + juce::String(t.llm.count()/1000.0, 1) + "s · "
            "TTS " + juce::String(t.tts.count()/1000.0, 1) + "s",
            juce::dontSendNotification);
    }
    using S = ai::ConversationEngine::State;
    recordBtn_.setButtonText(engine_.state() == S::Capturing ? "■ Stop" : "● Record");
}

} // namespace
```

- [ ] **Step 3: Run + commit**

```bash
./build/tests/guitar_dsp_tests "[app][ui][conv]"
git add src/app/ConversationPanel.{h,cpp} tests/unit/app/test_conversation_panel.cpp \
        src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(ui): ConversationPanel with compact + full layouts"
```

---

### Task 7.3: `AiSettingsPanel`

**Files:**
- Create: `src/app/AiSettingsPanel.h`
- Create: `src/app/AiSettingsPanel.cpp`
- Create: `tests/unit/app/test_ai_settings_panel.cpp`

The settings panel hosts: Model dropdown, Refresh Ollama + Test Anthropic buttons, API-key field, Persona dropdown + editable prompt area, reply-shape sliders, STT model dropdown, Input device dropdown (standalone-only), Pedal binding fields. All bindings persist via `AppPreferences` + `PluginState` (wired in Phase 8).

- [ ] **Step 1: Tests (construction + dropdown population)**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "app/AiSettingsPanel.h"
#include "ai/AppPreferences.h"
#include "ai/PersonaRegistry.h"
#include "FakeHttpTransport.h"

using guitar_dsp::AiSettingsPanel;
using guitar_dsp::ai::PersonaId;
using guitar_dsp::ai::PersonaRegistry;
using guitar_dsp::ai::AppPreferences;
using guitar_dsp::ai::test::FakeHttpTransport;

TEST_CASE("AiSettingsPanel: constructs with empty prefs",
          "[app][ui][settings]") {
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                 .getChildFile("aiset_test.xml");
    tmp.deleteFile();
    AppPreferences prefs{tmp};
    PersonaRegistry personas;
    FakeHttpTransport http;
    AiSettingsPanel p(prefs, personas, http);
    p.setBounds(0, 0, 600, 600);
    REQUIRE(p.modelDropdownItemCount() >= 2);   // at least 2 cloud models
}

TEST_CASE("AiSettingsPanel: persona dropdown populates editable prompt",
          "[app][ui][settings]") {
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                 .getChildFile("aiset_test2.xml");
    tmp.deleteFile();
    AppPreferences prefs{tmp};
    PersonaRegistry personas;
    FakeHttpTransport http;
    AiSettingsPanel p(prefs, personas, http);
    p.selectPersona(PersonaId::Snarky);
    auto text = p.editablePromptText();
    REQUIRE(text == PersonaRegistry::defaultPromptFor(PersonaId::Snarky));
}

TEST_CASE("AiSettingsPanel: refresh Ollama adds detected models",
          "[app][ui][settings]") {
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                 .getChildFile("aiset_test3.xml");
    tmp.deleteFile();
    AppPreferences prefs{tmp};
    PersonaRegistry personas;
    FakeHttpTransport http;
    http.replies.push({200, R"({"models":[{"name":"llama3.2:3b"},{"name":"qwen2.5:3b"}]})", "", {}});
    AiSettingsPanel p(prefs, personas, http);
    auto before = p.modelDropdownItemCount();
    p.refreshOllama();
    REQUIRE(p.modelDropdownItemCount() == before + 2);
}
```

- [ ] **Step 2: Implement**

`src/app/AiSettingsPanel.h`:
```cpp
#pragma once
#include "ai/AppPreferences.h"
#include "ai/PersonaRegistry.h"
#include "ai/IHttpTransport.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class AiSettingsPanel : public juce::Component {
public:
    AiSettingsPanel(ai::AppPreferences&, ai::PersonaRegistry&, ai::IHttpTransport&);

    void resized() override;

    // Test-visible helpers
    int  modelDropdownItemCount() const { return modelBox_.getNumItems(); }
    void selectPersona(ai::PersonaId);
    std::string editablePromptText() const { return promptEditor_.getText().toStdString(); }
    void refreshOllama();

private:
    ai::AppPreferences&   prefs_;
    ai::PersonaRegistry&  personas_;
    ai::IHttpTransport&   http_;

    juce::ComboBox        modelBox_;
    juce::Label           modelStatus_;
    juce::TextButton      refreshBtn_ {"Refresh Ollama"};
    juce::TextButton      testBtn_    {"Test Anthropic"};
    juce::TextEditor      apiKeyField_;
    juce::ComboBox        personaBox_;
    juce::TextEditor      promptEditor_;
    juce::TextButton      resetPromptBtn_ {"Reset to default"};
    juce::Slider          maxSentencesSlider_, maxWordsSlider_;
    juce::ComboBox        sttModelBox_;
    juce::ComboBox        inputDeviceBox_;

    void populateBaseModels();
    void onPersonaChanged();
};

} // namespace
```

`src/app/AiSettingsPanel.cpp`:
```cpp
#include "app/AiSettingsPanel.h"
#include "ai/OllamaClient.h"
namespace guitar_dsp {

AiSettingsPanel::AiSettingsPanel(ai::AppPreferences& p, ai::PersonaRegistry& r,
                                 ai::IHttpTransport& http)
    : prefs_(p), personas_(r), http_(http) {
    addAndMakeVisible(modelBox_);
    addAndMakeVisible(modelStatus_);
    addAndMakeVisible(refreshBtn_);
    addAndMakeVisible(testBtn_);
    addAndMakeVisible(apiKeyField_);
    apiKeyField_.setPasswordCharacter('•');
    apiKeyField_.setText(juce::String(prefs_.anthropicApiKey()));
    apiKeyField_.onTextChange = [this]{
        prefs_.setAnthropicApiKey(apiKeyField_.getText().toStdString());
    };
    addAndMakeVisible(personaBox_);
    addAndMakeVisible(promptEditor_);
    promptEditor_.setMultiLine(true);
    addAndMakeVisible(resetPromptBtn_);
    addAndMakeVisible(maxSentencesSlider_);
    addAndMakeVisible(maxWordsSlider_);
    addAndMakeVisible(sttModelBox_);
    addAndMakeVisible(inputDeviceBox_);

    populateBaseModels();
    refreshOllama();

    static constexpr ai::PersonaId all[] = {
        ai::PersonaId::Interviewer, ai::PersonaId::Snarky,
        ai::PersonaId::WeatheredGuitar, ai::PersonaId::StudioEngineer,
        ai::PersonaId::CuriousAi, ai::PersonaId::PlainAssistant };
    static const char* names[] = {"Interviewer","Snarky","Weathered session player",
                                  "Studio engineer","Curious AI","Plain assistant"};
    for (size_t i = 0; i < 6; ++i) personaBox_.addItem(names[i], (int)i + 1);
    personaBox_.setSelectedId(1);
    personaBox_.onChange = [this]{ onPersonaChanged(); };
    onPersonaChanged();

    refreshBtn_.onClick = [this]{ refreshOllama(); };
    resetPromptBtn_.onClick = [this]{
        auto id = ai::PersonaId(personaBox_.getSelectedId() - 1);
        personas_.resetToDefault(id);
        onPersonaChanged();
    };

    maxSentencesSlider_.setRange(1, 5, 1);
    maxWordsSlider_.setRange(5, 100, 5);
}

void AiSettingsPanel::populateBaseModels() {
    modelBox_.clear();
    modelBox_.addItem("Claude Haiku 4.5 (cloud)",   1);
    modelBox_.addItem("Claude Sonnet 4.6 (cloud)",  2);
}

void AiSettingsPanel::refreshOllama() {
    populateBaseModels();
    auto models = ai::OllamaClient::listInstalledModels(http_, prefs_.ollamaEndpoint());
    int id = 100;
    for (auto& m : models)
        modelBox_.addItem(juce::String(m) + " (local — Ollama)", id++);
    modelStatus_.setText(
        models.empty()
            ? juce::String("Ollama: not running")
            : "Ollama: " + juce::String((int)models.size()) + " models",
        juce::dontSendNotification);
}

void AiSettingsPanel::selectPersona(ai::PersonaId id) {
    personaBox_.setSelectedId((int)id + 1);
    onPersonaChanged();
}

void AiSettingsPanel::onPersonaChanged() {
    auto id = ai::PersonaId(personaBox_.getSelectedId() - 1);
    promptEditor_.setText(juce::String(personas_.promptFor(id)));
    promptEditor_.onTextChange = [this, id]{
        personas_.setCustomPrompt(id, promptEditor_.getText().toStdString());
    };
}

void AiSettingsPanel::resized() {
    // Implementation: simple stacked layout — flesh out as needed.
    auto r = getLocalBounds().reduced(8);
    auto row = [&](int h){ auto x = r.removeFromTop(h); r.removeFromTop(4); return x; };
    modelBox_      .setBounds(row(24));
    modelStatus_   .setBounds(row(18));
    auto btnRow = row(24);
    refreshBtn_    .setBounds(btnRow.removeFromLeft(140));
    testBtn_       .setBounds(btnRow.removeFromLeft(140).translated(8, 0));
    apiKeyField_   .setBounds(row(24));
    personaBox_    .setBounds(row(24));
    promptEditor_  .setBounds(row(100));
    resetPromptBtn_.setBounds(row(24).withWidth(140));
    maxSentencesSlider_.setBounds(row(24));
    maxWordsSlider_    .setBounds(row(24));
    sttModelBox_   .setBounds(row(24));
    inputDeviceBox_.setBounds(row(24));
}

} // namespace
```

- [ ] **Step 3: Run + commit**

```bash
./build/tests/guitar_dsp_tests "[app][ui][settings]"
git add src/app/AiSettingsPanel.{h,cpp} tests/unit/app/test_ai_settings_panel.cpp \
        src/CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(ui): AiSettingsPanel — model picker, persona editor, API key, etc."
```

---

## Phase 7 checkpoint — green.
