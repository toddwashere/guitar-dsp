#include "VocoderPanel.h"

#include "PluginProcessor.h"

namespace guitar_dsp {

VocoderPanel::VocoderPanel(PluginProcessor& p) : processor_(p) {
    setOpaque(true);

    configureSlider(makeup_, makeupLabel_, "Makeup");
    makeup_.setRange(0.5, 16.0, 0.01);
    makeup_.setValue(processor_.vocoderMakeup(), juce::dontSendNotification);
    makeup_.onValueChange = [this] {
        processor_.setVocoderMakeup(static_cast<float>(makeup_.getValue()));
    };

    configureSlider(carrierNoise_, carrierNoiseLabel_, "Carrier noise");
    carrierNoise_.setRange(0.0, 1.0, 0.01);
    carrierNoise_.setValue(processor_.vocoderCarrierNoise(), juce::dontSendNotification);
    carrierNoise_.onValueChange = [this] {
        processor_.setVocoderCarrierNoise(static_cast<float>(carrierNoise_.getValue()));
    };

    configureSlider(sibilance_, sibilanceLabel_, "Sibilance");
    sibilance_.setRange(0.0, 1.0, 0.01);
    sibilance_.setValue(processor_.vocoderSibilance(), juce::dontSendNotification);
    sibilance_.onValueChange = [this] {
        processor_.setVocoderSibilance(static_cast<float>(sibilance_.getValue()));
    };

    configureSlider(clarity_, clarityLabel_, "Clarity");
    clarity_.setRange(0.0, 1.0, 0.01);
    clarity_.setValue(processor_.vocoderClarity(), juce::dontSendNotification);
    clarity_.onValueChange = [this] {
        processor_.setVocoderClarity(static_cast<float>(clarity_.getValue()));
    };
}

void VocoderPanel::configureSlider(juce::Slider& s, juce::Label& l,
                                   const juce::String& name) {
    s.setSliderStyle(juce::Slider::LinearHorizontal);
    s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 18);
    addAndMakeVisible(s);

    l.setText(name, juce::dontSendNotification);
    l.setFont(juce::Font{juce::FontOptions{}.withHeight(11.0f)});
    l.setColour(juce::Label::textColourId, juce::Colour::fromRGB(150, 160, 175));
    addAndMakeVisible(l);
}

void VocoderPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(16, 18, 24));
    g.setColour(juce::Colour::fromRGB(110, 120, 135));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(10.0f)});
    g.drawText("VOCODER", getLocalBounds().removeFromTop(12).reduced(6, 0),
               juce::Justification::topLeft);
}

void VocoderPanel::resized() {
    auto area = getLocalBounds().reduced(6, 4);
    area.removeFromTop(12);  // header band
    const int rowH = area.getHeight() / 4;
    auto row = [&](juce::Slider& s, juce::Label& l) {
        auto r = area.removeFromTop(rowH);
        l.setBounds(r.removeFromLeft(86));
        s.setBounds(r);
    };
    row(makeup_,       makeupLabel_);
    row(carrierNoise_, carrierNoiseLabel_);
    row(sibilance_,    sibilanceLabel_);
    row(clarity_,      clarityLabel_);
}

} // namespace guitar_dsp
