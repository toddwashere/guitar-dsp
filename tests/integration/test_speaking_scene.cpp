#include <catch2/catch_test_macros.hpp>

#include "audio/AudioGraph.h"
#include "audio/PrebakedTTSSource.h"
#include "harness/GoldenFile.h"
#include "harness/SyntheticGuitar.h"
#include "scenes/SceneEngine.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

using guitar_dsp::audio::AudioGraph;
using guitar_dsp::audio::PrebakedTTSSource;
using guitar_dsp::scenes::Scene;
using guitar_dsp::scenes::SceneEngine;
using guitar_dsp::tests::SyntheticGuitar;

namespace {

std::string fixturePath(const std::string& rel) {
    auto p = std::filesystem::current_path();
    for (int i = 0; i < 6; ++i) {
        auto c = p / "tests" / "fixtures" / rel;
        if (std::filesystem::exists(c.parent_path())) return c.string();
        p = p.parent_path();
    }
    throw std::runtime_error("fixtures not found");
}

void ensureFixtureWav() {
    const auto path = fixturePath("tts/tiny_clip/audio.wav");
    if (std::filesystem::exists(path)) return;
    constexpr double sr = 48000.0;
    constexpr int N = 4800;
    std::vector<float> samples(N);
    for (int i = 0; i < N; ++i) {
        samples[i] = 0.5f * std::sin(2.0 * 3.14159265 * 880.0 * i / sr);
    }
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    guitar_dsp::tests::GoldenFile::writeMonoWav(path, sr, samples.data(), N);
}

} // namespace

TEST_CASE("integration: activate speaking scene -> vocoder produces output then silence",
          "[integration][speaking]") {
    ensureFixtureWav();

    // Wire it up.
    SceneEngine engine;
    Scene clean = Scene::defaults(0);
    Scene speak = Scene::defaults(1);
    speak.tts.source = "prebaked";
    speak.tts.clip = "tiny_clip";
    speak.mixer.dryWet = 1.0f;  // fully wet so the vocoder dominates
    engine.loadScenes({clean, speak});

    AudioGraph graph;
    graph.prepare(48000.0, 512);

    PrebakedTTSSource src{fixturePath("tts")};
    src.prepare(48000.0);

    // Activate the speaking scene and load the clip.
    engine.activateScene(1);
    auto clip = src.synthesize(engine.activeTtsKey());
    REQUIRE(clip);
    graph.ttsClipPlayer().setClip(clip);

    // Drive the mixer with the scene's params, then run audio.
    graph.mixer().setDryWet(speak.mixer.dryWet);
    graph.mixer().setMasterGainDb(speak.mixer.masterGainDb);
    graph.mixer().reset();

    SyntheticGuitar gen{48000.0};
    constexpr int N = 48000;
    std::vector<float> in(N), out(N);
    gen.sine(880.0f, 0.5f, in.data(), N);

    for (int i = 0; i < N; i += 512) {
        const int n = std::min(512, N - i);
        graph.process(in.data() + i, out.data() + i, static_cast<std::size_t>(n));
    }

    auto rms = [](const float* p, int n) {
        double s = 0.0;
        for (int i = 0; i < n; ++i) s += p[i] * p[i];
        return std::sqrt(s / n);
    };
    const double rmsDuringClip = rms(out.data() + 1000, 3000);
    const double rmsAfterClip  = rms(out.data() + 40000, 4000);
    INFO("during=" << rmsDuringClip << " after=" << rmsAfterClip);
    REQUIRE(rmsDuringClip > 0.01);
    REQUIRE(rmsAfterClip  < rmsDuringClip * 0.3);
}
