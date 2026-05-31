#pragma once

#include <string>

#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class PluginProcessor;

// Text input + "Say" button that overlays an ad-hoc Apple-TTS phrase onto
// the active scene's vocoder chain. Independent from scene scheduling —
// you type something, click Say (or press Enter), and that text plays
// once. The next scene change replaces it normally.
//
// Synthesis runs on the prewarmer's background thread (Apple's
// AVSpeechSynthesizer callback dispatches to the main queue, so calling
// synthesize directly from the message thread deadlocks). While waiting,
// the panel polls the prewarmer cache every 50 ms and disables the input.
class SayPanel : public juce::Component,
                 private juce::Timer {
public:
    explicit SayPanel(PluginProcessor& processor);
    ~SayPanel() override;

    void resized() override;

private:
    void say();
    void timerCallback() override;
    void finishPending(bool succeeded);

    PluginProcessor& processor_;

    juce::TextEditor input_;
    juce::TextButton sayButton_ {"Say"};

    // Currently-pending synthesis (empty when idle).
    std::string  pendingText_;
    juce::int64  pendingExpiryMs_ = 0;
};

} // namespace guitar_dsp
