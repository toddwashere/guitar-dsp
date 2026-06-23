#include <catch2/catch_test_macros.hpp>
#include "audio/AudioGraph.h"
#include "audio/GspeakBundle.h"
#include "audio/TTSClip.h"
#include "app/AssetLocator.h"
#include "harness/RealtimeSentinel.h"
#include "harness/SyntheticGuitar.h"
#include "scenes/SceneLibrary.h"
#include "scenes/SceneEngine.h"

#include <filesystem>
#include <vector>

using guitar_dsp::audio::AudioGraph;
using guitar_dsp::audio::TTSClipPtr;
using guitar_dsp::audio::TTSClip;
using guitar_dsp::audio::GspeakBundle;
using guitar_dsp::tests::RealtimeSentinel;
using guitar_dsp::tests::SyntheticGuitar;
using guitar_dsp::scenes::SceneLibrary;
using guitar_dsp::scenes::SceneEngine;

namespace {

// Walk up from CWD until we find tests/fixtures/, then return an absolute path
// under it. Same pattern used by test_vocal_guitar_clip_bank_scene.cpp.
std::string fixturePath(const std::string& rel) {
    auto p = std::filesystem::current_path();
    for (int i = 0; i < 6; ++i) {
        auto d = p / "tests" / "fixtures";
        if (std::filesystem::exists(d)) return (d / rel).string();
        p = p.parent_path();
    }
    return {};  // caller will REQUIRE the path is non-empty
}

// Build a per-grain bank from a GspeakBundle master clip's phoneme list.
// Mirrors PluginProcessor::splitMasterClipIntoBank_ logic.
std::vector<TTSClipPtr>
buildBank(const guitar_dsp::audio::GspeakBundle::Loaded& loaded) {
    std::vector<TTSClipPtr> bank;
    if (!loaded.clip) return bank;
    const auto& master = *loaded.clip;
    const std::size_t totalSamples = master.samples.size();
    for (const auto& p : master.phonemes) {
        const std::size_t start = std::min(p.startSample, totalSamples);
        const std::size_t end   = std::min(p.endSample,   totalSamples);
        if (end <= start) continue;

        auto sub = std::make_shared<TTSClip>();
        sub->sampleRate = master.sampleRate;
        sub->samples.assign(
            master.samples.begin() + static_cast<std::ptrdiff_t>(start),
            master.samples.begin() + static_cast<std::ptrdiff_t>(end));

        if (!p.bankKey.empty()) {
            sub->bankKey       = p.bankKey;
            sub->anchorPitchHz = p.anchorPitchHz;
        } else {
            if      (p.label == "a") sub->bankKey = "sung_ah";
            else if (p.label == "e") sub->bankKey = "sung_eh";
            else if (p.label == "i") sub->bankKey = "sung_ee";
            else if (p.label == "o") sub->bankKey = "sung_oh";
            else if (p.label == "u") sub->bankKey = "sung_oo";
            else                      sub->bankKey = "sung_ah";
        }
        bank.push_back(sub);
    }
    if (bank.empty()) {
        // Fallback: treat whole clip as one grain.
        auto sub = std::make_shared<TTSClip>(master);
        sub->bankKey = "sung_ah";
        bank.push_back(sub);
    }
    return bank;
}

} // namespace

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
    constexpr int blocks = 200;

    // I5: use AssetLocator::scenesDirectory() instead of an absolute path.
    auto scenes = SceneLibrary::loadDirectory(guitar_dsp::AssetLocator::scenesDirectory());
    SceneEngine engine;
    engine.loadScenes(std::move(scenes));
    REQUIRE(engine.activateScene(11));

    AudioGraph graph;
    graph.prepare(sr, static_cast<int>(block));

    // I4: load the synthetic fixture bundle and feed it to clipBankPlayer.
    const std::string fixPath = fixturePath("sung_vowels_test.gspeak");
    REQUIRE_FALSE(fixPath.empty());
    juce::File fixFile(fixPath);
    REQUIRE(fixFile.existsAsFile());
    auto loaded = GspeakBundle::read(fixFile, sr);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->clip);

    auto bank = buildBank(*loaded);
    REQUIRE_FALSE(bank.empty());

    // Wire scene 11: clip-bank → vocoder modulator.
    graph.clipBankPlayer().setBank(bank);
    graph.setModulatorSource(AudioGraph::ModulatorSource::ClipBank);
    graph.setWetSource(AudioGraph::WetSource::Vocoder);

    // Drain the bank-swap flag.
    std::vector<float> in(block), out(block);
    graph.process(in.data(), out.data(), block);

    SyntheticGuitar gen{sr};

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int i = 0; i < blocks; ++i) {
        if (i % 100 == 0) gen.silence(in.data(), in.size());
        else if (i % 50 == 0) gen.sweep(80.0f, 8000.0f, 0.4f, in.data(), in.size());
        else gen.sine(440.0f, 0.4f, in.data(), in.size());

        graph.process(in.data(), out.data(), in.size());
    }
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}

TEST_CASE("RT-safety: scene 12 (sung vowel path) is allocation-free on audio thread",
          "[integration][realtime-safety][scene-12]") {
    constexpr double sr = 48000.0;
    constexpr std::size_t block = 512;
    constexpr int blocks = 200;

    // I5: use AssetLocator::scenesDirectory() instead of an absolute path.
    auto scenes = SceneLibrary::loadDirectory(guitar_dsp::AssetLocator::scenesDirectory());
    SceneEngine engine;
    engine.loadScenes(std::move(scenes));
    REQUIRE(engine.activateScene(12));

    AudioGraph graph;
    graph.prepare(sr, static_cast<int>(block));

    // I4: load the synthetic fixture bundle and feed it to SungDirectPath.
    // The pre-rendering of all ratio variants happens inside setGrainsForBank()
    // on this (message) thread — so the audio thread never calls Synthesis().
    const std::string fixPath = fixturePath("sung_vowels_test.gspeak");
    REQUIRE_FALSE(fixPath.empty());
    juce::File fixFile(fixPath);
    REQUIRE(fixFile.existsAsFile());
    auto loaded = GspeakBundle::read(fixFile, sr);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->clip);

    auto bank = buildBank(*loaded);
    REQUIRE_FALSE(bank.empty());

    // Wire scene 12: SungDirectPath → wet bus (no vocoder).
    graph.sungDirectPath().setGrainsForBank(bank);
    graph.setWetSource(AudioGraph::WetSource::SungDirect);

    // Drain the routing-flag latency.
    std::vector<float> in(block), out(block);
    graph.process(in.data(), out.data(), block);

    SyntheticGuitar gen{sr};

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int i = 0; i < blocks; ++i) {
        if (i % 100 == 0) gen.silence(in.data(), in.size());
        else if (i % 50 == 0) gen.sweep(80.0f, 8000.0f, 0.4f, in.data(), in.size());
        else gen.sine(440.0f, 0.4f, in.data(), in.size());

        graph.process(in.data(), out.data(), in.size());
    }
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
