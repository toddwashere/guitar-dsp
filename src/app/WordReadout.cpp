#include "WordReadout.h"

#include "PluginProcessor.h"

#include <algorithm>

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
    const int  idx   = processor_.currentSpokenWordIndex();
    const auto sceneRgb = processor_.activeSceneColorRgb();
    const auto sceneColor = juce::Colour::fromRGB(
        static_cast<juce::uint8>((sceneRgb >> 16) & 0xFFu),
        static_cast<juce::uint8>((sceneRgb >> 8)  & 0xFFu),
        static_cast<juce::uint8>( sceneRgb        & 0xFFu));

    auto area = getLocalBounds();
    auto pipStrip = area.removeFromBottom(kPipStripHeight);

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
        // Idle pip strip: still draw N upcoming pips if there are scene words,
        // so the operator sees the chant length before the first pluck.
        const int n = static_cast<int>(words.size());
        if (n > 0) {
            const float w = static_cast<float>(pipStrip.getWidth());
            const float cy = pipStrip.getCentreY();
            const float r = kPipDiameter * 0.5f;
            for (int i = 0; i < n; ++i) {
                const float cx = w * (i + 0.5f) / n;
                g.setColour(sceneColor.withAlpha(kPipAlphaUpcoming));
                g.fillEllipse(cx - r, cy - r, kPipDiameter, kPipDiameter);
            }
        }
        return;
    }

    auto dim = [&](int i) {
        return (i >= 0 && i < static_cast<int>(words.size()))
                 ? juce::String(words[static_cast<std::size_t>(i)]) : juce::String();
    };

    const int n = static_cast<int>(words.size());

    const int third = area.getWidth() / 3;
    auto left  = area.removeFromLeft(third);
    auto right = area.removeFromRight(third);
    auto mid   = area;

    g.setColour(juce::Colour::fromRGB(70, 74, 88));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(18.0f)});
    g.drawText(dim(idx - 1), left,  juce::Justification::centredRight);
    g.drawText(dim(idx + 1), right, juce::Justification::centredLeft);

    // Center word — intensity ramp driven by progress through the chant.
    const float denom = static_cast<float>(std::max(1, n - 1));
    const float progress = std::clamp(static_cast<float>(idx) / denom, 0.0f, 1.0f);

    const float fontH = kCenterBaseHeight * (1.0f + kCenterGrowFactor * progress);
    const auto peakColor = juce::Colour::fromRGB(kPeakColorR, kPeakColorG, kPeakColorB);
    const auto centerColor = sceneColor.interpolatedWith(peakColor, progress);

    g.setColour(centerColor);
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(fontH).withStyle("Bold")});
    g.drawText(dim(idx), mid, juce::Justification::centred);

    // Pip strip: N pips evenly distributed. Past=dim, current=full, upcoming=very dim.
    const float w = static_cast<float>(pipStrip.getWidth());
    const float cy = static_cast<float>(pipStrip.getCentreY());
    const float r = kPipDiameter * 0.5f;
    for (int i = 0; i < n; ++i) {
        const float cx = w * (i + 0.5f) / n;
        float a;
        if      (i <  idx) a = kPipAlphaCompleted;
        else if (i == idx) a = kPipAlphaCurrent;
        else               a = kPipAlphaUpcoming;
        g.setColour(sceneColor.withAlpha(a));
        g.fillEllipse(cx - r, cy - r, kPipDiameter, kPipDiameter);
    }
}

void WordReadout::mouseDown(const juce::MouseEvent& e) {
    if (rewindButtonBounds().contains(e.getPosition())) {
        processor_.rewindSpoken();
        repaint();
    }
}

} // namespace guitar_dsp
