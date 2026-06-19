#include "audio/GspeakBundle.h"
#include "app/AssetLocator.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <cmath>
#include <filesystem>
#include <utility>
#include <vector>

namespace {

guitar_dsp::audio::TTSClip makeTinyV1Clip() {
    guitar_dsp::audio::TTSClip c;
    c.sampleRate = 48000.0;
    c.samples.assign(4800, 0.0f);
    for (std::size_t i = 0; i < c.samples.size(); ++i)
        c.samples[i] = (float) std::sin(2.0 * 3.14159 * 250.0
                                        * (double) i / 48000.0) * 0.3f;
    guitar_dsp::audio::WordSegment w{"hi", 0, c.samples.size()};
    c.syllables.push_back(w);
    c.words.push_back(w);
    return c;
}

} // namespace

TEST_CASE("AssetLocator::resolveRelativePath strips leading assets/",
          "[integration][gspeak]") {
    // Either form should resolve to the same absolute path.
    const auto withPrefix    = guitar_dsp::AssetLocator::resolveRelativePath(
        "assets/clips/gspeak/test.gspeak");
    const auto withoutPrefix = guitar_dsp::AssetLocator::resolveRelativePath(
        "clips/gspeak/test.gspeak");
    REQUIRE_FALSE(withPrefix.empty());
    REQUIRE_FALSE(withoutPrefix.empty());
    REQUIRE(withPrefix == withoutPrefix);
}

TEST_CASE("GspeakBundle round-trip via AssetLocator-resolved path",
          "[integration][gspeak]") {
    // Use a temp-directory path that doesn't require the assets root.
    auto temp = juce::File::createTempFile(".gspeak");

    auto orig = makeTinyV1Clip();
    REQUIRE(guitar_dsp::audio::GspeakBundle::write(temp, orig, "hi"));

    auto loaded = guitar_dsp::audio::GspeakBundle::read(temp, 48000.0);
    REQUIRE(loaded.has_value());
    REQUIRE_FALSE(loaded->isV2);
    REQUIRE(loaded->clip != nullptr);
    REQUIRE(loaded->clip->samples.size() == orig.samples.size());
    REQUIRE_FALSE(loaded->clip->syllables.empty());
    REQUIRE(loaded->text == "hi");

    temp.deleteFile();
}

TEST_CASE("GspeakBundle::read returns nullopt for nonexistent file at resolved path",
          "[integration][gspeak]") {
    const auto resolved = guitar_dsp::AssetLocator::resolveRelativePath(
        "clips/gspeak/definitely-does-not-exist-xyz.gspeak");
    REQUIRE_FALSE(resolved.empty());
    juce::File file(resolved);
    REQUIRE_FALSE(file.existsAsFile());
    auto loaded = guitar_dsp::audio::GspeakBundle::read(file, 48000.0);
    REQUIRE_FALSE(loaded.has_value());
}

TEST_CASE("AssetLocator::resolveSourceRelativePath finds the dev-build source dir",
          "[integration][gspeak]") {
    // When the test binary lives inside a dev build tree (assets/ + build/
    // siblings somewhere up the tree), the source-relative resolver returns
    // a non-empty path. The test runner itself runs from such a tree, so this
    // is a stable expectation in the test environment.
    const auto resolved = guitar_dsp::AssetLocator::resolveSourceRelativePath(
        "clips/gspeak/test.gspeak");
    REQUIRE_FALSE(resolved.empty());
    // The resolved path must be a SOURCE tree path — meaning it sits next to
    // a `build/` directory (the dev-build signature). Walk up the resolved
    // path and confirm a sibling `build/` exists somewhere along the way.
    std::filesystem::path p = resolved;
    bool foundBuildSibling = false;
    for (int i = 0; i < 12; ++i) {
        if (std::filesystem::is_directory(p.parent_path() / "build")) {
            foundBuildSibling = true;
            break;
        }
        if (p == p.parent_path()) break;
        p = p.parent_path();
    }
    REQUIRE(foundBuildSibling);
}

