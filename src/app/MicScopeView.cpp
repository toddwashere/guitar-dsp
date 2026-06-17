#include "MicScopeView.h"
#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>

namespace guitar_dsp {

namespace {
const juce::Colour kBackground = juce::Colour::fromRGB(20, 23, 30);
const juce::Colour kWaveform   = juce::Colour::fromRGB(108, 200, 154);  // matches WaveformView
const juce::Colour kAxisLine   = juce::Colour::fromRGB(50, 55, 65);
const juce::Colour kLabel      = juce::Colour::fromRGB(120, 130, 145);
}

MicScopeView::MicScopeView(PluginProcessor& p) : processor_(p) {
    setOpaque(true);
    snapshot_.assign(processor_.micScopeBuffer().capacity(), 0.0f);
    startTimerHz(30);
}

MicScopeView::~MicScopeView() { stopTimer(); }

void MicScopeView::timerCallback() {
    processor_.micScopeBuffer().copyMostRecent(snapshot_.data(), snapshot_.size());
    repaint();
}

void MicScopeView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.fillAll(kBackground);

    g.setColour(kLabel);
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
    g.drawText("Mic input (live, last 1 s)", bounds.reduced(8, 4),
               juce::Justification::topLeft);

    const auto plot = bounds.reduced(8, 18).toFloat();
    g.setColour(kAxisLine);
    g.drawHorizontalLine(static_cast<int>(plot.getCentreY()),
                         plot.getX(), plot.getRight());

    if (snapshot_.empty()) return;

    const float yMid   = plot.getCentreY();
    const float yScale = plot.getHeight() * 0.45f;
    const int   pxLeft  = static_cast<int>(plot.getX());
    const int   pxRight = static_cast<int>(plot.getRight());
    const int   pxWidth = std::max(1, pxRight - pxLeft);

    // Per-pixel min/max envelope across the snapshot. Snapshot covers the
    // last 1 s; oldest at index 0, newest at the end. Render left-to-right
    // with oldest on the left (scrolling-style visual).
    g.setColour(kWaveform);
    const float samplesPerPixel = float(snapshot_.size()) / float(pxWidth);
    for (int x = 0; x < pxWidth; ++x) {
        const std::size_t lo = std::size_t(float(x) * samplesPerPixel);
        const std::size_t hi = std::min(snapshot_.size(),
                                        std::size_t((float(x) + 1.0f) * samplesPerPixel));
        if (hi <= lo) continue;
        float lo_v = 0.0f, hi_v = 0.0f;
        for (std::size_t i = lo; i < hi; ++i) {
            lo_v = std::min(lo_v, snapshot_[i]);
            hi_v = std::max(hi_v, snapshot_[i]);
        }
        const float y0 = yMid - hi_v * yScale;
        const float y1 = yMid - lo_v * yScale;
        g.drawVerticalLine(pxLeft + x, y0, std::max(y0 + 1.0f, y1));
    }
}

} // namespace guitar_dsp
