#include "WaveformView.h"

#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp {

namespace {
constexpr int kTimerHz = 30;

// Dark base (matches Oscilloscope) so the waveform sits inside the same
// visual family as the rest of the bottom panel.
const juce::Colour kBackground   = juce::Colour::fromRGB(14, 16, 22);
const juce::Colour kAxisLine     = juce::Colour::fromRGB(40, 44, 52);
const juce::Colour kWaveformFill = juce::Colour::fromRGB(120, 220, 180);  // teal
const juce::Colour kBoundary     = juce::Colour::fromRGBA(220, 230, 240, 110);
const juce::Colour kActiveBand   = juce::Colour::fromRGBA(255, 170, 80, 50);
const juce::Colour kActiveBorder = juce::Colour::fromRGB(255, 170, 80);
const juce::Colour kPlayhead     = juce::Colour::fromRGB(255, 235, 90);
const juce::Colour kLabel        = juce::Colour::fromRGB(120, 130, 150);
}  // namespace

WaveformView::WaveformView(PluginProcessor& p) : processor_(p) {
    setOpaque(true);
    startTimerHz(kTimerHz);
}

WaveformView::~WaveformView() { stopTimer(); }

void WaveformView::timerCallback() {
    bool changed = false;

    auto newClip = processor_.lastPhonemeClip();
    if (newClip != clip_) { clip_ = std::move(newClip); changed = true; }

    const int newSyl  = processor_.currentSyllableIndex();
    if (newSyl != activeSylIdx_) { activeSylIdx_ = newSyl; changed = true; }

    const int newPlay = processor_.currentPhonemePlaySample();
    if (newPlay != playSample_) { playSample_ = newPlay; changed = true; }

    if (changed) repaint();
}

void WaveformView::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.fillAll(kBackground);

    // Label.
    g.setColour(kLabel);
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
    g.drawText("Waveform + slice boundaries", bounds.reduced(8, 4),
               juce::Justification::topLeft);

    const auto plot = bounds.reduced(8, 18).toFloat();

    // Center axis line — visible even when there's no clip yet.
    g.setColour(kAxisLine);
    g.drawHorizontalLine(static_cast<int>(plot.getCentreY()),
                         plot.getX(), plot.getRight());

    if (!clip_ || clip_->samples.empty()) {
        g.setColour(kLabel);
        g.drawText("(no clip loaded)", plot.toNearestInt(),
                   juce::Justification::centred);
        return;
    }

    const auto&  samples    = clip_->samples;
    const auto&  syls       = clip_->sylsV2;
    const float  totalSamps = static_cast<float>(samples.size());
    const float  yMid       = plot.getCentreY();
    const float  yScale     = plot.getHeight() * 0.45f;
    const int    pxLeft     = static_cast<int>(plot.getX());
    const int    pxRight    = static_cast<int>(plot.getRight());
    const int    pxWidth    = std::max(1, pxRight - pxLeft);

    // 1. Active-syllable highlight band (under the waveform).
    if (activeSylIdx_ >= 0 && activeSylIdx_ < static_cast<int>(syls.size())) {
        const auto& syl = syls[static_cast<std::size_t>(activeSylIdx_)];
        const float x0 = plot.getX()
                       + (syl.startSample / totalSamps) * plot.getWidth();
        const float x1 = plot.getX()
                       + (syl.endSample / totalSamps) * plot.getWidth();
        g.setColour(kActiveBand);
        g.fillRect(juce::Rectangle<float>(x0, plot.getY(),
                                          x1 - x0, plot.getHeight()));
        g.setColour(kActiveBorder);
        g.drawRect(juce::Rectangle<float>(x0, plot.getY(),
                                          x1 - x0, plot.getHeight()), 1.0f);
    }

    // 2. Waveform — peak-per-pixel-column from the clip samples.
    //    Mirrored above + below center to draw a symmetric envelope shape.
    g.setColour(kWaveformFill);
    juce::Path waveform;
    waveform.preallocateSpace(pxWidth * 4 + 16);
    waveform.startNewSubPath(plot.getX(), yMid);
    for (int px = 0; px < pxWidth; ++px) {
        const std::size_t s0 = static_cast<std::size_t>(
            (px       / static_cast<float>(pxWidth)) * totalSamps);
        const std::size_t s1 = static_cast<std::size_t>(
            ((px + 1) / static_cast<float>(pxWidth)) * totalSamps);
        const std::size_t end = std::min(s1, samples.size());
        float peak = 0.0f;
        for (std::size_t s = s0; s < end; ++s)
            peak = std::max(peak, std::fabs(samples[s]));
        const float x = plot.getX() + static_cast<float>(px);
        waveform.lineTo(x, yMid - peak * yScale);
    }
    // Close the top half, then walk back along the bottom to mirror.
    for (int px = pxWidth - 1; px >= 0; --px) {
        const std::size_t s0 = static_cast<std::size_t>(
            (px       / static_cast<float>(pxWidth)) * totalSamps);
        const std::size_t s1 = static_cast<std::size_t>(
            ((px + 1) / static_cast<float>(pxWidth)) * totalSamps);
        const std::size_t end = std::min(s1, samples.size());
        float peak = 0.0f;
        for (std::size_t s = s0; s < end; ++s)
            peak = std::max(peak, std::fabs(samples[s]));
        const float x = plot.getX() + static_cast<float>(px);
        waveform.lineTo(x, yMid + peak * yScale);
    }
    waveform.closeSubPath();
    g.fillPath(waveform);

    // 3. Syllable boundary lines — full height, ~55% alpha.
    g.setColour(kBoundary);
    for (const auto& syl : syls) {
        const float x = plot.getX()
                      + (syl.startSample / totalSamps) * plot.getWidth();
        g.drawVerticalLine(static_cast<int>(x),
                           plot.getY(), plot.getBottom());
    }

    // 4. Playhead — 2 px bright vertical, only when something is playing.
    if (playSample_ >= 0 && playSample_ < static_cast<int>(samples.size())) {
        const float x = plot.getX()
                      + (static_cast<float>(playSample_) / totalSamps)
                            * plot.getWidth();
        g.setColour(kPlayhead);
        g.fillRect(juce::Rectangle<float>(x - 1.0f, plot.getY(),
                                          2.0f, plot.getHeight()));
    }
}

} // namespace guitar_dsp
