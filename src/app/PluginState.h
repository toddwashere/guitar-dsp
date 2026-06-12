#pragma once

#include <juce_core/juce_core.h>

namespace guitar_dsp::app {

struct PluginStateData {
    int   sceneId      = 0;
    float makeup       = 5.0f;
    float carrierNoise = 0.30f;
    float sibilance    = 0.5f;
};

// Minimal, forward-compatible JSON (de)serialization of the persisted plugin
// state. Unknown keys are ignored; missing keys fall back to defaults.
struct PluginState {
    static juce::String     toJson(const PluginStateData& d);
    static PluginStateData  fromJson(const juce::String& json);
};

} // namespace guitar_dsp::app
