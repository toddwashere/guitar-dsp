#include "app/ConversationPanel.h"
#include <cstdio>

namespace guitar_dsp {

ConversationPanel::ConversationPanel(ai::ConversationEngine& e,
                                     ai::ConversationBuffer& b, bool compact)
    : engine_(e), buf_(b), compact_(compact) {
    addAndMakeVisible(pill_);
    transcript_.setMultiLine(true);
    transcript_.setReadOnly(true);
    transcript_.setScrollbarsShown(true);
    transcript_.setFont(juce::Font(13.0f));
    addAndMakeVisible(transcript_);
    addAndMakeVisible(recordBtn_);
    addAndMakeVisible(clearBtn_);
    addAndMakeVisible(timingsLabel_);
    recordBtn_.onClick = [this]{ onRecord(); };
    clearBtn_.onClick  = [this]{ onClear(); };
    recomposeTranscript();
    startTimerHz(20);
}

void ConversationPanel::resized() {
    auto r = getLocalBounds().reduced(6);
    pill_.setBounds(r.removeFromTop(compact_ ? 18 : 22));
    r.removeFromTop(4);
    auto btnRow = r.removeFromBottom(compact_ ? 22 : 30);
    recordBtn_  .setBounds(btnRow.removeFromLeft(compact_ ?  70 : 100));
    btnRow.removeFromLeft(4);
    clearBtn_   .setBounds(btnRow.removeFromLeft(compact_ ?  50 :  70));
    if (! compact_) {
        timingsLabel_.setBounds(r.removeFromBottom(18));
    }
    transcript_.setBounds(r);
}

void ConversationPanel::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour(0xff222426));
}

void ConversationPanel::onRecord() {
    using S = ai::ConversationEngine::State;
    auto s = engine_.state();
    std::fprintf(stderr, "[ConversationPanel] onRecord() clicked, state=%d\n", (int)s);
    std::fflush(stderr);
    if      (s == S::Idle)      engine_.startTurn();
    else if (s == S::Capturing) engine_.endTurn();
    else                        engine_.cancelTurn();
}
void ConversationPanel::onClear() { engine_.clearConversation(); }

void ConversationPanel::recomposeTranscript() {
    auto snap = buf_.snapshot();
    lastSeenSize_ = snap.size();
    if (compact_ && snap.size() > 2)
        snap.erase(snap.begin(), snap.end() - 2);

    juce::String out;
    for (auto& m : snap) {
        out << (m.role == ai::Message::Role::User ? "You: " : "AI : ")
            << juce::String(m.text) << "\n";
    }
    transcript_.setText(out, juce::dontSendNotification);
}

std::string ConversationPanel::composedTranscriptForTest() const {
    return transcript_.getText().toStdString();
}

void ConversationPanel::timerCallback() {
    pill_.setState(engine_.state());
    if (engine_.state() == ai::ConversationEngine::State::Error)
        pill_.setErrorReason(engine_.lastError());

    if (buf_.snapshot().size() != lastSeenSize_) recomposeTranscript();

    auto t = engine_.lastTimings();
    if (t.tts.count() > 0) {
        timingsLabel_.setText(
            "STT " + juce::String(t.stt.count()/1000.0, 1) + "s | "
            "LLM " + juce::String(t.llm.count()/1000.0, 1) + "s | "
            "TTS " + juce::String(t.tts.count()/1000.0, 1) + "s",
            juce::dontSendNotification);
    }
    using S = ai::ConversationEngine::State;
    recordBtn_.setButtonText(engine_.state() == S::Capturing
                              ? juce::String("[] Stop")
                              : juce::String("(*) Record"));
}

} // namespace guitar_dsp
