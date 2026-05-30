#include "DiagnosticPanel.h"

#include "PluginProcessor.h"

#include <cmath>

namespace guitar_dsp {

namespace {

constexpr float kMeterFloorDb = -60.0f;
constexpr float kMeterCeilingDb =   0.0f;
constexpr int   kTimerHz = 30;

float linearToDb(float linear) {
    if (linear < 1.0e-6f) return kMeterFloorDb;
    return 20.0f * std::log10(linear);
}

float meterNorm(float linear) {
    const float db = linearToDb(linear);
    const float n = (db - kMeterFloorDb) / (kMeterCeilingDb - kMeterFloorDb);
    return juce::jlimit(0.0f, 1.0f, n);
}

juce::Font makeFont(float height, bool bold = false) {
    auto opts = juce::FontOptions{}.withHeight(height);
    juce::Font f{opts};
    if (bold) f.setBold(true);
    return f;
}

} // namespace

DiagnosticPanel::DiagnosticPanel(PluginProcessor& p) : processor_(p) {
    setOpaque(true);
    startTimerHz(kTimerHz);
}

DiagnosticPanel::~DiagnosticPanel() {
    stopTimer();
}

void DiagnosticPanel::timerCallback() {
    const float in  = processor_.getInputPeak();
    const float out = processor_.getOutputPeak();

    constexpr float decayPerFrame = 0.85f;
    displayInputPeak_  = std::max(in,  displayInputPeak_  * decayPerFrame);
    displayOutputPeak_ = std::max(out, displayOutputPeak_ * decayPerFrame);

    repaint();
}

void DiagnosticPanel::resized() {}

juce::String DiagnosticPanel::describeAudioDevice() const {
    const auto sr = processor_.getSampleRate();
    const auto bs = processor_.getBlockSize();
    if (sr <= 0.0) return "(audio device not yet configured)";
    const double latencyMs = 1000.0 * bs / sr;
    return juce::String(sr, 0) + " Hz, "
         + juce::String(bs) + " samples ("
         + juce::String(latencyMs, 1) + " ms)";
}

void DiagnosticPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(18, 20, 26));

    auto bounds = getLocalBounds().reduced(20);
    auto area = bounds;

    g.setColour(juce::Colours::white);
    g.setFont(makeFont(26.0f, true));
    g.drawText("Guitar DSP — passthrough diagnostic",
               area.removeFromTop(38),
               juce::Justification::left);

    area.removeFromTop(8);
    g.setFont(makeFont(14.0f));

    auto drawRow = [&g, &area](const juce::String& label, const juce::String& value) {
        auto row = area.removeFromTop(22);
        const auto labelArea = row.removeFromLeft(140);
        g.setColour(juce::Colour::fromRGB(120, 130, 150));
        g.drawText(label, labelArea, juce::Justification::left);
        g.setColour(juce::Colour::fromRGB(220, 225, 235));
        g.drawText(value, row, juce::Justification::left);
    };

    drawRow("Audio I/O:", describeAudioDevice());

    const int inCh  = processor_.getLastInputChannelCount();
    const int outCh = processor_.getLastOutputChannelCount();
    drawRow("Input bus:",
            juce::String(inCh) + " ch"
            + (inCh >= 2 ? juce::String(" (downmixed L+R → mono)")
                         : (inCh == 1 ? juce::String(" (mono passthrough)")
                                      : juce::String(" (no input)"))));
    drawRow("Output bus:",
            juce::String(outCh) + " ch (mono fanned to all)");

    area.removeFromTop(20);

    const int meterHeight = 36;
    auto inputMeterRow  = area.removeFromTop(meterHeight);
    area.removeFromTop(10);
    auto outputMeterRow = area.removeFromTop(meterHeight);
    area.removeFromTop(20);

    drawMeter(g, inputMeterRow,  "Input",  displayInputPeak_);
    drawMeter(g, outputMeterRow, "Output", displayOutputPeak_);

    auto gateRow = area.removeFromTop(28);
    const float gateGain = processor_.getGateGain();
    const bool gateOpen = gateGain > 0.5f;
    g.setColour(juce::Colour::fromRGB(120, 130, 150));
    g.setFont(makeFont(14.0f));
    g.drawText("Gate:", gateRow.removeFromLeft(140), juce::Justification::left);

    auto lampBounds = gateRow.removeFromLeft(20).reduced(2);
    g.setColour(gateOpen ? juce::Colour::fromRGB(80, 220, 110)
                         : juce::Colour::fromRGB(80, 80, 90));
    g.fillEllipse(lampBounds.toFloat());

    g.setColour(juce::Colour::fromRGB(220, 225, 235));
    g.drawText((gateOpen ? juce::String("open") : juce::String("closed"))
               + " (gain " + juce::String(gateGain, 2) + ")",
               gateRow.withTrimmedLeft(8), juce::Justification::left);

    auto footer = bounds.removeFromBottom(20);
    g.setColour(juce::Colour::fromRGB(90, 100, 120));
    g.setFont(makeFont(11.0f));
    g.drawText("Refreshes at " + juce::String(kTimerHz) + " Hz • audio-thread peaks via lock-free atomics",
               footer, juce::Justification::centred);
}

void DiagnosticPanel::drawMeter(juce::Graphics& g,
                                juce::Rectangle<int> bounds,
                                const juce::String& label,
                                float peakLin) const {
    const auto labelArea = bounds.removeFromLeft(140);
    g.setColour(juce::Colour::fromRGB(120, 130, 150));
    g.setFont(makeFont(14.0f));
    g.drawText(label, labelArea, juce::Justification::left);

    const auto dbStr = (peakLin < 1.0e-6f)
        ? juce::String("-inf dBFS")
        : juce::String(linearToDb(peakLin), 1) + " dBFS";
    const auto dbArea = bounds.removeFromRight(80);
    g.setColour(juce::Colour::fromRGB(220, 225, 235));
    g.drawText(dbStr, dbArea, juce::Justification::right);

    auto barBounds = bounds.reduced(0, 8);
    g.setColour(juce::Colour::fromRGB(40, 44, 52));
    g.fillRoundedRectangle(barBounds.toFloat(), 3.0f);

    const float n = meterNorm(peakLin);
    auto fill = barBounds.toFloat();
    fill.setWidth(fill.getWidth() * n);

    juce::Colour barColour = juce::Colour::fromRGB(80, 200, 120);
    const float db = linearToDb(peakLin);
    if (db > -6.0f) barColour = juce::Colour::fromRGB(220, 90, 90);
    else if (db > -18.0f) barColour = juce::Colour::fromRGB(220, 200, 90);

    g.setColour(barColour);
    g.fillRoundedRectangle(fill, 3.0f);
}

} // namespace guitar_dsp
