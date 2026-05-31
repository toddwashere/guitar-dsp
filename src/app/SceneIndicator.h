#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class PluginProcessor;

class SceneIndicator : public juce::Component,
                       private juce::Timer {
public:
    explicit SceneIndicator(PluginProcessor& p);
    ~SceneIndicator() override;

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    PluginProcessor& processor_;
};

} // namespace guitar_dsp
