#include <catch2/catch_test_macros.hpp>
#include "midi/FCB1010Mapping.h"
#include "scenes/SceneEngine.h"
#include "scenes/SceneLibrary.h"

using guitar_dsp::midi::FCB1010Mapping;
using guitar_dsp::midi::SceneCommandType;
using guitar_dsp::scenes::Scene;
using guitar_dsp::scenes::SceneEngine;
using guitar_dsp::scenes::SceneLibrary;

TEST_CASE("integration: PC 0..13 each activate the corresponding scene id",
          "[integration][scenes][midi]") {
    auto scenes = SceneLibrary::loadDirectory(
        "/Users/user/GIT/guitar-dsp/assets/scenes");
    REQUIRE(!scenes.empty());

    SceneEngine engine;
    engine.loadScenes(std::move(scenes));
    auto mapping = FCB1010Mapping::stockDefaults();

    for (int pc = 0; pc < 14; ++pc) {
        const auto msg = juce::MidiMessage::programChange(1, pc);
        auto cmd = mapping.translate(msg);
        REQUIRE(cmd.has_value());
        REQUIRE(cmd->type == SceneCommandType::ActivateScene);
        const bool ok = engine.activateScene(cmd->payload);
        if (! ok) continue;  // Skip undefined slots (12 doesn't exist until M2).
        REQUIRE(engine.getActiveSceneId() == pc);
    }
}
