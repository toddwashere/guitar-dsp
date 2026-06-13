#include "FCB1010Mapping.h"

#include <juce_core/juce_core.h>

namespace guitar_dsp::midi {

FCB1010Mapping FCB1010Mapping::stockDefaults() {
    FCB1010Mapping m;
    for (int i = 0; i < 10; ++i) m.programChangeToScene_[i] = i;
    m.wetDryCc_     = 27;
    m.masterGainCc_ = 7;
    return m;
}

std::optional<FCB1010Mapping> FCB1010Mapping::loadFromJson(const std::string& jsonText) {
    auto parsed = juce::JSON::parse(juce::String(jsonText));
    if (!parsed.isObject()) return std::nullopt;
    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr) return std::nullopt;

    FCB1010Mapping m;

    if (obj->hasProperty("programChangeToScene")) {
        auto pcVar = obj->getProperty("programChangeToScene");
        if (auto* pcObj = pcVar.getDynamicObject()) {
            for (const auto& kv : pcObj->getProperties()) {
                const int pc = kv.name.toString().getIntValue();
                const int scene = static_cast<int>(kv.value);
                m.programChangeToScene_[pc] = scene;
            }
        }
    }

    if (obj->hasProperty("expressionPedalCcs")) {
        if (auto* ccObj = obj->getProperty("expressionPedalCcs").getDynamicObject()) {
            if (ccObj->hasProperty("wetDry"))
                m.wetDryCc_ = static_cast<int>(ccObj->getProperty("wetDry"));
            if (ccObj->hasProperty("masterGain"))
                m.masterGainCc_ = static_cast<int>(ccObj->getProperty("masterGain"));
        }
    }

    if (obj->hasProperty("aiPedals")) {
        if (auto* a = obj->getProperty("aiPedals").getDynamicObject()) {
            if (a->hasProperty("ptt"))         m.ai_.pttProgramChange       = static_cast<int>(a->getProperty("ptt"));
            if (a->hasProperty("clearChat"))   m.ai_.clearChatProgramChange = static_cast<int>(a->getProperty("clearChat"));
            if (a->hasProperty("longPressMs")) m.ai_.longPressMillis        = static_cast<int>(a->getProperty("longPressMs"));
        }
    }

    return m;
}

AiAction FCB1010Mapping::decodeAi(int pc, bool isLongPress) const {
    if (pc == ai_.pttProgramChange && !isLongPress)    return AiAction::PttToggle;
    if (pc == ai_.clearChatProgramChange) {
        return isLongPress ? AiAction::ClearChat : AiAction::CancelTurn;
    }
    return AiAction::None;
}

void FCB1010Mapping::setAiBindings(AiPedalBindings b) {
    ai_ = b;
}

std::optional<SceneCommand> FCB1010Mapping::translate(const juce::MidiMessage& msg) const {
    if (msg.isProgramChange()) {
        const int pc = msg.getProgramChangeNumber();
        auto it = programChangeToScene_.find(pc);
        if (it == programChangeToScene_.end()) return std::nullopt;
        return SceneCommand{SceneCommandType::ActivateScene, it->second};
    }
    if (msg.isController()) {
        const int cc = msg.getControllerNumber();
        const int val = msg.getControllerValue();
        if (cc == wetDryCc_     && wetDryCc_     >= 0)
            return SceneCommand{SceneCommandType::SetWetDry, val};
        if (cc == masterGainCc_ && masterGainCc_ >= 0)
            return SceneCommand{SceneCommandType::SetMasterGain, val};
    }
    return std::nullopt;
}

} // namespace guitar_dsp::midi
