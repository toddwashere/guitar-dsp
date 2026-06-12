#include "PluginState.h"

namespace guitar_dsp::app {

juce::String PluginState::toJson(const PluginStateData& d) {
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("sceneId", d.sceneId);
    o->setProperty("makeup", d.makeup);
    o->setProperty("carrierNoise", d.carrierNoise);
    o->setProperty("sibilance", d.sibilance);
    return juce::JSON::toString(juce::var(o.get()), true);
}

PluginStateData PluginState::fromJson(const juce::String& json) {
    PluginStateData d;  // defaults
    const juce::var v = juce::JSON::parse(json);
    if (auto* o = v.getDynamicObject()) {
        if (o->hasProperty("sceneId"))      d.sceneId      = (int)   o->getProperty("sceneId");
        if (o->hasProperty("makeup"))       d.makeup       = (float) (double) o->getProperty("makeup");
        if (o->hasProperty("carrierNoise")) d.carrierNoise = (float) (double) o->getProperty("carrierNoise");
        if (o->hasProperty("sibilance"))    d.sibilance    = (float) (double) o->getProperty("sibilance");
    }
    return d;
}

} // namespace guitar_dsp::app
