#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class PluginProcessor;

// Text input + "Say" button that overlays an ad-hoc Apple-TTS phrase onto
// the active scene's vocoder chain. Independent from scene scheduling —
// you type something, click Say (or press Enter), and that text plays
// once. The next scene change replaces it normally.
class SayPanel : public juce::Component {
public:
    explicit SayPanel(PluginProcessor& processor);

    void resized() override;

private:
    void say();

    PluginProcessor& processor_;

    juce::TextEditor input_;
    juce::TextButton sayButton_ {"Say"};
};

} // namespace guitar_dsp
