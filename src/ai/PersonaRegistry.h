#pragma once
#include "ai/ConversationBuffer.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace guitar_dsp::ai {

class KnowledgeDoc;  // forward decl

enum class PersonaId {
    Interviewer = 0,
    Snarky,
    WeatheredGuitar,
    StudioEngineer,
    CuriousAi,
    PlainAssistant,
    SongOldGuitar,        // "Song: I'm an old guitar" — wistful autobiographical lyrics
    SongRockingGuitar,    // "Song: Rocking guitar"     — cocky/swagger lyrics
    SessionQa,            // audience Q&A grounded in a knowledge doc
};

class PersonaRegistry {
public:
    static std::string defaultPromptFor(PersonaId);

    std::string promptFor(PersonaId) const;
    void        setCustomPrompt(PersonaId, std::string);
    void        resetToDefault(PersonaId);

    // Nullable; lifetime owned by the caller. Only used when persona is
    // SessionQa. When unset or contents() is empty, the reference-document
    // section of the system message becomes "REFERENCE DOCUMENT NOT LOADED."
    // so the on-stage answer is audibly diagnostic.
    void setSessionQaDoc(KnowledgeDoc* doc) noexcept { sessionQaDoc_ = doc; }

    std::vector<Message> buildMessages(const ConversationBuffer&,
                                       PersonaId) const;

private:
    std::unordered_map<int, std::string> overrides_;
    KnowledgeDoc*                        sessionQaDoc_ = nullptr;
};

} // namespace guitar_dsp::ai
