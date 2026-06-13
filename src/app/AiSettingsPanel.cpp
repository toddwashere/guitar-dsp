#include "app/AiSettingsPanel.h"
#include "ai/OllamaClient.h"

namespace guitar_dsp {

namespace {
struct PersonaEntry { ai::PersonaId id; const char* label; };
constexpr PersonaEntry kPersonas[] = {
    {ai::PersonaId::Interviewer,     "Interviewer"},
    {ai::PersonaId::Snarky,          "Snarky"},
    {ai::PersonaId::WeatheredGuitar, "Weathered session player"},
    {ai::PersonaId::StudioEngineer,  "Studio engineer"},
    {ai::PersonaId::CuriousAi,       "Curious AI"},
    {ai::PersonaId::PlainAssistant,  "Plain assistant"},
};
}

AiSettingsPanel::AiSettingsPanel(ai::AppPreferences& p, ai::PersonaRegistry& r,
                                 ai::IHttpTransport& http)
    : prefs_(p), personas_(r), http_(http) {
    addAndMakeVisible(modelBox_);
    addAndMakeVisible(modelStatus_);
    addAndMakeVisible(refreshBtn_);
    addAndMakeVisible(testBtn_);

    apiKeyField_.setPasswordCharacter(juce::juce_wchar('*'));
    apiKeyField_.setText(juce::String(prefs_.anthropicApiKey()), false);
    apiKeyField_.onTextChange = [this]{
        prefs_.setAnthropicApiKey(apiKeyField_.getText().toStdString());
    };
    addAndMakeVisible(apiKeyField_);

    addAndMakeVisible(personaBox_);
    addAndMakeVisible(promptEditor_);
    promptEditor_.setMultiLine(true);
    addAndMakeVisible(resetPromptBtn_);

    maxSentencesSlider_.setRange(1, 5, 1);
    maxSentencesSlider_.setValue(2);
    addAndMakeVisible(maxSentencesSlider_);

    maxWordsSlider_.setRange(5, 100, 5);
    maxWordsSlider_.setValue(25);
    addAndMakeVisible(maxWordsSlider_);

    addAndMakeVisible(sttModelBox_);
    sttModelBox_.addItem("whisper-base.en", 1);
    sttModelBox_.setSelectedId(1, juce::dontSendNotification);

    addAndMakeVisible(inputDeviceBox_);

    populateBaseModels();
    refreshOllama();

    for (size_t i = 0; i < std::size(kPersonas); ++i)
        personaBox_.addItem(kPersonas[i].label, static_cast<int>(i) + 1);
    personaBox_.setSelectedId(1, juce::dontSendNotification);
    personaBox_.onChange = [this]{ onPersonaChanged(); };
    onPersonaChanged();

    refreshBtn_.onClick = [this]{ refreshOllama(); };
    resetPromptBtn_.onClick = [this]{
        auto idx = personaBox_.getSelectedId() - 1;
        if (idx < 0 || idx >= (int)std::size(kPersonas)) return;
        personas_.resetToDefault(kPersonas[idx].id);
        onPersonaChanged();
    };
}

void AiSettingsPanel::populateBaseModels() {
    modelBox_.clear(juce::dontSendNotification);
    modelBox_.addItem("Claude Haiku 4.5 (cloud)",  1);
    modelBox_.addItem("Claude Sonnet 4.6 (cloud)", 2);
}

void AiSettingsPanel::refreshOllama() {
    populateBaseModels();
    auto models = ai::OllamaClient::listInstalledModels(http_, prefs_.ollamaEndpoint());
    int id = 100;
    for (auto& m : models)
        modelBox_.addItem(juce::String(m) + " (local — Ollama)", id++);
    modelStatus_.setText(
        models.empty()
            ? juce::String("Ollama: not running (run `ollama serve`)")
            : "Ollama: " + juce::String((int)models.size()) + " models",
        juce::dontSendNotification);
}

void AiSettingsPanel::selectPersona(ai::PersonaId id) {
    for (size_t i = 0; i < std::size(kPersonas); ++i) {
        if (kPersonas[i].id == id) {
            personaBox_.setSelectedId(static_cast<int>(i) + 1, juce::dontSendNotification);
            break;
        }
    }
    onPersonaChanged();
}

void AiSettingsPanel::setPromptForTest(const juce::String& s) {
    promptEditor_.setText(s, juce::dontSendNotification);
    if (auto idx = personaBox_.getSelectedId() - 1;
        idx >= 0 && idx < (int)std::size(kPersonas)) {
        personas_.setCustomPrompt(kPersonas[idx].id, s.toStdString());
    }
}

void AiSettingsPanel::onPersonaChanged() {
    auto idx = personaBox_.getSelectedId() - 1;
    if (idx < 0 || idx >= (int)std::size(kPersonas)) return;
    auto id = kPersonas[idx].id;
    promptEditor_.setText(juce::String(personas_.promptFor(id)),
                          juce::dontSendNotification);
    promptEditor_.onTextChange = [this, id]{
        personas_.setCustomPrompt(id, promptEditor_.getText().toStdString());
    };
}

void AiSettingsPanel::resized() {
    auto r = getLocalBounds().reduced(8);
    auto rowH = [&](int h){ auto x = r.removeFromTop(h); r.removeFromTop(4); return x; };

    modelBox_      .setBounds(rowH(24));
    modelStatus_   .setBounds(rowH(18));
    auto btnRow = rowH(24);
    refreshBtn_    .setBounds(btnRow.removeFromLeft(140));
    btnRow.removeFromLeft(8);
    testBtn_       .setBounds(btnRow.removeFromLeft(140));
    apiKeyField_   .setBounds(rowH(24));
    personaBox_    .setBounds(rowH(24));
    promptEditor_  .setBounds(rowH(100));
    resetPromptBtn_.setBounds(rowH(24).withWidth(140));
    maxSentencesSlider_.setBounds(rowH(24));
    maxWordsSlider_    .setBounds(rowH(24));
    sttModelBox_   .setBounds(rowH(24));
    inputDeviceBox_.setBounds(rowH(24));
}

} // namespace guitar_dsp
