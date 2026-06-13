#include "WordReadout.h"

#include "PluginProcessor.h"

namespace guitar_dsp {

WordReadout::WordReadout(PluginProcessor& processor) : processor_(processor) {
    setOpaque(true);
    startTimerHz(30);
}

WordReadout::~WordReadout() { stopTimer(); }

void WordReadout::timerCallback() {
    const int idx = processor_.currentSpokenWordIndex();
    if (idx != lastIndex_) { lastIndex_ = idx; repaint(); }
}

juce::Rectangle<int> WordReadout::rewindButtonBounds() const {
    auto b = getLocalBounds();
    constexpr int btnW = 60, btnH = 18;
    return juce::Rectangle<int>(b.getRight() - btnW - 6, b.getY() + 4, btnW, btnH);
}

void WordReadout::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(12, 13, 18));

    const auto words = processor_.activeSceneWords();
    const int idx = processor_.currentSpokenWordIndex();
    auto area = getLocalBounds();

    // Always draw the rewind button (corner) so it's discoverable.
    const auto rb = rewindButtonBounds();
    g.setColour(juce::Colour::fromRGB(40, 44, 56));
    g.fillRoundedRectangle(rb.toFloat(), 4.0f);
    g.setColour(juce::Colour::fromRGB(180, 185, 200));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
    g.drawText("Rewind", rb, juce::Justification::centred);

    if (words.empty() || idx < 0 || idx >= static_cast<int>(words.size())) {
        g.setColour(juce::Colour::fromRGB(90, 95, 110));
        g.setFont(juce::Font{juce::FontOptions{}.withHeight(16.0f)});
        g.drawText("(pluck a note to speak)", area, juce::Justification::centred);
        return;
    }

    auto dim = [&](int i) {
        return (i >= 0 && i < static_cast<int>(words.size()))
                 ? juce::String(words[static_cast<std::size_t>(i)]) : juce::String();
    };

    const int third = area.getWidth() / 3;
    auto left  = area.removeFromLeft(third);
    auto right = area.removeFromRight(third);
    auto mid   = area;

    g.setColour(juce::Colour::fromRGB(70, 74, 88));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(18.0f)});
    g.drawText(dim(idx - 1), left,  juce::Justification::centredRight);
    g.drawText(dim(idx + 1), right, juce::Justification::centredLeft);

    g.setColour(juce::Colour::fromRGB(240, 230, 180));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(34.0f).withStyle("Bold")});
    g.drawText(dim(idx), mid, juce::Justification::centred);
}

void WordReadout::mouseDown(const juce::MouseEvent& e) {
    if (rewindButtonBounds().contains(e.getPosition())) {
        processor_.rewindSpoken();
        repaint();
    }
}

} // namespace guitar_dsp
