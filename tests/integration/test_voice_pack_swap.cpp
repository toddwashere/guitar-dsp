#include <catch2/catch_test_macros.hpp>
#include "scenes/Scene.h"
#include "scenes/SceneEngine.h"
#include "scenes/SceneLibrary.h"
#include "app/AssetLocator.h"

#include <juce_core/juce_core.h>

using guitar_dsp::AssetLocator;
using guitar_dsp::scenes::Scene;
using guitar_dsp::scenes::SceneEngine;
using guitar_dsp::scenes::SceneLibrary;

TEST_CASE("Scene 11 voicePacks resolve to existing bundle paths",
          "[integration][voice-pack-swap]") {
    const auto scenesDir = AssetLocator::scenesDirectory();
    REQUIRE_FALSE(scenesDir.empty());

    auto scenes = SceneLibrary::loadDirectory(scenesDir);
    const Scene* s11 = nullptr;
    for (auto& s : scenes) if (s.id == 11) s11 = &s;
    REQUIRE(s11 != nullptr);
    REQUIRE(s11->voicePacks.size() == 4);

    for (std::size_t i = 0; i < s11->voicePacks.size(); ++i) {
        const auto p = s11->resolvedGspeakPath(static_cast<int>(i));
        INFO("voice " << i << " label=" << s11->voicePacks[i].label << " path=" << p);
        REQUIRE_FALSE(p.empty());
        const auto abs = AssetLocator::resolveForRead(p);
        INFO("resolved absolute path=" << abs);
        REQUIRE_FALSE(abs.empty());
        juce::File f(abs);
        REQUIRE(f.existsAsFile());
    }
}
