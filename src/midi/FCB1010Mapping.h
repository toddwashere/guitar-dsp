#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include <juce_audio_basics/juce_audio_basics.h>

#include "SceneCommand.h"

namespace guitar_dsp::midi {

class FCB1010Mapping {
public:
    static FCB1010Mapping stockDefaults();
    static std::optional<FCB1010Mapping> loadFromJson(const std::string& jsonText);

    std::optional<SceneCommand> translate(const juce::MidiMessage& msg) const;

private:
    std::unordered_map<int, int> programChangeToScene_;
    int wetDryCc_     = -1;
    int masterGainCc_ = -1;
};

} // namespace guitar_dsp::midi
