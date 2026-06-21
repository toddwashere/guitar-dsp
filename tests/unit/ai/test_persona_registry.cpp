#include <catch2/catch_test_macros.hpp>
#include "ai/ConversationBuffer.h"
#include "ai/PersonaRegistry.h"
#include "ai/KnowledgeDoc.h"

using guitar_dsp::ai::ConversationBuffer;
using guitar_dsp::ai::Message;
using guitar_dsp::ai::PersonaId;
using guitar_dsp::ai::PersonaRegistry;
using guitar_dsp::ai::KnowledgeDoc;

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

TEST_CASE("PersonaRegistry: SessionQa default prompt has 60-word guardrail",
          "[ai][persona]") {
    auto p = PersonaRegistry::defaultPromptFor(PersonaId::SessionQa);
    REQUIRE_FALSE(p.empty());
    REQUIRE(p.find("60 words") != std::string::npos);
    REQUIRE(p.find("No lists") != std::string::npos);
    // The prompt instructs the model to answer ONLY from the reference doc.
    REQUIRE(p.find("ONLY") != std::string::npos);
}

namespace {
juce::File writeTempPersonaDoc(const juce::String& body) {
    auto f = juce::File::createTempFile("session_qa_persona_test.md");
    f.replaceWithText(body);
    return f;
}
} // namespace

TEST_CASE("PersonaRegistry: SessionQa buildMessages injects doc body under heading",
          "[ai][persona][session_qa]") {
    auto f = writeTempPersonaDoc("My stack is JUCE and C++.");
    KnowledgeDoc doc(f);
    PersonaRegistry r;
    r.setSessionQaDoc(&doc);
    ConversationBuffer b;
    auto msgs = r.buildMessages(b, PersonaId::SessionQa);
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].role == Message::Role::System);
    REQUIRE(msgs[0].text.find("# Reference document") != std::string::npos);
    REQUIRE(msgs[0].text.find("My stack is JUCE and C++.") != std::string::npos);
    // Persona prompt must precede the doc.
    REQUIRE(msgs[0].text.find("audience")
            < msgs[0].text.find("# Reference document"));
    f.deleteFile();
}

TEST_CASE("PersonaRegistry: SessionQa with no doc set substitutes diagnostic line",
          "[ai][persona][session_qa]") {
    PersonaRegistry r;  // no setSessionQaDoc
    ConversationBuffer b;
    auto msgs = r.buildMessages(b, PersonaId::SessionQa);
    REQUIRE(msgs.size() == 1);
    REQUIRE(msgs[0].text.find("REFERENCE DOCUMENT NOT LOADED")
            != std::string::npos);
}

TEST_CASE("PersonaRegistry: SessionQa with empty doc substitutes diagnostic line",
          "[ai][persona][session_qa]") {
    auto f = writeTempPersonaDoc("");
    KnowledgeDoc doc(f);
    PersonaRegistry r;
    r.setSessionQaDoc(&doc);
    ConversationBuffer b;
    auto msgs = r.buildMessages(b, PersonaId::SessionQa);
    REQUIRE(msgs[0].text.find("REFERENCE DOCUMENT NOT LOADED")
            != std::string::npos);
    f.deleteFile();
}

TEST_CASE("PersonaRegistry: non-SessionQa personas unaffected by doc setter",
          "[ai][persona][session_qa]") {
    auto f = writeTempPersonaDoc("Doc body");
    KnowledgeDoc doc(f);
    PersonaRegistry r;
    r.setSessionQaDoc(&doc);
    ConversationBuffer b;
    auto msgs = r.buildMessages(b, PersonaId::Interviewer);
    REQUIRE(msgs[0].text.find("Doc body") == std::string::npos);
    REQUIRE(msgs[0].text.find("# Reference document") == std::string::npos);
    f.deleteFile();
}
