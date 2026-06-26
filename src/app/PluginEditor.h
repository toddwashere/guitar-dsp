#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "AiSettingsPanel.h"
#include "ConversationPanel.h"
#include "DiagToggleBar.h"
#include "DiagnosticPanel.h"
#include "MidiDevicePicker.h"
#include "Oscilloscope.h"
#include "PluginProcessor.h"
#include "SayPanel.h"
#include "SceneIndicator.h"
#include "SpectrumAnalyzer.h"
#include "SungDirectPanel.h"
#include "TtsStatusBar.h"
#include "VocoderPanel.h"
#include "RavePanel.h"
#include "MicScopeView.h"
#include "WaveformView.h"
#include "WordReadout.h"

namespace guitar_dsp {

class PluginEditor : public juce::AudioProcessorEditor,
                     private juce::KeyListener,
                     private juce::Timer {
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // KeyListener::keyPressed(KeyPress, Component*) would otherwise hide
    // Component::keyPressed(KeyPress), triggering -Woverloaded-virtual.
    using juce::Component::keyPressed;
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;

    // Polls the active scene id so we can trigger resized() when the
    // active scene's showChat flag flips — ConversationPanel needs to
    // appear/disappear and the oscilloscope area needs to expand/contract.
    void timerCallback() override;
    int  lastObservedSceneId_ = -1;

    PluginProcessor& processor_;

    DiagnosticPanel        diagnosticPanel_;
    SceneIndicator         sceneIndicator_;
    WordReadout            wordReadout_;
    DiagToggleBar          diagToggleBar_;
    VocoderPanel           vocoderPanel_;
    app::SungDirectPanel   sungDirectPanel_;
    TtsStatusBar           ttsStatusBar_;
    MidiDevicePicker   midiDevicePicker_;
    SayPanel           sayPanel_;
    Oscilloscope       oscilloscope_;
    SpectrumAnalyzer   spectrumAnalyzer_;
    WaveformView       waveformView_;
    MicScopeView       micScopeView_;

    std::unique_ptr<app::RavePanel>    ravePanel_;
    std::unique_ptr<ConversationPanel> conversationPanel_;
    std::unique_ptr<AiSettingsPanel>   aiSettingsPanel_;
    juce::TextButton                   toggleAiSettingsBtn_ {"Settings"};
    juce::TextButton                   qaButton_ {"Q&A"};

    // Persona to restore when the Q&A toggle is turned off. Captured at the
    // moment the toggle is engaged; reflects whatever was active before.
    ai::PersonaId                      previousPersona_ {ai::PersonaId::Interviewer};
};

} // namespace guitar_dsp
