#include <catch2/catch_test_macros.hpp>
#include "ai/ConversationEngine.h"
#include "ai/PersonaRegistry.h"
#include "ai/ConversationBuffer.h"
#include "FakeTranscriber.h"
#include "FakeLlmClient.h"
#include "FakeMicCapture.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace guitar_dsp::ai;
using namespace guitar_dsp::ai::test;

namespace {
struct Harness {
    FakeTranscriber  stt;
    FakeLlmClient    llm;
    FakeMicCapture   mic;
    ConversationBuffer buf;
    PersonaRegistry  personas;
    std::vector<std::string> spokenTexts;
    std::mutex spokenMutex;
    ConversationEngine engine;
    Harness()
      : engine(stt, llm, mic, buf, personas,
               [this](std::string s){
                   std::lock_guard lk(spokenMutex);
                   spokenTexts.push_back(std::move(s));
               }) {}

    void waitForState(ConversationEngine::State target,
                      std::chrono::milliseconds budget = std::chrono::milliseconds{2000}) {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        while (engine.state() != target) {
            if (std::chrono::steady_clock::now() > deadline)
                FAIL("waitForState timed out — current state index = "
                     << int(engine.state()));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
};
}

TEST_CASE("Engine: happy path Idle->Capturing->Transcribing->Thinking->Speaking->Idle",
          "[ai][engine]") {
    Harness h;
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.endTurn();
    h.waitForState(ConversationEngine::State::Speaking);
    h.engine.onTtsPlaybackFinished();
    h.waitForState(ConversationEngine::State::Idle);

    auto snap = h.buf.snapshot();
    REQUIRE(snap.size() == 2);
    REQUIRE(snap[0].role == Message::Role::User);
    REQUIRE(snap[0].text == "hello there");
    REQUIRE(snap[1].role == Message::Role::Assistant);
    REQUIRE(snap[1].text == "I'm a fake.");
    REQUIRE(h.spokenTexts.size() == 1);
    REQUIRE(h.spokenTexts[0] == "I'm a fake.");
}

TEST_CASE("Engine: PTT-style startTurn while Capturing -> endTurn semantics",
          "[ai][engine]") {
    Harness h;
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.startTurn();    // second press becomes endTurn
    h.waitForState(ConversationEngine::State::Speaking);
}

TEST_CASE("Engine: clearConversation empties buffer",
          "[ai][engine]") {
    Harness h;
    h.buf.append(Message::Role::User, "old");
    h.engine.clearConversation();
    // give worker a tick
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(h.buf.snapshot().empty());
}

TEST_CASE("Engine: setLlmClient ignored when not Idle",
          "[ai][engine]") {
    Harness h;
    FakeLlmClient other;
    h.llm.delay = std::chrono::milliseconds{300};
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.endTurn();
    h.waitForState(ConversationEngine::State::Thinking);
    h.engine.setLlmClient(other);     // ignored
    h.waitForState(ConversationEngine::State::Speaking);
    REQUIRE(h.llm.callCount.load() == 1);
    REQUIRE(other.callCount.load() == 0);
}
