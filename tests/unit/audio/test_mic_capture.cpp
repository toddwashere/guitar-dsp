#include <catch2/catch_test_macros.hpp>
#include "audio/MicCapture.h"
#include "harness/RealtimeSentinel.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::MicCapture;
using guitar_dsp::tests::RealtimeSentinel;

namespace {
std::vector<float> sine(int n, double rate, double hz, float amp = 0.5f) {
    std::vector<float> s(n);
    for (int i = 0; i < n; ++i)
        s[i] = amp * std::sin(2.0 * M_PI * hz * i / rate);
    return s;
}
}

TEST_CASE("MicCapture: end after begin returns resampled mono 16k",
          "[audio][mic]") {
    MicCapture m;
    m.prepare(48000.0, 1);
    m.beginCapture();
    auto in = sine(48000, 48000.0, 440.0);   // 1 s at 48k
    m.appendFromAudioBlock(in.data(), (int)in.size());
    auto out = m.endCapture();
    REQUIRE(out.size() >= 15800);            // ~1 s at 16k +- 1.5%
    REQUIRE(out.size() <= 16200);
}

TEST_CASE("MicCapture: passthrough when sample rate already 16k",
          "[audio][mic]") {
    MicCapture m;
    m.prepare(16000.0, 1);
    m.beginCapture();
    auto in = sine(16000, 16000.0, 440.0);
    m.appendFromAudioBlock(in.data(), (int)in.size());
    auto out = m.endCapture();
    REQUIRE(out.size() == in.size());
}

TEST_CASE("MicCapture: too-short capture flagged",
          "[audio][mic]") {
    MicCapture m;
    m.prepare(48000.0, 1);
    m.beginCapture();
    auto in = sine(1000, 48000.0, 440.0);    // ~21 ms
    m.appendFromAudioBlock(in.data(), (int)in.size());
    auto out = m.endCapture();
    REQUIRE(m.lastResultWasTooShort());
}

TEST_CASE("MicCapture: very long capture is truncated at 30s",
          "[audio][mic]") {
    MicCapture m;
    m.prepare(16000.0, 1);
    m.beginCapture();
    std::vector<float> chunk(1600, 0.5f);    // 100 ms per chunk
    for (int s = 0; s < 600; ++s)            // 60 seconds of pushes
        m.appendFromAudioBlock(chunk.data(), (int)chunk.size());
    auto out = m.endCapture();
    REQUIRE(out.size() <= 16000 * 30 + 100);
    REQUIRE(m.lastResultWasTruncated());
}

TEST_CASE("MicCapture: peak meter reflects largest sample",
          "[audio][mic]") {
    MicCapture m;
    m.prepare(48000.0, 1);
    m.beginCapture();
    std::vector<float> samples(512, 0.0f);
    samples[100] = 0.5f;
    m.appendFromAudioBlock(samples.data(), (int)samples.size());
    REQUIRE(m.currentPeakDbfs() > -10.0f);   // 0.5 ~ -6 dBFS
    REQUIRE(m.currentPeakDbfs() < -3.0f);
}

TEST_CASE("MicCapture: appendFromAudioBlock is allocation-free",
          "[audio][mic][rt]") {
    MicCapture m;
    m.prepare(48000.0, 1);
    m.beginCapture();
    std::vector<float> chunk(512, 0.1f);
    RealtimeSentinel rt;
    rt.markCurrentThreadAsRealtime();
    for (int i = 0; i < 100; ++i)
        m.appendFromAudioBlock(chunk.data(), (int)chunk.size());
    rt.unmarkCurrentThreadAsRealtime();
    REQUIRE(rt.violations() == 0);
}
