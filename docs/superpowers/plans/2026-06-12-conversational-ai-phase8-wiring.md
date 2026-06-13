# Phase 8 — Processor + Editor wiring

> Continuation of [2026-06-12-conversational-ai.md](2026-06-12-conversational-ai.md).

**Goal:** Wire the components into `PluginProcessor` and `PluginEditor`. After this phase, the app actually does conversational AI end-to-end (modulo pedal bindings in Phase 9).

---

### Task 8.1: Add `ConversationEngine` + clients to `PluginProcessor`

**Files:**
- Modify: `src/app/PluginProcessor.h`
- Modify: `src/app/PluginProcessor.cpp`

- [ ] **Step 1: Add members and factory**

In `PluginProcessor.h`, near the existing TTS members:

```cpp
#include "ai/AppPreferences.h"
#include "ai/PersonaRegistry.h"
#include "ai/ConversationBuffer.h"
#include "ai/ConversationEngine.h"
#include "ai/JuceHttpTransport.h"
#include "ai/WhisperTranscriber.h"
#include "ai/AnthropicClient.h"
#include "ai/OllamaClient.h"

class PluginProcessor : public juce::AudioProcessor {
public:
    // ...

    // AI accessors used by editor
    ai::ConversationEngine&  conversationEngine()  noexcept { return *engine_; }
    ai::ConversationBuffer&  conversationBuffer()  noexcept { return convBuf_; }
    ai::PersonaRegistry&     personaRegistry()     noexcept { return personas_; }
    ai::AppPreferences&      appPreferences()      noexcept { return *prefs_; }
    ai::IHttpTransport&      httpTransport()       noexcept { return http_; }
    void                     selectModelId(std::string);

private:
    void rebuildLlmClient();   // called on model swap

    std::unique_ptr<ai::AppPreferences>      prefs_;
    ai::PersonaRegistry                      personas_;
    ai::ConversationBuffer                   convBuf_;
    ai::JuceHttpTransport                    http_;
    std::unique_ptr<ai::WhisperTranscriber>  whisper_;
    std::unique_ptr<ai::ILlmClient>          llm_;
    std::string                              selectedModelId_ {"claude-haiku-4-5"};
    std::unique_ptr<ai::ConversationEngine>  engine_;
};
```

- [ ] **Step 2: Construct in constructor**

In `PluginProcessor::PluginProcessor()`, after the existing TTS source construction:

```cpp
prefs_   = std::make_unique<ai::AppPreferences>(ai::AppPreferences::defaultPath());

auto whisperPath = AssetLocator::resolve("whisper/ggml-base.en.bin");
whisper_ = std::make_unique<ai::WhisperTranscriber>(whisperPath);

rebuildLlmClient();

engine_ = std::make_unique<ai::ConversationEngine>(
    *whisper_, *llm_, micCapture_, convBuf_, personas_,
    [this](std::string text){ enqueueSayText(text); });
```

- [ ] **Step 3: Implement model swap**

```cpp
void PluginProcessor::rebuildLlmClient() {
    if (selectedModelId_.rfind("ollama:", 0) == 0) {
        auto tag = selectedModelId_.substr(7);
        llm_ = std::make_unique<ai::OllamaClient>(http_, prefs_->ollamaEndpoint(), tag);
    } else {
        llm_ = std::make_unique<ai::AnthropicClient>(
            http_, prefs_->anthropicApiKey(), selectedModelId_);
    }
    if (engine_) engine_->setLlmClient(*llm_);
}

void PluginProcessor::selectModelId(std::string id) {
    selectedModelId_ = std::move(id);
    rebuildLlmClient();
}
```

- [ ] **Step 4: Compile**

```bash
cmake --build build --target guitar_dsp_tests
```

Tests still pass (no behavior change to existing code).

- [ ] **Step 5: Commit**

```bash
git add src/app/PluginProcessor.{h,cpp}
git commit -m "feat(app): wire ConversationEngine + AI clients into PluginProcessor"
```

---

### Task 8.2: Mount panels in `PluginEditor`

**Files:**
- Modify: `src/app/PluginEditor.h`
- Modify: `src/app/PluginEditor.cpp`

- [ ] **Step 1: Add panel members**

In `PluginEditor.h`:
```cpp
#include "app/ConversationPanel.h"
#include "app/AiSettingsPanel.h"

class PluginEditor : public juce::AudioProcessorEditor {
    // ...
private:
    std::unique_ptr<ConversationPanel> conversationPanel_;
    std::unique_ptr<AiSettingsPanel>   aiSettingsPanel_;
    juce::ToggleButton                 showSettingsBtn_ {"AI Settings"};
};
```

- [ ] **Step 2: Construct + lay out**

In `PluginEditor::PluginEditor(PluginProcessor& p)`:
```cpp
const bool compact = processor.wrapperType == juce::AudioProcessor::wrapperType_AudioUnit;

conversationPanel_ = std::make_unique<ConversationPanel>(
    p.conversationEngine(), p.conversationBuffer(), compact);
addAndMakeVisible(*conversationPanel_);

aiSettingsPanel_ = std::make_unique<AiSettingsPanel>(
    p.appPreferences(), p.personaRegistry(), p.httpTransport());
addChildComponent(*aiSettingsPanel_);   // hidden by default

addAndMakeVisible(showSettingsBtn_);
showSettingsBtn_.onClick = [this]{
    aiSettingsPanel_->setVisible(showSettingsBtn_.getToggleState());
};
```

