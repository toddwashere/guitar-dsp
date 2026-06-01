#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class PluginProcessor;

// Large readout of the word the guitar is currently "speaking" in a
// note-triggered scene. Polls the processor on a timer; shows the current
// word centered, with the previous/next words dimmed on either side.
class WordReadout : public juce::Component,
                    private juce::Timer {
public:
    explicit WordReadout(PluginProcessor& processor);
    ~WordReadout() override;

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    PluginProcessor& processor_;
    int lastIndex_ = -2;
};

} // namespace guitar_dsp
