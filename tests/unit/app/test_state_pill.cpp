#include <catch2/catch_test_macros.hpp>
#include "app/StatePill.h"
#include <juce_gui_basics/juce_gui_basics.h>

using guitar_dsp::StatePill;
using guitar_dsp::ai::ConversationEngine;

TEST_CASE("StatePill: label reflects state", "[app][ui][pill]") {
    juce::ScopedJuceInitialiser_GUI juceInit;
    StatePill p;
    p.setState(ConversationEngine::State::Idle);
    REQUIRE(p.currentLabel() == "Idle");
    p.setState(ConversationEngine::State::Capturing);
    REQUIRE(p.currentLabel() == "Capturing");
    p.setState(ConversationEngine::State::Transcribing);
    REQUIRE(p.currentLabel() == "Transcribing");
    p.setState(ConversationEngine::State::Thinking);
    REQUIRE(p.currentLabel() == "Thinking");
    p.setState(ConversationEngine::State::Speaking);
    REQUIRE(p.currentLabel() == "Speaking");
    p.setState(ConversationEngine::State::Error);
    REQUIRE(p.currentLabel() == "Error");
}

TEST_CASE("StatePill: error label includes reason", "[app][ui][pill]") {
    juce::ScopedJuceInitialiser_GUI juceInit;
    StatePill p;
    p.setState(ConversationEngine::State::Error);
    p.setErrorReason("API key invalid");
    REQUIRE(p.currentLabel() == "Error: API key invalid");
}

TEST_CASE("StatePill: empty error reason falls back to 'Error'",
          "[app][ui][pill]") {
    juce::ScopedJuceInitialiser_GUI juceInit;
    StatePill p;
    p.setState(ConversationEngine::State::Error);
    REQUIRE(p.currentLabel() == "Error");
}
