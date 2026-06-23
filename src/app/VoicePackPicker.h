#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

namespace guitar_dsp::app {

// Small ComboBox for picking a sung-vowel voice. Populated from
// Scene::voicePacks; emits an integer index on selection change.
class VoicePackPicker : public juce::Component, private juce::ComboBox::Listener {
public:
    VoicePackPicker();
    ~VoicePackPicker() override;

    // Replace the visible list. Pass an empty vector to clear.
    void setPacks(const std::vector<std::pair<std::string, std::string>>& labelPathPairs,
                  int activeIndex);

    // Fired when the user picks a new voice.
    std::function<void(int newIndex)> onChange;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void comboBoxChanged(juce::ComboBox* cb) override;

    juce::Label    label_;
    juce::ComboBox combo_;
};

} // namespace guitar_dsp::app
