#include "VowelMaskPills.h"

namespace guitar_dsp::app {

const char* const VowelMaskPills::kLabels[VowelMaskPills::kNumPills] = {
    "Ah", "Eh", "Ee", "Oh", "Oo"
};

VowelMaskPills::VowelMaskPills() {
    for (int i = 0; i < kNumPills; ++i) {
        auto& p = pills_[static_cast<std::size_t>(i)];
        p.setButtonText(kLabels[i]);
        p.setClickingTogglesState(true);
        p.setToggleState(true, juce::dontSendNotification);
        p.setConnectedEdges(
            (i == 0 ? 0 : juce::Button::ConnectedOnLeft) |
            (i == kNumPills - 1 ? 0 : juce::Button::ConnectedOnRight));
        p.onClick = [this, i]() {
            const std::uint32_t bit = (1u << i);
            const auto& self = pills_[static_cast<std::size_t>(i)];
            if (self.getToggleState()) currentMask_ |=  bit;
            else                       currentMask_ &= ~bit;
            applyVisualState_();
            if (onMaskChange) onMaskChange(currentMask_);
        };
        addAndMakeVisible(p);
    }
    applyVisualState_();
}

void VowelMaskPills::setMask(std::uint32_t mask) {
    if (mask == currentMask_) return;
    currentMask_ = mask;
    for (int i = 0; i < kNumPills; ++i) {
        const bool on = (mask & (1u << i)) != 0;
        pills_[static_cast<std::size_t>(i)].setToggleState(
            on, juce::dontSendNotification);
    }
    applyVisualState_();
}

void VowelMaskPills::applyVisualState_() {
    // Lit when enabled, dimmed when off. Background colour change so the
    // pill state is readable even without focus or hover.
    for (int i = 0; i < kNumPills; ++i) {
        auto& p = pills_[static_cast<std::size_t>(i)];
        const bool on = (currentMask_ & (1u << i)) != 0;
        p.setColour(juce::TextButton::buttonColourId,
                    on ? juce::Colour::fromRGB(85, 105, 130)
                       : juce::Colour::fromRGB(28, 32, 40));
        p.setColour(juce::TextButton::buttonOnColourId,
                    juce::Colour::fromRGB(110, 145, 180));
        p.setColour(juce::TextButton::textColourOnId,
                    juce::Colour::fromRGB(245, 250, 255));
        p.setColour(juce::TextButton::textColourOffId,
                    juce::Colour::fromRGB(140, 150, 165));
    }
}

void VowelMaskPills::resized() {
    auto r = getLocalBounds();
    const int w = r.getWidth() / kNumPills;
    for (int i = 0; i < kNumPills; ++i) {
        auto cell = r.removeFromLeft(i == kNumPills - 1 ? r.getWidth() : w);
        pills_[static_cast<std::size_t>(i)].setBounds(cell.reduced(0, 1));
    }
}

void VowelMaskPills::paint(juce::Graphics& /*g*/) {}

} // namespace guitar_dsp::app
