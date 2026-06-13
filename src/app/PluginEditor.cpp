#include "PluginEditor.h"

namespace guitar_dsp {

PluginEditor::PluginEditor(PluginProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processor_(p),
      diagnosticPanel_(p),
      sceneIndicator_(p),
      wordReadout_(p),
      diagToggleBar_(p),
      vocoderPanel_(p),
      ttsStatusBar_(p),
      midiDevicePicker_(p),
      sayPanel_(p),
      oscilloscope_(p),
      spectrumAnalyzer_(p) {
    setSize(720, 880);
    setResizable(true, true);
    setResizeLimits(520, 556, 1800, 1200);
    addAndMakeVisible(diagnosticPanel_);
    addAndMakeVisible(sceneIndicator_);
    addAndMakeVisible(wordReadout_);
    addAndMakeVisible(diagToggleBar_);
    addAndMakeVisible(vocoderPanel_);
    addAndMakeVisible(ttsStatusBar_);
    if (processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        addAndMakeVisible(midiDevicePicker_);
    addAndMakeVisible(sayPanel_);
    addAndMakeVisible(oscilloscope_);
    addAndMakeVisible(spectrumAnalyzer_);

    const bool compact = (processor_.wrapperType == juce::AudioProcessor::wrapperType_AudioUnit);

    conversationPanel_ = std::make_unique<ConversationPanel>(
        processor_.conversationEngine(), processor_.conversationBuffer(), compact);
    addAndMakeVisible(*conversationPanel_);

    aiSettingsPanel_ = std::make_unique<AiSettingsPanel>(
        processor_.appPreferences(),
        processor_.personaRegistry(),
        processor_.httpTransport());
    addChildComponent(*aiSettingsPanel_);   // hidden by default (overlay)

    addAndMakeVisible(toggleAiSettingsBtn_);
    toggleAiSettingsBtn_.setClickingTogglesState(true);
    toggleAiSettingsBtn_.onClick = [this] {
        const bool visible = toggleAiSettingsBtn_.getToggleState();
        aiSettingsPanel_->setVisible(visible);
        if (visible) aiSettingsPanel_->toFront(false);
        resized();
    };

    setWantsKeyboardFocus(true);
    addKeyListener(this);

    // setSize(720, 880) above triggers resized() before conversationPanel_ and
    // aiSettingsPanel_ are constructed, so they never get bounds. Run resized()
    // once more now that all children exist.
    resized();
}

void PluginEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(18, 20, 26));
}

void PluginEditor::resized() {
    auto bounds = getLocalBounds();
    diagnosticPanel_.setBounds(bounds.removeFromTop(62));
    sceneIndicator_.setBounds(bounds.removeFromTop(48));
    wordReadout_.setBounds(bounds.removeFromTop(44));
    diagToggleBar_.setBounds(bounds.removeFromTop(26));
    vocoderPanel_.setBounds(bounds.removeFromTop(106));
    ttsStatusBar_.setBounds(bounds.removeFromTop(24));
    if (midiDevicePicker_.isVisible())
        midiDevicePicker_.setBounds(bounds.removeFromTop(28));
    sayPanel_.setBounds(bounds.removeFromTop(40));

    // Reserve AI Settings toggle and ConversationPanel at the bottom.
    toggleAiSettingsBtn_.setBounds(bounds.removeFromTop(24));
    const bool compact = (processor_.wrapperType == juce::AudioProcessor::wrapperType_AudioUnit);
    const int convHeight = compact ? 80 : 220;
    auto convArea = bounds.removeFromBottom(convHeight);
    if (conversationPanel_) conversationPanel_->setBounds(convArea);

    const int remaining = bounds.getHeight();
    oscilloscope_.setBounds(bounds.removeFromTop(remaining / 2));
    spectrumAnalyzer_.setBounds(bounds);

    if (aiSettingsPanel_ && aiSettingsPanel_->isVisible())
        aiSettingsPanel_->setBounds(getLocalBounds().reduced(40));
}

bool PluginEditor::keyPressed(const juce::KeyPress& key, juce::Component*) {
    const auto kc = key.getKeyCode();

    // Digit = scene id: '1' -> scene 1 ... '9' -> scene 9, '0' -> scene 0.
    // Matches the on-screen slot labels and the FCB1010's identity
    // program-change -> scene mapping (no more off-by-one).
    if (kc >= '0' && kc <= '9') {
        processor_.sceneEngine().activateScene(kc - '0');
        return true;
    }

    // Vocoder diagnostic toggles (case-insensitive). Let the operator isolate
    // by ear why vocoded speech is unintelligible.
    switch (key.getTextCharacter()) {
        case 'v': case 'V': processor_.toggleDiagBypassVocoder(); return true;
        case 'n': case 'N': processor_.toggleDiagNoiseCarrier();  return true;
        case 's': case 'S': processor_.toggleDiagSibilanceOff();  return true;
        default: break;
    }
    return false;
}

} // namespace guitar_dsp
