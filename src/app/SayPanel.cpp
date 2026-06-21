#include "SayPanel.h"

#include "PluginProcessor.h"

namespace guitar_dsp {

namespace {
constexpr int  kPollIntervalMs = 50;
constexpr long kSynthTimeoutMs = 10000;
} // namespace

SayPanel::SayPanel(PluginProcessor& processor) : processor_(processor) {
    input_.setMultiLine(true);
    input_.setReturnKeyStartsNewLine(false);
    input_.setTextToShowWhenEmpty("Type a phrase, then press Enter or click Say",
                                  juce::Colours::grey);
    input_.onReturnKey = [this] { say(); };
    addAndMakeVisible(input_);

    sayButton_.onClick = [this] { say(); };
    addAndMakeVisible(sayButton_);

    // Song generation row.
    genOldBtn_.onClick  = [this] { generateSong(ai::PersonaId::SongOldGuitar); };
    genRockBtn_.onClick = [this] { generateSong(ai::PersonaId::SongRockingGuitar); };
    saveBtn_.onClick    = [this] { saveCurrentSong(); };
    loadCombo_.setTextWhenNothingSelected("Load saved song…");
    loadCombo_.setTextWhenNoChoicesAvailable("(no saved songs)");
    loadCombo_.onChange = [this] { loadSelectedSong(); };
    addAndMakeVisible(genOldBtn_);
    addAndMakeVisible(genRockBtn_);
    addAndMakeVisible(saveBtn_);
    addAndMakeVisible(loadCombo_);

    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    statusLabel_.setColour(juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible(statusLabel_);

    refreshSavedSongList();

    // Poll for scene-change events so the input field can show each
    // scene's default text. 10 Hz is plenty for a UI affordance.
    startTimer(100);
}

SayPanel::~SayPanel() {
    stopTimer();
}

void SayPanel::resized() {
    auto bounds = getLocalBounds().reduced(6, 6);
    constexpr int rowHeight = 26;
    constexpr int gap       = 6;

    // Row 1: text input + Say.
    auto row1 = bounds.removeFromTop(rowHeight);
    {
        auto sayB = row1.removeFromRight(64);
        row1.removeFromRight(gap);
        input_.setBounds(row1);
        sayButton_.setBounds(sayB);
    }
    bounds.removeFromTop(gap);

    // Row 2: Generate A | Generate B | Save | Load.
    auto row2 = bounds.removeFromTop(rowHeight);
    {
        const int colW = (row2.getWidth() - 3 * gap) / 4;
        genOldBtn_ .setBounds(row2.removeFromLeft(colW)); row2.removeFromLeft(gap);
        genRockBtn_.setBounds(row2.removeFromLeft(colW)); row2.removeFromLeft(gap);
        saveBtn_   .setBounds(row2.removeFromLeft(colW)); row2.removeFromLeft(gap);
        loadCombo_ .setBounds(row2);
    }
    bounds.removeFromTop(gap);

    // Remaining space: status line.
    statusLabel_.setBounds(bounds.removeFromTop(rowHeight));
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

juce::String SayPanel::currentText() const {
    return input_.getText();
}

void SayPanel::setText(juce::String text) {
    input_.setText(std::move(text), juce::dontSendNotification);
    // Prevent the timer-driven scene-default from overwriting this on
    // the next 100 ms tick.
    lastSeenSceneId_ = processor_.activeSceneId();
}

// ---------------------------------------------------------------------------
// Song generation row
// ---------------------------------------------------------------------------

void SayPanel::generateSong(ai::PersonaId p) {
    if (songGenerating_) return;
    songGenerating_ = true;
    genOldBtn_.setEnabled(false);
    genRockBtn_.setEnabled(false);
    statusLabel_.setText("Generating song (5-15s)…", juce::dontSendNotification);

    // safeThis: SayPanel lives as long as the editor; if the editor is
    // closed mid-flight the callback is still safe to fire because
    // PluginProcessor's alive_ check inside generateSong drops the
    // posted async lambda. But we ALSO guard here with a JUCE component
    // safe pointer in case only the panel is destroyed.
    juce::Component::SafePointer<SayPanel> safe(this);
    processor_.generateSong(p, [safe](std::string text, std::string error) {
        if (auto* self = safe.getComponent()) {
            self->onSongResponse(juce::String(text), juce::String(error));
        }
    });
}

void SayPanel::onSongResponse(const juce::String& text, const juce::String& error) {
    songGenerating_ = false;
    genOldBtn_.setEnabled(true);
    genRockBtn_.setEnabled(true);

    if (error.isNotEmpty()) {
        statusLabel_.setText("Song generation failed: " + error,
                             juce::dontSendNotification);
        return;
    }
    if (text.trim().isEmpty()) {
        statusLabel_.setText("Song generation returned no text.",
                             juce::dontSendNotification);
        return;
    }
    setText(text.trim());
    statusLabel_.setText("Song ready. Click Say to synthesize, then pluck.",
                         juce::dontSendNotification);
}

void SayPanel::saveCurrentSong() {
    const auto current = input_.getText().trim().toStdString();
    if (current.empty()) {
        statusLabel_.setText("Nothing to save — text box is empty.",
                             juce::dontSendNotification);
        return;
    }
    auto* nameWindow = new juce::AlertWindow(
        "Save song", "Name this song:", juce::AlertWindow::NoIcon);
    nameWindow->addTextEditor("name", "", "");
    nameWindow->addButton("Save",   1, juce::KeyPress(juce::KeyPress::returnKey));
    nameWindow->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

    juce::Component::SafePointer<SayPanel> safe(this);
    nameWindow->enterModalState(true,
        juce::ModalCallbackFunction::create([safe, nameWindow, current](int result) {
            const auto name = nameWindow->getTextEditorContents("name").toStdString();
            delete nameWindow;
            if (result == 0 || name.empty()) return;
            auto* self = safe.getComponent();
            if (!self) return;
            const bool ok = self->processor_.songStore().save(name, current);
            self->refreshSavedSongList();
            self->statusLabel_.setText(ok ? "Saved." : "Save failed (illegal name?).",
                                       juce::dontSendNotification);
        }),
        /*deleteWhenDismissed*/ false);
}

void SayPanel::refreshSavedSongList() {
    loadCombo_.clear(juce::dontSendNotification);
    int id = 1;
    for (const auto& n : processor_.songStore().list()) {
        loadCombo_.addItem(juce::String(n), id++);
    }
}

void SayPanel::loadSelectedSong() {
    const auto name = loadCombo_.getText().toStdString();
    if (name.empty()) return;
    auto loaded = processor_.songStore().load(name);
    if (!loaded) {
        statusLabel_.setText("Failed to load \"" + juce::String(name) + "\".",
                             juce::dontSendNotification);
        return;
    }
    setText(juce::String(*loaded));
    statusLabel_.setText("Loaded \"" + juce::String(name) + "\". Click Say.",
                         juce::dontSendNotification);
    // Reset combo selection so re-picking the same name re-loads it.
    loadCombo_.setSelectedId(0, juce::dontSendNotification);
}

} // namespace guitar_dsp
