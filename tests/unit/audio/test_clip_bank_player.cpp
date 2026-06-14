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
