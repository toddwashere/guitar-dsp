#include "ai/AppPreferences.h"
#include <cstdlib>

namespace guitar_dsp::ai {

AppPreferences::AppPreferences(juce::File path) : path_(std::move(path)) {
    load();
}

void AppPreferences::load() {
    if (! path_.existsAsFile()) return;
    if (auto parsed = juce::parseXML(path_)) xml_ = *parsed;
}

void AppPreferences::save() const {
    path_.getParentDirectory().createDirectory();
    xml_.writeTo(path_);
}

std::string AppPreferences::anthropicApiKey() const {
    auto stored = xml_.getStringAttribute("anthropic_api_key", "").toStdString();
    if (! stored.empty()) return stored;
    if (const char* env = std::getenv("ANTHROPIC_API_KEY")) return env;
    return "";
}

void AppPreferences::setAnthropicApiKey(std::string k) {
    xml_.setAttribute("anthropic_api_key", juce::String(k));
    save();
}

std::string AppPreferences::ollamaEndpoint() const {
    return xml_.getStringAttribute("ollama_endpoint",
                                   "http://localhost:11434").toStdString();
}

void AppPreferences::setOllamaEndpoint(std::string e) {
    xml_.setAttribute("ollama_endpoint", juce::String(e));
    save();
}

bool AppPreferences::cannedFallbackOnLlmError() const {
    return xml_.getBoolAttribute("canned_fallback_on_llm_error", false);
}

void AppPreferences::setCannedFallbackOnLlmError(bool b) {
    xml_.setAttribute("canned_fallback_on_llm_error", b);
    save();
}

juce::File AppPreferences::defaultPath() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
             .getChildFile("Todd B Fisher/Guitar Speak/settings.xml");
}

} // namespace guitar_dsp::ai
