#include <catch2/catch_test_macros.hpp>
#include "ai/CancellationToken.h"

using guitar_dsp::ai::CancellationToken;

TEST_CASE("CancellationToken: starts unset", "[ai][cancel]") {
    CancellationToken t;
    REQUIRE_FALSE(t.isCancelled());
}

TEST_CASE("CancellationToken: cancel sets the flag", "[ai][cancel]") {
    CancellationToken t;
    t.cancel();
    REQUIRE(t.isCancelled());
}

TEST_CASE("CancellationToken: reset clears the flag", "[ai][cancel]") {
    CancellationToken t;
    t.cancel();
    t.reset();
    REQUIRE_FALSE(t.isCancelled());
}

TEST_CASE("CancellationToken: cancel is idempotent", "[ai][cancel]") {
    CancellationToken t;
    t.cancel();
    t.cancel();
    REQUIRE(t.isCancelled());
}
