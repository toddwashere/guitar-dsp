#include "audio/GspeakBundle.h"
#include "app/AssetLocator.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <cmath>

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
