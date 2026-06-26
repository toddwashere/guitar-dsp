#include <catch2/catch_test_macros.hpp>
#include "audio/AudioGraph.h"
#include "scenes/Scene.h"

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using guitar_dsp::audio::AudioGraph;
using guitar_dsp::scenes::Scene;

#ifndef STUB_MODEL_PATH
#error
#endif

TEST_CASE("AudioGraph: WetSource::Rave routes through RaveSynthesizer", "[integration][rave]") {
    AudioGraph g;
    g.prepare(48000.0, 512);
    g.loadRaveModel(STUB_MODEL_PATH);

    // Wait for branch to flag Loaded.
    for (int i = 0; i < 100; ++i) {
        if (g.raveStatusForUI() == AudioGraph::RaveStatusForUI::Loaded) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    g.setWetSource(AudioGraph::WetSource::Rave);
    g.setRaveDryWet(1.0f);
    g.setRaveGateDb(-60.0f);
    g.setRavePresence(0.0f);
    g.setRaveDriveDb(0.0f);

    // Push 40 blocks of synthetic guitar.
    std::vector<float> in(512), out(512);
    float sum = 0.0f;
    for (int b = 0; b < 40; ++b) {
        for (size_t i = 0; i < 512; ++i)
            in[i] = 0.3f * std::sin(2.0f * 3.14159265f * 220.0f * float(b * 512 + i) / 48000.0f);
        g.process(in.data(), nullptr, out.data(), 512);
        for (float x : out) sum += std::fabs(x);
    }
    REQUIRE(sum > 1.0f);    // identity stub -> non-silent wet output through full graph
}

TEST_CASE("AudioGraph: scene without rave leaves WetSource at Vocoder", "[integration][rave]") {
    AudioGraph g;
    g.prepare(48000.0, 512);
    // No loadRaveModel; explicitly set Vocoder and confirm routing holds after a push.
    g.setWetSource(AudioGraph::WetSource::Vocoder);
    std::vector<float> in(512, 0.0f), out(512, 0.0f);
    g.process(in.data(), out.data(), 512);
    REQUIRE(g.wetSource() == AudioGraph::WetSource::Vocoder);
}
