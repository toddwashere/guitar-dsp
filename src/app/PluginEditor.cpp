#include "PluginEditor.h"
#include <cstdio>

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
    std::fprintf(stderr, "[PluginEditor] ctor begin\n"); std::fflush(stderr);
    // Default height is sized for the most common scene (chrome hidden) plus
    // a reasonable middle area. Tall scenes (Scene 7 Talk Box) still fit; the
    // user can drag the resize handle to expand. Was 880 — shrunk because
    // per-scene + view toggles now hide most chrome by default.
    setSize(720, 620);
    setResizable(true, true);
    setResizeLimits(520, 420, 1800, 1200);
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

    // Close button + ESC key both route through this — keeps the
    // toggle button state in sync with the overlay's visibility.
    aiSettingsPanel_->onClose = [this] {
        toggleAiSettingsBtn_.setToggleState(false, juce::dontSendNotification);
        aiSettingsPanel_->setVisible(false);
        resized();
    };
    aiSettingsPanel_->onModelChanged = [this](std::string id) {
        processor_.selectModelId(std::move(id));
    };

    setWantsKeyboardFocus(true);
    addKeyListener(this);

    // setSize(720, 880) above triggers resized() before conversationPanel_ and
    // aiSettingsPanel_ are constructed, so they never get bounds. Run resized()
    // once more now that all children exist.
    resized();

    // Poll active scene id; trigger resized() when it flips so the
    // ConversationPanel visibility toggles correctly per scene.
    startTimer(100);
}

void PluginEditor::timerCallback() {
    const int id = processor_.sceneEngine().getActiveSceneId();
    if (id != lastObservedSceneId_) {
        lastObservedSceneId_ = id;
        resized();
    }
}

void PluginEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(18, 20, 26));
}

void PluginEditor::resized() {
    auto bounds = getLocalBounds();

    // --- Per-scene + view-toggle visibility decisions --------------------
    // Resolved up-front so every layout decision below reads from one place.
    const bool showDiag         = processor_.showDiagHeader();
    const bool showScope        = processor_.showScope();
    const bool showChat         = processor_.activeSceneShowsChat();
    const bool showVocoder      = processor_.activeSceneShowsVocoder();
    const bool showSay          = processor_.activeSceneShowsSay();
    const bool showWordReadout  = processor_.activeSceneShowsWordReadout();
    std::fprintf(stderr,
        "[PluginEditor::resized] scene=%d chat=%d vocoder=%d say=%d wordReadout=%d "
        "diag=%d scope=%d\n",
        processor_.sceneEngine().getActiveSceneId(),
        (int)showChat, (int)showVocoder, (int)showSay,
        (int)showWordReadout, (int)showDiag, (int)showScope);
    std::fflush(stderr);

    diagnosticPanel_.setVisible(showDiag);
    wordReadout_   .setVisible(showWordReadout);
    vocoderPanel_  .setVisible(showVocoder);
    ttsStatusBar_  .setVisible(showDiag);  // tts source pills are dev-only
    sayPanel_      .setVisible(showSay);
    oscilloscope_     .setVisible(showScope);
    spectrumAnalyzer_ .setVisible(showScope);
    if (conversationPanel_) conversationPanel_->setVisible(showChat);

    // --- Top fixed band --------------------------------------------------
    if (showDiag) diagnosticPanel_.setBounds(bounds.removeFromTop(62));
    sceneIndicator_.setBounds(bounds.removeFromTop(48));
    if (showWordReadout) wordReadout_.setBounds(bounds.removeFromTop(44));
    diagToggleBar_.setBounds(bounds.removeFromTop(26));
    if (showVocoder) vocoderPanel_.setBounds(bounds.removeFromTop(170));
    if (showDiag) ttsStatusBar_.setBounds(bounds.removeFromTop(24));

    // Share one row between MIDI picker (standalone only) + "Settings"
    // button. Saves a row and groups the two "configure" widgets together.
    // If standalone hidden, Settings gets its own slim row.
    if (midiDevicePicker_.isVisible()) {
        auto controlsRow = bounds.removeFromTop(28);
        toggleAiSettingsBtn_.setBounds(controlsRow.removeFromRight(100));
        controlsRow.removeFromRight(6);  // gap
        midiDevicePicker_.setBounds(controlsRow);
    } else {
        toggleAiSettingsBtn_.setBounds(bounds.removeFromTop(24));
    }

    if (showSay) sayPanel_.setBounds(bounds.removeFromTop(40));

    // --- Middle area: conversation (Scene 4) > scope > empty -------------
    if (showChat) {
        if (conversationPanel_) conversationPanel_->setBounds(bounds);
    } else if (showScope) {
        const int h = bounds.getHeight();
        oscilloscope_.setBounds(bounds.removeFromTop(h / 2));
        spectrumAnalyzer_.setBounds(bounds);
    }
    // else: middle stays empty (painted by editor background).

    if (aiSettingsPanel_ && aiSettingsPanel_->isVisible())
        aiSettingsPanel_->setBounds(getLocalBounds().reduced(40));
}

bool PluginEditor::keyPressed(const juce::KeyPress& key, juce::Component*) {
    const auto kc = key.getKeyCode();

    // ESC closes the AI Settings overlay when it's open.
    if (kc == juce::KeyPress::escapeKey
            && aiSettingsPanel_ && aiSettingsPanel_->isVisible()) {
        if (aiSettingsPanel_->onClose) aiSettingsPanel_->onClose();
        return true;
    }

    // Digit = scene id: '1' -> scene 1 ... '9' -> scene 9, '0' -> scene 0.
    // Matches the on-screen slot labels and the FCB1010's identity
    // program-change -> scene mapping (no more off-by-one).
    if (kc >= '0' && kc <= '9') {
        processor_.sceneEngine().activateScene(kc - '0');
        return true;
    }

    // Vocoder diagnostic toggles + pitch/sing mode flips + word-sync cycle.
    // Matches the on-screen pill labels (V/N/S/P/M in DiagToggleBar and the
    // 3-way mode pills in VocoderPanel).
    switch (key.getTextCharacter()) {
        case 'v': case 'V': processor_.toggleDiagBypassVocoder(); return true;
        case 'n': case 'N': processor_.toggleDiagNoiseCarrier();  return true;
        case 's': case 'S': processor_.toggleDiagSibilanceOff();  return true;
        case 'p': case 'P': processor_.togglePitchSinging();      return true;
        case 'm': case 'M': processor_.toggleSinging();           return true;
        case 'w': case 'W': {
            const int next = (static_cast<int>(processor_.wordSyncMode()) + 1) % 3;
            processor_.setWordSyncMode(static_cast<audio::WordSyncMode>(next));
            return true;
        }
        case 'd': case 'D': processor_.toggleShowDiagHeader(); resized(); return true;
        case 'o': case 'O': processor_.toggleShowScope();      resized(); return true;
        default: break;
    }
    return false;
}

} // namespace guitar_dsp
