#include <catch2/catch_test_macros.hpp>
#include "audio/AudioGraph.h"
#include "harness/RealtimeSentinel.h"
#include "harness/SyntheticGuitar.h"
#include "scenes/SceneLibrary.h"
#include "scenes/SceneEngine.h"

#include <vector>

using guitar_dsp::audio::AudioGraph;
using guitar_dsp::tests::RealtimeSentinel;
using guitar_dsp::tests::SyntheticGuitar;
using guitar_dsp::scenes::SceneLibrary;
using guitar_dsp::scenes::SceneEngine;

TEST_CASE("integration: 60 s of audio thread activity is allocation-free", "[integration][realtime][slow]") {
    constexpr double sr = 48000.0;
    constexpr std::size_t block = 512;
    constexpr int blocks = static_cast<int>(60.0 * sr / block);  // ~5625 blocks

    AudioGraph graph;
    graph.prepare(sr, static_cast<int>(block));

    SyntheticGuitar gen{sr};
    std::vector<float> in(block), out(block);

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int i = 0; i < blocks; ++i) {
        // Vary input pattern so we hit different code paths.
        if (i % 100 == 0) gen.silence(in.data(), in.size());
        else if (i % 50 == 0) gen.sweep(80.0f, 8000.0f, 0.4f, in.data(), in.size());
        else gen.sine(440.0f, 0.4f, in.data(), in.size());

        graph.process(in.data(), out.data(), in.size());
    }
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}

TEST_CASE("RT-safety: scene 11 (clip bank modulator) is allocation-free on audio thread",
          "[integration][realtime-safety][scene-11]") {
    constexpr double sr = 48000.0;
    constexpr std::size_t block = 512;
    constexpr int blocks = 200;  // 200 blocks * 512 / 48000 = ~2.1 seconds

    auto scenes = SceneLibrary::loadDirectory("/Users/user/GIT/guitar-dsp/assets/scenes");
    SceneEngine engine;
    engine.loadScenes(std::move(scenes));
    REQUIRE(engine.activateScene(11));

    const auto& scene11 = engine.getActiveScene();
    AudioGraph graph;
    graph.prepare(sr, static_cast<int>(block));

    // Apply scene 11 configuration to the graph.
    // Scene 11 uses clip-bank as the modulator source.
    graph.setModulatorSource(AudioGraph::ModulatorSource::ClipBank);
    graph.mixer().setDryWet(scene11.mixer.dryWet);
    graph.mixer().setMasterGainDb(scene11.mixer.masterGainDb);
    graph.mixer().reset();

    SyntheticGuitar gen{sr};
    std::vector<float> in(block), out(block);

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int i = 0; i < blocks; ++i) {
        // Vary input pattern to exercise onset detection and clip-bank playback.
        if (i % 100 == 0) gen.silence(in.data(), in.size());
        else if (i % 50 == 0) gen.sweep(80.0f, 8000.0f, 0.4f, in.data(), in.size());
        else gen.sine(440.0f, 0.4f, in.data(), in.size());

        graph.process(in.data(), out.data(), in.size());
    }
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
