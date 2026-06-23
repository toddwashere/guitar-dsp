#include "VocoderPanel.h"

#include "PluginProcessor.h"

namespace guitar_dsp {

VocoderPanel::VocoderPanel(PluginProcessor& p)
    : processor_(p), noteReadout_(p), wordSyncSelector_(p) {
    setOpaque(true);
    addAndMakeVisible(voicePackPicker_);
    addAndMakeVisible(vowelPills_);
    vowelPills_.setVisible(false);  // shown only on sung-vowel scenes
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

    configureSlider(gateThreshold_, gateThresholdLabel_, "Noise Gate");
    gateThreshold_.setRange(-90.0, -20.0, 0.5);
    gateThreshold_.setTextValueSuffix(" dB");
    gateThreshold_.setValue(processor_.noiseGateThresholdDb(), juce::dontSendNotification);
    gateThreshold_.onValueChange = [this] {
        processor_.setNoiseGateThresholdDb(static_cast<float>(gateThreshold_.getValue()));
    };

    // Mic capture gain — boost applied to the mic input before BOTH the
    // VocoderPanel meter and MicCapture (whisper). Tunable for quiet mic
    // interfaces where -30 dBFS peaks are too faint for transcription.
    // Slider shows the linear multiplier in dB.
    configureSlider(micGain_, micGainLabel_, "Mic gain");
    micGain_.setRange(0.0, 24.0, 0.5);    // dB
    micGain_.setTextValueSuffix(" dB");
    micGain_.setValue(20.0 * std::log10(juce::jmax(0.001f, processor_.micCaptureGain())),
                      juce::dontSendNotification);
    micGain_.onValueChange = [this] {
        const float linear = std::pow(10.0f,
            static_cast<float>(micGain_.getValue()) / 20.0f);
        processor_.setMicCaptureGain(linear);
    };

    startTimerHz(4);  // poll the active scene's clarity for the label readout
}

VocoderPanel::~VocoderPanel() { stopTimer(); }

void VocoderPanel::timerCallback() {
    // Sync sliders to processor values so that a scene activation (which writes
    // new knob values directly into the processor) is reflected on-screen.
    // dontSendNotification prevents the onValueChange lambda from re-firing and
    // pushing the value back through to the audio thread needlessly.
    const auto syncSlider = [](juce::Slider& s, double expected,
                                double epsilon = 0.001) {
        if (std::abs(s.getValue() - expected) > epsilon)
            s.setValue(expected, juce::dontSendNotification);
    };

    syncSlider(makeup_,        processor_.vocoderMakeup());
    syncSlider(carrierNoise_,  processor_.vocoderCarrierNoise());
    syncSlider(sibilance_,     processor_.vocoderSibilance());
    syncSlider(clarity_,       processor_.vocoderClarity());
    syncSlider(gateThreshold_, processor_.noiseGateThresholdDb());
    syncSlider(micGain_,
               20.0 * std::log10(juce::jmax(0.001f, processor_.micCaptureGain())));

    // Clarity label is intentionally just "Clarity" — the per-scene authored
    // default was previously appended (" (scene 0.50)") but it bloated the
    // column width in the knob grid and the operator can see the live slider
    // value in the knob's own readout.

    const juce::String desiredCarrierLabel = processor_.pitchSinging()
        ? juce::String("Pitched floor")
        : juce::String("Noise floor");
    if (desiredCarrierLabel != lastCarrierNoiseLabel_) {
        lastCarrierNoiseLabel_ = desiredCarrierLabel;
        carrierNoiseLabel_.setText(desiredCarrierLabel, juce::dontSendNotification);
    }

    const float p = processor_.micPeak();
    const int  src = processor_.micRoutingSource();
    if (std::fabs(p - lastMicPeak_) > 0.005f || src != lastMicSource_) {
        lastMicPeak_   = p;
        lastMicSource_ = src;
        repaint();
    }
}

void VocoderPanel::configureSlider(juce::Slider& s, juce::Label& l,
                                   const juce::String& name) {
    // Rotary knob style — value displayed inside the knob, labels above.
    // Tradeoff: tighter than linear horizontal sliders (~80x80 vs 28x300)
    // and reads as a "vocoder module" instead of a settings dialog.
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
    addAndMakeVisible(s);

    l.setText(name, juce::dontSendNotification);
    l.setFont(juce::Font{juce::FontOptions{}.withHeight(10.5f)});
    l.setColour(juce::Label::textColourId, juce::Colour::fromRGB(150, 160, 175));
    l.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(l);
}

void VocoderPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(16, 18, 24));
    g.setColour(juce::Colour::fromRGB(110, 120, 135));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(10.0f)});
    g.drawText("VOCODER", getLocalBounds().removeFromTop(12).reduced(6, 0),
               juce::Justification::topLeft);

    // Mic level meter — always-visible strip at the bottom of the panel.
    // Tall enough to show a numeric dB readout and the routing source so the
    // operator can see at a glance which physical input is being read.
    const auto micStripFull = getLocalBounds().removeFromBottom(20).reduced(4, 2);
    g.setColour(juce::Colour::fromRGB(0x22, 0x22, 0x22));
    g.fillRect(micStripFull);

    // Source label on the left (e.g., "MIC ch2", "MIC SC", "MIC self").
    auto leftLabel = micStripFull.withWidth(56);
    g.setColour(juce::Colour::fromRGB(0xB0, 0xB0, 0xB0));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(10.0f)});
    const char* srcText = "MIC --";
    switch (lastMicSource_) {
        case 1: srcText = "MIC SC";   break;  // sidechain (AU)
        case 2: srcText = "MIC ch2";  break;  // standalone stereo, ch 1
        case 3: srcText = "MIC self"; break;  // mono / self-mod
        default: break;
    }
    g.drawText(srcText, leftLabel, juce::Justification::centredLeft);

    // Bar fills the middle.
    auto bar = micStripFull.withTrimmedLeft(60).withTrimmedRight(56);
    if (lastMicPeak_ > 0.0001f) {
        const int w = static_cast<int>(bar.getWidth() * std::min(1.0f, lastMicPeak_));
        auto fill = bar.withWidth(w);
        const juce::Colour colour = lastMicPeak_ > 0.7f
            ? juce::Colour::fromRGB(0xE0, 0x60, 0x40)
            : juce::Colour::fromRGB(0x40, 0xC0, 0x60);
        g.setColour(colour);
        g.fillRect(fill);
    } else {
        g.setColour(juce::Colour::fromRGB(0x40, 0x40, 0x40));
        g.fillRect(bar);
    }

    // dB readout on the right.
    auto rightLabel = micStripFull.withTrimmedLeft(micStripFull.getWidth() - 52);
    juce::String dbStr;
    if (lastMicPeak_ < 0.0001f) {
        dbStr = "-inf dB";
    } else {
        const float dbfs = 20.0f * std::log10(lastMicPeak_);
        dbStr = juce::String(static_cast<int>(std::round(dbfs))) + " dB";
    }
    g.setColour(juce::Colour::fromRGB(0xCC, 0xCC, 0xCC));
    g.drawText(dbStr, rightLabel, juce::Justification::centredRight);
}

