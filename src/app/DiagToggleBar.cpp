#include "DiagToggleBar.h"

#include "PluginProcessor.h"

namespace guitar_dsp {

DiagToggleBar::DiagToggleBar(PluginProcessor& p) : processor_(p) {
    setOpaque(true);
    startTimerHz(15);  // reflect keyboard toggles, not just clicks
}

DiagToggleBar::~DiagToggleBar() { stopTimer(); }

void DiagToggleBar::timerCallback() { repaint(); }

// Pill layout: 5 vocoder-mode pills on the left + a gap + 2 view-toggle
// pills on the right. The view toggles use a distinct muted color so the
// operator can tell at a glance which are audio-affecting vs UI-only.
namespace {
    constexpr int kNumVocoderPills = 5;
    constexpr int kNumViewPills    = 2;
    constexpr int kSplitGap        = 14;
}

juce::Rectangle<int> DiagToggleBar::pillBounds(int index) const {
    auto area = getLocalBounds().reduced(6, 5);
    constexpr int gap = 6;
    const int totalPills = kNumVocoderPills + kNumViewPills;
    const int innerGaps  = (totalPills - 1) * gap + kSplitGap;
    const int w = (area.getWidth() - innerGaps) / totalPills;
    int x = area.getX();
    for (int i = 0; i < index; ++i) {
        x += w + gap;
        if (i == kNumVocoderPills - 1) x += (kSplitGap - gap);
    }
    return juce::Rectangle<int>(x, area.getY(), w, area.getHeight());
}

void DiagToggleBar::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(14, 16, 22));

    struct Pill { const char* label; bool active; juce::Colour on; };
    const Pill pills[kNumVocoderPills + kNumViewPills] = {
        { "V  Bypass vocoder", processor_.diagBypassVocoder(), juce::Colour::fromRGB(230, 170,  70) },
        { "N  Noise carrier",  processor_.diagNoiseCarrier(),  juce::Colour::fromRGB( 90, 200, 120) },
        { "S  Sibilance off",  processor_.diagSibilanceOff(),  juce::Colour::fromRGB(110, 170, 230) },
        { "P  Pitch sing",     processor_.pitchSinging(),      juce::Colour::fromRGB(220, 120, 220) },
        { "M  Sing",           processor_.singing(),           juce::Colour::fromRGB(120, 220, 200) },
        // View-only pills (no audio impact).
        { "K  Knobs",          processor_.showKnobs(),         juce::Colour::fromRGB(140, 145, 160) },
        { "O  Scope",          processor_.showScope(),         juce::Colour::fromRGB(140, 145, 160) },
    };

    for (int i = 0; i < kNumVocoderPills + kNumViewPills; ++i) {
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
    for (int i = 0; i < kNumVocoderPills + kNumViewPills; ++i) {
        if (!pillBounds(i).contains(e.getPosition())) continue;
        if      (i == 0) processor_.toggleDiagBypassVocoder();
        else if (i == 1) processor_.toggleDiagNoiseCarrier();
        else if (i == 2) processor_.toggleDiagSibilanceOff();
        else if (i == 3) processor_.togglePitchSinging();
        else if (i == 4) processor_.toggleSinging();
        else if (i == 5) processor_.toggleShowKnobs();
        else             processor_.toggleShowScope();
        if (auto* parent = getParentComponent()) parent->resized();
        repaint();
        return;
    }
}

} // namespace guitar_dsp
