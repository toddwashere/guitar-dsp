#pragma once
#include <juce_core/juce_core.h>
#include <string>

namespace guitar_dsp::ai {

class AppPreferences {
public:
    explicit AppPreferences(juce::File path);

    // File value wins; falls back to $ANTHROPIC_API_KEY env-var; else empty.
    std::string anthropicApiKey() const;
    void        setAnthropicApiKey(std::string);

    // Default: http://localhost:11434
    std::string ollamaEndpoint() const;
    void        setOllamaEndpoint(std::string);

    // Default: false
    bool        cannedFallbackOnLlmError() const;
    void        setCannedFallbackOnLlmError(bool);

    static juce::File defaultPath();    // ~/Library/.../settings.xml

private:
    void load();
    void save() const;

    juce::File                path_;
    mutable juce::XmlElement  xml_ {"GuitarSpeakPrefs"};
};

} // namespace guitar_dsp::ai
