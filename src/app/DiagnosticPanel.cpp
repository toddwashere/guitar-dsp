#include "DiagnosticPanel.h"

#include "PluginProcessor.h"

#include <cmath>

namespace guitar_dsp {

namespace {

constexpr float kMeterFloorDb   = -60.0f;
constexpr float kMeterCeilingDb =   0.0f;
constexpr int   kTimerHz        = 30;

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

    const int currentSummary = processor_.getLastMidiSummary();
    if (currentSummary != lastMidiSummary_) {
        lastMidiSummary_ = currentSummary;
        lastMidiTimeMs_  = juce::Time::currentTimeMillis();
    }

    repaint();
}

void DiagnosticPanel::resized() {}

juce::String DiagnosticPanel::describeAudioDevice() const {
    const auto sr = processor_.getSampleRate();
    const auto bs = processor_.getBlockSize();
    if (sr <= 0.0) return "(audio not configured)";
    const double latencyMs = 1000.0 * bs / sr;
    return juce::String(sr, 0) + " Hz · "
         + juce::String(bs) + " samples · "
         + juce::String(latencyMs, 1) + " ms";
}

void DiagnosticPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(18, 20, 26));

    const auto bounds = getLocalBounds().reduced(8, 6);
    auto area = bounds;

    // --- Row 1: status line (audio config + channel routing + gate) -------
    auto statusRow = area.removeFromTop(20);

    g.setFont(makeFont(13.0f, true));
    g.setColour(juce::Colour::fromRGB(220, 225, 235));

    const int inCh  = processor_.getLastInputChannelCount();
    const int outCh = processor_.getLastOutputChannelCount();
    const float gateGain = processor_.getGateGain();
    const bool gateOpen = gateGain > 0.5f;

    const juce::String statusText =
        describeAudioDevice()
      + juce::String("   ·   I:") + juce::String(inCh)
      + (inCh >= 2 ? juce::String("→mono") : juce::String(""))
      + juce::String(" → O:") + juce::String(outCh)
      + juce::String("   ·   Gate ");

    g.drawText(statusText, statusRow, juce::Justification::left);

    // Gate LED inline at the end of the status text.
    const auto textWidth = g.getCurrentFont().getStringWidth(statusText);
    auto lampBounds = juce::Rectangle<int>(statusRow.getX() + textWidth + 2,
                                           statusRow.getY() + 4,
                                           14, 14);
    g.setColour(gateOpen ? juce::Colour::fromRGB(80, 220, 110)
                         : juce::Colour::fromRGB(80, 80, 90));
    g.fillEllipse(lampBounds.toFloat());

    g.setColour(juce::Colour::fromRGB(150, 160, 175));
    g.setFont(makeFont(11.0f));
    g.drawText("(" + juce::String(gateGain, 2) + ")",
               juce::Rectangle<int>(lampBounds.getRight() + 4,
                                    statusRow.getY(), 60, statusRow.getHeight()),
               juce::Justification::left);

    // MIDI activity LED at the far right of the status row.
    {
        const auto now = juce::Time::currentTimeMillis();
        const bool midiHot = (now - lastMidiTimeMs_) < 200 && lastMidiTimeMs_ > 0;

        auto statusBounds = getLocalBounds().reduced(8, 6).removeFromTop(20);
        auto midiArea = statusBounds.removeFromRight(70);  // ~70 px for LED + label
        auto midiLamp = midiArea.removeFromLeft(20).withSizeKeepingCentre(14, 14);
        g.setColour(midiHot ? juce::Colour::fromRGB(80, 180, 220)
                            : juce::Colour::fromRGB(80, 80, 90));
        g.fillEllipse(midiLamp.toFloat());
        g.setColour(juce::Colour::fromRGB(150, 160, 175));
        g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
        g.drawText("MIDI", midiArea, juce::Justification::left);
    }

    area.removeFromTop(4);

    // --- Row 2: side-by-side input + output meters -----------------------
    auto metersRow = area.removeFromTop(28);
    const int gap = 12;
    const int halfWidth = (metersRow.getWidth() - gap) / 2;
    auto inputMeterArea  = metersRow.removeFromLeft(halfWidth);
    metersRow.removeFromLeft(gap);
    auto outputMeterArea = metersRow;

    drawMeter(g, inputMeterArea,  "In",  displayInputPeak_);
    drawMeter(g, outputMeterArea, "Out", displayOutputPeak_);
}

void DiagnosticPanel::drawMeter(juce::Graphics& g,
                                juce::Rectangle<int> bounds,
                                const juce::String& label,
                                float peakLin) const {
    const auto labelArea = bounds.removeFromLeft(28);
    g.setColour(juce::Colour::fromRGB(150, 160, 175));
    g.setFont(makeFont(12.0f, true));
    g.drawText(label, labelArea, juce::Justification::left);

    const auto dbStr = (peakLin < 1.0e-6f)
        ? juce::String("-inf")
        : juce::String(linearToDb(peakLin), 1);
    const auto dbArea = bounds.removeFromRight(48);
    g.setColour(juce::Colour::fromRGB(220, 225, 235));
    g.setFont(makeFont(11.0f));
    g.drawText(dbStr + " dB", dbArea, juce::Justification::right);

    auto barBounds = bounds.reduced(4, 7);
    g.setColour(juce::Colour::fromRGB(40, 44, 52));
    g.fillRoundedRectangle(barBounds.toFloat(), 2.0f);

    const float n = meterNorm(peakLin);
    auto fill = barBounds.toFloat();
    fill.setWidth(fill.getWidth() * n);

    juce::Colour barColour = juce::Colour::fromRGB(80, 200, 120);
    const float db = linearToDb(peakLin);
    if (db > -6.0f)       barColour = juce::Colour::fromRGB(220, 90, 90);
    else if (db > -18.0f) barColour = juce::Colour::fromRGB(220, 200, 90);

    g.setColour(barColour);
    g.fillRoundedRectangle(fill, 2.0f);
}

} // namespace guitar_dsp
