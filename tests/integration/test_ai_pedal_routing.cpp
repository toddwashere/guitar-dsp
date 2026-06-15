#include <catch2/catch_test_macros.hpp>
#include "midi/FCB1010Mapping.h"
#include "ai/ConversationEngine.h"
#include "ai/PersonaRegistry.h"
#include "ai/ConversationBuffer.h"
#include "unit/ai/FakeTranscriber.h"
#include "unit/ai/FakeLlmClient.h"
#include "unit/ai/FakeMicCapture.h"
#include <juce_audio_basics/juce_audio_basics.h>

#include <chrono>
#include <thread>

using namespace guitar_dsp::ai;
using namespace guitar_dsp::ai::test;
using guitar_dsp::midi::FCB1010Mapping;
using guitar_dsp::midi::AiAction;
using guitar_dsp::midi::AiPedalBindings;

namespace {
// Mirrors the PluginProcessor MIDI callback logic — keeps the integration
// realistic without needing to instantiate the full processor.
void handleMidiForAi(const FCB1010Mapping& mapping,
                     ConversationEngine& engine,
                     const juce::MidiMessage& msg) {
    if (! msg.isProgramChange()) return;
    auto a = mapping.decodeAi(msg.getProgramChangeNumber(), false);
    switch (a) {
        case AiAction::PttToggle:  engine.startTurn();         break;
        case AiAction::ClearChat:  engine.clearConversation(); break;
        case AiAction::CancelTurn: engine.cancelTurn();        break;
        case AiAction::None:       break;
    }
}
} // namespace

TEST_CASE("AI pedal routing: PTT PC starts a turn",
          "[midi][ai][integration]") {
    FakeTranscriber stt; auto llm = std::make_shared<FakeLlmClient>(); FakeMicCapture mic;
    ConversationBuffer buf; PersonaRegistry personas;
    std::vector<std::string> spoken;
    ConversationEngine engine(stt, llm, mic, buf, personas,
        [&](std::string s){ spoken.push_back(std::move(s)); });

    auto mapping = FCB1010Mapping::stockDefaults();
    AiPedalBindings b;
    b.pttProgramChange       = 8;
    b.clearChatProgramChange = 9;
    mapping.setAiBindings(b);

    // PC = 8 should start a turn
    handleMidiForAi(mapping, engine,
        juce::MidiMessage::programChange(/*channel=*/1, /*program=*/8));

    // Wait briefly for the worker to process the StartTurn job
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (engine.state() != ConversationEngine::State::Capturing) {
        if (std::chrono::steady_clock::now() > deadline)
            FAIL("engine did not reach Capturing");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE(engine.state() == ConversationEngine::State::Capturing);
}

TEST_CASE("AI pedal routing: Clear PC empties the conversation buffer",
          "[midi][ai][integration]") {
    FakeTranscriber stt; auto llm = std::make_shared<FakeLlmClient>(); FakeMicCapture mic;
    ConversationBuffer buf; PersonaRegistry personas;
    ConversationEngine engine(stt, llm, mic, buf, personas, [](std::string){});

    buf.append(Message::Role::User, "leftover");
    auto mapping = FCB1010Mapping::stockDefaults();
    AiPedalBindings b;
    b.clearChatProgramChange = 9;
    mapping.setAiBindings(b);

    handleMidiForAi(mapping, engine,
        juce::MidiMessage::programChange(1, 9));

    // Worker processes Clear synchronously enough — wait briefly
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(buf.snapshot().empty());
}

TEST_CASE("AI pedal routing: unmapped PC does nothing",
          "[midi][ai][integration]") {
    FakeTranscriber stt; auto llm = std::make_shared<FakeLlmClient>(); FakeMicCapture mic;
    ConversationBuffer buf; PersonaRegistry personas;
    ConversationEngine engine(stt, llm, mic, buf, personas, [](std::string){});

    auto mapping = FCB1010Mapping::stockDefaults();
    AiPedalBindings b;
    b.pttProgramChange       = 8;
    b.clearChatProgramChange = 9;
    mapping.setAiBindings(b);

    handleMidiForAi(mapping, engine, juce::MidiMessage::programChange(1, 5));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(engine.state() == ConversationEngine::State::Idle);
}
