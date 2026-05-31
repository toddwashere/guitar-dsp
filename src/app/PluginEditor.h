#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DiagnosticPanel.h"
#include "Oscilloscope.h"
#include "PluginProcessor.h"
#include "SayPanel.h"
#include "SceneIndicator.h"
#include "SpectrumAnalyzer.h"

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
    SayPanel           sayPanel_;
    Oscilloscope       oscilloscope_;
    SpectrumAnalyzer   spectrumAnalyzer_;
};

} // namespace guitar_dsp
