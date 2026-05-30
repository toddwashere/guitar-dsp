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
        // The `volatile` storage and `g_sink` write prevent the optimizer
        // from eliding the new/delete pair under -O3.
        std::vector<int>* volatile v = new std::vector<int>(16);
        v->at(0) = 1;
        delete v;
    }

    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() > 0);
}

TEST_CASE("sentinel: allocations on non-realtime thread do not count", "[harness]") {
    RealtimeSentinel sentinel;

    {
        // volatile + side-effect prevent the optimizer from eliding the
        // allocation, ensuring this test really exercises the not-marked
        // code path rather than passing vacuously.
        std::vector<int>* volatile v = new std::vector<int>(16);
        v->at(0) = 1;
        delete v;
    }

    REQUIRE(sentinel.violations() == 0);
}
