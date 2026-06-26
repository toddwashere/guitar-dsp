#include <catch2/catch_test_macros.hpp>
#include "audio/RaveSynthesizer.h"

#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

using guitar_dsp::audio::RaveSynthesizer;
using guitar_dsp::audio::RaveBranchStatus;

#ifndef STUB_MODEL_PATH
#error "STUB_MODEL_PATH must be defined by test build"
#endif

namespace {
void runBlocks(RaveSynthesizer& syn, std::size_t blocks, std::size_t bs, float amp = 0.3f) {
    std::vector<float> in(bs), out(bs);
    for (std::size_t b = 0; b < blocks; ++b) {
        for (std::size_t i = 0; i < bs; ++i)
            in[i] = amp * std::sin(2.0f * 3.14159265f * 220.0f * float(b * bs + i) / 48000.0f);
        syn.processBlock(in.data(), out.data(), bs);
    }
}
} // namespace

TEST_CASE("RaveSynthesizer: missing model -> Unavailable", "[audio][rave-synth][status]") {
    RaveSynthesizer syn;
    syn.prepare(48000.0, 512);
    syn.loadModel("/nope.onnx");
    // Allow background init to settle.
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    REQUIRE(syn.status() == RaveBranchStatus::Unavailable);
}

TEST_CASE("RaveSynthesizer: stub model -> Loaded after process", "[audio][rave-synth][status]") {
    RaveSynthesizer syn;
    syn.prepare(48000.0, 512);
    syn.loadModel(STUB_MODEL_PATH);
    // Wait for background thread to flag Loaded.
    for (int i = 0; i < 50 && syn.status() != RaveBranchStatus::Loaded; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(syn.status() == RaveBranchStatus::Loaded);
}

TEST_CASE("RaveSynthesizer: stub model passes audio through (after warm-up)", "[audio][rave-synth][audio]") {
    RaveSynthesizer syn;
    syn.prepare(48000.0, 512);
    syn.loadModel(STUB_MODEL_PATH);
    for (int i = 0; i < 50 && syn.status() != RaveBranchStatus::Loaded; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

    syn.setGateDb(-60.0f); syn.setPresence(0.0f); syn.setDriveDb(0.0f);
    // Push enough audio to fill the input ring + complete one inference window.
    runBlocks(syn, /*blocks=*/40, /*bs=*/512);

    REQUIRE(syn.inputRms() > 0.01f);
    REQUIRE(syn.outputRms() > 0.0f);  // identity model + audio in -> audio out
}

TEST_CASE("RaveSynthesizer: scene activation after long quiet period does not spuriously stall",
          "[audio][rave-synth][status]") {
    RaveSynthesizer syn;
    syn.prepare(48000.0, 512);
    syn.loadModel(STUB_MODEL_PATH);
    for (int i = 0; i < 50 && syn.status() != RaveBranchStatus::Loaded; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(syn.status() == RaveBranchStatus::Loaded);

    // Simulate scene 5 being inactive for >500 ms (no processBlock calls).
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    // Now activate: push a single block. The watchdog must NOT fire on this
    // call just because lastOutputReadMs is now far in the past.
    std::vector<float> in(512), out(512);
    for (size_t i = 0; i < 512; ++i)
        in[i] = 0.3f * std::sin(2.0f * 3.14159265f * 220.0f * float(i) / 48000.0f);
    syn.processBlock(in.data(), out.data(), 512);

    // Status should still be Loaded (not Stalled).
    REQUIRE(syn.status() == RaveBranchStatus::Loaded);
}

TEST_CASE("RaveSynthesizer: Unavailable produces silent wet output", "[audio][rave-synth][audio]") {
    RaveSynthesizer syn;
    syn.prepare(48000.0, 512);
    syn.loadModel("/nope.onnx");
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::vector<float> in(512, 0.5f), out(512, -1.0f);
    syn.processBlock(in.data(), out.data(), 512);
    for (float x : out) REQUIRE(x == 0.0f);
}
