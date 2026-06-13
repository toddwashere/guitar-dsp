# Phase 9 — FCB pedal bindings

> Continuation of [2026-06-12-conversational-ai.md](2026-06-12-conversational-ai.md).

**Goal:** Hook PTT, Clear, and Cancel actions into the existing `FCB1010Mapping` and route them through `HostMidiSceneRouter` (or the equivalent existing MIDI dispatch) into the `ConversationEngine`.

---

### Task 9.1: Extend `FCB1010Mapping`

**Files:**
- Modify: `src/midi/FCB1010Mapping.h`
- Modify: `src/midi/FCB1010Mapping.cpp`
- Create: `tests/unit/midi/test_fcb1010_mapping_ai.cpp`

- [ ] **Step 1: Add `AiAction` enum + decode**

```cpp
// FCB1010Mapping.h
namespace guitar_dsp::midi {

enum class AiAction { None, PttToggle, ClearChat, CancelTurn };

struct AiPedalBindings {
    int pttPedal           = 9;
    int clearChatPedal     = 10;     // long-press
    int cancelTurnPedal    = 10;     // short-press of same pedal
    int longPressMillis    = 700;
};

class FCB1010Mapping {
public:
    // existing scene-change API stays
    AiAction decodeAi(int pedalIndex, bool isLongPress) const;
    void     setAiBindings(AiPedalBindings);

private:
    AiPedalBindings ai_;
};

} // namespace
```

- [ ] **Step 2: Tests**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "midi/FCB1010Mapping.h"

using guitar_dsp::midi::FCB1010Mapping;
using guitar_dsp::midi::AiAction;

TEST_CASE("FCB1010 AI: PTT pedal short press maps to PttToggle",
          "[midi][fcb][ai]") {
    FCB1010Mapping m;
    REQUIRE(m.decodeAi(9, /*long=*/false) == AiAction::PttToggle);
}

TEST_CASE("FCB1010 AI: clear pedal short press = CancelTurn, long press = ClearChat",
          "[midi][fcb][ai]") {
    FCB1010Mapping m;
    REQUIRE(m.decodeAi(10, false) == AiAction::CancelTurn);
    REQUIRE(m.decodeAi(10, true)  == AiAction::ClearChat);
}

TEST_CASE("FCB1010 AI: unmapped pedal returns None",
          "[midi][fcb][ai]") {
    FCB1010Mapping m;
    REQUIRE(m.decodeAi(1, false) == AiAction::None);
}

TEST_CASE("FCB1010 AI: rebound PTT pedal honored",
          "[midi][fcb][ai]") {
    FCB1010Mapping m;
    guitar_dsp::midi::AiPedalBindings b; b.pttPedal = 5;
    m.setAiBindings(b);
    REQUIRE(m.decodeAi(5, false) == AiAction::PttToggle);
    REQUIRE(m.decodeAi(9, false) == AiAction::None);
}
```

- [ ] **Step 3: Implement**

```cpp
// FCB1010Mapping.cpp
namespace guitar_dsp::midi {

void FCB1010Mapping::setAiBindings(AiPedalBindings b) { ai_ = b; }

AiAction FCB1010Mapping::decodeAi(int pedalIndex, bool isLongPress) const {
    if (pedalIndex == ai_.pttPedal && ! isLongPress) return AiAction::PttToggle;
    if (pedalIndex == ai_.clearChatPedal &&   isLongPress) return AiAction::ClearChat;
    if (pedalIndex == ai_.cancelTurnPedal && ! isLongPress) return AiAction::CancelTurn;
    return AiAction::None;
}

} // namespace
```

- [ ] **Step 4: Run + commit**

```bash
./build/tests/guitar_dsp_tests "[midi][fcb][ai]"
git add src/midi/FCB1010Mapping.{h,cpp} tests/unit/midi/test_fcb1010_mapping_ai.cpp \
        tests/CMakeLists.txt
