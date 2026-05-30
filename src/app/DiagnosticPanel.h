#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class PluginProcessor;

// Single-screen diagnostic readout for the standalone app. Shows audio
// device info, channel configuration, input/output peak meters, and gate
// state. Refreshes at ~30 Hz from a Timer on the message thread; never
// touches the audio thread except through the processor's atomic getters.
class DiagnosticPanel : public juce::Component,
                        private juce::Timer {
public:
    explicit DiagnosticPanel(PluginProcessor& p);
    ~DiagnosticPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void drawMeter(juce::Graphics&, juce::Rectangle<int> bounds,
                   const juce::String& label, float peakLin) const;
    juce::String describeAudioDevice() const;

    PluginProcessor& processor_;

    // Smoothed display values; peak is decayed linearly between updates so
    // the visual movement is calm at 30 Hz.
    float displayInputPeak_  = 0.0f;
    float displayOutputPeak_ = 0.0f;
};

} // namespace guitar_dsp
