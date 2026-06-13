#pragma once
#include "ai/ConversationEngine.h"
#include <juce_gui_basics/juce_gui_basics.h>

#include <string>

namespace guitar_dsp {

class StatePill : public juce::Component {
public:
    void setState(ai::ConversationEngine::State);
    void setErrorReason(std::string);

    std::string currentLabel() const;

    void paint(juce::Graphics&) override;

private:
    ai::ConversationEngine::State state_ {ai::ConversationEngine::State::Idle};
    std::string errorReason_;
};

} // namespace guitar_dsp
