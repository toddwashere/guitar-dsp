#pragma once
#include "audio/AudioGraph.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp::app {

class RavePanel : public juce::Component, private juce::Timer {
public:
    explicit RavePanel(audio::AudioGraph& graph);
    void resized() override;
    void paint(juce::Graphics& g) override;

    // Test hooks
    juce::String statusPillText() const { return statusPill_.getText(); }
    void refreshNow() { timerCallback(); }
    void setGateForTest(float db)     { gateSlider_.setValue(db); }
    void setPresenceForTest(float a)  { presenceSlider_.setValue(a); }
    void setDriveForTest(float db)    { driveSlider_.setValue(db); }
    float gateValue() const     { return (float)gateSlider_.getValue(); }
    float presenceValue() const { return (float)presenceSlider_.getValue(); }
    float driveValue() const    { return (float)driveSlider_.getValue(); }

private:
    void timerCallback() override;

    audio::AudioGraph& graph_;

    juce::Slider gateSlider_;
    juce::Slider presenceSlider_;
    juce::Slider driveSlider_;
    juce::Label  gateLabel_, presenceLabel_, driveLabel_;
    juce::Label  statusPill_;
    juce::Label  latencyLabel_;
    juce::Label  inputMeter_;
    juce::Label  outputMeter_;
};

} // namespace guitar_dsp::app
