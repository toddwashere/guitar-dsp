#include <catch2/catch_test_macros.hpp>
#include "app/AiSettingsPanel.h"
#include "ai/AppPreferences.h"
#include "ai/PersonaRegistry.h"
#include "unit/ai/FakeHttpTransport.h"
#include <juce_gui_basics/juce_gui_basics.h>

using guitar_dsp::AiSettingsPanel;
using guitar_dsp::ai::AppPreferences;
using guitar_dsp::ai::PersonaId;
using guitar_dsp::ai::PersonaRegistry;
using guitar_dsp::ai::test::FakeHttpTransport;

namespace {
juce::File tempPath(const char* n) {
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
              .getChildFile(n);
}
}

TEST_CASE("AiSettingsPanel: constructs with empty prefs",
          "[app][ui][settings]") {
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto tmp = tempPath("aiset_empty.xml"); tmp.deleteFile();
    AppPreferences  prefs{tmp};
    PersonaRegistry personas;
    FakeHttpTransport http;
    AiSettingsPanel p(prefs, personas, http);
    p.setBounds(0, 0, 600, 600);
    REQUIRE(p.modelDropdownItemCount() >= 2);   // at least the 2 cloud models
    tmp.deleteFile();
}

TEST_CASE("AiSettingsPanel: persona dropdown populates editable prompt",
          "[app][ui][settings]") {
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto tmp = tempPath("aiset_persona.xml"); tmp.deleteFile();
    AppPreferences  prefs{tmp};
    PersonaRegistry personas;
    FakeHttpTransport http;
    AiSettingsPanel p(prefs, personas, http);
    p.selectPersona(PersonaId::Snarky);
    REQUIRE(p.editablePromptText()
            == PersonaRegistry::defaultPromptFor(PersonaId::Snarky));
    tmp.deleteFile();
}

TEST_CASE("AiSettingsPanel: refresh Ollama populates detected models",
          "[app][ui][settings]") {
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto tmp = tempPath("aiset_ollama.xml"); tmp.deleteFile();
    AppPreferences  prefs{tmp};
    PersonaRegistry personas;
    FakeHttpTransport http;
    // The constructor calls refreshOllama() — give it an empty reply so it
    // starts with only the 2 cloud models.  Then give the explicit call a
    // reply with 2 Ollama models so we can verify the delta.
    http.replies.push({0, "", "connection refused", {}});   // ctor call: no Ollama
    http.replies.push({200, R"({"models":[{"name":"llama3.2:3b"},{"name":"qwen2.5:3b"}]})",
                       "", {}});                            // explicit call: 2 models
    AiSettingsPanel p(prefs, personas, http);
    const int before = p.modelDropdownItemCount();          // 2 cloud models
    p.refreshOllama();
    REQUIRE(p.modelDropdownItemCount() == before + 2);
    tmp.deleteFile();
}

TEST_CASE("AiSettingsPanel: refresh Ollama with no Ollama running keeps cloud models",
          "[app][ui][settings]") {
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto tmp = tempPath("aiset_noollama.xml"); tmp.deleteFile();
    AppPreferences  prefs{tmp};
    PersonaRegistry personas;
    FakeHttpTransport http;
    http.replies.push({0, "", "connection refused", {}});
    AiSettingsPanel p(prefs, personas, http);
    p.refreshOllama();
    REQUIRE(p.modelDropdownItemCount() >= 2);   // still has cloud models
    tmp.deleteFile();
}

TEST_CASE("AiSettingsPanel: editing prompt persists into PersonaRegistry",
          "[app][ui][settings]") {
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto tmp = tempPath("aiset_edit.xml"); tmp.deleteFile();
    AppPreferences  prefs{tmp};
    PersonaRegistry personas;
    FakeHttpTransport http;
    AiSettingsPanel p(prefs, personas, http);
    p.selectPersona(PersonaId::Interviewer);
    p.setPromptForTest("override prompt");
    REQUIRE(personas.promptFor(PersonaId::Interviewer) == "override prompt");
    tmp.deleteFile();
}

TEST_CASE("AiSettingsPanel: API key field reflects stored value",
          "[app][ui][settings]") {
    juce::ScopedJuceInitialiser_GUI juceInit;
    auto tmp = tempPath("aiset_key.xml"); tmp.deleteFile();
    AppPreferences prefs{tmp};
    prefs.setAnthropicApiKey("sk-ant-stored");
    PersonaRegistry personas;
    FakeHttpTransport http;
    AiSettingsPanel p(prefs, personas, http);
    REQUIRE(p.apiKeyFieldText() == "sk-ant-stored");
    tmp.deleteFile();
}
