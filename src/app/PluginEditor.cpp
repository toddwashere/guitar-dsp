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
      spectrumAnalyzer_(p),
      waveformView_(p),
      micScopeView_(p) {
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
    addAndMakeVisible(sungDirectPanel_);
    addAndMakeVisible(ttsStatusBar_);
    if (processor_.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        addAndMakeVisible(midiDevicePicker_);
    addAndMakeVisible(sayPanel_);
    addAndMakeVisible(oscilloscope_);
    addAndMakeVisible(spectrumAnalyzer_);
    addAndMakeVisible(waveformView_);
    addAndMakeVisible(micScopeView_);

    processor_.setStatusBar(&ttsStatusBar_);
    processor_.setSayPanel(&sayPanel_);

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

    addAndMakeVisible(qaButton_);
    qaButton_.setClickingTogglesState(true);
    qaButton_.onClick = [this] {
        const bool wantsQa = qaButton_.getToggleState();
        if (wantsQa) {
            previousPersona_ = processor_.currentPersonaId();
            processor_.setCurrentPersona(ai::PersonaId::SessionQa);
        } else {
            processor_.setCurrentPersona(previousPersona_);
        }
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

PluginEditor::~PluginEditor() {
    processor_.setStatusBar(nullptr);
    processor_.setSayPanel(nullptr);
}

void PluginEditor::timerCallback() {
    const auto persona = processor_.currentPersonaId();
    const bool inQa = persona == ai::PersonaId::SessionQa;
    // Track the most recent non-Q&A persona so the toggle-off path restores
    // whatever the user was actually using, regardless of whether they
    // entered Q&A via the button or the dropdown.
    if (!inQa) previousPersona_ = persona;
    if (qaButton_.getToggleState() != inQa) {
        qaButton_.setToggleState(inQa, juce::dontSendNotification);
    }

    // Push SungDirect background-load status into the panel every tick
    // (cheap — the panel ignores no-op updates). Visible only when the
    // panel itself is visible (scene 12), so this is a small per-tick
    // atomic read on other scenes.
    {
        const auto raw = processor_.sungDirectLoadState();
        guitar_dsp::app::SungDirectPanel::LoadStatus uiStatus = guitar_dsp::app::SungDirectPanel::LoadStatus::Idle;
        switch (raw) {
            case PluginProcessor::SungDirectLoadState::Loading:
                uiStatus = guitar_dsp::app::SungDirectPanel::LoadStatus::Loading; break;
            case PluginProcessor::SungDirectLoadState::Ready:
                uiStatus = guitar_dsp::app::SungDirectPanel::LoadStatus::Ready;   break;
            case PluginProcessor::SungDirectLoadState::Idle:
            default: break;
        }
        sungDirectPanel_.setLoadStatus(uiStatus,
                                       processor_.sungDirectLoadProgressPercent());
    }

    const int id = processor_.sceneEngine().getActiveSceneId();
    if (id != lastObservedSceneId_) {
        lastObservedSceneId_ = id;

        // Wire voice-pack pickers once per scene change (not every resized()).
        // setVoicePacks internally hides the picker for scenes with no voicePacks.
        const auto& s = processor_.sceneEngine().getActiveScene();
        {
            std::vector<std::pair<std::string, std::string>> packs;
            packs.reserve(s.voicePacks.size());
            for (const auto& vp : s.voicePacks)
                packs.emplace_back(vp.label, vp.path);
            if (s.showVoicePackPicker) {
                vocoderPanel_.setVoicePacks(packs, processor_.activeVoiceIndex());
                vocoderPanel_.setOnVoicePackChange(
                    [this](int idx) { processor_.setActiveVoiceIndex(idx); });
            } else {
                vocoderPanel_.setVoicePacks({}, 0);
            }

            if (s.showSungDirectPanel) {
                sungDirectPanel_.setVoicePacks(packs, processor_.activeVoiceIndex());
                sungDirectPanel_.onVoicePackChange = [this](int idx) {
                    processor_.setActiveVoiceIndex(idx);
                };
                sungDirectPanel_.onFormantTintChange = [this](float n) {
                    processor_.setSungDirectFormantTintSemitones(n);
                };
                sungDirectPanel_.onPortamentoMsChange = [this](float ms) {
                    processor_.setSungDirectPortamentoMs(ms);
                };
                sungDirectPanel_.onScoopInMsChange = [](float ms) {
                    /* scoop-in hookup is a future task */ (void)ms;
                };
            }
        }

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
    const bool showKnobs        = processor_.showKnobs();
    const bool showScope        = processor_.showScope();
    const bool showChat         = processor_.activeSceneShowsChat();
    const bool showSay          = processor_.activeSceneShowsSay();
    const bool showWordReadout  = processor_.activeSceneShowsWordReadout();
    // Waveform + slice overlay: v2 phoneme-stepped scenes (4, 10) always show
    // the slice view; v1 speaking scenes (1, 2, 6, 9) show it too so the
    // audience can see the word/syllable boundaries vs the energy-aware v2
    // slices. 'L' key hides it on either class of scene.
    const int  activeSceneId     = processor_.activeSceneId();
    const bool isV1WaveformScene = (activeSceneId == 0 || activeSceneId == 1
                                 || activeSceneId == 2 || activeSceneId == 6
                                 || activeSceneId == 9);
    const bool showWaveform      = (processor_.activeSceneIsPhoneme() || isV1WaveformScene)
                                   && processor_.showSlices();
    // Scene 7 (Talk Box) has tts.source == "mic" — no static clip to display.
    // Show the live scrolling mic-input scope instead. Mutually exclusive with
    // the static waveform view (scene 7 is never a waveform scene).
    const bool showMicScope = (activeSceneId == 7);
    std::fprintf(stderr,
        "[PluginEditor::resized] scene=%d chat=%d say=%d wordReadout=%d "
        "knobs=%d scope=%d\n",
        processor_.sceneEngine().getActiveSceneId(),
        (int)showChat, (int)showSay,
        (int)showWordReadout, (int)showKnobs, (int)showScope);
    std::fflush(stderr);

    diagnosticPanel_.setVisible(true);  // always-on now
    wordReadout_   .setVisible(showWordReadout);
    vocoderPanel_  .setVisible(showKnobs);
    ttsStatusBar_  .setVisible(false);  // TTS source pills are dev-only; off in stage UI

    // SungDirectPanel: shown only when the active scene requests it (scene 12).
    // Callback wiring is done once per scene change in timerCallback(), not here.
    sungDirectPanel_.setVisible(processor_.sceneEngine().getActiveScene().showSungDirectPanel);
    sayPanel_      .setVisible(showSay);
    oscilloscope_     .setVisible(showScope);
    spectrumAnalyzer_ .setVisible(showScope);
    waveformView_     .setVisible(showWaveform);
    micScopeView_     .setVisible(showMicScope);
    if (conversationPanel_) conversationPanel_->setVisible(showChat);

    // --- Top fixed band --------------------------------------------------
    diagnosticPanel_.setBounds(bounds.removeFromTop(62));
    sceneIndicator_.setBounds(bounds.removeFromTop(48));
    if (showWordReadout) wordReadout_.setBounds(bounds.removeFromTop(44));
    diagToggleBar_.setBounds(bounds.removeFromTop(26));
    if (showKnobs) vocoderPanel_.setBounds(bounds.removeFromTop(170));
    if (sungDirectPanel_.isVisible()) sungDirectPanel_.setBounds(bounds.removeFromTop(108));

    // Waveform + slice overlay sits BETWEEN the knobs and the scope so
    // when v2 is active the boundary lines are the most prominent thing
    // on screen — the audience reads "here is the clip, here is where it
    // cuts." Off when the active scene isn't phoneme-stepped or 'L' is
    // toggled off.
    if (showWaveform)  waveformView_.setBounds(bounds.removeFromTop(120));
    if (showMicScope)  micScopeView_.setBounds(bounds.removeFromTop(120));

    // Scope (oscilloscope + spectrum) sits in the top stack, right under
    // knobs and ABOVE the MIDI/say/chat row. This way toggling K or O
    // doesn't shift the lower controls around — they only move when a
    // new top section appears, not when the middle changes per scene.
    // Fixed 200 px split evenly between scope (top) and spectrum (bottom).
    if (showScope) {
        auto scopeArea = bounds.removeFromTop(200);
        oscilloscope_.setBounds(scopeArea.removeFromTop(100));
        spectrumAnalyzer_.setBounds(scopeArea);
    }

    // Share one row between MIDI picker (standalone only) + "Settings"
    // button. Saves a row and groups the two "configure" widgets together.
    // If standalone hidden, Settings gets its own slim row.
    if (midiDevicePicker_.isVisible()) {
        auto controlsRow = bounds.removeFromTop(28);
        toggleAiSettingsBtn_.setBounds(controlsRow.removeFromRight(100));
        controlsRow.removeFromRight(4);
        qaButton_.setBounds(controlsRow.removeFromRight(80));
        controlsRow.removeFromRight(6);  // gap
        midiDevicePicker_.setBounds(controlsRow);
    } else {
        toggleAiSettingsBtn_.setBounds(bounds.removeFromTop(24));
        qaButton_.setBounds(bounds.removeFromTop(24));
    }

    if (showSay) sayPanel_.setBounds(bounds.removeFromTop(40));

    // --- Middle area ----------------------------------------------------
    // Conversation panel gets up to 280 px (~10 lines of transcript +
    // chrome). Doubled from 140 to make the chat readable when the LLM
    // gives a longer reply. Still capped so it doesn't stretch past
    // what the window can offer.
    if (showChat) {
        const int convHeight = std::min(bounds.getHeight(), 280);
        if (conversationPanel_) conversationPanel_->setBounds(bounds.removeFromTop(convHeight));
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
        case 'k': case 'K': processor_.toggleShowKnobs();  resized(); return true;
        case 'o': case 'O': processor_.toggleShowScope();  resized(); return true;
        case 'l': case 'L': processor_.toggleShowSlices(); resized(); return true;
        default: break;
    }
    return false;
}

} // namespace guitar_dsp
