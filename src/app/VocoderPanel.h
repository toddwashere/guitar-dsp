#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class PluginProcessor;

// Live vocoder controls — makeup gain, broadband carrier-noise floor, and
// sibilance — for dialing in speech intelligibility and for live
// demonstration. A permanent panel (not just diagnostics).
class VocoderPanel : public juce::Component {
public:
    explicit VocoderPanel(PluginProcessor& p);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void configureSlider(juce::Slider& s, juce::Label& l, const juce::String& name);

    PluginProcessor& processor_;
    juce::Slider makeup_, carrierNoise_, sibilance_, clarity_;
    juce::Label  makeupLabel_, carrierNoiseLabel_, sibilanceLabel_, clarityLabel_;
};

} // namespace guitar_dsp