git commit -m "feat(midi): FCB1010 AI pedal decode (PTT, Cancel, Clear with long-press)"
```

---

### Task 9.2: Route AI pedal actions into `ConversationEngine`

**Files:**
- Modify: `src/midi/HostMidiSceneRouter.h` (or the existing MIDI ingest class — check repo)
- Modify: `src/midi/HostMidiSceneRouter.cpp`
- Modify: `src/app/PluginProcessor.cpp` (wire the router to engine)

Long-press detection uses press-time stamps + the `longPressMillis` threshold from `AiPedalBindings`.

- [ ] **Step 1: Add long-press detector to the router**

In `HostMidiSceneRouter.h`:
```cpp
#include "midi/FCB1010Mapping.h"
#include "ai/ConversationEngine.h"

class HostMidiSceneRouter {
public:
    // existing scene-related members
    void setAiSink(ai::ConversationEngine*);    // optional; null = no AI dispatch

    void processMidiMessage(const juce::MidiMessage&,
                            std::chrono::steady_clock::time_point now);

private:
    midi::FCB1010Mapping*     mapping_   {nullptr};
    ai::ConversationEngine*   aiSink_    {nullptr};
    std::array<std::chrono::steady_clock::time_point, 32> downAt_{};
};
```

- [ ] **Step 2: Process pedal-down/pedal-up**

```cpp
void HostMidiSceneRouter::processMidiMessage(
    const juce::MidiMessage& m, std::chrono::steady_clock::time_point now) {
    // existing scene-change dispatch first
    if (! aiSink_ || ! mapping_) return;
    if (! m.isController()) return;

    // FCB1010 sends CC for pedal events — adapt to the project's conventions
    const int pedal = decodePedalIndexFromCc(m);   // existing helper
    const bool isDown = m.getControllerValue() >= 64;
    if (pedal < 0) return;

    if (isDown) {
        downAt_[(size_t)pedal] = now;
        return;
    }
    auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now - downAt_[(size_t)pedal]);
    bool isLong = dt.count() >= 700;          // bindings.longPressMillis

    auto action = mapping_->decodeAi(pedal, isLong);
    switch (action) {
        case midi::AiAction::PttToggle:  aiSink_->startTurn(); break;  // engine handles Idle vs Capturing
        case midi::AiAction::CancelTurn: aiSink_->cancelTurn(); break;
        case midi::AiAction::ClearChat:  aiSink_->clearConversation(); break;
        case midi::AiAction::None:       break;
    }
}
```

- [ ] **Step 3: Test the long-press dispatch**

```cpp
#include <catch2/catch_test_macros.hpp>
#include "midi/HostMidiSceneRouter.h"
#include "ai/ConversationEngine.h"
// ... fake mic, fake stt, fake llm, harness ...

TEST_CASE("HostMidiSceneRouter: short press on clear-pedal cancels turn, long press clears",
          "[midi][router][ai][integration]") {
    Harness h;                                     // from test_conversation_engine
    midi::FCB1010Mapping mapping;
    HostMidiSceneRouter router;
    router.setMapping(&mapping);
    router.setAiSink(&h.engine);

    auto t0 = std::chrono::steady_clock::time_point(std::chrono::seconds(0));
    auto t1 = t0 + std::chrono::milliseconds(800);
    auto pressDown = juce::MidiMessage::controllerEvent(1, 10, 127);
    auto pressUp   = juce::MidiMessage::controllerEvent(1, 10, 0);

    h.buf.append(Message::Role::User, "old");
    router.processMidiMessage(pressDown, t0);
    router.processMidiMessage(pressUp,   t1);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(h.buf.snapshot().empty());
}
```

- [ ] **Step 4: Wire in `PluginProcessor`**

In `PluginProcessor` ctor:
```cpp
hostMidiRouter_.setMapping(&fcbMapping_);
hostMidiRouter_.setAiSink(engine_.get());
```

- [ ] **Step 5: Run + commit**

```bash
./build/tests/guitar_dsp_tests "[midi][router][ai]"
git add src/midi/HostMidiSceneRouter.{h,cpp} src/app/PluginProcessor.cpp \
        tests/integration/test_host_midi_router_ai.cpp \
        tests/CMakeLists.txt
git commit -m "feat(midi): route FCB AI pedals (PTT/Clear/Cancel) into ConversationEngine"
```

---

## Phase 9 checkpoint — green.

After this phase, you can press FCB pedal 9 in Logic and the AI loop runs. The remaining phases (10, 11) are polish and docs.
