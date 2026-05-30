#include <catch2/catch_test_macros.hpp>
#include "audio/AudioGraph.h"
#include "harness/GoldenFile.h"

#include <filesystem>
#include <vector>

using guitar_dsp::audio::AudioGraph;
using guitar_dsp::tests::GoldenFile;

namespace {
std::string findFixturePath(const std::string& relative) {
    // Walk up from the current dir until we find the fixtures folder.
    auto p = std::filesystem::current_path();
    for (int i = 0; i < 6; ++i) {
        auto candidate = p / "tests" / "fixtures" / relative;
        if (std::filesystem::exists(candidate.parent_path())) return candidate.string();
        p = p.parent_path();
    }
    throw std::runtime_error("Could not locate tests/fixtures from " + std::filesystem::current_path().string());
}
}

TEST_CASE("integration: AudioGraph passthrough golden file", "[integration][golden]") {
    const auto inputPath = findFixturePath("inputs/sine_440.wav");
    const auto goldenPath = findFixturePath("expected/passthrough_sine_440.wav");

    auto input = GoldenFile::readMonoWav(inputPath);
    REQUIRE(input.sampleRate == 48000.0);

    AudioGraph graph;
    graph.prepare(input.sampleRate, 512);

    std::vector<float> output(input.samples.size());

    // Process in 512-sample blocks.
    constexpr std::size_t block = 512;
    for (std::size_t i = 0; i < input.samples.size(); i += block) {
        const std::size_t n = std::min(block, input.samples.size() - i);
        graph.process(input.samples.data() + i, output.data() + i, n);
    }

    // Tolerance accommodates filter group delay + DC blocker settling.
    auto diff = GoldenFile::assertMatchesGolden(goldenPath,
                                                input.sampleRate,
                                                output.data(),
                                                output.size(),
                                                /*tolerance=*/1e-4f);
    INFO("max abs diff: " << diff.maxAbsDiff
         << ", differing samples: " << diff.numDifferingSamples);
    REQUIRE(diff.maxAbsDiff < 1e-4f);
}
