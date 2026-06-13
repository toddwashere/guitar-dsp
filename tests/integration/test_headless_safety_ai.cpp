#include <catch2/catch_test_macros.hpp>
#include "ai/ConversationEngine.h"
#include "ai/PersonaRegistry.h"
#include "ai/ConversationBuffer.h"
#include "ai/WhisperTranscriber.h"
#include "ai/AnthropicClient.h"
#include "ai/OllamaClient.h"
#include "ai/JuceHttpTransport.h"
#include "unit/ai/FakeMicCapture.h"
#include <juce_core/juce_core.h>
#include <chrono>
#include <thread>

using namespace guitar_dsp::ai;
using guitar_dsp::ai::test::FakeMicCapture;

namespace {
void waitForState(ConversationEngine& e, ConversationEngine::State target,
                  std::chrono::milliseconds budget = std::chrono::seconds(2)) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (e.state() != target) {
        if (std::chrono::steady_clock::now() > deadline) {
            FAIL("waitForState timed out — current=" + std::to_string(int(e.state())));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
}

TEST_CASE("Headless safety: WhisperTranscriber missing model surfaces error, no crash",
          "[ai][headless][integration]") {
    juce::File bogus("/tmp/definitely_not_real_xyz.bin");
    WhisperTranscriber w{bogus};
    auto r = w.transcribe(std::vector<float>(16000, 0.0f));
    REQUIRE_FALSE(r.error.empty());
    SUCCEED();
}

TEST_CASE("Headless safety: AnthropicClient with empty key returns pre-flight error",
          "[ai][headless][integration]") {
    JuceHttpTransport http;
    AnthropicClient c{http, "", "claude-haiku-4-5"};
    LlmRequest req;
    req.messages = {{Message::Role::User, "ping"}};
    req.timeout  = std::chrono::milliseconds{200};
    auto r = c.generate(req);
    REQUIRE_FALSE(r.error.empty());
    REQUIRE(r.text.empty());
}

TEST_CASE("Headless safety: OllamaClient with no running ollama returns error",
          "[ai][headless][integration]") {
    JuceHttpTransport http;
    OllamaClient c{http, "http://127.0.0.1:1", "llama3.2:3b"};   // unreachable port
    LlmRequest req;
    req.messages = {{Message::Role::User, "ping"}};
    req.timeout  = std::chrono::milliseconds{200};
    auto r = c.generate(req);
    REQUIRE_FALSE(r.error.empty());
    REQUIRE(r.error.find("not running") != std::string::npos);
}

TEST_CASE("Headless safety: ConversationEngine with broken STT errors back to Idle",
          "[ai][headless][integration]") {
    juce::File bogus("/tmp/definitely_not_real_xyz.bin");
    WhisperTranscriber w{bogus};

    JuceHttpTransport http;
    AnthropicClient   client{http, "", "claude-haiku-4-5"};

    FakeMicCapture mic;
    ConversationBuffer buf;
    PersonaRegistry    personas;
    std::vector<std::string> spoken;
    ConversationEngine engine(w, client, mic, buf, personas,
        [&](std::string s){ spoken.push_back(std::move(s)); });

    engine.startTurn();
    waitForState(engine, ConversationEngine::State::Capturing);
    engine.endTurn();
    // Should NOT crash; should land in Idle (STT error) or Error/Idle (LLM error)
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (engine.state() != ConversationEngine::State::Idle
        && engine.state() != ConversationEngine::State::Error) {
        if (std::chrono::steady_clock::now() > deadline)
            FAIL("engine never returned to terminal state");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    REQUIRE_FALSE(engine.lastError().empty());
    REQUIRE(spoken.empty());
}
