#include "SungDirectPanel.h"

namespace guitar_dsp::app {

SungDirectPanel::SungDirectPanel() {
    addAndMakeVisible(picker_);
    picker_.onChange = [this](int idx) { if (onVoicePackChange) onVoicePackChange(idx); };

    // Load-status label — small caption beneath the voice picker. Hidden
    // by default; PluginEditor's timerCallback drives setLoadStatus().
    loadStatusLabel_.setText("", juce::dontSendNotification);
    loadStatusLabel_.setFont(juce::Font{juce::FontOptions{}.withHeight(11.5f)});
    loadStatusLabel_.setColour(juce::Label::textColourId,
                               juce::Colour::fromRGB(255, 200, 100));
    loadStatusLabel_.setJustificationType(juce::Justification::centredLeft);
    loadStatusLabel_.setVisible(false);
    addAndMakeVisible(loadStatusLabel_);

    formantLabel_.setText("Formant tint", juce::dontSendNotification);
    portamentoLabel_.setText("Portamento", juce::dontSendNotification);
    scoopLabel_.setText("Scoop", juce::dontSendNotification);
    for (auto* l : { &formantLabel_, &portamentoLabel_, &scoopLabel_ }) {
        l->setFont(juce::Font{juce::FontOptions{}.withHeight(10.5f)});
        l->setColour(juce::Label::textColourId, juce::Colour::fromRGB(150, 160, 175));
        addAndMakeVisible(*l);
    }
    // Note: formantTint_ is wired through FormantShifter::setFormantTintSemitones().
    // It adds a semitone offset to the ratio lookup in the pre-rendered table.
    // This is not true formant-axis warping but gives a visible, audible effect.

    formantTint_.setRange(-6.0, 6.0, 0.1);
    formantTint_.setValue(0.0);
    formantTint_.setTextValueSuffix(" st");

    portamento_.setRange(0.0, 200.0, 1.0);
    portamento_.setValue(40.0);
    portamento_.setTextValueSuffix(" ms");

    scoopIn_.setRange(0.0, 150.0, 1.0);
    scoopIn_.setValue(0.0);
    scoopIn_.setTextValueSuffix(" ms");

    for (auto* s : { &formantTint_, &portamento_ })
        addAndMakeVisible(*s);

    // TODO(scoop-in): scoopIn_ slider is hidden until the scoop-in effect is
    // implemented. The slider exists and its callback is wired, but the audio
    // engine has no corresponding implementation yet. Keep it invisible so
    // users don't interact with a no-op control.
    scoopIn_.setVisible(false);
    scoopLabel_.setVisible(false);

    formantTint_.onValueChange = [this] {
        if (onFormantTintChange) onFormantTintChange(static_cast<float>(formantTint_.getValue()));
    };
    portamento_.onValueChange = [this] {
        if (onPortamentoMsChange) onPortamentoMsChange(static_cast<float>(portamento_.getValue()));
    };
    scoopIn_.onValueChange = [this] {
        if (onScoopInMsChange) onScoopInMsChange(static_cast<float>(scoopIn_.getValue()));
    };
}

SungDirectPanel::~SungDirectPanel() = default;

void SungDirectPanel::setVoicePacks(
    const std::vector<std::pair<std::string, std::string>>& packs, int idx) {
    picker_.setPacks(packs, idx);
}

void SungDirectPanel::setLoadStatus(LoadStatus status, int progressPercent) {
    if (status == lastLoadStatus_ && progressPercent == lastLoadProgress_)
        return;
    lastLoadStatus_   = status;
    lastLoadProgress_ = progressPercent;
    switch (status) {
        case LoadStatus::Idle:
            loadStatusLabel_.setVisible(false);
            loadStatusLabel_.setText("", juce::dontSendNotification);
            break;
        case LoadStatus::Loading: {
            loadStatusLabel_.setVisible(true);
            const auto txt = juce::String("Loading vocals… ")
                           + juce::String(progressPercent) + "%";
            loadStatusLabel_.setText(txt, juce::dontSendNotification);
            break;
        }
        case LoadStatus::Ready:
            // Briefly show "Ready" then auto-hide on the next Idle transition.
            loadStatusLabel_.setVisible(true);
            loadStatusLabel_.setText("Ready", juce::dontSendNotification);
            break;
    }
}

void SungDirectPanel::resized() {
    auto r = getLocalBounds().reduced(4);
    picker_.setBounds(r.removeFromTop(24));
    r.removeFromTop(2);
    // Status label sits just under the picker, only takes height when shown.
    loadStatusLabel_.setBounds(r.removeFromTop(loadStatusLabel_.isVisible() ? 18 : 0));
    if (loadStatusLabel_.isVisible()) r.removeFromTop(2);

    constexpr int rowH  = 22;
    constexpr int labelW = 90;

    auto row = r.removeFromTop(rowH);
    formantLabel_.setBounds(row.removeFromLeft(labelW));
    formantTint_.setBounds(row);
    r.removeFromTop(4);

    row = r.removeFromTop(rowH);
    portamentoLabel_.setBounds(row.removeFromLeft(labelW));
    portamento_.setBounds(row);
    r.removeFromTop(4);

    row = r.removeFromTop(rowH);
    scoopLabel_.setBounds(row.removeFromLeft(labelW));
    scoopIn_.setBounds(row);
}

void SungDirectPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(16, 18, 24).withAlpha(0.95f));
    g.setColour(juce::Colour::fromRGB(110, 120, 135));
    g.setFont(juce::Font{juce::FontOptions{}.withHeight(10.0f)});
    g.drawText("SUNG DIRECT", getLocalBounds().removeFromTop(12).reduced(6, 0),
               juce::Justification::topLeft);
}

} // namespace guitar_dsp::app