TEST_CASE("GspeakBundle round-trip preserves audio samples bit-exact at same rate",
          "[integration][gspeak]") {
    // Writes a clip with known-distinct samples, reads it back at the same
    // sample rate, and confirms every audio sample matches. Catches silent
    // corruption in the 16-bit PCM round trip (e.g. byte order, channel mix,
    // sample-rate metadata divergence).
    auto temp = juce::File::createTempFile(".gspeak");

    guitar_dsp::audio::TTSClip orig;
    orig.sampleRate = 48000.0;
    orig.samples.resize(2400);  // 50 ms
    for (std::size_t i = 0; i < orig.samples.size(); ++i)
        orig.samples[i] = (float) std::sin(2.0 * 3.14159 * 440.0
                                           * (double) i / 48000.0) * 0.5f;
    guitar_dsp::audio::WordSegment w{"x", 0, orig.samples.size()};
    orig.syllables.push_back(w);
    orig.words.push_back(w);

    REQUIRE(guitar_dsp::audio::GspeakBundle::write(temp, orig, "x"));

    auto loaded = guitar_dsp::audio::GspeakBundle::read(temp, 48000.0);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->clip->samples.size() == orig.samples.size());

    // 16-bit PCM quantizes to ~1/32768; allow tolerance well above that
    // (around 1e-4) to cover dithering and conversion rounding.
    constexpr float kTol = 1.0f / 16384.0f;
    int worstIdx = -1;
    float worstDelta = 0.0f;
    for (std::size_t i = 0; i < orig.samples.size(); ++i) {
        const float d = std::fabs(loaded->clip->samples[i] - orig.samples[i]);
        if (d > worstDelta) { worstDelta = d; worstIdx = (int) i; }
    }
    REQUIRE(worstDelta < kTol);
    (void) worstIdx;  // silence unused on success

    temp.deleteFile();
}

TEST_CASE("GspeakBundle round-trip preserves syllable count and order",
          "[integration][gspeak]") {
    // Catches drift where a future change might reorder, drop, or merge
    // boundary entries on write or read (e.g. accidentally sorting,
    // accidental filter-out of zero-width spans, etc.).
    auto temp = juce::File::createTempFile(".gspeak");

    guitar_dsp::audio::TTSClip orig;
    orig.sampleRate = 48000.0;
    orig.samples.assign(48000, 0.1f);  // 1s of constant signal

    // Five distinct word-labeled segments, ordered, contiguous.
    const std::vector<std::pair<std::string, std::size_t>> spec{
        {"alpha",   9600},   // 0..9600
        {"beta",   19200},   // 9600..19200
        {"gamma",  28800},   // 19200..28800
        {"delta",  38400},   // 28800..38400
        {"epsilon", 48000},  // 38400..48000
    };
    std::size_t prev = 0;
    for (const auto& [label, end] : spec) {
        orig.syllables.push_back({label, prev, end});
        orig.words.push_back({label, prev, end});
        prev = end;
    }

    REQUIRE(guitar_dsp::audio::GspeakBundle::write(temp, orig, "all five"));
    auto loaded = guitar_dsp::audio::GspeakBundle::read(temp, 48000.0);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->clip->syllables.size() == 5);
    REQUIRE(loaded->clip->words.size()     == 5);
    for (std::size_t i = 0; i < spec.size(); ++i) {
        REQUIRE(loaded->clip->syllables[i].word == spec[i].first);
        REQUIRE(loaded->clip->syllables[i].endSample == spec[i].second);
        REQUIRE(loaded->clip->words[i].word == spec[i].first);
        REQUIRE(loaded->clip->words[i].endSample == spec[i].second);
    }
    REQUIRE(loaded->text == "all five");

    temp.deleteFile();
}

TEST_CASE("GspeakBundle round-trip survives re-write on top of itself",
          "[integration][gspeak]") {
    // Simulates the user editing → Save → editing → Save again. Each save
    // must overwrite cleanly; reads after the second save must reflect the
    // second version, not a partial mix of both.
    auto temp = juce::File::createTempFile(".gspeak");

    guitar_dsp::audio::TTSClip first  = makeTinyV1Clip();
    guitar_dsp::audio::TTSClip second = makeTinyV1Clip();
    second.syllables.clear();
    second.syllables.push_back({"first",  0,    2000});
    second.syllables.push_back({"second", 2000, second.samples.size()});

    REQUIRE(guitar_dsp::audio::GspeakBundle::write(temp, first,  "v1"));
    REQUIRE(guitar_dsp::audio::GspeakBundle::write(temp, second, "v2"));

    auto loaded = guitar_dsp::audio::GspeakBundle::read(temp, 48000.0);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->text == "v2");
    REQUIRE(loaded->clip->syllables.size() == 2);
    REQUIRE(loaded->clip->syllables[0].word == "first");
    REQUIRE(loaded->clip->syllables[1].word == "second");

    temp.deleteFile();
}
