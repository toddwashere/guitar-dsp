#include "PluginState.h"

namespace guitar_dsp::app {

// Helpers for PersonaId <-> int conversion with range checking.
// Valid range: Interviewer (0) .. SongRockingGuitar (7).
static constexpr int kPersonaMin = static_cast<int>(ai::PersonaId::Interviewer);
static constexpr int kPersonaMax = static_cast<int>(ai::PersonaId::SongRockingGuitar);

static ai::PersonaId personaFromInt(int v) {
    if (v < kPersonaMin || v > kPersonaMax)
        return ai::PersonaId::Interviewer;
    return static_cast<ai::PersonaId>(v);
}

juce::String PluginState::toJson(const PluginStateData& d) {
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    // Existing fields
    o->setProperty("sceneId",      d.sceneId);
    o->setProperty("makeup",       d.makeup);
    o->setProperty("carrierNoise", d.carrierNoise);
    o->setProperty("sibilance",    d.sibilance);
    o->setProperty("gateThresholdDb", d.gateThresholdDb);
    o->setProperty("micCaptureGain",  d.micCaptureGain);
    o->setProperty("pitchSinging", d.pitchSinging);
    o->setProperty("singing", d.singing);
    o->setProperty("wordSyncMode", d.wordSyncMode);
    // AI fields
    o->setProperty("selectedModelId", juce::String(d.selectedModelId));
    o->setProperty("personaId",       static_cast<int>(d.personaId));
    o->setProperty("maxSentences",    d.maxSentences);
    o->setProperty("maxWords",        d.maxWords);
    o->setProperty("sttModelId",      juce::String(d.sttModelId));
    o->setProperty("pttPedalId",      d.pttPedalId);
    o->setProperty("clearChatPedalId", d.clearChatPedalId);
    o->setProperty("sungVowelMask",      static_cast<int>(d.sungVowelMask));
    o->setProperty("limiterEnabled",     d.limiterEnabled);
    o->setProperty("limiterThresholdDb", d.limiterThresholdDb);
    // activeVoiceIndexByScene — omit if empty
    if (!d.activeVoiceIndexByScene.empty()) {
        auto* voicesObj = new juce::DynamicObject();
        for (const auto& kv : d.activeVoiceIndexByScene) {
            voicesObj->setProperty(juce::String(kv.first), kv.second);
        }
        o->setProperty("activeVoiceIndexByScene", juce::var(voicesObj));
    }
    // customPromptByPersona — omit entirely if empty
    if (!d.customPromptByPersona.empty()) {
        juce::DynamicObject::Ptr prompts = new juce::DynamicObject();
        for (const auto& [pid, prompt] : d.customPromptByPersona) {
            const auto key = juce::String(static_cast<int>(pid));
            prompts->setProperty(key, juce::String(prompt));
        }
        o->setProperty("customPromptByPersona", juce::var(prompts.get()));
    }
    return juce::JSON::toString(juce::var(o.get()), true);
}

PluginStateData PluginState::fromJson(const juce::String& json) {
    PluginStateData d;  // defaults
    const juce::var v = juce::JSON::parse(json);
    if (auto* o = v.getDynamicObject()) {
        // Existing fields
        if (o->hasProperty("sceneId"))      d.sceneId      = (int)   o->getProperty("sceneId");
        if (o->hasProperty("makeup"))       d.makeup       = (float) (double) o->getProperty("makeup");
        if (o->hasProperty("carrierNoise")) d.carrierNoise = (float) (double) o->getProperty("carrierNoise");
        if (o->hasProperty("sibilance"))    d.sibilance    = (float) (double) o->getProperty("sibilance");
        if (o->hasProperty("gateThresholdDb"))
            d.gateThresholdDb = (float) (double) o->getProperty("gateThresholdDb");
        if (o->hasProperty("micCaptureGain"))
            d.micCaptureGain = (float) (double) o->getProperty("micCaptureGain");
        if (o->hasProperty("pitchSinging"))
            d.pitchSinging = (bool) o->getProperty("pitchSinging");
        if (o->hasProperty("singing"))
            d.singing = (bool) o->getProperty("singing");
        if (o->hasProperty("wordSyncMode"))
            d.wordSyncMode = (int) o->getProperty("wordSyncMode");
        // AI fields
        if (o->hasProperty("selectedModelId"))
            d.selectedModelId = o->getProperty("selectedModelId").toString().toStdString();
        if (o->hasProperty("personaId"))
            d.personaId = personaFromInt((int) o->getProperty("personaId"));
        if (o->hasProperty("maxSentences"))
            d.maxSentences = (int) o->getProperty("maxSentences");
        if (o->hasProperty("maxWords"))
            d.maxWords = (int) o->getProperty("maxWords");
        if (o->hasProperty("sttModelId"))
            d.sttModelId = o->getProperty("sttModelId").toString().toStdString();
        if (o->hasProperty("pttPedalId"))
            d.pttPedalId = (int) o->getProperty("pttPedalId");
        if (o->hasProperty("clearChatPedalId"))
            d.clearChatPedalId = (int) o->getProperty("clearChatPedalId");
        if (o->hasProperty("sungVowelMask"))
            d.sungVowelMask =
                static_cast<std::uint32_t>((int) o->getProperty("sungVowelMask"));
        if (o->hasProperty("limiterEnabled"))
            d.limiterEnabled = (bool) o->getProperty("limiterEnabled");
        if (o->hasProperty("limiterThresholdDb"))
            d.limiterThresholdDb =
                static_cast<float>((double) o->getProperty("limiterThresholdDb"));
        // activeVoiceIndexByScene sub-object
        if (o->hasProperty("activeVoiceIndexByScene")) {
            if (auto* vo = o->getProperty("activeVoiceIndexByScene").getDynamicObject()) {
                for (const auto& kv : vo->getProperties()) {
                    const int sceneId = kv.name.toString().getIntValue();
                    const int idx     = static_cast<int>(kv.value);
                    d.activeVoiceIndexByScene[sceneId] = idx;
                }
            }
        }
        // customPromptByPersona sub-object — explicit per-known-id lookup
        // to avoid getIntValue() silently returning 0 for non-numeric keys.
        if (o->hasProperty("customPromptByPersona")) {
            const juce::var sub = o->getProperty("customPromptByPersona");
            if (auto* subObj = sub.getDynamicObject()) {
                for (int i = kPersonaMin; i <= kPersonaMax; ++i) {
                    const juce::Identifier key { juce::String(i) };
                    if (subObj->hasProperty(key)) {
                        d.customPromptByPersona[static_cast<ai::PersonaId>(i)] =
                            subObj->getProperty(key).toString().toStdString();
                    }
                }
            }
        }
    }
    return d;
}

} // namespace guitar_dsp::app
