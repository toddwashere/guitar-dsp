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

    // Display a transient muted-grey message overlaying the regular
    // status text for `durationMs` milliseconds. Used by the
    // GspeakBundle Load/Save paths and scene-activation auto-load.
    // Message thread only.
    void flashMessage(juce::String message, int durationMs = 5000);
private:
    void timerCallback() override;
    PluginProcessor& processor_;
    // Cached pill rectangles, refreshed each paint(), so mouseDown knows
    // which pill the user clicked without re-doing the layout math.
    juce::Rectangle<int> applePillRect_;
    juce::Rectangle<int> piperPillRect_;
    juce::Rectangle<int> prebakedPillRect_;

    juce::String flashText_;
    juce::int64  flashUntilMs_ = 0;
};

} // namespace guitar_dsp
