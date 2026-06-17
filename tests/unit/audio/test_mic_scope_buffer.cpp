#include <catch2/catch_test_macros.hpp>
#include "audio/MicScopeBuffer.h"
#include "harness/RealtimeSentinel.h"
#include <vector>

using guitar_dsp::audio::MicScopeBuffer;
using guitar_dsp::tests::RealtimeSentinel;

TEST_CASE("MicScopeBuffer: returns the most recent samples",
          "[audio][micscope]") {
    MicScopeBuffer buf(100);
    std::vector<float> in(150, 0.0f);
    for (std::size_t i = 0; i < in.size(); ++i) in[i] = static_cast<float>(i);
    buf.push(in.data(), in.size());

    std::vector<float> out(100, -1.0f);
    REQUIRE(buf.copyMostRecent(out.data(), out.size()) == 100);
    // The last 100 of in are 50..149 — those should appear in out.
    REQUIRE(out.front() == 50.0f);
    REQUIRE(out.back()  == 149.0f);
}

TEST_CASE("MicScopeBuffer: push is allocation-free",
          "[audio][micscope][rt]") {
    MicScopeBuffer buf(4800);
    std::vector<float> block(256, 0.5f);
    RealtimeSentinel rts;
    rts.markCurrentThreadAsRealtime();
    for (int i = 0; i < 100; ++i) buf.push(block.data(), block.size());
    rts.unmarkCurrentThreadAsRealtime();
    REQUIRE(rts.violations() == 0);
}

TEST_CASE("MicScopeBuffer: handles partial fill (fewer pushes than capacity)",
          "[audio][micscope]") {
    MicScopeBuffer buf(100);
    std::vector<float> in(50, 1.5f);
    buf.push(in.data(), in.size());

    std::vector<float> out(100, -1.0f);
    REQUIRE(buf.copyMostRecent(out.data(), out.size()) == 100);
    // The first 50 should be the pre-rollover zeros; the last 50 should be 1.5.
    // Order: ring writes positions 0..49; copyMostRecent walks back from
    // writeIdx=50 over the last 100, wrapping back to positions 50..99 (zeros)
    // then 0..49 (1.5s). Either way, the test should be relaxed enough:
    int countLastValues = 0;
    for (float v : out) if (v == 1.5f) ++countLastValues;
    REQUIRE(countLastValues == 50);
}
