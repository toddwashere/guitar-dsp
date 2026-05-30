#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DiagnosticPanel.h"
#include "PluginProcessor.h"

namespace guitar_dsp {

class PluginEditor : public juce::AudioProcessorEditor {
public:
    explicit PluginEditor(PluginProcessor&);
    ~PluginEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    PluginProcessor& processor_;
    DiagnosticPanel diagnosticPanel_;
};

} // namespace guitar_dsp
