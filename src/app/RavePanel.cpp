#include "app/RavePanel.h"

namespace guitar_dsp::app {

RavePanel::RavePanel(audio::AudioGraph& graph,
                     std::vector<juce::String> modelNames,
                     ModelSwapFn onSwap)
    : graph_(graph), onSwap_(std::move(onSwap)),
      hasPicker_(!modelNames.empty() && onSwap_) {
    auto addSlider = [this](juce::Slider& s, juce::Label& lbl, const juce::String& name,
                            double lo, double hi, double init,
                            std::function<void(float)> onChange) {
        s.setRange(lo, hi, 0.01); s.setValue(init);
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f,
                              juce::MathConstants<float>::pi * 2.8f,
                              true);
        s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 14);
        s.setColour(juce::Slider::rotarySliderFillColourId,
                    juce::Colour::fromRGB(140, 180, 230));
        s.setColour(juce::Slider::rotarySliderOutlineColourId,
                    juce::Colour::fromRGB(40, 44, 52));
        s.setColour(juce::Slider::textBoxOutlineColourId,
                    juce::Colours::transparentBlack);
        s.setColour(juce::Slider::textBoxBackgroundColourId,
                    juce::Colours::transparentBlack);
        s.setColour(juce::Slider::textBoxTextColourId,
                    juce::Colour::fromRGB(210, 215, 225));
        s.onValueChange = [&s, onChange]() { onChange((float)s.getValue()); };
        lbl.setText(name, juce::dontSendNotification);
        lbl.setFont(juce::Font{juce::FontOptions{}.withHeight(10.5f)});
        lbl.setColour(juce::Label::textColourId, juce::Colour::fromRGB(150, 160, 175));
        lbl.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(s); addAndMakeVisible(lbl);
    };

    addSlider(gateSlider_,     gateLabel_,     "Gate (dB)",  -60.0, -20.0, -40.0,
              [this](float v){ graph_.setRaveGateDb(v); });
    addSlider(presenceSlider_, presenceLabel_, "Presence",     0.0,   1.0,   0.5,
              [this](float v){ graph_.setRavePresence(v); });
    addSlider(driveSlider_,    driveLabel_,    "Drive (dB)", -12.0,  12.0,   0.0,
              [this](float v){ graph_.setRaveDriveDb(v); });

    statusPill_.setText("RAVE: Loading", juce::dontSendNotification);
    statusPill_.setJustificationType(juce::Justification::centred);
    statusPill_.setColour(juce::Label::backgroundColourId, juce::Colours::orange);
    addAndMakeVisible(statusPill_);

    latencyLabel_.setJustificationType(juce::Justification::centred);
    latencyLabel_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(150, 160, 175));
    inputMeter_.setJustificationType(juce::Justification::left);
    inputMeter_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(150, 160, 175));
    outputMeter_.setJustificationType(juce::Justification::left);
    outputMeter_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(150, 160, 175));
    addAndMakeVisible(latencyLabel_);
    addAndMakeVisible(inputMeter_);
    addAndMakeVisible(outputMeter_);

    if (hasPicker_) {
        modelPickerLabel_.setText("Model", juce::dontSendNotification);
        modelPickerLabel_.setFont(juce::Font{juce::FontOptions{}.withHeight(10.5f)});
        modelPickerLabel_.setColour(juce::Label::textColourId, juce::Colour::fromRGB(150, 160, 175));
        addAndMakeVisible(modelPickerLabel_);

        int id = 1;
        for (const auto& name : modelNames) modelPicker_.addItem(name, id++);
        modelPicker_.setSelectedId(1, juce::dontSendNotification);
        modelPicker_.onChange = [this]() {
            const auto name = modelPicker_.getText();
            if (onSwap_ && name.isNotEmpty()) onSwap_(name);
        };
        addAndMakeVisible(modelPicker_);
    }

    startTimerHz(30);
}

void RavePanel::resized() {
    auto r = getLocalBounds().reduced(8);
    // Header row: status pill + latency readout
    auto top = r.removeFromTop(24);
    statusPill_.setBounds(top.removeFromLeft(140));
    top.removeFromLeft(8);
    latencyLabel_.setBounds(top.removeFromLeft(180));
    r.removeFromTop(6);
    // Knob row: 3 knobs
    auto knobRow = r.removeFromTop(120);
    const int knobW = knobRow.getWidth() / 3;
    auto col = [&](juce::Slider& s, juce::Label& l) {
        auto box = knobRow.removeFromLeft(knobW);
        l.setBounds(box.removeFromTop(14));
        s.setBounds(box.reduced(2));
    };
    col(gateSlider_,     gateLabel_);
    col(presenceSlider_, presenceLabel_);
    col(driveSlider_,    driveLabel_);
    r.removeFromTop(6);
    inputMeter_.setBounds(r.removeFromTop(18));
    outputMeter_.setBounds(r.removeFromTop(18));
    if (hasPicker_) {
        r.removeFromTop(6);
        auto pickerRow = r.removeFromTop(22);
        modelPickerLabel_.setBounds(pickerRow.removeFromLeft(48));
        modelPicker_.setBounds(pickerRow);
    }
}

void RavePanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(16, 18, 24));
    g.setColour(juce::Colour::fromRGB(110, 120, 135));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(10.0f)});
    g.drawText("RAVE", getLocalBounds().removeFromTop(12).reduced(6, 0),
               juce::Justification::topLeft);
}

void RavePanel::timerCallback() {
    using S = audio::AudioGraph::RaveStatusForUI;
    const auto s = graph_.raveStatusForUI();
    juce::String text; juce::Colour bg;
    switch (s) {
        case S::Loading:     text = "RAVE: Loading";     bg = juce::Colours::orange;      break;
        case S::Loaded:      text = "RAVE: Loaded";      bg = juce::Colours::green;       break;
        case S::Unavailable: text = "RAVE: Unavailable"; bg = juce::Colours::red;         break;
        case S::Stalled:     text = "RAVE: Stalled";     bg = juce::Colours::darkorange;  break;
    }
    statusPill_.setText(text, juce::dontSendNotification);
    statusPill_.setColour(juce::Label::backgroundColourId, bg);
    latencyLabel_.setText(
        juce::String("Inference: ") + juce::String(graph_.raveInferenceMs(), 1) + " ms",
        juce::dontSendNotification);
    inputMeter_.setText(
        juce::String("In RMS:  ") + juce::String(graph_.raveInputRms(), 4),
        juce::dontSendNotification);
    outputMeter_.setText(
        juce::String("Out RMS: ") + juce::String(graph_.raveOutputRms(), 4),
        juce::dontSendNotification);
}

} // namespace guitar_dsp::app
