#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class PluginProcessor;

// A thin row of toggle pills used to isolate vocoder behavior and select
// pitch-singing/sing modes:
//   V — Bypass vocoder (hear the raw TTS modulator)
//   N — Noise carrier  (swap the guitar carrier for white noise)
//   S — Sibilance off  (mute the vocoder's noise/sibilance path)
//   P — Pitch sing     (use a pitch-tracked sawtooth as the carrier floor)
//   M — Modulate sing  (vibrato + chromatic semitone quantize on the saw)
// Click a pill or press V / N / S / P / M to toggle. Active toggles are highlighted.
class DiagToggleBar : public juce::Component,
                      private juce::Timer {
public:
    explicit DiagToggleBar(PluginProcessor& p);
    ~DiagToggleBar() override;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    juce::Rectangle<int> pillBounds(int index) const;  // index 0..4

    PluginProcessor& processor_;
};

} // namespace guitar_dsp
