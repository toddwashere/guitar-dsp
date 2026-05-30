#include <catch2/catch_test_macros.hpp>
#include "audio/AudioGraph.h"
#include "harness/RealtimeSentinel.h"
#include "harness/SyntheticGuitar.h"

#include <vector>

using guitar_dsp::audio::AudioGraph;
using guitar_dsp::tests::RealtimeSentinel;
using guitar_dsp::tests::SyntheticGuitar;

TEST_CASE("integration: 60 s of audio thread activity is allocation-free", "[integration][realtime][slow]") {
    constexpr double sr = 48000.0;
    constexpr std::size_t block = 512;
    constexpr int blocks = static_cast<int>(60.0 * sr / block);  // ~5625 blocks

    AudioGraph graph;
    graph.prepare(sr, static_cast<int>(block));

    SyntheticGuitar gen{sr};
    std::vector<float> in(block), out(block);

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int i = 0; i < blocks; ++i) {
        // Vary input pattern so we hit different code paths.
        if (i % 100 == 0) gen.silence(in.data(), in.size());
        else if (i % 50 == 0) gen.sweep(80.0f, 8000.0f, 0.4f, in.data(), in.size());
        else gen.sine(440.0f, 0.4f, in.data(), in.size());

        graph.process(in.data(), out.data(), in.size());
    }
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
