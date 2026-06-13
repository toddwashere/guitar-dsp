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

namespace {

// 3-syllable clip: like makeThreeWordClip but with `syllables` populated
// and `words` left empty so the test isolates syllable behavior.
guitar_dsp::audio::TTSClipPtr makeThreeSyllableClip() {
    using guitar_dsp::audio::TTSClip;
    using guitar_dsp::audio::WordSegment;
    auto clip = std::make_shared<TTSClip>();
    clip->sampleRate = 48000.0;
    constexpr int sylSamples = (48000 * 8) / 10;   // 800 ms per syllable — long
    constexpr int gap         = 48000 / 5;          // 200 ms gap
    clip->samples.resize(3 * (sylSamples + gap), 0.0f);
    const float freqs[3] = { 220.0f, 247.0f, 277.0f };
    for (int s = 0; s < 3; ++s) {
        const int base = s * (sylSamples + gap);
        for (int i = 0; i < sylSamples; ++i)
            clip->samples[base + i] = 0.5f * std::sin(
                2.0 * 3.14159265 * freqs[s] * i / 48000.0);
        clip->syllables.push_back(WordSegment{
            "syl" + std::to_string(s),
            static_cast<std::size_t>(base),
            static_cast<std::size_t>(base + sylSamples)});
    }
    return clip;
}

} // namespace

TEST_CASE("NoteSteppedTTSPlayer Syllable: advances through syllables when populated",
          "[audio][note_stepped][syllable]") {
    using namespace guitar_dsp::audio;
    NoteSteppedTTSPlayer player;
    player.prepare(48000.0, 512);
    player.setMode(WordSyncMode::Syllable);
    player.setClip(makeThreeSyllableClip());

    // Three onsets spaced past the 800 ms syllable + 200 ms gap, so each
    // syllable completes before the next onset arrives → Syllable mode
    // (Latch-on-syllables semantics) advances cleanly through all 3.
    constexpr std::size_t N = 48000 * 4;  // 4 seconds
    std::vector<float> onsets(N, 0.0f);
    plantOnset(onsets,          100, 0.9f);
    plantOnset(onsets,        52800, 0.9f);    // t ≈ 1100 ms
    plantOnset(onsets,       105600, 0.9f);    // t ≈ 2200 ms

    std::vector<float> out(N);
    for (std::size_t i = 0; i < onsets.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, onsets.size() - i);
        player.process(onsets.data() + i, out.data() + i, n);
    }
    REQUIRE(player.currentWordIndex() == 2);  // index across the 3 syllables
}

TEST_CASE("NoteSteppedTTSPlayer Syllable: falls back to Latch on words when no syllables",
          "[audio][note_stepped][syllable]") {
    using namespace guitar_dsp::audio;
    NoteSteppedTTSPlayer player;
    player.prepare(48000.0, 512);
    player.setMode(WordSyncMode::Syllable);
    // makeThreeWordClip has WORDS populated, no syllables. With syllable mode
    // requested but no syllables, behavior must match Latch on words.
    player.setClip(makeThreeWordClip());

    // Mid-word second onset (same pattern as the Latch-mid-word test).
    std::vector<float> onsets(48000, 0.0f);
    plantOnset(onsets,    100, 0.9f);
    plantOnset(onsets,  12000, 0.9f);   // t ≈ 250 ms, mid-word

    std::vector<float> out(48000);
    for (std::size_t i = 0; i < onsets.size(); i += 512) {
        const std::size_t n = std::min<std::size_t>(512, onsets.size() - i);
        player.process(onsets.data() + i, out.data() + i, n);
    }
    REQUIRE(player.currentWordIndex() == 0);  // fallback to Latch
}

TEST_CASE("NoteSteppedTTSPlayer: setMode to a new mode rewinds the sequence",
          "[audio][note_stepped][mode_change]") {
    // Drive the player to wordIndex=1 in Latch mode, then switch mode. The
    // next onset (well after the previous word completes) should advance to
    // index 0, not 2 — confirms setMode triggered the pendingRewind path.
    using namespace guitar_dsp::audio;
    NoteSteppedTTSPlayer player;
    player.prepare(48000.0, 512);
    player.setMode(WordSyncMode::Latch);
    player.setClip(makeThreeWordClip());

    constexpr std::size_t N = 48000 * 3;  // 3 seconds — past two 800 ms words
    std::vector<float> onsets(N, 0.0f);
    plantOnset(onsets,       100, 0.9f);    // advance to word 0
    plantOnset(onsets,     52800, 0.9f);    // t ≈ 1100 ms, advance to word 1

    std::vector<float> out(N);
    // Process the first two onsets through.
    for (std::size_t i = 0; i < N; i += 512) {
        const std::size_t n = std::min<std::size_t>(512, N - i);
        player.process(onsets.data() + i, out.data() + i, n);
    }
    REQUIRE(player.currentWordIndex() == 1);  // sanity: latched to word 1

    // Switch mode — should request a rewind. The next process() drains it.
    player.setMode(WordSyncMode::Advance);

    // After rewind, currentWordIndex should be -1 within one block.
    std::vector<float> blank(512, 0.0f), bout(512);
    player.process(blank.data(), bout.data(), 512);
    REQUIRE(player.currentWordIndex() == -1);

    // A subsequent fresh onset advances to index 0 (not 2).
    std::vector<float> trailing(N, 0.0f);
    plantOnset(trailing, 1000, 0.9f);
    for (std::size_t i = 0; i < N; i += 512) {
        const std::size_t n = std::min<std::size_t>(512, N - i);
        player.process(trailing.data() + i, bout.data(), n);
        if (i > 1000) break;  // capture first onset
    }
    REQUIRE(player.currentWordIndex() == 0);
}

TEST_CASE("NoteSteppedTTSPlayer: setMode to SAME mode does not rewind",
          "[audio][note_stepped][mode_change]") {
    // No-op mode change should NOT reset wordIndex_ — confirms the prev/new
    // comparison in setMode short-circuits when the value didn't change.
    using namespace guitar_dsp::audio;
    NoteSteppedTTSPlayer player;
    player.prepare(48000.0, 512);
    player.setMode(WordSyncMode::Latch);
    player.setClip(makeThreeWordClip());

    constexpr std::size_t N = 48000 * 3;
    std::vector<float> onsets(N, 0.0f);
    plantOnset(onsets, 100,   0.9f);
    plantOnset(onsets, 52800, 0.9f);

    std::vector<float> out(N);
    for (std::size_t i = 0; i < N; i += 512) {
        const std::size_t n = std::min<std::size_t>(512, N - i);
        player.process(onsets.data() + i, out.data() + i, n);
    }
    REQUIRE(player.currentWordIndex() == 1);

    player.setMode(WordSyncMode::Latch);  // same mode → no rewind
    std::vector<float> blank(512, 0.0f), bout(512);
    player.process(blank.data(), bout.data(), 512);
    REQUIRE(player.currentWordIndex() == 1);  // still on word 1
}
