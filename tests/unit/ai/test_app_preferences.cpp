#include <catch2/catch_test_macros.hpp>
#include "ai/AppPreferences.h"
#include <juce_core/juce_core.h>
#include <cstdlib>

using guitar_dsp::ai::AppPreferences;

namespace {
juce::File tempPrefsPath(const char* name) {
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
             .getChildFile(name);
}
}

TEST_CASE("AppPreferences: round-trip API key via file", "[ai][prefs]") {
    auto tmp = tempPrefsPath("prefs_roundtrip.xml");
    tmp.deleteFile();
    {
        AppPreferences p{tmp};
        p.setAnthropicApiKey("sk-ant-test-xyz");
        REQUIRE(p.anthropicApiKey() == "sk-ant-test-xyz");
    }
    AppPreferences p2{tmp};
    REQUIRE(p2.anthropicApiKey() == "sk-ant-test-xyz");
    tmp.deleteFile();
}

TEST_CASE("AppPreferences: env-var fallback when no stored value",
          "[ai][prefs]") {
    auto tmp = tempPrefsPath("prefs_env.xml");
    tmp.deleteFile();
    ::setenv("ANTHROPIC_API_KEY", "sk-ant-from-env", 1);
    {
        AppPreferences p{tmp};
        REQUIRE(p.anthropicApiKey() == "sk-ant-from-env");
    }
    ::unsetenv("ANTHROPIC_API_KEY");
}

TEST_CASE("AppPreferences: stored key wins over env-var",
          "[ai][prefs]") {
    auto tmp = tempPrefsPath("prefs_both.xml");
    tmp.deleteFile();
    {
        AppPreferences p{tmp};
        p.setAnthropicApiKey("sk-ant-from-file");
    }
    ::setenv("ANTHROPIC_API_KEY", "sk-ant-from-env", 1);
    {
        AppPreferences p{tmp};
        REQUIRE(p.anthropicApiKey() == "sk-ant-from-file");
    }
    ::unsetenv("ANTHROPIC_API_KEY");
    tmp.deleteFile();
}

TEST_CASE("AppPreferences: ollamaEndpoint default + custom",
          "[ai][prefs]") {
    auto tmp = tempPrefsPath("prefs_ollama.xml");
    tmp.deleteFile();
    {
        AppPreferences p{tmp};
        REQUIRE(p.ollamaEndpoint() == "http://localhost:11434");
        p.setOllamaEndpoint("http://192.168.1.10:11434");
        REQUIRE(p.ollamaEndpoint() == "http://192.168.1.10:11434");
    }
    AppPreferences p2{tmp};
    REQUIRE(p2.ollamaEndpoint() == "http://192.168.1.10:11434");
    tmp.deleteFile();
}

TEST_CASE("AppPreferences: cannedFallbackOnLlmError default false + toggle",
          "[ai][prefs]") {
    auto tmp = tempPrefsPath("prefs_fallback.xml");
    tmp.deleteFile();
    {
        AppPreferences p{tmp};
        REQUIRE(p.cannedFallbackOnLlmError() == false);
        p.setCannedFallbackOnLlmError(true);
        REQUIRE(p.cannedFallbackOnLlmError() == true);
    }
    AppPreferences p2{tmp};
    REQUIRE(p2.cannedFallbackOnLlmError() == true);
    tmp.deleteFile();
}

TEST_CASE("AppPreferences: empty key + no env-var returns empty",
          "[ai][prefs]") {
    auto tmp = tempPrefsPath("prefs_empty.xml");
    tmp.deleteFile();
    ::unsetenv("ANTHROPIC_API_KEY");
    AppPreferences p{tmp};
    REQUIRE(p.anthropicApiKey().empty());
    tmp.deleteFile();
}

TEST_CASE("AppPreferences: defaultPath returns expected location",
          "[ai][prefs]") {
    auto path = AppPreferences::defaultPath();
    REQUIRE(path.getFullPathName().contains("Guitar Speak"));
    REQUIRE(path.getFullPathName().endsWith("settings.xml"));
}
