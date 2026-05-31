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
    std::string clip;     // identifier passed to PrebakedTTSSource
    std::string text;     // text passed to live sources (apple, piper)
    std::string voice;    // optional voice id for live sources
    std::string fallback; // source name to try if primary fails ("" = none)
};

struct CarouselConfig {
    bool  enabled = false;
    float drive   = 0.0f;          // dB pre-gain

    enum class Shaper { None, Tanh, HardClip, Foldback };
    Shaper shaper       = Shaper::None;
    float  shaperAmount = 1.0f;    // pre-shaper gain multiplier

    int crusherBits       = 0;     // 0 = bypass; else 1..16
    int crusherDownsample = 1;     // 1 = bypass; else hold N samples

    enum class FilterMode { Off, LowPass, BandPass, HighPass };
    enum class FilterMod  { Static, Envelope, Lfo };
    FilterMode filterMode     = FilterMode::Off;
    FilterMod  filterMod      = FilterMod::Static;
    float      filterCutoffHz = 1000.0f;
    float      filterResonance= 0.5f;
    float      filterEnvAmount= 0.0f;   // Hz added at full envelope
    float      filterLfoHz    = 0.0f;

    float chorusRateHz = 0.0f;     // 0 = bypass
    float chorusDepth  = 0.0f;
    float chorusMix    = 0.0f;

    float reverbRoomSize = 0.0f;   // 0 = bypass
    float reverbWet      = 0.0f;

    float outputTrimDb = 0.0f;
};

struct Scene {
    int          id        = 0;
    std::string  name      = "(unnamed)";
    std::uint32_t colorRgb = 0xCCCCCCu;   // 0xRRGGBB
    MixerParams    mixer{};
    TtsConfig      tts{};
    CarouselConfig carousel{};

    static Scene defaults(int id);
};

inline Scene Scene::defaults(int id) {
    Scene s;
    s.id = id;
    s.name = "Scene " + std::to_string(id);
    return s;
}

} // namespace guitar_dsp::scenes
