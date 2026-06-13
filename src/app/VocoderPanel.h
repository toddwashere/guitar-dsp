#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class PluginProcessor;

// Live vocoder controls — makeup gain, broadband carrier-noise floor,
// sibilance, and the per-scene clarity blend — for dialing in speech
// intelligibility and for live demonstration. A permanent panel (not just
// diagnostics). Visibility: the Clarity label tracks the active scene's
// authored default so the operator can see when the live slider has drifted.
class VocoderPanel : public juce::Component, private juce::Timer {
public:
    explicit VocoderPanel(PluginProcessor& p);
    ~VocoderPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void configureSlider(juce::Slider& s, juce::Label& l, const juce::String& name);
    void timerCallback() override;

    PluginProcessor& processor_;
    juce::Slider makeup_, carrierNoise_, sibilance_, clarity_, gateThreshold_;
    juce::Label  makeupLabel_, carrierNoiseLabel_, sibilanceLabel_, clarityLabel_,
                 gateThresholdLabel_;
    float lastSceneClarity_ = -1.0f;  // sentinel — forces first paint
};

} // namespace guitar_dsp
