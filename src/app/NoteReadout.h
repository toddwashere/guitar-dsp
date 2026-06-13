#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class PluginProcessor;

// Live readout of the pitch detected by AudioGraph: note name + octave (big),
// cents offset (small), Hz (small). Dims when unvoiced + hold expired. Runs
// at 30 Hz off a juce::Timer; reads three atomics on the processor.
class NoteReadout : public juce::Component, private juce::Timer {
public:
    explicit NoteReadout(PluginProcessor& p);
    ~NoteReadout() override;

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    PluginProcessor& processor_;
    int   midiNote_ = -1;
    float cents_    = 0.0f;
    float hz_       = 0.0f;
};

} // namespace guitar_dsp
