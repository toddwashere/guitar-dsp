#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "audio/PrebakedTTSSource.h"
#include "harness/GoldenFile.h"

#include <cmath>
#include <filesystem>
#include <vector>

using guitar_dsp::audio::PrebakedTTSSource;
using guitar_dsp::audio::TTSClipPtr;

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
    constexpr int N = 4800;  // 0.1 s
    std::vector<float> samples(N);
    for (int i = 0; i < N; ++i) {
        samples[i] = 0.5f * std::sin(2.0 * 3.14159265358979 * 880.0 * i / sr);
    }
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    guitar_dsp::tests::GoldenFile::writeMonoWav(path, sr, samples.data(), N);
}

} // namespace

TEST_CASE("PrebakedTTSSource: loads a known clip into TTSClipPtr", "[audio][tts][prebaked]") {
    ensureFixtureWav();

    const auto root = fixturePath("tts");
    PrebakedTTSSource src{root};
    src.prepare(48000.0);

    auto clip = src.synthesize("tiny_clip");
    REQUIRE(clip);
    REQUIRE(clip->sampleRate == 48000.0);
    REQUIRE(clip->samples.size() == 4800);
    REQUIRE(clip->name == "tiny_clip");
}

TEST_CASE("PrebakedTTSSource: missing clip returns nullptr", "[audio][tts][prebaked]") {
    const auto root = fixturePath("tts");
    PrebakedTTSSource src{root};
    src.prepare(48000.0);

    auto clip = src.synthesize("nonexistent");
    REQUIRE_FALSE(clip);
}

TEST_CASE("PrebakedTTSSource: sourceName is descriptive", "[audio][tts][prebaked]") {
    PrebakedTTSSource src{"/dev/null"};
    REQUIRE(src.sourceName() == "prebaked");
}
