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

// Small numeric "N / total" line directly under the Rewind button.
static juce::Rectangle<int> progressIndicatorBounds(const juce::Rectangle<int>& rb) {
    return juce::Rectangle<int>(rb.getX(), rb.getBottom() + 2, rb.getWidth(), 14);
}

static void drawRewindButton(juce::Graphics& g, const juce::Rectangle<int>& rb) {
    g.setColour(juce::Colour::fromRGB(40, 44, 56));
    g.fillRoundedRectangle(rb.toFloat(), 4.0f);
    g.setColour(juce::Colour::fromRGB(180, 185, 200));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
    g.drawText("Rewind", rb, juce::Justification::centred);
}

static void drawProgressNumber(juce::Graphics& g, const juce::Rectangle<int>& rb,
                                const juce::String& label) {
    const auto pb = progressIndicatorBounds(rb);
    g.setColour(juce::Colour::fromRGB(140, 145, 160));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
    g.drawText(label, pb, juce::Justification::centred);
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
                         bounds,
                         juce::Justification::centred, 1);
        return;
    }

    if (processor_.activeSceneIsClipBank()) {
        const int cursor = processor_.clipBankCursor();
        const int total  = processor_.clipBankSize();
        const auto key   = processor_.clipBankCurrentKey();
        juce::String centerLabel;
        if (cursor < 0) {
            centerLabel = juce::String("vocal guitar");
        } else {
            centerLabel = juce::String(key);
        }

        auto bounds = getLocalBounds().reduced(8);
        g.setColour(juce::Colour::fromRGB(0xE8, 0xE8, 0xE8));
        const float h = static_cast<float>(
            std::min(bounds.getHeight(),
                     static_cast<int>(WordReadout::kCenterBaseHeight) * 2));
        g.setFont(juce::Font{juce::FontOptions{}.withHeight(h)});
        g.drawFittedText(centerLabel, bounds, juce::Justification::centred, 1);

        const auto rb = rewindButtonBounds();
        drawRewindButton(g, rb);
        const juce::String num = (cursor < 0)
            ? (juce::String(total) + " clips")
            : (juce::String(cursor + 1) + " / " + juce::String(total));
        drawProgressNumber(g, rb, num);
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

    const auto rb = rewindButtonBounds();
    drawRewindButton(g, rb);

    const int n = static_cast<int>(words.size());

    if (words.empty() || idx < 0 || idx >= n) {
        g.setColour(juce::Colour::fromRGB(90, 95, 110));
        g.setFont(juce::Font{juce::FontOptions{}.withHeight(16.0f)});
        g.drawText("(pluck a note to speak)", area, juce::Justification::centred);
        if (n > 0) drawProgressNumber(g, rb, juce::String(n) + " words");
        else       drawProgressNumber(g, rb, juce::String("--"));
        return;
    }

    auto dim = [&](int i) {
        return (i >= 0 && i < n)
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

    const float denom = static_cast<float>(std::max(1, n - 1));
    const float progress = std::clamp(static_cast<float>(idx) / denom, 0.0f, 1.0f);

    const float fontH = kCenterBaseHeight * (1.0f + kCenterGrowFactor * progress);
    const auto peakColor = juce::Colour::fromRGB(kPeakColorR, kPeakColorG, kPeakColorB);
    const auto centerColor = sceneColor.interpolatedWith(peakColor, progress);

    g.setColour(centerColor);
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(fontH).withStyle("Bold")});
    g.drawFittedText(dim(idx), mid, juce::Justification::centred, 1, 0.5f);

    drawProgressNumber(g, rb, juce::String(idx + 1) + " / " + juce::String(n));
}

void WordReadout::mouseDown(const juce::MouseEvent& e) {
    if (rewindButtonBounds().contains(e.getPosition())) {
        processor_.rewindActive();
        repaint();
    }
}

} // namespace guitar_dsp