In `resized()`, place the conversation panel below `SayPanel`:
```cpp
// after existing layout calculations:
const int convH = compact ? 80 : 220;
auto convRect = remaining.removeFromBottom(convH);
conversationPanel_->setBounds(convRect);

if (showSettingsBtn_.getToggleState())
    aiSettingsPanel_->setBounds(getLocalBounds().reduced(40));
```

- [ ] **Step 3: Build standalone + plugin**

```bash
cmake --build build --target GuitarSpeak_Standalone GuitarSpeak_AU
```

Both succeed.

- [ ] **Step 4: Smoke run standalone**

```bash
./build/.../GuitarSpeak.app/Contents/MacOS/Guitar\ Speak &
sleep 2
killall "Guitar Speak"
```

App launches, ConversationPanel visible at the bottom, no crash.

- [ ] **Step 5: Commit**

```bash
git add src/app/PluginEditor.{h,cpp}
git commit -m "feat(ui): mount ConversationPanel + AiSettingsPanel in PluginEditor"
```

---

### Task 8.3: Persist AI state across project save/load (AU)

**Files:**
- Modify: `src/app/PluginState.cpp` (toXml/fromXml — done in Task 1.3, re-verify writeback path)
- Modify: `src/app/PluginProcessor.cpp` (`getStateInformation` / `setStateInformation`)

- [ ] **Step 1: Test (integration)**

Add to `tests/integration/test_au_state_persist_ai.cpp`:
```cpp
#include <catch2/catch_test_macros.hpp>
#include "app/PluginProcessor.h"

using guitar_dsp::PluginProcessor;

TEST_CASE("PluginProcessor: getState/setState round-trips AI fields",
          "[app][state][ai][integration]") {
    PluginProcessor a;
    a.selectModelId("ollama:llama3.2:3b");
    a.personaRegistry().setCustomPrompt(
        guitar_dsp::ai::PersonaId::Snarky, "be brutal but witty");

    juce::MemoryBlock blob;
    a.getStateInformation(blob);

    PluginProcessor b;
    b.setStateInformation(blob.getData(), (int)blob.getSize());
    REQUIRE(b.personaRegistry().promptFor(guitar_dsp::ai::PersonaId::Snarky)
            == "be brutal but witty");
    // model id is restored via PluginState — test it via accessor
    // (add a `selectedModelId()` accessor on PluginProcessor if not present)
}
```

- [ ] **Step 2: Implement save/load**

In `PluginProcessor::getStateInformation`:
```cpp
guitar_dsp::PluginState s;
s.sceneId         = scene_->currentSceneId();
s.vocoderBands    = vocoder_->bandCount();
s.vocoderGate     = vocoder_->gate();
s.vocoderMix      = vocoder_->mix();
s.selectedModelId = selectedModelId_;
s.personaId       = currentPersonaId_;       // store an atomic in engine
for (auto id : {PersonaId::Interviewer, PersonaId::Snarky, ...}) {
    auto p = personas_.promptFor(id);
    if (p != PersonaRegistry::defaultPromptFor(id))
        s.customPromptByPersona[id] = p;
}
auto xml = s.toXml();
destData.replaceAll(xml.data(), xml.size());
```

In `setStateInformation`:
```cpp
auto s = guitar_dsp::PluginState::fromXml(
    std::string(static_cast<const char*>(data), sizeInBytes));
scene_->loadSceneId(s.sceneId);
vocoder_->setBandCount(s.vocoderBands);
vocoder_->setGate(s.vocoderGate);
vocoder_->setMix(s.vocoderMix);
selectModelId(s.selectedModelId);
for (auto& [id, prompt] : s.customPromptByPersona) personas_.setCustomPrompt(id, prompt);
```

(Adapt accessor names to actual repo conventions.)

- [ ] **Step 3: Run + commit**

```bash
./build/tests/guitar_dsp_tests "[app][state][ai]"
git add src/app/PluginProcessor.cpp tests/integration/test_au_state_persist_ai.cpp \
        tests/CMakeLists.txt
git commit -m "feat(au): persist AI fields in getState/setState"
```

---

### Task 8.4: Standalone mic device input

**Files:**
- Modify: `src/app/PluginProcessor.cpp` (standalone wrapper hook OR push from `processBlock` when in standalone mode)

The AU path is covered by sidechain. Standalone has the JUCE Standalone host wrapping the processor. The audio input is already delivered as bus 0 (main input). For the conversational AI mic, we need a separate device.

**Decision:** keep it simple — in standalone, when capture is active, use bus 0 as the mic source (the user can configure JUCE's audio device manager to use a USB mic / interface mic input). Tradeoff: in standalone you can't have both guitar and mic at once, which is fine for v1 because the conference rig uses the AU/Logic path. Document this in the spec section 9 follow-up (do that in Phase 11 docs task).

- [ ] **Step 1: Modify `processBlock` standalone path**

```cpp
if (micCapture_.isCapturing()) {
    if (getBusCount(true) >= 2) {
        // AU: use sidechain (covered in Phase 5)
    } else {
        // Standalone: capture from bus 0 main input
        auto main = getBusBuffer(buffer, true, 0);
        if (main.getNumChannels() >= 1)
            micCapture_.appendFromAudioBlock(main.getReadPointer(0), main.getNumSamples());
    }
}
```

This duplicates the existing AU branch — extract a helper if cleanup is needed.

- [ ] **Step 2: Commit**

```bash
git add src/app/PluginProcessor.cpp
git commit -m "feat(standalone): route main input bus into MicCapture when capturing"
```

---

## Phase 8 checkpoint — green.
