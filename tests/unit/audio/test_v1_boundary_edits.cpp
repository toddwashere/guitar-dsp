#include "audio/V1BoundaryEdits.h"

#include <catch2/catch_test_macros.hpp>

using guitar_dsp::audio::addBoundaryV1;
using guitar_dsp::audio::moveBoundaryV1;
using guitar_dsp::audio::removeBoundaryV1;
using guitar_dsp::audio::WordSegment;

namespace {
std::vector<WordSegment> threeSegs() {
    return {
        {"a", 0,    1000},
        {"b", 1000, 2000},
        {"c", 2000, 3000},
    };
}
} // namespace

TEST_CASE("moveBoundaryV1 moves an interior boundary", "[audio][v1edit]") {
    auto segs = threeSegs();
    auto pos = moveBoundaryV1(segs, 1, 1500);
    REQUIRE(pos == 1500);
    REQUIRE(segs[0].endSample == 1500);
    REQUIRE(segs[1].startSample == 1500);
}

TEST_CASE("moveBoundaryV1 clamps to minWidth", "[audio][v1edit]") {
    auto segs = threeSegs();
    auto pos = moveBoundaryV1(segs, 1, 999990, 240);
    REQUIRE(pos == segs[1].endSample - 240);
}

TEST_CASE("addBoundaryV1 splits a segment", "[audio][v1edit]") {
    auto segs = threeSegs();
    REQUIRE(addBoundaryV1(segs, 1500));
    REQUIRE(segs.size() == 4);
    REQUIRE(segs[1].word == segs[2].word);
    REQUIRE(segs[1].endSample == 1500);
    REQUIRE(segs[2].startSample == 1500);
}

TEST_CASE("addBoundaryV1 rejects insertion too close to a boundary",
          "[audio][v1edit]") {
    auto segs = threeSegs();
    REQUIRE_FALSE(addBoundaryV1(segs, 1050, 100));
}

TEST_CASE("removeBoundaryV1 merges two segments", "[audio][v1edit]") {
    auto segs = threeSegs();
    REQUIRE(removeBoundaryV1(segs, 1));
    REQUIRE(segs.size() == 2);
    REQUIRE(segs[0].word == "a");
    REQUIRE(segs[0].startSample == 0);
    REQUIRE(segs[0].endSample == 2000);
}

TEST_CASE("removeBoundaryV1 rejects non-interior index", "[audio][v1edit]") {
    auto segs = threeSegs();
    REQUIRE_FALSE(removeBoundaryV1(segs, 0));
    REQUIRE_FALSE(removeBoundaryV1(segs, segs.size()));
}
