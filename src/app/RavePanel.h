#pragma once
#include "audio/AudioGraph.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace guitar_dsp::app {

class RavePanel : public juce::Component, private juce::Timer {
public:
    // Callback the picker fires when the user selects a model. Receives the
    // display name (e.g. "Funk Drums"); the caller is responsible for
    // resolving to an .onnx path and triggering AudioGraph::swapRaveModel.
    using ModelSwapFn = std::function<void(const juce::String&)>;
    // modelNames empty (or onSwap unset) hides the picker — RavePanel still
    // works as a plain readout-and-knobs component.
    explicit RavePanel(audio::AudioGraph& graph,
                       std::vector<juce::String> modelNames = {},
                       ModelSwapFn onSwap = nullptr);
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

    // Model picker (optional — only constructed/shown when modelNames non-empty).
    juce::ComboBox modelPicker_;
    juce::Label    modelPickerLabel_;
    ModelSwapFn    onSwap_;
    bool           hasPicker_ = false;
};

} // namespace guitar_dsp::app
