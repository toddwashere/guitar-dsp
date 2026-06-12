#include "TtsStatusBar.h"
#include "PluginProcessor.h"

namespace guitar_dsp {

TtsStatusBar::TtsStatusBar(PluginProcessor& p) : processor_(p) {
    setOpaque(true);
    startTimerHz(4);
}
TtsStatusBar::~TtsStatusBar() { stopTimer(); }

void TtsStatusBar::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(16, 18, 24));

    const bool piper = processor_.piperReady();
    const auto active = processor_.activeTtsSourceName();
    const auto resolved = processor_.lastResolvedSource();

    auto pill = [&](juce::Rectangle<int> r, const juce::String& label, bool ok) {
        g.setColour(ok ? juce::Colour::fromRGB(60, 140, 90)
                       : juce::Colour::fromRGB(120, 70, 70));
        g.fillRoundedRectangle(r.toFloat(), 3.0f);
        g.setColour(juce::Colour::fromRGB(225, 230, 240));
        g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
        g.drawText(label, r, juce::Justification::centred);
    };

    auto area = getLocalBounds().reduced(6, 4);
    const int w = 86;
    pill(area.removeFromLeft(w), "Apple", true);
    area.removeFromLeft(4);
    pill(area.removeFromLeft(w), piper ? "Piper OK" : "Piper x", piper);
    area.removeFromLeft(4);
    pill(area.removeFromLeft(w), "Prebaked", true);
    area.removeFromLeft(10);

    g.setColour(juce::Colour::fromRGB(150, 160, 175));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
    juce::String msg = "scene: " + (active.isEmpty() ? juce::String("(none)") : active);
    if (resolved.isNotEmpty() && resolved != active)
        msg += "   -> using: " + resolved;
    g.drawText(msg, area, juce::Justification::centredLeft);
}

} // namespace guitar_dsp
