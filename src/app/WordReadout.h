#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class PluginProcessor;

// Large readout of the word the guitar is currently "speaking" in a
// note-triggered scene. Polls the processor on a timer; shows the current
// word centered, with the previous/next words dimmed on either side.
// Also displays a pip-row at the bottom: one small circle per word, colored
// to show progress (past=dim, current=bright, upcoming=very dim).
class WordReadout : public juce::Component,
                    private juce::Timer {
public:
    explicit WordReadout(PluginProcessor& processor);
    ~WordReadout() override;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    // Layout/ramp constants — exposed for tests, never changed at runtime.
    static constexpr int   kPipStripHeight    = 18;
    static constexpr int   kPipDiameter       = 10;
    static constexpr float kPipAlphaCompleted = 0.45f;
    static constexpr float kPipAlphaUpcoming  = 0.15f;
    static constexpr float kPipAlphaCurrent   = 1.0f;

    // Center-word ramp (used in Task 6 — declared here so tests can reference).
    static constexpr float kCenterBaseHeight = 34.0f;
    static constexpr float kCenterGrowFactor = 0.6f;
    static constexpr juce::uint8 kPeakColorR = 0xFF;
    static constexpr juce::uint8 kPeakColorG = 0x30;
    static constexpr juce::uint8 kPeakColorB = 0x30;

private:
    void timerCallback() override;
    juce::Rectangle<int> rewindButtonBounds() const;

    PluginProcessor& processor_;
    int lastIndex_ = -2;
};

} // namespace guitar_dsp
