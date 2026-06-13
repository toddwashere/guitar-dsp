#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class PluginProcessor;

// Always-on readout of the speech engines: Apple / Piper / Prebaked
// availability, the active scene's declared source, and the source that
// actually produced the current clip (so fallbacks are visible). Polls the
// processor on a timer (visibility principle).
class TtsStatusBar : public juce::Component, private juce::Timer {
public:
    explicit TtsStatusBar(PluginProcessor& p);
    ~TtsStatusBar() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent& e) override;
private:
    void timerCallback() override { repaint(); }
    PluginProcessor& processor_;
    // Cached pill rectangles, refreshed each paint(), so mouseDown knows
    // which pill the user clicked without re-doing the layout math.
    juce::Rectangle<int> applePillRect_;
    juce::Rectangle<int> piperPillRect_;
    juce::Rectangle<int> prebakedPillRect_;
};

} // namespace guitar_dsp
