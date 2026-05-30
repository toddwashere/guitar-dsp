#include <catch2/catch_test_macros.hpp>

TEST_CASE("smoke: arithmetic works", "[smoke]") {
    REQUIRE(2 + 2 == 4);
}
