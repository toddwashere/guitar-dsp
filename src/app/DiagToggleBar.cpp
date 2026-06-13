#include "DiagToggleBar.h"

#include "PluginProcessor.h"

namespace guitar_dsp {

DiagToggleBar::DiagToggleBar(PluginProcessor& p) : processor_(p) {
    setOpaque(true);
    startTimerHz(15);  // reflect keyboard toggles, not just clicks
}

DiagToggleBar::~DiagToggleBar() { stopTimer(); }

void DiagToggleBar::timerCallback() { repaint(); }

juce::Rectangle<int> DiagToggleBar::pillBounds(int index) const {
    auto area = getLocalBounds().reduced(6, 5);
    constexpr int gap = 6;
    const int w = (area.getWidth() - 4 * gap) / 5;
    return juce::Rectangle<int>(area.getX() + index * (w + gap),
                                area.getY(), w, area.getHeight());
}

void DiagToggleBar::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(14, 16, 22));

    struct Pill { const char* label; bool active; juce::Colour on; };
    const Pill pills[5] = {
        { "V  Bypass vocoder", processor_.diagBypassVocoder(), juce::Colour::fromRGB(230, 170,  70) },
        { "N  Noise carrier",  processor_.diagNoiseCarrier(),  juce::Colour::fromRGB( 90, 200, 120) },
        { "S  Sibilance off",  processor_.diagSibilanceOff(),  juce::Colour::fromRGB(110, 170, 230) },
        { "P  Pitch sing",     processor_.pitchSinging(),      juce::Colour::fromRGB(220, 120, 220) },
        { "M  Sing",           processor_.singing(),           juce::Colour::fromRGB(120, 220, 200) },
    };

    for (int i = 0; i < 5; ++i) {
        const auto b = pillBounds(i);
        const bool on = pills[i].active;
        g.setColour(on ? pills[i].on : juce::Colour::fromRGB(34, 38, 46));
        g.fillRoundedRectangle(b.toFloat(), 4.0f);
        g.setColour(on ? juce::Colour::fromRGB(18, 20, 26)
                       : juce::Colour::fromRGB(150, 160, 175));
        g.setFont(juce::Font{juce::FontOptions{}.withHeight(12.0f)
                                 .withStyle(on ? "Bold" : "Regular")});
        g.drawText(pills[i].label, b, juce::Justification::centred);
    }
}

void DiagToggleBar::mouseDown(const juce::MouseEvent& e) {
    for (int i = 0; i < 5; ++i) {
        if (!pillBounds(i).contains(e.getPosition())) continue;
        if      (i == 0) processor_.toggleDiagBypassVocoder();
        else if (i == 1) processor_.toggleDiagNoiseCarrier();
        else if (i == 2) processor_.toggleDiagSibilanceOff();
        else if (i == 3) processor_.togglePitchSinging();
        else             processor_.toggleSinging();
        repaint();
        return;
    }
}

} // namespace guitar_dsp
