#include "WordSyncSelector.h"

#include "PluginProcessor.h"
#include "audio/WordSyncMode.h"

namespace guitar_dsp {

namespace {
constexpr const char* kLabels[3] = { "Latch", "Advance", "Syllable" };
}

WordSyncSelector::WordSyncSelector(PluginProcessor& p) : processor_(p) {
    setOpaque(true);
    startTimerHz(4);
}

WordSyncSelector::~WordSyncSelector() { stopTimer(); }

void WordSyncSelector::timerCallback() {
    const int now = static_cast<int>(processor_.wordSyncMode());
    if (now != lastActiveIndex_) {
        lastActiveIndex_ = now;
        repaint();
    }
}

juce::Rectangle<int> WordSyncSelector::pillBounds(int index) const {
    auto area = getLocalBounds().reduced(6, 4);
    constexpr int gap = 6;
    const int w = (area.getWidth() - 2 * gap) / 3;
    return juce::Rectangle<int>(area.getX() + index * (w + gap),
                                area.getY(), w, area.getHeight());
}

void WordSyncSelector::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(16, 18, 24));
    const int active = static_cast<int>(processor_.wordSyncMode());
    for (int i = 0; i < 3; ++i) {
        const auto b = pillBounds(i);
        const bool on = (i == active);
        g.setColour(on ? juce::Colour::fromRGB(180, 140, 230)
                       : juce::Colour::fromRGB(34, 38, 46));
        g.fillRoundedRectangle(b.toFloat(), 4.0f);
        g.setColour(on ? juce::Colour::fromRGB(18, 20, 26)
                       : juce::Colour::fromRGB(150, 160, 175));
        g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)
                                 .withStyle(on ? "Bold" : "Regular")});
        g.drawText(kLabels[i], b, juce::Justification::centred);
    }
}

void WordSyncSelector::mouseDown(const juce::MouseEvent& e) {
    for (int i = 0; i < 3; ++i) {
        if (!pillBounds(i).contains(e.getPosition())) continue;
        processor_.setWordSyncMode(static_cast<audio::WordSyncMode>(i));
        repaint();
        return;
    }
}

} // namespace guitar_dsp
