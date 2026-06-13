#pragma once
#include "ai/ConversationBuffer.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace guitar_dsp::ai {

enum class PersonaId {
    Interviewer = 0,
    Snarky,
    WeatheredGuitar,
    StudioEngineer,
    CuriousAi,
    PlainAssistant,
};

class PersonaRegistry {
public:
    static std::string defaultPromptFor(PersonaId);

    std::string promptFor(PersonaId) const;
    void        setCustomPrompt(PersonaId, std::string);
    void        resetToDefault(PersonaId);

    std::vector<Message> buildMessages(const ConversationBuffer&,
                                       PersonaId) const;

private:
    std::unordered_map<int, std::string> overrides_;
};

} // namespace guitar_dsp::ai
