#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "VoicePackPicker.h"

#include <functional>
#include <string>
#include <vector>

namespace guitar_dsp::app {

// Controls for the SungDirect path (scene 12): voice-pack picker + formant-tint
// slider + portamento slider + scoop-in slider.  Mirrors the pattern of
// VocoderPanel but is lighter-weight (no processor ref; all state forwarded via
// callbacks so it stays decoupled from the audio graph).
class SungDirectPanel : public juce::Component {
public:
    SungDirectPanel();
    ~SungDirectPanel() override;

    void setVoicePacks(
        const std::vector<std::pair<std::string, std::string>>& packs,
        int activeIdx);

    std::function<void(int)>   onVoicePackChange;
    std::function<void(float)> onFormantTintChange;   // semitones
    std::function<void(float)> onPortamentoMsChange;
    std::function<void(float)> onScoopInMsChange;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    VoicePackPicker picker_;
    juce::Slider    formantTint_;
    juce::Slider    portamento_;
    juce::Slider    scoopIn_;
    juce::Label     formantLabel_, portamentoLabel_, scoopLabel_;
};

} // namespace guitar_dsp::app
