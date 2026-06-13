#include <catch2/catch_test_macros.hpp>
#include "audio/MicCapture.h"
#include "harness/RealtimeSentinel.h"
#include <juce_audio_basics/juce_audio_basics.h>

using guitar_dsp::audio::MicCapture;
using guitar_dsp::tests::RealtimeSentinel;

// We can't easily instantiate the full PluginProcessor in a test binary (see
// Task 5.2 note), so this test directly stress-tests MicCapture under the
// realistic processBlock pattern: stereo downmix + repeated appends.
TEST_CASE("MicCapture: stereo downmix + 100 processBlock-sized pushes are alloc-free",
          "[audio][mic][rt][integration]") {
    MicCapture m;
    m.prepare(48000.0, 1);
    m.beginCapture();

    constexpr int kBlock = 512;
    juce::AudioBuffer<float> buf(2, kBlock);
    buf.clear();

    RealtimeSentinel rt;
    rt.markCurrentThreadAsRealtime();
    for (int i = 0; i < 100; ++i) {
        // Simulate stereo downmix path: allocation-free tmp buffer on stack
        constexpr int kMaxBlock = 8192;
        float tmp[kMaxBlock];
        const float* L = buf.getReadPointer(0);
        const float* R = buf.getReadPointer(1);
        for (int j = 0; j < kBlock; ++j) tmp[j] = 0.5f * (L[j] + R[j]);
        m.appendFromAudioBlock(tmp, kBlock);
    }
    rt.unmarkCurrentThreadAsRealtime();
    REQUIRE(rt.violations() == 0);

    auto out = m.endCapture();
    REQUIRE(out.size() > 0);
}
