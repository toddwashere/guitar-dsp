#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include <juce_audio_basics/juce_audio_basics.h>

#include "SceneCommand.h"

namespace guitar_dsp::midi {

enum class AiAction { None, PttToggle, CancelTurn, ClearChat };

struct AiPedalBindings {
    int  pttProgramChange       = -1;   // -1 = disabled
    int  clearChatProgramChange = -1;
    int  longPressMillis        = 700;
};

class FCB1010Mapping {
public:
    static FCB1010Mapping stockDefaults();
    static std::optional<FCB1010Mapping> loadFromJson(const std::string& jsonText);

    std::optional<SceneCommand> translate(const juce::MidiMessage& msg) const;

    AiAction        decodeAi(int programChangeNumber, bool isLongPress) const;
    void            setAiBindings(AiPedalBindings);
    AiPedalBindings aiBindings() const { return ai_; }

private:
    std::unordered_map<int, int> programChangeToScene_;
    int wetDryCc_     = -1;
    int masterGainCc_ = -1;
    int pitchSingingToggleCc_ = -1;
    AiPedalBindings ai_;
};

} // namespace guitar_dsp::midi
