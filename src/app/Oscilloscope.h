#pragma once

#include <array>

#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class PluginProcessor;

// Live waveform display of the post-DSP mono signal. Pulls a window of
// samples from the processor's ring buffer at ~30 Hz and renders them as
// a polyline. Trigger-aligned to a rising zero-crossing so the waveform
// doesn't visibly drift between frames.
class Oscilloscope : public juce::Component,
                     private juce::Timer {
public:
    explicit Oscilloscope(PluginProcessor& p);
    ~Oscilloscope() override;

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    static constexpr int kSnapshotSize = 2048;
    static constexpr int kDisplaySize  = 1024;

    PluginProcessor& processor_;
    std::array<float, kSnapshotSize> snapshot_{};
};

} // namespace guitar_dsp
