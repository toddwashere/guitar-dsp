#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace guitar_dsp {

class PluginProcessor;

// Live scrolling waveform of the mic input. Polls
// PluginProcessor::micScopeBuffer() at 30 Hz and renders the last
// ~1 second. Used on Scene 7 (Talk Box) where the mic is the
// vocoder modulator and there is no static TTS clip to display.
class MicScopeView : public juce::Component,
                     private juce::Timer {
public:
    explicit MicScopeView(PluginProcessor& p);
    ~MicScopeView() override;
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    PluginProcessor& processor_;
    std::vector<float> snapshot_;  // resized in constructor
};

} // namespace guitar_dsp
