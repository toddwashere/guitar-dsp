#include "SayPanel.h"

#include "PluginProcessor.h"

namespace guitar_dsp {

namespace {
constexpr int  kPollIntervalMs = 50;
constexpr long kSynthTimeoutMs = 10000;
} // namespace

SayPanel::SayPanel(PluginProcessor& processor) : processor_(processor) {
    input_.setMultiLine(false);
    input_.setReturnKeyStartsNewLine(false);
    input_.setTextToShowWhenEmpty("Type a phrase, then press Enter or click Say",
                                  juce::Colours::grey);
    input_.onReturnKey = [this] { say(); };
    addAndMakeVisible(input_);

    sayButton_.onClick = [this] { say(); };
    addAndMakeVisible(sayButton_);
}

SayPanel::~SayPanel() {
    stopTimer();
}

void SayPanel::resized() {
    auto bounds = getLocalBounds().reduced(6, 6);
    constexpr int buttonWidth = 64;
    constexpr int gap = 6;
    auto buttonBounds = bounds.removeFromRight(buttonWidth);
    bounds.removeFromRight(gap);
    input_.setBounds(bounds);
    sayButton_.setBounds(buttonBounds);
}

void SayPanel::say() {
    const auto text = input_.getText().trim().toStdString();
    if (text.empty()) return;

    pendingText_     = text;
    pendingExpiryMs_ = juce::Time::currentTimeMillis() + kSynthTimeoutMs;

    sayButton_.setEnabled(false);
    sayButton_.setButtonText("…");
    input_.setEnabled(false);

    processor_.enqueueSayText(text);
    startTimer(kPollIntervalMs);
}

void SayPanel::timerCallback() {
    if (pendingText_.empty()) {
        stopTimer();
        return;
    }
    const int result = processor_.tryInstallSayText(pendingText_);
    if (result != 0) {
        finishPending(result > 0);
        return;
    }
    if (juce::Time::currentTimeMillis() > pendingExpiryMs_) {
        finishPending(false);
    }
}

void SayPanel::finishPending(bool /*succeeded*/) {
    pendingText_.clear();
    stopTimer();
    sayButton_.setEnabled(true);
    sayButton_.setButtonText("Say");
    input_.setEnabled(true);
}

} // namespace guitar_dsp
