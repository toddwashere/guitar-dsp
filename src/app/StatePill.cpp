#include "app/StatePill.h"

namespace guitar_dsp {

void StatePill::setState(ai::ConversationEngine::State s) {
    state_ = s;
    repaint();
}

void StatePill::setErrorReason(std::string r) {
    errorReason_ = std::move(r);
    repaint();
}

std::string StatePill::currentLabel() const {
    using S = ai::ConversationEngine::State;
    switch (state_) {
        case S::Idle:         return "Idle";
        case S::Capturing:    return "Capturing";
        case S::Transcribing: return "Transcribing";
        case S::Thinking:     return "Thinking";
        case S::Speaking:     return "Speaking";
        case S::Error:
            return errorReason_.empty() ? std::string{"Error"}
                                        : "Error: " + errorReason_;
    }
    return "?";
}

void StatePill::paint(juce::Graphics& g) {
    using S = ai::ConversationEngine::State;
    juce::Colour bg;
    switch (state_) {
        case S::Idle:         bg = juce::Colours::darkgrey;        break;
        case S::Capturing:    bg = juce::Colours::red;             break;
        case S::Transcribing: bg = juce::Colours::orange;          break;
        case S::Thinking:     bg = juce::Colours::yellow.darker(); break;
        case S::Speaking:     bg = juce::Colours::green;           break;
        case S::Error:        bg = juce::Colours::orange.darker(); break;
    }
    g.setColour(bg);
    g.fillRoundedRectangle(getLocalBounds().toFloat(), 8.0f);
    g.setColour(juce::Colours::white);
    g.setFont(13.0f);
    g.drawText(juce::String(currentLabel()), getLocalBounds(),
               juce::Justification::centred);
}

} // namespace guitar_dsp
