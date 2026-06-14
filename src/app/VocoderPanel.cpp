#include "VocoderPanel.h"

#include "PluginProcessor.h"

namespace guitar_dsp {

VocoderPanel::VocoderPanel(PluginProcessor& p)
    : processor_(p), noteReadout_(p), wordSyncSelector_(p) {
    setOpaque(true);
    addAndMakeVisible(noteReadout_);
    addAndMakeVisible(wordSyncSelector_);

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

    configureSlider(gateThreshold_, gateThresholdLabel_, "Gate threshold");
    gateThreshold_.setRange(-90.0, -20.0, 0.5);
    gateThreshold_.setTextValueSuffix(" dB");
    gateThreshold_.setValue(processor_.noiseGateThresholdDb(), juce::dontSendNotification);
    gateThreshold_.onValueChange = [this] {
        processor_.setNoiseGateThresholdDb(static_cast<float>(gateThreshold_.getValue()));
    };

    startTimerHz(4);  // poll the active scene's clarity for the label readout
}

VocoderPanel::~VocoderPanel() { stopTimer(); }

void VocoderPanel::timerCallback() {
    const float sceneClarity = processor_.activeSceneClarity();
    if (sceneClarity != lastSceneClarity_) {
        lastSceneClarity_ = sceneClarity;
        clarityLabel_.setText("Clarity  (scene "
                                  + juce::String(sceneClarity, 2) + ")",
                              juce::dontSendNotification);
    }

    const juce::String desiredCarrierLabel = processor_.pitchSinging()
        ? juce::String("Pitched floor")
        : juce::String("Noise floor");
    if (desiredCarrierLabel != lastCarrierNoiseLabel_) {
        lastCarrierNoiseLabel_ = desiredCarrierLabel;
        carrierNoiseLabel_.setText(desiredCarrierLabel, juce::dontSendNotification);
    }

    const float p = processor_.micPeak();
    if (std::fabs(p - lastMicPeak_) > 0.005f) {
        lastMicPeak_ = p;
        repaint();
    }
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

    // Mic level meter — always-visible thin strip at the bottom of the panel.
    // Driven by AudioGraph::micPeak() via timerCallback.
    const auto micStripBounds = getLocalBounds()
                                    .removeFromBottom(8)
                                    .reduced(4, 1);
    g.setColour(juce::Colour::fromRGB(0x22, 0x22, 0x22));
    g.fillRect(micStripBounds);

    if (lastMicPeak_ > 0.0001f) {
        const int w = static_cast<int>(micStripBounds.getWidth()
                                        * std::min(1.0f, lastMicPeak_));
        auto fill = micStripBounds.withWidth(w);
        const juce::Colour colour = lastMicPeak_ > 0.7f
            ? juce::Colour::fromRGB(0xE0, 0x60, 0x40)
            : juce::Colour::fromRGB(0x40, 0xC0, 0x60);
        g.setColour(colour);
        g.fillRect(fill);
    } else {
        g.setColour(juce::Colour::fromRGB(0x55, 0x55, 0x55));
        g.setFont(juce::Font{juce::FontOptions{}.withHeight(8.0f)});
        g.drawText("no mic", micStripBounds, juce::Justification::centred);
    }
}

void VocoderPanel::resized() {
    auto area = getLocalBounds().reduced(6, 4);
    area.removeFromBottom(8);  // mic-meter strip (painted directly in paint())
    area.removeFromTop(12);    // header band

    constexpr int selectorH = 22;
    wordSyncSelector_.setBounds(area.removeFromBottom(selectorH));
    constexpr int readoutH = 36;
    noteReadout_.setBounds(area.removeFromBottom(readoutH));

    const int rowH = area.getHeight() / 5;
    auto row = [&](juce::Slider& s, juce::Label& l, int labelW) {
        auto r = area.removeFromTop(rowH);
        l.setBounds(r.removeFromLeft(labelW));
        s.setBounds(r);
    };
    row(makeup_,         makeupLabel_,         86);
    row(carrierNoise_,   carrierNoiseLabel_,   86);
    row(sibilance_,      sibilanceLabel_,      86);
    row(clarity_,        clarityLabel_,       140);  // wider — also shows "(scene 0.50)"
    row(gateThreshold_,  gateThresholdLabel_,  86);
}

} // namespace guitar_dsp
