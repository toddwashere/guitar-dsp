#include <catch2/catch_test_macros.hpp>
#include "audio/MicShaper.h"
#include "harness/RealtimeSentinel.h"

#include <algorithm>
#include <cmath>
#include <vector>

using guitar_dsp::audio::MicShaper;
using guitar_dsp::tests::RealtimeSentinel;

TEST_CASE("MicShaper: near-silence below threshold is attenuated",
          "[audio][mic_shaper]") {
    MicShaper s;
    s.prepare(48000.0);

    // Feed 1 second of near-silence below the -52 dBFS gate threshold.
    constexpr std::size_t N = 48000;
    std::vector<float> in(N, 0.0005f);   // ~-66 dBFS, well below threshold
    std::vector<float> out(N, 1.0f);
    s.process(in.data(), out.data(), N);

    // After the gate's release time settles (last quarter), output should be
    // very small — close to zero, not amplified by makeup gain.
    float maxTail = 0.0f;
    for (std::size_t i = N * 3 / 4; i < N; ++i)
        maxTail = std::max(maxTail, std::fabs(out[i]));
    REQUIRE(maxTail < 0.001f);
}

TEST_CASE("MicShaper: signal above threshold passes through with makeup gain",
          "[audio][mic_shaper]") {
    MicShaper s;
    s.prepare(48000.0);

    // Feed 1 second of a 0.05 amplitude sine wave (~-26 dBFS, well above
    // the -52 dBFS gate threshold).
    constexpr std::size_t N = 48000;
    std::vector<float> in(N), out(N);
    for (std::size_t i = 0; i < N; ++i)
        in[i] = 0.05f * std::sin(2.0f * 3.14159265f * 200.0f * i / 48000.0f);

    s.process(in.data(), out.data(), N);

    // After the gate's attack settles, peak should be ~0.05 × 4.0 = 0.2.
    // Look at the last quarter to skip the attack ramp.
    float peakTail = 0.0f;
    for (std::size_t i = N * 3 / 4; i < N; ++i)
        peakTail = std::max(peakTail, std::fabs(out[i]));
    REQUIRE(peakTail > 0.15f);
    REQUIRE(peakTail < 0.25f);
}

TEST_CASE("MicShaper: output hard-limited to +/-1.0 when input is loud",
          "[audio][mic_shaper]") {
    MicShaper s;
    s.prepare(48000.0);

    // Full-scale square-wave input. Times 4.0 makeup gain = ±4.0, must clip
    // to ±1.0.
    constexpr std::size_t N = 48000;
    std::vector<float> in(N), out(N);
    for (std::size_t i = 0; i < N; ++i)
        in[i] = (i % 100 < 50) ? 1.0f : -1.0f;

    s.process(in.data(), out.data(), N);

    for (std::size_t i = N * 3 / 4; i < N; ++i)
        REQUIRE(std::fabs(out[i]) <= 1.0f);
}

TEST_CASE("MicShaper: process is allocation-free",
          "[audio][mic_shaper][rt]") {
    MicShaper s;
    s.prepare(48000.0);

    constexpr std::size_t N = 512;
    std::vector<float> in(N), out(N);
    for (std::size_t i = 0; i < N; ++i)
        in[i] = 0.1f * std::sin(2.0f * 3.14159265f * 220.0f * i / 48000.0f);

    // Warm up once before locking allocation.
    s.process(in.data(), out.data(), N);

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int blk = 0; blk < 50; ++blk)
        s.process(in.data(), out.data(), N);
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
