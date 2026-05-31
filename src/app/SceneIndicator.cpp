#include "SceneIndicator.h"

#include "PluginProcessor.h"
#include "scenes/SceneEngine.h"

namespace guitar_dsp {

SceneIndicator::SceneIndicator(PluginProcessor& p) : processor_(p) {
    setOpaque(true);
    startTimerHz(15);
}

SceneIndicator::~SceneIndicator() { stopTimer(); }

void SceneIndicator::timerCallback() { repaint(); }

void SceneIndicator::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.fillAll(juce::Colour::fromRGB(18, 20, 26));

    const auto& engine = processor_.sceneEngine();
    const int activeId = engine.getActiveSceneId();
    const auto& activeScene = engine.getActiveScene();

    // Active scene name + id on the left half.
    auto leftHalf = bounds.reduced(8, 4).removeFromLeft(bounds.getWidth() / 2);
    g.setColour(juce::Colour::fromRGB(150, 160, 175));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
    g.drawText("Scene", leftHalf, juce::Justification::topLeft);

    auto activeColour = juce::Colour{0xFF000000u | activeScene.colorRgb};
    g.setColour(activeColour);
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(16.0f).withStyle("Bold")});
    g.drawText(juce::String(activeId) + "  " + juce::String(activeScene.name),
               leftHalf.withTrimmedTop(12),
               juce::Justification::topLeft);

    // 10-slot strip on the right half.
    auto stripArea = bounds.reduced(8, 8).removeFromRight(bounds.getWidth() / 2 - 16);
    const int slotWidth = stripArea.getWidth() / 10;

    for (int i = 0; i < 10; ++i) {
        auto slot = juce::Rectangle<int>(stripArea.getX() + i * slotWidth,
                                          stripArea.getY(),
                                          slotWidth - 2,
                                          stripArea.getHeight());
        const bool isActive = (i == activeId);
        g.setColour(isActive ? activeColour
                             : juce::Colour::fromRGB(40, 44, 52));
        g.fillRoundedRectangle(slot.toFloat(), 3.0f);

        g.setColour(isActive ? juce::Colour::fromRGB(20, 22, 28)
                             : juce::Colour::fromRGB(150, 160, 175));
        g.setFont(juce::Font{juce::FontOptions{}.withHeight(12.0f)});
        g.drawText(juce::String(i), slot, juce::Justification::centred);
    }
}

} // namespace guitar_dsp
