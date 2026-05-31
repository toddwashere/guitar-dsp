#include <catch2/catch_test_macros.hpp>

#include "audio/TTSClipPlayer.h"
#include "audio/TTSClip.h"
#include "harness/RealtimeSentinel.h"

#include <vector>

using guitar_dsp::audio::TTSClip;
using guitar_dsp::audio::TTSClipPlayer;
using guitar_dsp::audio::TTSClipPtr;
using guitar_dsp::tests::RealtimeSentinel;

namespace {
TTSClipPtr makeRamp(std::size_t n, float startVal, float endVal) {
    auto c = std::make_shared<TTSClip>();
    c->name = "ramp";
    c->sampleRate = 48000.0;
    c->samples.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n - 1);
        c->samples[i] = startVal + (endVal - startVal) * t;
    }
    return c;
}
}

TEST_CASE("TTSClipPlayer: no clip means silence", "[audio][tts]") {
    TTSClipPlayer p;
    p.prepare(48000.0, 256);

    std::vector<float> out(256, 0.42f);
    p.process(out.data(), out.size());
    for (float s : out) REQUIRE(s == 0.0f);
}

TEST_CASE("TTSClipPlayer: setClip + process emits the clip data", "[audio][tts]") {
    TTSClipPlayer p;
    p.prepare(48000.0, 256);

    auto clip = makeRamp(100, 0.1f, 1.0f);
    p.setClip(clip);

    std::vector<float> out(100);
    p.process(out.data(), 100);

    for (std::size_t i = 0; i < 100; ++i) {
        REQUIRE(out[i] == clip->samples[i]);
    }
}

TEST_CASE("TTSClipPlayer: emits silence after clip ends", "[audio][tts]") {
    TTSClipPlayer p;
    p.prepare(48000.0, 256);

    auto clip = makeRamp(50, 0.5f, 0.5f);
    p.setClip(clip);

    std::vector<float> out(100);
    p.process(out.data(), 100);

    for (std::size_t i = 0; i < 50; ++i) REQUIRE(out[i] == 0.5f);
    for (std::size_t i = 50; i < 100; ++i) REQUIRE(out[i] == 0.0f);
}

TEST_CASE("TTSClipPlayer: setClip after end restarts from sample 0", "[audio][tts]") {
    TTSClipPlayer p;
    p.prepare(48000.0, 256);

    auto clip = makeRamp(10, 0.1f, 1.0f);
    p.setClip(clip);

    std::vector<float> firstPass(20), secondPass(20);
    p.process(firstPass.data(),  firstPass.size());   // plays + ends
    p.setClip(clip);                                  // restart
    p.process(secondPass.data(), secondPass.size());

    for (std::size_t i = 0; i < 10; ++i) {
        REQUIRE(secondPass[i] == clip->samples[i]);
    }
}

TEST_CASE("TTSClipPlayer: zero allocations on audio thread", "[audio][tts][realtime]") {
    TTSClipPlayer p;
    p.prepare(48000.0, 512);
    p.setClip(makeRamp(48000, 0.0f, 0.5f));  // 1 s clip

    std::vector<float> out(512);

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int i = 0; i < 200; ++i) p.process(out.data(), out.size());
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
