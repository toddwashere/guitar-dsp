#pragma once
#include "ai/ConversationEngine.h"
#include "ai/ConversationBuffer.h"
#include "app/StatePill.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace guitar_dsp {

class ConversationPanel : public juce::Component, private juce::Timer {
public:
    ConversationPanel(ai::ConversationEngine&, ai::ConversationBuffer&, bool compact);

    void resized() override;
    void paint(juce::Graphics&) override;

    // Returns the composed transcript text (for testing).
    std::string composedTranscriptForTest() const;

private:
    void timerCallback() override;
    void onRecord();
    void onClear();
    void recomposeTranscript();

    ai::ConversationEngine&  engine_;
    ai::ConversationBuffer&  buf_;
    bool                     compact_;

    StatePill                pill_;
    juce::TextEditor         transcript_;
    juce::TextButton         recordBtn_   {"(*) Record"};
    juce::TextButton         clearBtn_    {"Clear"};
    juce::Label              timingsLabel_;
    size_t                   lastSeenSize_ {0};
};

} // namespace guitar_dsp
