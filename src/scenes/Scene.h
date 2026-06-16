#pragma once

#include <cstdint>
#include <string>
#include <vector>

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
    std::vector<std::string> bank; // ordered clip keys when source == "clipBank"; empty otherwise
    std::string trigger;  // "auto"/"" (whole clip, default) | "note" (per-note word stepping)
    std::string wordSync = "global";  // "global"|"latch"|"advance"|"syllable"
    float clarity = 0.0f; // 0 = fully vocoded; 1 = dry TTS (speak-clearly mode)
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

    // --- Phase 4b: pitch / harmony stages (all bypass at the listed default) ---
    static constexpr int kMaxHarmVoices = 4;

    float pitchSemitones = 0.0f;   // 0 = bypass single-voice pitch shift
    float pitchMix       = 0.0f;   // dry/shifted blend (0=dry .. 1=shifted)
    float pitchGrainMs   = 40.0f;  // granular window size

    int   harmVoiceCount = 0;      // 0 = bypass harmonizer
    int   harmSemitones[kMaxHarmVoices]   = {0, 0, 0, 0};
    int   harmDetuneCents[kMaxHarmVoices] = {0, 0, 0, 0};
    float harmMix        = 0.0f;   // dry/harmonized blend

    float combFreqHz   = 0.0f;     // 0 = bypass comb
    float combFeedback = 0.0f;
    float combMix      = 0.0f;

    enum class Vowel { None, Ah, Oh, Ee };
    Vowel formantVowel  = Vowel::None;  // None = bypass formant
    float formantAmount = 0.0f;

    // --- Phase C (Auto-Vocal Formant) -----------------------------------
    // Drives Formant's vowel position over time. Defaults to Static + empty
    // breakpoints, which is back-compat with the existing static-vowel API:
    // CarouselMod calls Formant::setVowel(formantVowel) and the position is
    // pinned. Set `formantMode = Lfo` (or Envelope) and populate
    // `formantBreakpoints` to use the new continuous-vowel-space path.
    enum class FormantMode { Static, Lfo, Envelope };
    FormantMode        formantMode = FormantMode::Static;

    // Vowel position breakpoints in [0,1). Anchors: 0.0=EE, 0.25=EH, 0.5=AH,
    // 0.75=OH, ~1.0=OO (wraps back to EE in the circular vowel space).
    // Empty in Static mode; populated in Lfo/Envelope mode.
    std::vector<float> formantBreakpoints;

    float              formantLfoHz       = 0.0f;    // Lfo mode: cycles/s
    float              formantEnvAttackMs = 30.0f;   // Envelope mode: attack ramp

    float outputTrimDb = 0.0f;
};

struct Scene {
    // --- v2 speech playback config -------------------------------------------
    struct Speech {
        enum class Player { NoteStepped, PhonemeStepped };
        enum class AttackInterrupt { Finish, Interrupt };

        Player          player          = Player::NoteStepped;
        double          maxSustainMs    = 1500.0;
        AttackInterrupt attackInterrupt = AttackInterrupt::Finish;
    };

    int          id        = 0;
    std::string  name      = "(unnamed)";
    std::uint32_t colorRgb = 0xCCCCCCu;   // 0xRRGGBB
    MixerParams    mixer{};
    TtsConfig      tts{};
    CarouselConfig carousel{};
    Speech         speech{};

    // Per-scene editor panel visibility. Designed so the live performance
    // UI only shows what each scene actually uses. Defaults preserve old
    // behavior for any scene that doesn't declare these explicitly: vocoder
    // hidden, say box + word readout visible. Each scene JSON overrides
    // what it needs.
    bool showChat        = false;   // Scene 4 only: big conversation panel
    bool showVocoder     = false;   // Scenes 7, 8 (talkbox, auto-vocal)
    bool showSay         = true;    // Most TTS scenes; off for clipBank/mic
    bool showWordReadout = true;    // Off for scenes with no per-word stepping

    static Scene defaults(int id);
};

inline Scene Scene::defaults(int id) {
    Scene s;
    s.id = id;
    s.name = "Scene " + std::to_string(id);
    return s;
}

} // namespace guitar_dsp::scenes
