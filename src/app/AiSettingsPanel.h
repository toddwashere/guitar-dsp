#pragma once
#include "ai/AppPreferences.h"
#include "ai/PersonaRegistry.h"
#include "ai/IHttpTransport.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class AiSettingsPanel : public juce::Component {
public:
    AiSettingsPanel(ai::AppPreferences&, ai::PersonaRegistry&, ai::IHttpTransport&);

    void paint(juce::Graphics&) override;
    void resized() override;

    // Test-facing helpers
    int         modelDropdownItemCount() const { return modelBox_.getNumItems(); }
    void        selectPersona(ai::PersonaId);
    void        setPromptForTest(const juce::String& s);
    std::string editablePromptText() const { return promptEditor_.getText().toStdString(); }
    std::string apiKeyFieldText()    const { return apiKeyField_.getText().toStdString(); }
    void        refreshOllama();
    void        selectModel(std::string modelId);
    std::string modelStatusText() const { return modelStatus_.getText().toStdString(); }

private:
    void populateBaseModels();
    void onPersonaChanged();

    ai::AppPreferences&   prefs_;
    ai::PersonaRegistry&  personas_;
    ai::IHttpTransport&   http_;

    juce::ComboBox        modelBox_;
    juce::Label           modelStatus_;
    juce::TextButton      refreshBtn_     {"Refresh Ollama"};
    juce::TextButton      testBtn_        {"Test Anthropic"};
    juce::TextEditor      apiKeyField_;
    juce::ComboBox        personaBox_;
    juce::TextEditor      promptEditor_;
    juce::TextButton      resetPromptBtn_ {"Reset to default"};
    juce::Slider          maxSentencesSlider_, maxWordsSlider_;
    juce::ComboBox        sttModelBox_;
    juce::ComboBox        inputDeviceBox_;
};

} // namespace guitar_dsp
