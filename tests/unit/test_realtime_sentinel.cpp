#include <catch2/catch_test_macros.hpp>
#include "harness/RealtimeSentinel.h"

#include <thread>
#include <vector>

using guitar_dsp::tests::RealtimeSentinel;

TEST_CASE("sentinel: zero violations when no allocation occurs", "[harness]") {
    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();

    int x = 0;
    for (int i = 0; i < 1000; ++i) x += i;  // stack-only

    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}

TEST_CASE("sentinel: detects heap allocation on realtime thread", "[harness]") {
    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();

    {
        // Force a heap allocation in the marked region.
        auto* v = new std::vector<int>(16);
        delete v;
    }

    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() > 0);
}

TEST_CASE("sentinel: allocations on non-realtime thread do not count", "[harness]") {
    RealtimeSentinel sentinel;

    {
        auto* v = new std::vector<int>(16);
        delete v;
    }

    REQUIRE(sentinel.violations() == 0);
}
