#include <catch2/catch_test_macros.hpp>
#include "audio/NoteSteppedTTSPlayer.h"
#include "audio/TTSClip.h"
#include "audio/WordSyncMode.h"
#include "harness/RealtimeSentinel.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using guitar_dsp::audio::NoteSteppedTTSPlayer;
using guitar_dsp::audio::TTSClip;
using guitar_dsp::audio::WordSegment;
using guitar_dsp::tests::RealtimeSentinel;

namespace {
std::shared_ptr<const TTSClip> threeWordClip() {
    auto c = std::make_shared<TTSClip>();
    c->sampleRate = 48000.0;
    c->samples.assign(3000, 0.0f);
    for (int i = 0; i < 1000; ++i) c->samples[i] = 0.1f;
    for (int i = 1000; i < 2000; ++i) c->samples[i] = 0.2f;
    for (int i = 2000; i < 3000; ++i) c->samples[i] = 0.3f;
    c->words = { {"a",0,1000}, {"b",1000,2000}, {"c",2000,3000} };
    return c;
}
void pluckBlock(NoteSteppedTTSPlayer& p, std::vector<float>& mod) {
    std::vector<float> onset(2000, 0.8f);
    mod.assign(2000, 0.0f);
    p.process(onset.data(), mod.data(), mod.size());
}
void silentBlock(NoteSteppedTTSPlayer& p) {
    std::vector<float> onset(8000, 0.0f), mod(8000, 0.0f);
    p.process(onset.data(), mod.data(), mod.size());
}
}

TEST_CASE("NoteSteppedTTSPlayer: each onset advances one word, wraps",
          "[audio][notestep]") {
    NoteSteppedTTSPlayer p;
    p.prepare(48000.0, 8000);
    p.setClip(threeWordClip());

    REQUIRE(p.currentWordIndex() == -1);

    std::vector<float> mod;
    pluckBlock(p, mod);  REQUIRE(p.currentWordIndex() == 0);  silentBlock(p);
    pluckBlock(p, mod);  REQUIRE(p.currentWordIndex() == 1);  silentBlock(p);
    pluckBlock(p, mod);  REQUIRE(p.currentWordIndex() == 2);  silentBlock(p);
    pluckBlock(p, mod);  REQUIRE(p.currentWordIndex() == 0);
}

TEST_CASE("NoteSteppedTTSPlayer: emits the active word's samples",
          "[audio][notestep]") {
    NoteSteppedTTSPlayer p;
    p.prepare(48000.0, 8000);
    p.setClip(threeWordClip());

    std::vector<float> onset(2000, 0.8f), mod(2000, 0.0f);
    p.process(onset.data(), mod.data(), mod.size());
    float maxAbs = 0.0f;
    for (float v : mod) maxAbs = std::max(maxAbs, std::fabs(v));
    REQUIRE(maxAbs > 0.05f);
    REQUIRE(maxAbs < 0.15f);
}

TEST_CASE("NoteSteppedTTSPlayer: silence in, silence out (no pluck)",
          "[audio][notestep]") {
    NoteSteppedTTSPlayer p;
    p.prepare(48000.0, 8000);
    p.setClip(threeWordClip());
    std::vector<float> onset(2000, 0.0f), mod(2000, 1.0f);
    p.process(onset.data(), mod.data(), mod.size());
    for (float v : mod) REQUIRE(v == 0.0f);
    REQUIRE(p.currentWordIndex() == -1);
}

TEST_CASE("NoteSteppedTTSPlayer: process is allocation-free",
          "[audio][notestep][rt]") {
    NoteSteppedTTSPlayer p;
    p.prepare(48000.0, 512);
    p.setClip(threeWordClip());
    std::vector<float> onset(512), mod(512);
    for (int i = 0; i < 512; ++i)
        onset[static_cast<size_t>(i)] = 0.5f * std::sin(2.0f*3.14159265f*110.0f*i/48000.0f);
    p.process(onset.data(), mod.data(), mod.size());

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int blk = 0; blk < 50; ++blk) p.process(onset.data(), mod.data(), mod.size());
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}

TEST_CASE("WordSyncMode: string round-trip", "[audio][word_sync_mode]") {
    using guitar_dsp::audio::WordSyncMode;
    using guitar_dsp::audio::toString;
    using guitar_dsp::audio::wordSyncModeFromString;
    REQUIRE(wordSyncModeFromString("latch")    == WordSyncMode::Latch);
    REQUIRE(wordSyncModeFromString("advance")  == WordSyncMode::Advance);
    REQUIRE(wordSyncModeFromString("syllable") == WordSyncMode::Syllable);
    REQUIRE(wordSyncModeFromString("nonsense") == WordSyncMode::Latch);
    REQUIRE(std::string(toString(WordSyncMode::Syllable)) == "syllable");
}
