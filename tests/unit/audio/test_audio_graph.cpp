#include <catch2/catch_test_macros.hpp>
#include "audio/AudioGraph.h"
#include "harness/SyntheticGuitar.h"
#include "harness/RealtimeSentinel.h"

#include <vector>

using guitar_dsp::audio::AudioGraph;
using guitar_dsp::tests::SyntheticGuitar;
using guitar_dsp::tests::RealtimeSentinel;

TEST_CASE("AudioGraph: produces non-silent output for sine input", "[audio][graph]") {
    AudioGraph graph;
    graph.prepare(48000.0, 512);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(512), out(512);
    gen.sine(440.0f, 0.4f, in.data(), in.size());

    graph.process(in.data(), out.data(), out.size());

    float peak = 0.0f;
    for (float s : out) peak = std::max(peak, std::abs(s));
    REQUIRE(peak > 0.05f);
}

TEST_CASE("AudioGraph: silence in -> silence out", "[audio][graph]") {
    AudioGraph graph;
    graph.prepare(48000.0, 512);

    std::vector<float> in(512, 0.0f), out(512);
    graph.process(in.data(), out.data(), out.size());

    for (float s : out) REQUIRE(std::abs(s) < 1e-3f);
}

TEST_CASE("AudioGraph: zero allocations on audio thread", "[audio][graph][realtime]") {
    AudioGraph graph;
    graph.prepare(48000.0, 512);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(512), out(512);
    gen.sine(440.0f, 0.4f, in.data(), in.size());

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int i = 0; i < 938; ++i)
        graph.process(in.data(), out.data(), out.size());
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
