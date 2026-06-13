#pragma once

namespace guitar_dsp::midi {

enum class SceneCommandType {
    ActivateScene,        // payload = scene id
    SetWetDry,            // payload = 0..127 (CC value, caller normalizes)
    SetMasterGain,        // payload = 0..127 (CC value, caller normalizes)
    TogglePitchSinging,   // payload unused; emit once per CC press
};

struct SceneCommand {
    SceneCommandType type;
    int              payload;
};

} // namespace guitar_dsp::midi
