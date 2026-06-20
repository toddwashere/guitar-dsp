#include "audio/GspeakBundle.h"
#include "app/AssetLocator.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <cmath>
#include <filesystem>

namespace {
constexpr const char* kRel =
    "clips/gspeak/_test_save_then_load.gspeak";

guitar_dsp::audio::TTSClip makeV1ClipWithSamples(float marker) {
    guitar_dsp::audio::TTSClip c;
    c.sampleRate = 48000.0;
    c.samples.assign(4800, marker);
    guitar_dsp::audio::WordSegment w{"x", 0, c.samples.size()};
    c.words.push_back(w);
    c.syllables.push_back(w);
    return c;
}
}

TEST_CASE("Save then Load in same session returns saved samples",
          "[integration][gspeak]") {
    namespace fs = std::filesystem;
    const auto src = guitar_dsp::AssetLocator::resolveSourceRelativePath(kRel);
    if (src.empty()) {
        WARN("no source-tree dev build detected; skipping");
        return;
    }

    // Save a marker clip to the source path (what onSavePressed_ does).
    fs::create_directories(fs::path(src).parent_path());
    const auto orig = makeV1ClipWithSamples(0.42f);
    REQUIRE(guitar_dsp::audio::GspeakBundle::write(
        juce::File(src), orig, "marker"));

    // Read via resolveForRead — should see the source-tree file we just
    // wrote, not the (possibly older) bundle copy.
    const auto readPath = guitar_dsp::AssetLocator::resolveForRead(kRel);
    auto loaded = guitar_dsp::audio::GspeakBundle::read(
        juce::File(readPath), 48000.0);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->clip);
    REQUIRE(loaded->clip->samples.size() == orig.samples.size());
    // 16-bit PCM quantization tolerance — same threshold the existing
    // GspeakBundle round-trip test uses.
    for (std::size_t i = 0; i < orig.samples.size(); ++i)
        CHECK(std::fabs(loaded->clip->samples[i] - orig.samples[i]) < 1.0f / 16384.0f);

    fs::remove(src);
}
