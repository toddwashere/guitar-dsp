#pragma once

#include <juce_core/juce_core.h>

namespace guitar_dsp::app {

struct PluginStateData {
    int   sceneId      = 0;
    float makeup       = 5.0f;
    float carrierNoise = 0.30f;
    float sibilance    = 0.5f;
    // NOTE: vocoder `clarity` is intentionally NOT persisted — it is a per-
    // SCENE control (every scene change overwrites it from cfg.clarity), so
    // saving it would just race with the scene-change handler on reload. To
    // change a scene's clarity persistently, edit the scene JSON.
};

// Minimal, forward-compatible JSON (de)serialization of the persisted plugin
// state. Unknown keys are ignored; missing keys fall back to defaults.
struct PluginState {
    static juce::String     toJson(const PluginStateData& d);
    static PluginStateData  fromJson(const juce::String& json);
};

} // namespace guitar_dsp::app
