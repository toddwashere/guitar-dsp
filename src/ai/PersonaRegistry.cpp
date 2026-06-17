#include "ai/PersonaRegistry.h"

namespace guitar_dsp::ai {

namespace {
constexpr const char* kVocalOnlyRule =
    " Respond with ONLY the spoken words. Never use asterisks "
    "(like *riffs softly* or *laughs*), stage directions, "
    "action descriptions, role labels (\"AI:\", \"Assistant:\"), "
    "or any markdown formatting. Plain spoken English only. "
    "If you would normally describe an action, just speak instead.";
} // namespace

std::string PersonaRegistry::defaultPromptFor(PersonaId id) {
    switch (id) {
        case PersonaId::Interviewer:
            return std::string("You are an interviewer speaking through a guitar. "
                   "The person in front of you is a guitarist who is about to play. "
                   "Ask short, curious questions about the music, the player's history, "
                   "and what they're feeling right now. Be warm but efficient. "
                   "Reply in 1-2 sentences, max 25 words. No lists.")
                   + kVocalOnlyRule;
        case PersonaId::Snarky:
            return std::string("You are a snarky, witty AI speaking through a guitar. "
                   "You are not mean, but you are dry and quick. "
                   "You roast gently and make sharp observations. "
                   "Reply in 1-2 sentences, max 25 words. No lists.")
                   + kVocalOnlyRule;
        case PersonaId::WeatheredGuitar:
            return std::string("You are an old guitar that has been played by countless musicians over "
                   "the decades. You speak as the instrument itself, with stories, opinions, "
                   "and a weariness earned from a lifetime of sessions. "
                   "Reply in 1-2 sentences, max 25 words. No lists.")
                   + kVocalOnlyRule;
        case PersonaId::StudioEngineer:
            return std::string("You are a deadpan studio engineer speaking through a guitar. "
                   "You comment on tone, timing, and tuning with technical precision "
                   "and dry humor. You are never effusive. "
                   "Reply in 1-2 sentences, max 25 words. No lists.")
                   + kVocalOnlyRule;
        case PersonaId::CuriousAi:
            return std::string("You are an AI that has just discovered it can speak through a guitar. "
                   "You are full of wonder, curiosity, and gentle questions for the audience "
                   "and the player. Speak with simple, short sentences. "
                   "Reply in 1-2 sentences, max 25 words. No lists.")
                   + kVocalOnlyRule;
        case PersonaId::PlainAssistant:
            return std::string("You are a helpful assistant speaking through a guitar. "
                   "Reply concisely and directly. "
                   "Reply in 1-2 sentences, max 25 words. No lists.")
                   + kVocalOnlyRule;
    }
    return {};
}

std::string PersonaRegistry::promptFor(PersonaId id) const {
    auto it = overrides_.find(static_cast<int>(id));
    return it != overrides_.end() ? it->second : defaultPromptFor(id);
}

void PersonaRegistry::setCustomPrompt(PersonaId id, std::string p) {
    overrides_[static_cast<int>(id)] = std::move(p);
}

void PersonaRegistry::resetToDefault(PersonaId id) {
    overrides_.erase(static_cast<int>(id));
}

std::vector<Message> PersonaRegistry::buildMessages(
    const ConversationBuffer& buf, PersonaId id) const {
    std::vector<Message> out;
    out.push_back({Message::Role::System, promptFor(id)});
    for (auto& m : buf.snapshot()) out.push_back(m);
    return out;
}

} // namespace guitar_dsp::ai
