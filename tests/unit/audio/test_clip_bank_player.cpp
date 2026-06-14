#include <catch2/catch_test_macros.hpp>
#include "audio/ClipBankPlayer.h"
#include "audio/TTSClip.h"
#include "harness/RealtimeSentinel.h"

#include <cmath>
#include <memory>
#include <vector>

using guitar_dsp::audio::ClipBankPlayer;
using guitar_dsp::audio::TTSClip;
using guitar_dsp::audio::TTSClipPtr;
using guitar_dsp::tests::RealtimeSentinel;

namespace {

// One short clip filled with a single constant value, so the test can
// assert which clip's samples the player emitted.
TTSClipPtr makeClip(float value, std::size_t numSamples) {
    auto c = std::make_shared<TTSClip>();
    c->sampleRate = 48000.0;
    c->samples.assign(numSamples, value);
    return c;
}

// Generate a short pluck-like transient at position `at` in `buf`.
void plantOnset(std::vector<float>& buf, std::size_t at, float amp = 0.9f) {
    for (std::size_t i = 0; i < 64 && at + i < buf.size(); ++i)
        buf[at + i] = amp * std::exp(-static_cast<float>(i) * 0.05f);
}

} // namespace

TEST_CASE("ClipBankPlayer: first onset advances cursor from -1 to 0",
          "[audio][clip_bank]") {
    ClipBankPlayer p;
    p.prepare(48000.0, 512);
    p.setBank({ makeClip(0.1f, 2000), makeClip(0.2f, 2000) });

    REQUIRE(p.currentClipIndex() == -1);

    std::vector<float> onset(2000, 0.0f);
    plantOnset(onset, 100);
    std::vector<float> mod(2000, 0.0f);
    p.process(onset.data(), mod.data(), mod.size());

    REQUIRE(p.currentClipIndex() == 0);
}

TEST_CASE("ClipBankPlayer: subsequent onsets advance through bank then wrap",
          "[audio][clip_bank]") {
    ClipBankPlayer p;
    p.prepare(48000.0, 512);
    // 3-clip bank, each clip 800 ms so onsets land after the prior clip ends.
    constexpr std::size_t clipSamples = (48000 * 8) / 10;
    p.setBank({ makeClip(0.1f, clipSamples),
                makeClip(0.2f, clipSamples),
                makeClip(0.3f, clipSamples) });

    // Four onsets spaced 1100 ms apart (past each clip's 800 ms duration).
    constexpr std::size_t N = 48000 * 5;  // 5 seconds
    std::vector<float> onset(N, 0.0f);
    plantOnset(onset, 100);
    plantOnset(onset,  52800);   // ~1100 ms
    plantOnset(onset, 105600);   // ~2200 ms
    plantOnset(onset, 158400);   // ~3300 ms — should wrap to clip 0

    std::vector<float> mod(N, 0.0f);
    for (std::size_t i = 0; i < N; i += 512) {
        const std::size_t n = std::min<std::size_t>(512, N - i);
        p.process(onset.data() + i, mod.data() + i, n);
    }

    REQUIRE(p.currentClipIndex() == 0);  // wrapped
}

TEST_CASE("ClipBankPlayer: emits active clip's samples",
          "[audio][clip_bank]") {
    ClipBankPlayer p;
    p.prepare(48000.0, 512);
    p.setBank({ makeClip(0.10f, 200), makeClip(0.25f, 200) });

    std::vector<float> onset(400, 0.0f);
    plantOnset(onset, 50);
    std::vector<float> mod(400, 0.0f);
    p.process(onset.data(), mod.data(), mod.size());

    // After the onset settles and the clip plays, mod should contain the
    // first clip's value (0.10) at some sample inside the played region.
    bool sawClipValue = false;
    for (float v : mod)
        if (std::fabs(v - 0.10f) < 1e-5f) { sawClipValue = true; break; }
    REQUIRE(sawClipValue);
}

