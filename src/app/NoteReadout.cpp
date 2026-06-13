#include "NoteReadout.h"

#include "PluginProcessor.h"

#include <cmath>

namespace guitar_dsp {

namespace {
juce::String noteName(int midi) {
    static const char* kNames[12] = {
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
    };
    if (midi < 0) return "-";
    const int n = ((midi % 12) + 12) % 12;
    const int oct = (midi / 12) - 1;  // MIDI 0 = C-1; A4 = MIDI 69
    return juce::String(kNames[n]) + juce::String(oct);
}
}

NoteReadout::NoteReadout(PluginProcessor& p) : processor_(p) {
    setOpaque(true);
    startTimerHz(30);
}

NoteReadout::~NoteReadout() { stopTimer(); }

void NoteReadout::timerCallback() {
    const int   m = processor_.detectedNoteMidi();
    const float c = processor_.detectedCents();
    const float h = processor_.detectedHz();
    if (m != midiNote_ || std::abs(c - cents_) > 0.5f
                       || std::abs(h - hz_)    > 0.1f) {
        midiNote_ = m;
        cents_    = c;
        hz_       = h;
        repaint();
    }
}

void NoteReadout::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(16, 18, 24));

    const bool active = midiNote_ >= 0;
    g.setColour(active ? juce::Colour::fromRGB(220, 220, 230)
                       : juce::Colour::fromRGB(70, 75, 85));

    auto area = getLocalBounds().reduced(8, 4);

    // Note name + octave (big)
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(28.0f).withStyle("Bold")});
    g.drawText(noteName(midiNote_),
               area.removeFromTop(area.getHeight() * 2 / 3),
               juce::Justification::centred);

    // Cents and Hz (small, side by side)
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
    if (active) {
        const auto centsStr = (cents_ >= 0.0f ? juce::String("+") : juce::String("-"))
                              + juce::String(std::abs(cents_), 0) + juce::String("c");
        const auto hzStr    = juce::String(hz_, 1) + " Hz";
        const auto half = area.removeFromTop(area.getHeight());
        const auto left  = half.withWidth(half.getWidth() / 2);
        const auto right = half.withTrimmedLeft(half.getWidth() / 2);
        g.drawText(centsStr, left, juce::Justification::centred);
        g.drawText(hzStr,    right, juce::Justification::centred);
    } else {
        g.drawText("(no pitch)", area, juce::Justification::centred);
    }
}

} // namespace guitar_dsp
