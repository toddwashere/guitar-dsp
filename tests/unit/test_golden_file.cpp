#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "harness/GoldenFile.h"

#include <filesystem>

using guitar_dsp::tests::GoldenFile;

TEST_CASE("golden: identical buffers match bit-exact", "[harness]") {
    std::vector<float> a(1024, 0.5f);
    std::vector<float> b(1024, 0.5f);
    auto diff = GoldenFile::compareBuffers(a.data(), b.data(), a.size());
    REQUIRE(diff.maxAbsDiff == 0.0f);
    REQUIRE(diff.numDifferingSamples == 0);
}

TEST_CASE("golden: differing buffers report diff", "[harness]") {
    std::vector<float> a(8, 0.0f);
    std::vector<float> b(8, 0.0f);
    b[3] = 0.25f;
    auto diff = GoldenFile::compareBuffers(a.data(), b.data(), a.size());
    REQUIRE(diff.maxAbsDiff == 0.25f);
    REQUIRE(diff.numDifferingSamples == 1);
}

TEST_CASE("golden: round-trip write-then-read preserves data", "[harness]") {
    const auto path = std::filesystem::temp_directory_path() / "golden_roundtrip.wav";
    std::vector<float> data(2048);
    for (size_t i = 0; i < data.size(); ++i) data[i] = std::sin(0.01f * i);

    GoldenFile::writeMonoWav(path.string(), 48000.0, data.data(), data.size());
    auto readBack = GoldenFile::readMonoWav(path.string());

    REQUIRE(readBack.sampleRate == 48000.0);
    REQUIRE(readBack.samples.size() == data.size());
    auto diff = GoldenFile::compareBuffers(data.data(), readBack.samples.data(), data.size());
    // 24-bit WAV → tolerance ~1/2^23.
    REQUIRE(diff.maxAbsDiff < 1.5e-7f);
}