TEST_CASE("ClipBankPlayer: outputs zero after clip ends (no next onset)",
          "[audio][clip_bank]") {
    ClipBankPlayer p;
    p.prepare(48000.0, 512);
    // Short clip (300 samples). Process 600 samples; samples 300+ must be 0
    // (after the onset triggers and the clip exhausts).
    p.setBank({ makeClip(0.5f, 300) });

    std::vector<float> onset(600, 0.0f);
    plantOnset(onset, 0);
    std::vector<float> mod(600, 0.0f);
    p.process(onset.data(), mod.data(), mod.size());

    // The tail half of the buffer should be silent.
    for (std::size_t i = 400; i < 600; ++i)
        REQUIRE(mod[i] == 0.0f);
}

TEST_CASE("ClipBankPlayer: empty bank outputs silence and cursor stays -1",
          "[audio][clip_bank]") {
    ClipBankPlayer p;
    p.prepare(48000.0, 512);
    p.setBank({});  // empty

    std::vector<float> onset(500, 0.0f);
    plantOnset(onset, 50);   // would otherwise advance
    std::vector<float> mod(500, 1.0f);  // sentinel; must be zeroed by process
    p.process(onset.data(), mod.data(), mod.size());

    for (float v : mod) REQUIRE(v == 0.0f);
    REQUIRE(p.currentClipIndex() == -1);
}

TEST_CASE("ClipBankPlayer: rewind() resets cursor; next onset plays clip 0",
          "[audio][clip_bank]") {
    ClipBankPlayer p;
    p.prepare(48000.0, 512);
    constexpr std::size_t clipSamples = (48000 * 8) / 10;
    p.setBank({ makeClip(0.1f, clipSamples),
                makeClip(0.2f, clipSamples),
                makeClip(0.3f, clipSamples) });

    // Two onsets — cursor to 1.
    constexpr std::size_t N = 48000 * 3;
    std::vector<float> onset(N, 0.0f);
    plantOnset(onset, 100);
    plantOnset(onset, 52800);
    std::vector<float> mod(N, 0.0f);
    for (std::size_t i = 0; i < N; i += 512) {
        const std::size_t n = std::min<std::size_t>(512, N - i);
        p.process(onset.data() + i, mod.data() + i, n);
    }
    REQUIRE(p.currentClipIndex() == 1);

    // Rewind, then drain one block so the pending flag lands.
    p.rewind();
    std::vector<float> blank(512, 0.0f), bout(512, 0.0f);
    p.process(blank.data(), bout.data(), 512);
    REQUIRE(p.currentClipIndex() == -1);

    // A subsequent fresh onset advances to clip 0 (not 2).
    std::vector<float> trailing(N, 0.0f);
    plantOnset(trailing, 1000);
    for (std::size_t i = 0; i < N; i += 512) {
        const std::size_t n = std::min<std::size_t>(512, N - i);
        p.process(trailing.data() + i, bout.data(), n);
        if (i > 2000) break;
    }
    REQUIRE(p.currentClipIndex() == 0);
}

TEST_CASE("ClipBankPlayer: process is allocation-free",
          "[audio][clip_bank][rt]") {
    ClipBankPlayer p;
    p.prepare(48000.0, 512);
    p.setBank({ makeClip(0.1f, 4800), makeClip(0.2f, 4800) });

    // Drain the pending bank flag with one non-RT block.
    std::vector<float> onset(512, 0.0f), mod(512, 0.0f);
    p.process(onset.data(), mod.data(), 512);

    // Now lock allocation and process 50 blocks.
    for (int i = 0; i < 512; ++i)
        onset[static_cast<std::size_t>(i)] =
            0.5f * std::sin(2.0f * 3.14159265f * 110.0f * i / 48000.0f);

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int blk = 0; blk < 50; ++blk)
        p.process(onset.data(), mod.data(), mod.size());
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
