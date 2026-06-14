#include <catch2/catch_test_macros.hpp>

#include "audio/AudioGraph.h"
#include "audio/PrebakedTTSSource.h"
#include "audio/TTSClip.h"
#include "harness/GoldenFile.h"

#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

using guitar_dsp::audio::AudioGraph;
using guitar_dsp::audio::PrebakedTTSSource;
using guitar_dsp::audio::TTSClipPtr;

namespace {

std::string fixturePath(const std::string& rel) {
    auto p = std::filesystem::current_path();
    for (int i = 0; i < 6; ++i) {
        auto fixturesDir = p / "tests" / "fixtures";
        if (std::filesystem::exists(fixturesDir)) return (fixturesDir / rel).string();
        p = p.parent_path();
    }
    throw std::runtime_error("fixtures not found");
}

void ensureClipFixture(const std::string& key, float value) {
    const auto path = fixturePath("clips/vocal-guitar/" + key + "/audio.wav");
    if (std::filesystem::exists(path)) return;
    constexpr int N = 4800;  // 100 ms
    std::vector<float> samples(N, value);
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path());
    guitar_dsp::tests::GoldenFile::writeMonoWav(path, 48000.0,
                                                 samples.data(), N);
}

} // namespace

TEST_CASE("Clip-bank scene: ClipBankPlayer.setBank wires through to AudioGraph",
          "[integration][clip_bank][scene]") {
    ensureClipFixture("00_test", 0.1f);
    ensureClipFixture("01_test", 0.2f);

    // Load the bank via PrebakedTTSSource — same path PluginProcessor takes.
    PrebakedTTSSource src{ fixturePath("clips/vocal-guitar") };
    src.prepare(48000.0);

    auto c0 = src.synthesize("00_test");
    auto c1 = src.synthesize("01_test");
    REQUIRE(c0);
    REQUIRE(c1);

    AudioGraph g;
    g.prepare(48000.0, 512);
    g.clipBankPlayer().setBank({ c0, c1 });
    g.clipBankPlayer().rewind();
    g.setModulatorSource(AudioGraph::ModulatorSource::ClipBank);
    g.mixer().setDryWet(1.0f);

    // Two pluck-shaped onsets spaced 1100 ms apart.
    constexpr std::size_t N = 48000 * 2;
    std::vector<float> in(N, 0.0f), out(N, 0.0f);
    for (std::size_t i = 0; i < 64; ++i)
        in[i] = 0.9f * std::exp(-static_cast<int>(i) * 0.05f);
    for (std::size_t i = 0; i < 64; ++i)
        in[52800 + i] = 0.9f * std::exp(-static_cast<int>(i) * 0.05f);

    // Drain one block first so the pending setBank flag lands.
    g.process(in.data(), out.data(), 512);

    for (std::size_t i = 512; i < N; i += 512) {
        const std::size_t n = std::min<std::size_t>(512, N - i);
        g.process(in.data() + i, out.data() + i, n);
    }

    // After two onsets, cursor should be on clip 1.
    REQUIRE(g.clipBankPlayer().currentClipIndex() == 1);
}
