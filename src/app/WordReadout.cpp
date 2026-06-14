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
    const int idx     = processor_.currentSpokenWordIndex();
    const int sceneId = processor_.activeSceneId();
    if (idx != lastIndex_ || sceneId != lastSceneId_) {
        lastIndex_   = idx;
        lastSceneId_ = sceneId;
        repaint();
    }
    // Mic scenes have no cursor or word index to poll — skip per-tick polling.
    if (processor_.activeSceneIsMic()) {
        return;
    }
    if (processor_.activeSceneIsClipBank()) {
        const int curCursor = processor_.clipBankCursor();
        if (curCursor != lastIndex_) {
            lastIndex_ = curCursor;
            repaint();
        }
    }
}

juce::Rectangle<int> WordReadout::rewindButtonBounds() const {
    auto b = getLocalBounds();
    constexpr int btnW = 60, btnH = 18;
    return juce::Rectangle<int>(b.getRight() - btnW - 6, b.getY() + 4, btnW, btnH);
}

void WordReadout::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(12, 13, 18));

    if (processor_.activeSceneIsMic()) {
        auto bounds = getLocalBounds().reduced(8);
        const float h = (float) std::min(bounds.getHeight(),
                                          (int) WordReadout::kCenterBaseHeight * 2);

        g.setColour(juce::Colour::fromRGB(0xE8, 0xE8, 0xE8));
        g.setFont(juce::Font{juce::FontOptions{}.withHeight(h)});
        g.drawFittedText(juce::String::fromUTF8("\xF0\x9F\x8E\xA4 MIC"),
                         bounds.removeFromTop(bounds.getHeight() - kPipStripHeight),
                         juce::Justification::centred, 1);
        // No Rewind pill on mic scenes — nothing to rewind.
        return;
    }

    if (processor_.activeSceneIsClipBank()) {
        const int cursor = processor_.clipBankCursor();
        const int total  = processor_.clipBankSize();
        const auto key   = processor_.clipBankCurrentKey();
        juce::String label;
        if (cursor < 0) {
            label = juce::String("vocal guitar  \xE2\x80\xA2  ")
                  + juce::String(total) + " clips";
        } else {
            label = juce::String(key) + "  \xE2\x80\xA2  "
                  + juce::String(cursor + 1) + " / " + juce::String(total);
        }

        // Centered single-line render — match the same font-height path used
        // by the note-stepped centered-word render so the type size feels
        // consistent. Then draw the Rewind pill and return (skip pip strip).
        auto bounds = getLocalBounds().reduced(8);
        g.setColour(juce::Colour::fromRGB(0xE8, 0xE8, 0xE8));
        const float h = static_cast<float>(
            std::min(bounds.getHeight(),
                     static_cast<int>(WordReadout::kCenterBaseHeight) * 2));
        g.setFont(juce::Font{juce::FontOptions{}.withHeight(h)});
        g.drawFittedText(label,
                         bounds.removeFromTop(bounds.getHeight() - kPipStripHeight),
                         juce::Justification::centred, 1);

        // Rewind pill — same visual approach as the existing button.
        const auto rb = rewindButtonBounds();
        g.setColour(juce::Colour::fromRGB(40, 44, 56));
        g.fillRoundedRectangle(rb.toFloat(), 4.0f);
        g.setColour(juce::Colour::fromRGB(180, 185, 200));
        g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
        g.drawText("Rewind", rb, juce::Justification::centred);

        return;
    }

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
    // drawFittedText squeezes horizontally (down to 50%) when the ramped font
    // overflows the center slot — prevents "DEVELO..." truncation at peak.
    g.drawFittedText(dim(idx), mid, juce::Justification::centred, 1, 0.5f);

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
        processor_.rewindActive();
        repaint();
    }
}

} // namespace guitar_dsp
