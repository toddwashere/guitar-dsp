#include <catch2/catch_test_macros.hpp>
#include "ai/ConversationBuffer.h"
#include "ai/PersonaRegistry.h"

using guitar_dsp::ai::ConversationBuffer;
using guitar_dsp::ai::Message;
using guitar_dsp::ai::PersonaId;
using guitar_dsp::ai::PersonaRegistry;

TEST_CASE("PersonaRegistry: every preset has a non-empty default with reply-shape guardrail",
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
    // Other personas unaffected — still return default
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
    REQUIRE(msgs.size() == 3);
    REQUIRE(msgs[0].role == Message::Role::System);
    REQUIRE_FALSE(msgs[0].text.empty());
    REQUIRE(msgs[1].role == Message::Role::User);
    REQUIRE(msgs[1].text == "hi");
    REQUIRE(msgs[2].role == Message::Role::Assistant);
    REQUIRE(msgs[2].text == "hello");
}

TEST_CASE("PersonaRegistry: buildMessages with empty buffer returns just system",
          "[ai][persona]") {
    PersonaRegistry r;
    ConversationBuffer b;
    auto msgs = r.buildMessages(b, PersonaId::Snarky);
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].role == Message::Role::System);
}

TEST_CASE("PersonaRegistry: buildMessages uses custom prompt when set",
          "[ai][persona]") {
    PersonaRegistry r;
    ConversationBuffer b;
    r.setCustomPrompt(PersonaId::Snarky, "be brutal but witty");
    auto msgs = r.buildMessages(b, PersonaId::Snarky);
    REQUIRE(msgs[0].text == "be brutal but witty");
}
