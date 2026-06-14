#include <catch2/catch_test_macros.hpp>
#include "audio/MicShaper.h"

#include <algorithm>
#include <cmath>
#include <vector>

using guitar_dsp::audio::MicShaper;

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
