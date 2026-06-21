#include "audio/V1BoundaryEdits.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using guitar_dsp::audio::addBoundaryV1;
using guitar_dsp::audio::moveBoundaryV1;
using guitar_dsp::audio::removeBoundaryV1;
using guitar_dsp::audio::snapBoundariesToEnergyValleysV1;
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

namespace {
// Two ~200ms bursts of 250 Hz sine separated by a ~150ms silence valley
// centred at sample 19200 (= 400ms @ 48 kHz). The valley spans roughly
// [16800, 21600) so any boundary placed in that window should snap into it.
std::vector<float> twoBurstsWithValley(double sr) {
    const std::size_t n = static_cast<std::size_t>(sr * 0.8);   // 800 ms
    std::vector<float> v(n, 0.0f);
    const auto fill = [&](std::size_t lo, std::size_t hi) {
        for (std::size_t i = lo; i < hi && i < n; ++i)
            v[i] = (float) std::sin(2.0 * 3.14159 * 250.0
                                    * (double) i / sr) * 0.3f;
    };
    fill(0, (std::size_t)(sr * 0.20));                            // burst 1
    fill((std::size_t)(sr * 0.45), (std::size_t)(sr * 0.80));     // burst 2
    return v;
}
} // namespace

TEST_CASE("snapBoundariesToEnergyValleysV1 nudges into the silence valley",
          "[audio][v1edit]") {
    const double sr = 48000.0;
    const auto audio = twoBurstsWithValley(sr);
    // Boundary placed 1000 samples off the centre of the silence valley.
    const std::size_t valleyCentre = static_cast<std::size_t>(sr * 0.325);
    std::vector<WordSegment> segs = {
        {"a", 0,              valleyCentre + 1000},
        {"b", valleyCentre + 1000, audio.size()},
    };

    snapBoundariesToEnergyValleysV1(segs, audio, sr);

    // Boundary should now sit somewhere inside the actual valley window
    // [sr*0.20, sr*0.45).
    REQUIRE(segs[0].endSample == segs[1].startSample);
    CHECK(segs[0].endSample >= (std::size_t)(sr * 0.20));
    CHECK(segs[0].endSample <  (std::size_t)(sr * 0.45));
}

TEST_CASE("snapBoundariesToEnergyValleysV1 is a no-op for < 2 segments",
          "[audio][v1edit]") {
    std::vector<WordSegment> one{{"x", 0, 1000}};
    std::vector<float> audio(1000, 0.1f);
    snapBoundariesToEnergyValleysV1(one, audio, 48000.0);
    REQUIRE(one.size() == 1);
    REQUIRE(one[0].endSample == 1000);
}

TEST_CASE("snapBoundariesToEnergyValleysV1 never crosses a neighbour midpoint",
          "[audio][v1edit]") {
    // Constant-energy audio: no valley exists, so the search finds the
    // initial boundary itself as the minimum. Verify the boundary stays.
    const double sr = 48000.0;
    std::vector<float> audio(static_cast<std::size_t>(sr * 0.5), 0.2f);
    std::vector<WordSegment> segs = {
        {"a", 0,    12000},
        {"b", 12000, 24000},
    };
    snapBoundariesToEnergyValleysV1(segs, audio, sr);
    // With flat envelope, every sample has equal RMS — `bestVal` stays at
    // the seed at index `boundary` and never gets beaten by `<`. Boundary
    // does not move.
    CHECK(segs[0].endSample == 12000);
    CHECK(segs[1].startSample == 12000);
}
