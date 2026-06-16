#include <catch2/catch_test_macros.hpp>
#include "audio/NoteSteppedTTSPlayer.h"
#include "audio/TTSClip.h"
#include "audio/WordSyncMode.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using guitar_dsp::audio::NoteSteppedTTSPlayer;
using guitar_dsp::audio::TTSClip;
using guitar_dsp::audio::WordSegment;
using guitar_dsp::audio::WordSyncMode;

namespace {
std::string goldenPath(const std::string& rel) {
    auto p = std::filesystem::current_path();
    for (int i = 0; i < 6; ++i) {
        auto c = p / "tests" / "golden" / rel;
        if (std::filesystem::exists(c.parent_path())) return c.string();
        p = p.parent_path();
    }
    throw std::runtime_error(
        "golden not found from " + std::filesystem::current_path().string());
}

std::vector<float> readF32(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    INFO("opening: " << path);
    REQUIRE(f);
    f.seekg(0, std::ios::end);
    const auto n = static_cast<std::size_t>(f.tellg()) / sizeof(float);
    f.seekg(0, std::ios::beg);
    std::vector<float> v(n);
    f.read(reinterpret_cast<char*>(v.data()), static_cast<std::streamsize>(n*sizeof(float)));
    return v;
}

std::shared_ptr<const TTSClip> deterministicClip() {
    auto c = std::make_shared<TTSClip>();
    c->sampleRate = 48000.0;
    c->samples.assign(9000, 0.0f);
    for (int i = 0; i < 3000; ++i)         c->samples[i] = 0.10f;
    for (int i = 3000; i < 6000; ++i)      c->samples[i] = 0.20f;
    for (int i = 6000; i < 9000; ++i)      c->samples[i] = 0.30f;
    c->words = { {"a",0,3000}, {"b",3000,6000}, {"c",6000,9000} };
    return c;
}
} // namespace

TEST_CASE("v1 NoteSteppedTTSPlayer output is byte-equal to reference",
          "[audio][v1golden]") {
    NoteSteppedTTSPlayer p;
    p.prepare(48000.0, 256);
    p.setClip(deterministicClip());
    p.setMode(WordSyncMode::Latch);

    std::vector<float> onsetTrack(36000, 0.0f);
    for (auto s : {0, 12000, 24000})
        for (int i = 0; i < 480; ++i) onsetTrack[s + i] = 0.8f;

    std::vector<float> modOut(36000, 0.0f);
    p.process(onsetTrack.data(), modOut.data(), modOut.size());

    auto ref = readF32(goldenPath("v1_speech/reference_output.f32"));
    REQUIRE(modOut.size() == ref.size());
    for (std::size_t i = 0; i < ref.size(); ++i) {
        REQUIRE(modOut[i] == ref[i]);
    }
}
