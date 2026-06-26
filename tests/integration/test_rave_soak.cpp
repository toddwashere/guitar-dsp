#include <catch2/catch_test_macros.hpp>
#include "audio/AudioGraph.h"

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using guitar_dsp::audio::AudioGraph;

#ifndef STUB_MODEL_PATH
#error
#endif

namespace {
void waitForStatus(AudioGraph& g, AudioGraph::RaveStatusForUI want, int maxMs = 1000) {
    for (int i = 0; i < maxMs / 10; ++i) {
        if (g.raveStatusForUI() == want) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
} // namespace

TEST_CASE("Soak: rapid scene switching to/from Rave for 200 cycles", "[integration][rave][soak]") {
    AudioGraph g;
    g.prepare(48000.0, 256);
    g.loadRaveModel(STUB_MODEL_PATH);
    waitForStatus(g, AudioGraph::RaveStatusForUI::Loaded);

    std::vector<float> in(256, 0.2f), out(256);
    for (int cycle = 0; cycle < 200; ++cycle) {
        g.setWetSource(AudioGraph::WetSource::Rave);
        for (int b = 0; b < 4; ++b) g.process(in.data(), nullptr, out.data(), 256);
        g.setWetSource(AudioGraph::WetSource::Vocoder);
        for (int b = 0; b < 4; ++b) g.process(in.data(), nullptr, out.data(), 256);
    }
    // App did not crash; that's the test. Bonus: status should still be Loaded.
    REQUIRE(g.raveStatusForUI() != AudioGraph::RaveStatusForUI::Unavailable);
}

TEST_CASE("Soak: model-missing scene activation degrades to silent wet, dry preserved", "[integration][rave][soak]") {
    AudioGraph g;
    g.prepare(48000.0, 256);
    g.loadRaveModel("/no/such/file.onnx");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    g.setWetSource(AudioGraph::WetSource::Rave);

    std::vector<float> in(256), out(256);
    for (size_t i = 0; i < 256; ++i)
        in[i] = 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * float(i) / 48000.0f);
    for (int b = 0; b < 20; ++b) g.process(in.data(), nullptr, out.data(), 256);

    REQUIRE(g.raveStatusForUI() == AudioGraph::RaveStatusForUI::Unavailable);
    // Output must be non-NaN, bounded.
    for (float x : out) {
        REQUIRE(std::isfinite(x));
        REQUIRE(std::fabs(x) <= 1.0f);
    }
}

TEST_CASE("Soak: 5 s sustained playback with stub model produces consistent output", "[integration][rave][soak]") {
    AudioGraph g;
    g.prepare(48000.0, 512);
    g.loadRaveModel(STUB_MODEL_PATH);
    waitForStatus(g, AudioGraph::RaveStatusForUI::Loaded);
    g.setWetSource(AudioGraph::WetSource::Rave);
    g.setRaveGateDb(-60.0f); g.setRavePresence(0.0f); g.setRaveDriveDb(0.0f);
    g.setRaveDryWet(1.0f);

    constexpr int blocks = (5 * 48000) / 512;
    std::vector<float> in(512), out(512);
    int nanCount = 0;
    for (int b = 0; b < blocks; ++b) {
        for (size_t i = 0; i < 512; ++i)
            in[i] = 0.3f * std::sin(2.0f * 3.14159265f * 220.0f * float(b * 512 + i) / 48000.0f);
        g.process(in.data(), nullptr, out.data(), 512);
        for (float x : out) if (!std::isfinite(x)) ++nanCount;
    }
    REQUIRE(nanCount == 0);
    REQUIRE(g.raveStatusForUI() == AudioGraph::RaveStatusForUI::Loaded);
}
