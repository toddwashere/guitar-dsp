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
    void mouseDown(const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    // The N-slot selector strip in local coordinates (N = sceneCount, capped at 16).
    // Shared by paint() and mouseDown() so the clickable regions always match what's drawn.
    juce::Rectangle<int> stripArea() const;

    PluginProcessor& processor_;
};

} // namespace guitar_dsp
