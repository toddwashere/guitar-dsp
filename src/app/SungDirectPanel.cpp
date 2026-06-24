#include "SungDirectPanel.h"

namespace guitar_dsp::app {

SungDirectPanel::SungDirectPanel() {
    addAndMakeVisible(picker_);
    picker_.onChange = [this](int idx) { if (onVoicePackChange) onVoicePackChange(idx); };

    // Vowel toggle pills (Ah/Eh/Ee/Oh/Oo) — toggle per-vowel inclusion in
    // the rotation. Default all enabled (mask 0x1F). PluginEditor's
    // scene-change branch pushes the persisted state in.
    addAndMakeVisible(vowelPills_);
    vowelPills_.onMaskChange = [this](std::uint32_t mask) {
        if (onVowelMaskChange) onVowelMaskChange(mask);
    };

    // Load-status label — small caption beneath the voice picker. Hidden
    // by default; PluginEditor's timerCallback drives setLoadStatus().
    loadStatusLabel_.setText("", juce::dontSendNotification);
    loadStatusLabel_.setFont(juce::Font{juce::FontOptions{}.withHeight(11.5f)});
    loadStatusLabel_.setColour(juce::Label::textColourId,
                               juce::Colour::fromRGB(255, 200, 100));
    loadStatusLabel_.setJustificationType(juce::Justification::centredLeft);
    loadStatusLabel_.setVisible(false);
    addAndMakeVisible(loadStatusLabel_);

    // Detected-pitch readout — gives the operator a visual confirmation
    // that YIN is tracking the played note (the SungDirect path is gated
    // by the same detected-pitch atomic as the carrier). Always visible.
    pitchLabel_.setText("(no pitch)", juce::dontSendNotification);
    pitchLabel_.setFont(juce::Font{juce::FontOptions{}.withHeight(12.5f).withStyle("Bold")});
    pitchLabel_.setColour(juce::Label::textColourId,
                          juce::Colour::fromRGB(180, 190, 205));
    pitchLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(pitchLabel_);

    formantLabel_.setText("Pitch offset", juce::dontSendNotification);
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

void SungDirectPanel::setDetectedPitch(int midi, float hz) {
    if (midi == lastPitchMidi_ && std::abs(hz - lastPitchHz_) < 0.5f) return;
    lastPitchMidi_ = midi;
    lastPitchHz_   = hz;
    if (midi < 0 || hz <= 0.0f) {
        pitchLabel_.setText("(no pitch)", juce::dontSendNotification);
        pitchLabel_.setColour(juce::Label::textColourId,
                              juce::Colour::fromRGB(110, 120, 135));
        return;
    }
    static constexpr const char* kNoteNames[] = {
        "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
    };
    const int  noteIdx = ((midi % 12) + 12) % 12;
    const int  octave  = (midi / 12) - 1;
    const juce::String txt = juce::String(kNoteNames[noteIdx])
                           + juce::String(octave) + "   "
                           + juce::String(hz, 1) + " Hz";
    pitchLabel_.setText(txt, juce::dontSendNotification);
    pitchLabel_.setColour(juce::Label::textColourId,
                          juce::Colour::fromRGB(150, 220, 255));
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
    // Top row: picker on the left, detected-pitch readout on the right.
    {
        auto top = r.removeFromTop(24);
        pitchLabel_.setBounds(top.removeFromRight(140));
        picker_.setBounds(top);
    }
    r.removeFromTop(2);
    // Vowel pills strip — under the picker, above the load status.
    vowelPills_.setBounds(r.removeFromTop(22));
    r.removeFromTop(4);
    // Status label sits just under the pills, only takes height when shown.
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
