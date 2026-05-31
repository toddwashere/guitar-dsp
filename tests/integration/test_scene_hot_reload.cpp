#include <catch2/catch_test_macros.hpp>
#include "harness/RealtimeSentinel.h"
#include "scenes/SceneEngine.h"
#include "scenes/SceneLibrary.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using guitar_dsp::scenes::Scene;
using guitar_dsp::scenes::SceneEngine;
using guitar_dsp::scenes::SceneLibrary;
using guitar_dsp::tests::RealtimeSentinel;

namespace {

std::string sceneJson(int id, float gainDb, float dryWet) {
    return std::string("{ \"id\": ") + std::to_string(id)
         + ", \"name\": \"hot" + std::to_string(id) + "\""
         + ", \"color\": \"#101010\""
         + ", \"mixer\": { \"masterGainDb\": " + std::to_string(gainDb)
         + ", \"dryWet\": " + std::to_string(dryWet)
         + ", \"transitionMs\": 20 } }";
}

void writeSceneSet(const std::filesystem::path& dir, int generation) {
    namespace fs = std::filesystem;
    // Overwrite the three scene files with new gain/wet values so each
    // reload pass actually changes the published snapshot.
    const float g = static_cast<float>(generation);
    std::ofstream(dir / "a.json")
        << sceneJson(0, -1.0f * g, 0.10f * (g + 1));
    std::ofstream(dir / "b.json")
        << sceneJson(1, -2.0f * g, 0.20f * (g + 1));
    std::ofstream(dir / "c.json")
        << sceneJson(2, -3.0f * g, 0.30f * (g + 1));
}

} // namespace

TEST_CASE("integration: scene hot-reload doesn't disturb the audio thread",
          "[integration][realtime][scenes][slow]") {
    namespace fs = std::filesystem;
    const auto tmp = fs::temp_directory_path() / "guitar_dsp_hot_reload_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    // Seed with an initial scene set so the engine starts in a known state.
    writeSceneSet(tmp, 0);
    SceneEngine engine;
    engine.loadScenes(SceneLibrary::loadDirectory(tmp.string()));
    REQUIRE(engine.getSceneCount() == 3);
    REQUIRE(engine.getActiveSceneId() == 0);

    std::atomic<bool> stop{false};

    // Writer thread: rewrites JSON files and calls reloadFrom on a cadence
    // faster than the production 2 s poll, to stress the cross-thread path.
    std::thread writer([&]() {
        int generation = 1;
        while (! stop.load(std::memory_order_relaxed)) {
            writeSceneSet(tmp, generation++);
            engine.reloadFrom(tmp.string());
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    });

    // Audio-thread mimic: tight loop of currentMixerParams() for ~1 s,
    // accumulating into a volatile sink so the reads can't be optimized away.
    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();

    volatile float sink = 0.0f;
    const auto deadline = std::chrono::steady_clock::now()
                        + std::chrono::milliseconds(1000);
    std::size_t iterations = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        for (int i = 0; i < 1024; ++i) {
            const auto p = engine.currentMixerParams();
            sink = sink + p.masterGainDb + p.dryWet + p.transitionMs;
            ++iterations;
        }
    }

    sentinel.unmarkCurrentThreadAsRealtime();

    stop.store(true, std::memory_order_relaxed);
    writer.join();

    REQUIRE(iterations > 0);
    REQUIRE(sentinel.violations() == 0);

    fs::remove_all(tmp);
}
