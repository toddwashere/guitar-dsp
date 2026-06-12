#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DiagToggleBar.h"
#include "DiagnosticPanel.h"
#include "MidiDevicePicker.h"
#include "Oscilloscope.h"
#include "PluginProcessor.h"
#include "SayPanel.h"
#include "SceneIndicator.h"
#include "SpectrumAnalyzer.h"
#include "TtsStatusBar.h"
#include "VocoderPanel.h"
#include "WordReadout.h"

namespace guitar_dsp {

class PluginEditor : public juce::AudioProcessorEditor,
                     private juce::KeyListener {
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    // KeyListener::keyPressed(KeyPress, Component*) would otherwise hide
    // Component::keyPressed(KeyPress), triggering -Woverloaded-virtual.
    using juce::Component::keyPressed;
    bool keyPressed(const juce::KeyPress&, juce::Component*) override;

    PluginProcessor& processor_;

    DiagnosticPanel    diagnosticPanel_;
    SceneIndicator     sceneIndicator_;
    WordReadout        wordReadout_;
    DiagToggleBar      diagToggleBar_;
    VocoderPanel       vocoderPanel_;
    TtsStatusBar       ttsStatusBar_;
    MidiDevicePicker   midiDevicePicker_;
    SayPanel           sayPanel_;
    Oscilloscope       oscilloscope_;
    SpectrumAnalyzer   spectrumAnalyzer_;
};

} // namespace guitar_dsp
