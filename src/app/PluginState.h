#pragma once

#include "ai/PersonaRegistry.h"

#include <juce_core/juce_core.h>

#include <map>
#include <string>

namespace guitar_dsp::app {

struct PluginStateData {
    int   sceneId      = 0;
    float makeup       = 4.0f;
    float carrierNoise = 0.30f;
    float sibilance    = 0.3f;
    float gateThresholdDb = -60.0f;  // noise gate (lower = more permissive)
    float micCaptureGain  = 4.0f;    // linear; +12 dB default. Boosts the
                                      // mic input fed to MicCapture (whisper)
                                      // and the VocoderPanel meter. Tunable
                                      // via the Mic gain slider for quiet
                                      // mic interfaces (e.g. low-gain Scarlett
                                      // settings) where -30 dB peaks are too
                                      // faint for reliable transcription.
    bool pitchSinging = false;
    bool singing = false;
    int wordSyncMode = 0;  // 0=Latch, 1=Advance, 2=Syllable
    // NOTE: vocoder `clarity` is intentionally NOT persisted — it is a per-
    // SCENE control (every scene change overwrites it from cfg.clarity), so
    // saving it would just race with the scene-change handler on reload. To
    // change a scene's clarity persistently, edit the scene JSON.

    // AI feature fields
    std::string                                       selectedModelId       {"claude-haiku-4-5"};
    ai::PersonaId                                     personaId             {ai::PersonaId::Interviewer};
    std::map<ai::PersonaId, std::string>              customPromptByPersona {};
    int                                               maxSentences          {2};
    int                                               maxWords              {25};
    std::string                                       sttModelId            {"whisper-base.en"};
    int                                               pttPedalId            {9};
    int                                               clearChatPedalId      {10};
};

// Minimal, forward-compatible JSON (de)serialization of the persisted plugin
// state. Unknown keys are ignored; missing keys fall back to defaults.
struct PluginState {
    static juce::String     toJson(const PluginStateData& d);
    static PluginStateData  fromJson(const juce::String& json);
};

} // namespace guitar_dsp::app
