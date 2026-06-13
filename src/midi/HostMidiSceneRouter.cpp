#include "HostMidiSceneRouter.h"

#include "FCB1010Mapping.h"
#include "SceneCommand.h"

namespace guitar_dsp::midi {

int sceneFromMidiBuffer(const juce::MidiBuffer& midi, const FCB1010Mapping& mapping) {
    int scene = -1;
    for (const auto metadata : midi) {
        const auto msg = metadata.getMessage();
        if (auto cmd = mapping.translate(msg)) {
            if (cmd->type == SceneCommandType::ActivateScene)
                scene = cmd->payload;  // keep the last one in the block
        }
    }
    return scene;
}

bool pitchSingingToggleFromMidiBuffer(const juce::MidiBuffer& midi,
                                      const FCB1010Mapping& mapping) {
    for (const auto metadata : midi) {
        const auto msg = metadata.getMessage();
        if (auto cmd = mapping.translate(msg)) {
            if (cmd->type == SceneCommandType::TogglePitchSinging)
                return true;
        }
    }
    return false;
}

} // namespace guitar_dsp::midi
