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

namespace {

// Manually build a 3-word TTSClip: each word is 200 ms of a different tone
// followed by 100 ms of silence between words. Sample rate 48 k.
guitar_dsp::audio::TTSClipPtr makeThreeWordClip() {
    using guitar_dsp::audio::TTSClip;
    using guitar_dsp::audio::WordSegment;
    auto clip = std::make_shared<TTSClip>();
    clip->sampleRate = 48000.0;
    constexpr int wordSamples = (48000 * 8) / 10;  // 800 ms per word — long
    constexpr int gapSamples  = 48000 / 5;         // 200 ms gap
    clip->samples.resize(3 * (wordSamples + gapSamples), 0.0f);
    const float freqs[3] = { 220.0f, 330.0f, 440.0f };
    for (int w = 0; w < 3; ++w) {
        const int base = w * (wordSamples + gapSamples);
        for (int i = 0; i < wordSamples; ++i)
            clip->samples[base + i] = 0.5f * std::sin(
                2.0 * 3.14159265 * freqs[w] * i / 48000.0);
        clip->words.push_back(WordSegment{
            "word" + std::to_string(w),
            static_cast<std::size_t>(base),
            static_cast<std::size_t>(base + wordSamples)});
    }
    return clip;
}

// Generate a short pluck-like transient at position `at` in `buf`.
void plantOnset(std::vector<float>& buf, std::size_t at, float amp = 0.6f) {
    for (std::size_t i = 0; i < 64 && at + i < buf.size(); ++i)
        buf[at + i] = amp * std::exp(-static_cast<float>(i) * 0.05f);
}

} // namespace

TEST_CASE("NoteSteppedTTSPlayer Latch: second onset 250 ms in is ignored mid-word",
          "[audio][note_stepped][latch]") {
    // Word 0 is 800 ms long. First onset at t≈2 ms starts playback. Second
    // onset at t≈250 ms — well past OnsetDetector's 80 ms debounce, with a
    // big enough amplitude that env clears the rearm threshold before
    // arrival — would advance to word 1 if Latch weren't holding the guard.
    using namespace guitar_dsp::audio;
    NoteSteppedTTSPlayer player;
    player.prepare(48000.0, 512);
    player.setMode(WordSyncMode::Latch);
    player.setClip(makeThreeWordClip());

    std::vector<float> onsets(48000, 0.0f);
    plantOnset(onsets,    100, 0.9f);   // pulse #1 amplitude 0.9 (clear)
    plantOnset(onsets,  12000, 0.9f);   // pulse #2 at t≈250 ms, also 0.9

    std::vector<float> out(48000);
    for (std::size_t i = 0; i < onsets.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, onsets.size() - i);
        player.process(onsets.data() + i, out.data() + i, n);
    }
    REQUIRE(player.currentWordIndex() == 0);
}

TEST_CASE("NoteSteppedTTSPlayer Latch: second onset after word completes advances",
          "[audio][note_stepped][latch]") {
    // Word 0 is 800 ms long. Second onset arrives at t=1100 ms — well past
    // word completion + gap. Latch lets it through; word index 0 -> 1.
    using namespace guitar_dsp::audio;
    NoteSteppedTTSPlayer player;
    player.prepare(48000.0, 512);
    player.setMode(WordSyncMode::Latch);
    player.setClip(makeThreeWordClip());

    constexpr std::size_t N = 48000 * 3 / 2;  // 1.5 seconds
    std::vector<float> onsets(N, 0.0f);
    plantOnset(onsets,      100, 0.9f);
    plantOnset(onsets,    52800, 0.9f);  // t ≈ 1100 ms — past word 0 (800ms + 200ms gap)

    std::vector<float> out(N);
    for (std::size_t i = 0; i < onsets.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, onsets.size() - i);
        player.process(onsets.data() + i, out.data() + i, n);
    }
    REQUIRE(player.currentWordIndex() == 1);
}

TEST_CASE("NoteSteppedTTSPlayer Advance: onset during playback advances and restarts",
          "[audio][note_stepped][advance]") {
    using namespace guitar_dsp::audio;
    NoteSteppedTTSPlayer player;
    player.prepare(48000.0, 512);
    player.setMode(WordSyncMode::Advance);
    player.setClip(makeThreeWordClip());

    // Two onsets 200 ms apart so they clear the 80 ms onset debounce. The
    // first hits at t=2 ms (env from rest), the second at t=200 ms. With
    // Advance, both fire (env decays cleanly between hits) - index goes
    // 0 -> 1.
    std::vector<float> onsets(48000, 0.0f);
    plantOnset(onsets,    100);
    plantOnset(onsets,   9700);   // t ~ 202 ms

    std::vector<float> out(48000);
    for (std::size_t i = 0; i < onsets.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, onsets.size() - i);
        player.process(onsets.data() + i, out.data() + i, n);
    }
    // Both onsets fire (Advance mode) -> index goes 0 then 1.
    REQUIRE(player.currentWordIndex() == 1);
}
