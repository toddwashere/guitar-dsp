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
    applePillRect_    = area.removeFromLeft(w);  pill(applePillRect_,    "Apple", true);
    area.removeFromLeft(4);
    piperPillRect_    = area.removeFromLeft(w);  pill(piperPillRect_,    piper ? "Piper OK" : "Piper x", piper);
    area.removeFromLeft(4);
    prebakedPillRect_ = area.removeFromLeft(w);  pill(prebakedPillRect_, "Prebaked", true);
    area.removeFromLeft(10);

    g.setColour(juce::Colour::fromRGB(150, 160, 175));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
    juce::String msg = "scene: " + (active.isEmpty() ? juce::String("(none)") : active);
    if (resolved.isNotEmpty() && resolved != active)
        msg += "   -> using: " + resolved;
    g.drawText(msg, area, juce::Justification::centredLeft);

    if (juce::Time::currentTimeMillis() < flashUntilMs_ && flashText_.isNotEmpty()) {
        g.setColour(juce::Colour::fromRGB(150, 150, 150));  // muted grey
        g.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
        g.drawText(flashText_, getLocalBounds().reduced(8, 2),
                   juce::Justification::centredRight);
    }
}

void TtsStatusBar::flashMessage(juce::String message, int durationMs) {
    flashText_    = std::move(message);
    flashUntilMs_ = juce::Time::currentTimeMillis() + (juce::int64) durationMs;
    repaint();
}

void TtsStatusBar::timerCallback() {
    if (flashUntilMs_ != 0 && juce::Time::currentTimeMillis() > flashUntilMs_) {
        flashUntilMs_ = 0;
        flashText_.clear();
        repaint();
    }
    repaint();
}

void TtsStatusBar::mouseDown(const juce::MouseEvent& e) {
    // Each pill is a click-for-detail status indicator — not a toggle. The
    // popup mirrors the green/red color and surfaces *why* a red one is red.
    auto info = [](juce::String engine, bool ok, juce::String detail) {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(ok ? juce::MessageBoxIconType::InfoIcon
                                 : juce::MessageBoxIconType::WarningIcon)
                .withTitle(engine + (ok ? ": ready" : ": not ready"))
                .withMessage(ok ? detail
                                : detail + "\n\n"
                                  "These status pills are read-only "
                                  "indicators — selecting which engine plays "
                                  "is done by the active scene.")
                .withButton("OK"),
            nullptr);
    };

    if (applePillRect_.contains(e.getPosition())) {
        info("Apple TTS", true,
             "AVSpeechSynthesizer — bundled with macOS. Always available "
             "in the standalone; in an AU host it depends on the host "
             "pumping its main run loop (Logic does; auval does not).");
        return;
    }
    if (piperPillRect_.contains(e.getPosition())) {
        const bool ok = processor_.piperReady();
        const auto detail = ok
            ? juce::String("Local CLI binary at assets/piper/piper with the "
                           "en_US-amy-medium voice. Runs as a child process; "
                           "first synth has ~300-800 ms latency.")
            : juce::String(processor_.piperStatusDetail());
        info("Piper TTS", ok, detail);
        return;
    }
    if (prebakedPillRect_.contains(e.getPosition())) {
        info("Prebaked clips", true,
             "Per-scene WAVs under assets/tts/. Loaded at scene activation "
             "and used directly - or as the fallback when a live engine "
             "fails to synthesize. Latency is essentially zero.");
        return;
    }
}

} // namespace guitar_dsp
