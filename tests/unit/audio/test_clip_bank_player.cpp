#include <catch2/catch_approx.hpp>
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

TEST_CASE("ClipBankPlayer anchor mode picks closest pitch within bankKey",
          "[clipbank][anchor]") {
    using guitar_dsp::audio::ClipBankPlayer;
    using guitar_dsp::audio::TTSClip;

    auto makeGrain = [](const std::string& key, float anchor, float fillValue,
                        std::size_t lenSamples) {
        auto c = std::make_shared<TTSClip>();
        c->sampleRate    = 48000.0;
        c->samples.assign(lenSamples, fillValue);
        c->bankKey       = key;
        c->anchorPitchHz = anchor;
        return std::const_pointer_cast<const TTSClip>(c);
    };

    ClipBankPlayer p;
    p.prepare(48000.0, 64);
    // 3 ah grains at G2/D3/A3, 3 eh grains at the same anchors.
    std::vector<TTSClipPtr> bank = {
        makeGrain("sung_ah",  98.0f, 0.11f, 96),
        makeGrain("sung_ah", 147.0f, 0.12f, 96),
        makeGrain("sung_ah", 220.0f, 0.13f, 96),
        makeGrain("sung_eh",  98.0f, 0.21f, 96),
        makeGrain("sung_eh", 147.0f, 0.22f, 96),
        makeGrain("sung_eh", 220.0f, 0.23f, 96),
    };
    p.setBank(bank);
    // Drain the pending-bank flag with one no-onset block.
    {
        float in[64] = {0}; float out[64] = {0};
        p.process(in, out, 64);
    }
    // Simulate detected pitch at 200 Hz → closest anchor is 220 Hz.
    p.setDetectedPitchHz(200.0f);
    // First onset → first key (sung_ah), should pick the 220 Hz grain (index 2).
    float in[64] = {0}; float out[64] = {0};
    in[0] = 1.0f;  // synthetic transient
    p.process(in, out, 64);
    // The first non-zero sample of `out` should be ~0.13 (the 220 Hz grain).
    bool found = false;
    for (float v : out) if (v != 0.0f) { CHECK(v == Catch::Approx(0.13f)); found = true; break; }
    CHECK(found);

    // Next onset → key cursor advances to sung_eh; pitch is still ~200,
    // so we expect the 220 Hz eh grain (~0.23).
    // Use a large buffer (7000 samples) so env decays below rearm threshold
    // and the debounce window (80 ms = 3840 samples) expires before the spike.
    {
        constexpr std::size_t N2 = 7000;
        std::vector<float> in2(N2, 0.0f);
        std::vector<float> out2(N2, 0.0f);
        in2[6000] = 1.0f;  // second onset well past debounce + rearm
        p.process(in2.data(), out2.data(), N2);
        found = false;
        for (std::size_t i = 6001; i < N2; ++i) if (out2[i] != 0.0f) {
            CHECK(out2[i] == Catch::Approx(0.23f)); found = true; break;
        }
        CHECK(found);
    }
}

TEST_CASE("ClipBankPlayer anchor-mode fallback when pitch unknown picks first",
          "[clipbank][anchor][fallback]") {
    using guitar_dsp::audio::ClipBankPlayer;
    using guitar_dsp::audio::TTSClip;
    auto g = [](const std::string& k, float anchor, float fill) {
        auto c = std::make_shared<TTSClip>();
        c->sampleRate = 48000.0;
        c->samples.assign(96, fill);
        c->bankKey = k; c->anchorPitchHz = anchor;
        return std::const_pointer_cast<const TTSClip>(c);
    };
    ClipBankPlayer p; p.prepare(48000.0, 64);
    p.setBank({ g("sung_ah", 98.0f, 0.5f),
                g("sung_ah", 220.0f, 0.6f) });
    float in[64] = {0}; float out[64] = {0}; p.process(in, out, 64); // drain
    // Default detected pitch is 0 — anchor mode falls back to first grain
    // of current key.
    in[0] = 1.0f;
    p.process(in, out, 64);
    bool found = false;
    for (float v : out) if (v != 0.0f) { CHECK(v == Catch::Approx(0.5f)); found = true; break; }
    CHECK(found);
}

TEST_CASE("ClipBankPlayer legacy mode unchanged when no bankKey",
          "[clipbank][legacy]") {
    using guitar_dsp::audio::ClipBankPlayer;
    using guitar_dsp::audio::TTSClip;
    auto g = [](float fill) {
        auto c = std::make_shared<TTSClip>();
        c->sampleRate = 48000.0;
        c->samples.assign(96, fill);
        return std::const_pointer_cast<const TTSClip>(c);
    };
    ClipBankPlayer p; p.prepare(48000.0, 64);
    p.setBank({ g(0.1f), g(0.2f) });
    float in[64] = {0}; float out[64] = {0}; p.process(in, out, 64);
    in[0] = 1.0f;
    p.process(in, out, 64);
    bool found = false;
    for (float v : out) if (v != 0.0f) { CHECK(v == Catch::Approx(0.1f)); found = true; break; }
    CHECK(found);
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
