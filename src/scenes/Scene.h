#pragma once

#include <cstdint>
#include <string>

namespace guitar_dsp::scenes {

struct MixerParams {
    float masterGainDb = 0.0f;
    float dryWet       = 0.0f;   // 0 = fully dry, 1 = fully wet
    float transitionMs = 20.0f;
};

struct TtsConfig {
    std::string source;   // "prebaked" | "apple" | "piper" | "" (none)
    std::string clip;     // identifier passed to ITTSSource::synthesize()
};

struct Scene {
    int          id        = 0;
    std::string  name      = "(unnamed)";
    std::uint32_t colorRgb = 0xCCCCCCu;   // 0xRRGGBB
    MixerParams  mixer{};
    TtsConfig    tts{};

    static Scene defaults(int id);
};

inline Scene Scene::defaults(int id) {
    Scene s;
    s.id = id;
    s.name = "Scene " + std::to_string(id);
    return s;
}

} // namespace guitar_dsp::scenes
