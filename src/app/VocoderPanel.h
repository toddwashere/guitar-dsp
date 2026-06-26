#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "NoteReadout.h"
#include "VoicePackPicker.h"
#include "VowelMaskPills.h"
#include "WordSyncSelector.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

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
    juce::Slider makeup_, carrierNoise_, sibilance_, clarity_, sensitivity_, micGain_;
    juce::Label  makeupLabel_, carrierNoiseLabel_, sibilanceLabel_, clarityLabel_,
                 sensitivityLabel_, micGainLabel_;
    float lastSceneClarity_ = -1.0f;  // sentinel — forces first paint
    float lastMicPeak_      = 0.0f;
    int   lastMicSource_    = -1;     // -1 forces first paint
    NoteReadout  noteReadout_;
    WordSyncSelector wordSyncSelector_;
    juce::String lastCarrierNoiseLabel_;
    app::VoicePackPicker voicePackPicker_;
    app::VowelMaskPills  vowelPills_;

public:
    void setVoicePacks(const std::vector<std::pair<std::string, std::string>>& packs,
                       int activeIdx);
    void setOnVoicePackChange(std::function<void(int)> cb);
    // Vowel-pill controls — visible whenever the voice-pack picker is.
    void setVowelMask(std::uint32_t mask) { vowelPills_.setMask(mask); }
    void setOnVowelMaskChange(std::function<void(std::uint32_t)> cb);
    // Whether to show the vowel pills row (typically follows the same
    // flag that shows the voice-pack picker — only the sung-vowel scenes).
    void setVowelPillsVisible(bool visible) { vowelPills_.setVisible(visible); }
};

} // namespace guitar_dsp
