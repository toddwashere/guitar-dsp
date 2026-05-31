#include <catch2/catch_test_macros.hpp>
#include "midi/FCB1010Mapping.h"
#include "scenes/SceneEngine.h"

using guitar_dsp::midi::FCB1010Mapping;
using guitar_dsp::midi::SceneCommandType;
using guitar_dsp::scenes::Scene;
using guitar_dsp::scenes::SceneEngine;

TEST_CASE("integration: PC message switches scene and updates mixer params",
          "[integration][scenes][midi]") {
    SceneEngine engine;
    std::vector<Scene> scenes;
    for (int i = 0; i < 10; ++i) {
        Scene s = Scene::defaults(i);
        s.mixer.masterGainDb = -static_cast<float>(i);  // 0..-9 dB
        scenes.push_back(s);
    }
    engine.loadScenes(std::move(scenes));
    REQUIRE(engine.getActiveSceneId() == 0);

    auto mapping = FCB1010Mapping::stockDefaults();

    // PC 7 → scene 7 → masterGainDb -7.
    const auto msg = juce::MidiMessage::programChange(1, 7);
    auto cmd = mapping.translate(msg);
    REQUIRE(cmd.has_value());
    REQUIRE(cmd->type == SceneCommandType::ActivateScene);

    REQUIRE(engine.activateScene(cmd->payload));
    REQUIRE(engine.getActiveSceneId() == 7);
    REQUIRE(engine.currentMixerParams().masterGainDb == -7.0f);
}
