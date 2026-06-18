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

    // Poll for scene-change events so the input field can show each
    // scene's default text. 10 Hz is plenty for a UI affordance.
    startTimer(100);
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
    // Don't change the timer — it's already running at 100 ms from the
    // constructor for scene-change / auto-say polling. Faster polling
    // isn't required here; synthesis takes >100 ms anyway.
}

void SayPanel::timerCallback() {
    // Per-scene default text: when the active scene changes, populate the
    // input field with that scene's tts.text so the operator can edit or
    // re-trigger it. Doesn't fire mid-edit because the user's edits leave
    // the scene id unchanged — only an actual scene change resets the box.
    const int curSceneId = processor_.activeSceneId();
    if (curSceneId != lastSeenSceneId_) {
        lastSeenSceneId_ = curSceneId;
        const auto defaultText = processor_.activeSceneTtsText();
        input_.setText(juce::String(defaultText), juce::dontSendNotification);
    }

    // Auto-say from the ConversationEngine — when the LLM produces a reply,
    // PluginProcessor::onLlmResponse stashes the text here. We pull it,
    // populate the input field, and fire say() so the reply gets installed
    // into the note-stepped player. The user then plucks notes to advance
    // through the reply word-by-word, with WordReadout showing each word.
    if (pendingText_.empty()) {
        const auto autoSay = processor_.takePendingAutoSay();
        if (!autoSay.empty()) {
            input_.setText(juce::String(autoSay), juce::dontSendNotification);
            say();
        }
    }

    // Pending-synth poll path (runs on top of the always-on 100 ms poll).
    if (pendingText_.empty()) {
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
    // Keep the timer running — it's the always-on 100 ms poll for scene
    // changes and LLM auto-say. We don't need a separate per-synth timer
    // anymore; pendingText_.empty() is the signal that no synth is active.
    sayButton_.setEnabled(true);
    sayButton_.setButtonText("Say");
    input_.setEnabled(true);
}

void SayPanel::setText(juce::String text) {
    input_.setText(std::move(text), juce::dontSendNotification);
    // Prevent the timer-driven scene-default from overwriting this on
    // the next 100 ms tick.
    lastSeenSceneId_ = processor_.activeSceneId();
}

} // namespace guitar_dsp
