#include "Oscilloscope.h"

#include "PluginProcessor.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp {

namespace {
constexpr int kTimerHz = 30;

int findRisingZeroCrossing(const float* samples, int searchLength) {
    // Look for x[i-1] <= 0 < x[i]; return i. If none, return 0.
    for (int i = 1; i < searchLength; ++i) {
        if (samples[i - 1] <= 0.0f && samples[i] > 0.0f) return i;
    }
    return 0;
}
} // namespace

Oscilloscope::Oscilloscope(PluginProcessor& p) : processor_(p) {
    setOpaque(true);
    startTimerHz(kTimerHz);
}

Oscilloscope::~Oscilloscope() { stopTimer(); }

void Oscilloscope::timerCallback() {
    processor_.snapshotRecentSamples(snapshot_.data(), kSnapshotSize);
    repaint();
}

void Oscilloscope::paint(juce::Graphics& g) {
    const auto bounds = getLocalBounds();
    g.fillAll(juce::Colour::fromRGB(14, 16, 22));

    // Label.
    g.setColour(juce::Colour::fromRGB(120, 130, 150));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
    g.drawText("Oscilloscope (post-DSP)", bounds.reduced(8, 4),
               juce::Justification::topLeft);

    // Center line.
    const auto plot = bounds.reduced(8, 18).toFloat();
    g.setColour(juce::Colour::fromRGB(40, 44, 52));
    g.drawHorizontalLine(static_cast<int>(plot.getCentreY()),
                         plot.getX(), plot.getRight());

    // Trigger-align: anchor on a rising zero-crossing within the first
    // half of the snapshot so the rest of the snapshot is displayable.
    const int triggerOffset = findRisingZeroCrossing(snapshot_.data(),
                                                     kSnapshotSize - kDisplaySize);

    // Polyline through the next kDisplaySize samples after the trigger.
    const float xStep = plot.getWidth() / static_cast<float>(kDisplaySize - 1);
    const float yMid  = plot.getCentreY();
    const float yScale = plot.getHeight() * 0.45f;  // leave headroom

    juce::Path path;
    path.preallocateSpace(kDisplaySize * 3);
    path.startNewSubPath(plot.getX(),
                         yMid - juce::jlimit(-1.0f, 1.0f, snapshot_[triggerOffset]) * yScale);
    for (int i = 1; i < kDisplaySize; ++i) {
        const float s = juce::jlimit(-1.0f, 1.0f, snapshot_[triggerOffset + i]);
        path.lineTo(plot.getX() + i * xStep, yMid - s * yScale);
    }

    g.setColour(juce::Colour::fromRGB(120, 220, 180));
    g.strokePath(path, juce::PathStrokeType{1.5f});
}

} // namespace guitar_dsp
