#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class PluginProcessor;

// Three pills in a horizontal row: [ Latch ] [ Advance ] [ Syllable ].
// Active mode is highlighted; clicking switches modes. Polls the
// processor at 4 Hz so external changes (per-scene override) reflect.
class WordSyncSelector : public juce::Component, private juce::Timer {
public:
    explicit WordSyncSelector(PluginProcessor& p);
    ~WordSyncSelector() override;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<int> pillBounds(int index) const;  // 0..2

    PluginProcessor& processor_;
    int lastActiveIndex_ = -1;
};

} // namespace guitar_dsp
