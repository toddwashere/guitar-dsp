#include <catch2/catch_test_macros.hpp>
#include "app/ConversationPanel.h"
#include "ai/ConversationBuffer.h"
#include "ai/PersonaRegistry.h"
#include "unit/ai/FakeTranscriber.h"
#include "unit/ai/FakeLlmClient.h"
#include "unit/ai/FakeMicCapture.h"
#include <juce_gui_basics/juce_gui_basics.h>

using namespace guitar_dsp::ai;
using namespace guitar_dsp::ai::test;
using guitar_dsp::ConversationPanel;

namespace {
struct PanelHarness {
    juce::ScopedJuceInitialiser_GUI juceInit;
    FakeTranscriber  stt;
    std::shared_ptr<FakeLlmClient> llm = std::make_shared<FakeLlmClient>();
    FakeMicCapture   mic;
    ConversationBuffer buf;
    PersonaRegistry  personas;
    std::vector<std::string> spoken;
    ConversationEngine engine;
    PanelHarness()
      : engine(stt, llm, mic, buf, personas,
               [this](std::string s){ spoken.push_back(std::move(s)); }) {}
};
}

TEST_CASE("ConversationPanel: constructs and resizes in compact mode",
          "[app][ui][conv]") {
    PanelHarness h;
    ConversationPanel panel(h.engine, h.buf, /*compact=*/true);
    panel.setBounds(0, 0, 320, 80);
    REQUIRE(panel.getWidth() == 320);
    REQUIRE(panel.getHeight() == 80);
}

TEST_CASE("ConversationPanel: constructs in full mode",
          "[app][ui][conv]") {
    PanelHarness h;
    ConversationPanel panel(h.engine, h.buf, /*compact=*/false);
    panel.setBounds(0, 0, 600, 280);
    REQUIRE(panel.getWidth() == 600);
    REQUIRE(panel.getHeight() == 280);
}

TEST_CASE("ConversationPanel: transcript text composed from buffer snapshot",
          "[app][ui][conv]") {
    PanelHarness h;
    h.buf.append(Message::Role::User, "hi");
    h.buf.append(Message::Role::Assistant, "hello there");
    ConversationPanel panel(h.engine, h.buf, /*compact=*/false);
    auto txt = panel.composedTranscriptForTest();
    REQUIRE(txt.find("You: hi") != std::string::npos);
    REQUIRE(txt.find("AI : hello there") != std::string::npos);
}

TEST_CASE("ConversationPanel: compact mode shows only last 2 messages",
          "[app][ui][conv]") {
    PanelHarness h;
    h.buf.append(Message::Role::User, "old user");
    h.buf.append(Message::Role::Assistant, "old AI");
    h.buf.append(Message::Role::User, "new user");
    h.buf.append(Message::Role::Assistant, "new AI");
    ConversationPanel panel(h.engine, h.buf, /*compact=*/true);
    auto txt = panel.composedTranscriptForTest();
    REQUIRE(txt.find("new user") != std::string::npos);
    REQUIRE(txt.find("new AI") != std::string::npos);
    REQUIRE(txt.find("old user") == std::string::npos);
    REQUIRE(txt.find("old AI") == std::string::npos);
}

TEST_CASE("ConversationPanel: full mode shows full history",
          "[app][ui][conv]") {
    PanelHarness h;
    h.buf.append(Message::Role::User, "first");
    h.buf.append(Message::Role::Assistant, "second");
    h.buf.append(Message::Role::User, "third");
    ConversationPanel panel(h.engine, h.buf, /*compact=*/false);
    auto txt = panel.composedTranscriptForTest();
    REQUIRE(txt.find("first") != std::string::npos);
    REQUIRE(txt.find("second") != std::string::npos);
    REQUIRE(txt.find("third") != std::string::npos);
}