void VocoderPanel::setVoicePacks(
    const std::vector<std::pair<std::string, std::string>>& packs,
    int activeIdx) {
    voicePackPicker_.setPacks(packs, activeIdx);
}

void VocoderPanel::setOnVoicePackChange(std::function<void(int)> cb) {
    voicePackPicker_.onChange = std::move(cb);
}

void VocoderPanel::setOnVowelMaskChange(std::function<void(std::uint32_t)> cb) {
    vowelPills_.onMaskChange = std::move(cb);
}

void VocoderPanel::resized() {
    auto area = getLocalBounds().reduced(6, 4);
    area.removeFromBottom(20);  // mic-meter strip (painted directly in paint())
    area.removeFromTop(12);    // header band

    // Voice pack picker — only takes space when visible (non-empty voicePacks).
    if (voicePackPicker_.isVisible()) {
        constexpr int pickerH = 24;
        voicePackPicker_.setBounds(area.removeFromTop(pickerH));
    }
    // Vowel pills row — under the voice picker, sung-vowel scenes only.
    if (vowelPills_.isVisible()) {
        constexpr int pillsH = 22;
        area.removeFromTop(2);
        vowelPills_.setBounds(area.removeFromTop(pillsH));
    }

    constexpr int selectorH = 22;
    wordSyncSelector_.setBounds(area.removeFromBottom(selectorH));
    constexpr int readoutH = 28;
    noteReadout_.setBounds(area.removeFromBottom(readoutH));

    // Single row of 6 rotary knobs. Each cell: label (12px) + rotary knob.
    // Width per cell ~= panel width / 6 — labels are short enough to fit at
    // 10.5 px and the rotary inscribes in the smaller dimension (cell height
    // minus label) so a single-row layout actually gives the knob MORE room
    // than a 2x3 grid would in the same panel height.
    const int colW = area.getWidth() / 6;
    auto cell = [&](int col, juce::Slider& s, juce::Label& l) {
        auto c = juce::Rectangle<int>(area.getX() + col * colW,
                                      area.getY(),
                                      colW, area.getHeight());
        l.setBounds(c.removeFromTop(12));
        s.setBounds(c.reduced(2));
    };
    cell(0, makeup_,        makeupLabel_);
    cell(1, carrierNoise_,  carrierNoiseLabel_);
    cell(2, sibilance_,     sibilanceLabel_);
    cell(3, clarity_,       clarityLabel_);
    cell(4, gateThreshold_, gateThresholdLabel_);
    cell(5, micGain_,       micGainLabel_);
}

} // namespace guitar_dsp
