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
    std::shared_ptr<FakeLlmClient> llm = std::make_shared<FakeLlmClient>();
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
    auto other = std::make_shared<FakeLlmClient>();
    h.llm->delay = std::chrono::milliseconds{300};
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.endTurn();
    h.waitForState(ConversationEngine::State::Thinking);
    h.engine.setLlmClient(other);     // swap is now allowed mid-turn
    h.waitForState(ConversationEngine::State::Speaking);
    // The in-flight generate completes on h.llm (snapshot taken before
    // swap). other isn't used for THIS turn.
    REQUIRE(h.llm->callCount.load() == 1);
    REQUIRE(other->callCount.load() == 0);
}

TEST_CASE("Engine: cancel during Thinking returns to Idle, no assistant message",
          "[ai][engine][cancel]") {
    Harness h;
    h.llm->delay = std::chrono::milliseconds{1000};
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.endTurn();
    h.waitForState(ConversationEngine::State::Thinking);
    h.engine.cancelTurn();
    h.waitForState(ConversationEngine::State::Idle);

    auto snap = h.buf.snapshot();
    REQUIRE(snap.size() == 1);                       // only user message added
    REQUIRE(snap[0].role == Message::Role::User);
}

TEST_CASE("Engine: LLM error transitions to Error state with reason",
          "[ai][engine][error]") {
    Harness h;
    h.llm->scriptedError = "API key invalid";
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.endTurn();
    h.waitForState(ConversationEngine::State::Error);
    REQUIRE(h.engine.lastError() == "API key invalid");
}

TEST_CASE("Engine: STT empty-text error transitions to Error with 'couldn't transcribe'",
          "[ai][engine][error]") {
    Harness h;
    h.stt.scriptedText = "";
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.endTurn();
    h.waitForState(ConversationEngine::State::Error);
    REQUIRE(h.engine.lastError().find("transcribe") != std::string::npos);
}

TEST_CASE("Engine: STT scripted error surfaces verbatim",
          "[ai][engine][error]") {
    Harness h;
    h.stt.scriptedError = "model not loaded";
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.endTurn();
    h.waitForState(ConversationEngine::State::Error);
    REQUIRE(h.engine.lastError() == "model not loaded");
}

TEST_CASE("Engine: too-short mic capture surfaces friendly error",
          "[ai][engine][error]") {
    Harness h;
    h.mic.tooShort = true;
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.endTurn();
    h.waitForState(ConversationEngine::State::Error);
    REQUIRE(h.engine.lastError().find("didn't hear") != std::string::npos);
}

TEST_CASE("Engine: destructor mid-turn joins cleanly without deadlock",
          "[ai][engine][cancel]") {
    auto stt = std::make_unique<FakeTranscriber>();
    auto llm = std::make_shared<FakeLlmClient>();
    llm->delay = std::chrono::seconds{5};
    auto mic = std::make_unique<FakeMicCapture>();
    ConversationBuffer buf;
    PersonaRegistry    personas;
    std::vector<std::string> spoken;
    {
        ConversationEngine engine(*stt, llm, *mic, buf, personas,
            [&](std::string s){ spoken.push_back(std::move(s)); });
        engine.startTurn();
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        engine.endTurn();
        std::this_thread::sleep_for(std::chrono::milliseconds{50});
        // Destructor fires here; must NOT hang on the 5-second LLM delay.
    }
    SUCCEED();
}

TEST_CASE("Engine: clear during Thinking cancels in-flight and empties buffer",
          "[ai][engine][cancel]") {
    Harness h;
    h.llm->delay = std::chrono::milliseconds{500};
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.endTurn();
    h.waitForState(ConversationEngine::State::Thinking);
    h.engine.clearConversation();
    // Worker should process the Clear job and end up Idle with empty buffer.
    std::this_thread::sleep_for(std::chrono::milliseconds{600});
    REQUIRE(h.engine.state() == ConversationEngine::State::Idle);
    REQUIRE(h.buf.snapshot().empty());
}

TEST_CASE("Engine: canned fallback fires on LLM error -> still speaks",
          "[ai][engine][fallback]") {
    Harness h;
    h.llm->scriptedError = "rate limited";
    h.engine.setCannedFallbackEnabled(true);
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.endTurn();
    h.waitForState(ConversationEngine::State::Speaking);
    h.engine.onTtsPlaybackFinished();
    h.waitForState(ConversationEngine::State::Idle);

    REQUIRE(h.spokenTexts.size() == 1);
    REQUIRE_FALSE(h.spokenTexts[0].empty());
    REQUIRE(h.engine.lastError().find("rate limited") != std::string::npos);
}

TEST_CASE("Engine: canned fallback disabled (default) -> Error state with reason",
          "[ai][engine][fallback]") {
    Harness h;
    h.llm->scriptedError = "rate limited";
    // Note: not enabling fallback
    h.engine.startTurn();
    h.waitForState(ConversationEngine::State::Capturing);
    h.engine.endTurn();
    h.waitForState(ConversationEngine::State::Error);
    REQUIRE(h.spokenTexts.empty());
}
