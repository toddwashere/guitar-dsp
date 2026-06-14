#include <catch2/catch_test_macros.hpp>

#include "audio/AudioGraph.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::AudioGraph;

TEST_CASE("Mic-talkbox scene: ModulatorSource::Mic produces vocoded output end-to-end",
          "[integration][mic][scene]") {
    AudioGraph g;
    g.prepare(48000.0, 512);

    g.setModulatorSource(AudioGraph::ModulatorSource::Mic);
    g.mixer().setDryWet(1.0f);

    // 2 seconds of carrier (guitar) + mic at 512-sample blocks.
    constexpr std::size_t blockSize = 512;
    constexpr std::size_t blocks    = 200;
    constexpr std::size_t N         = blockSize * blocks;

    std::vector<float> carrier(N), mic(N), out(N);
    for (std::size_t i = 0; i < N; ++i) {
        carrier[i] = 0.5f * std::sin(2.0f * 3.14159265f * 220.0f * i / 48000.0f);
        mic[i]     = 0.3f * std::sin(2.0f * 3.14159265f * 880.0f * i / 48000.0f);
    }

    for (std::size_t b = 0; b < blocks; ++b) {
        g.setMicBlock(mic.data() + b * blockSize, blockSize);
        g.process(carrier.data() + b * blockSize,
                  out.data() + b * blockSize,
                  blockSize);
    }

    // After the vocoder envelope followers settle, the wet bus must carry
    // nonzero output. Look at the last 200 ms.
    float peak = 0.0f;
    for (std::size_t i = N - 9600; i < N; ++i)
        peak = std::max(peak, std::fabs(out[i]));
    REQUIRE(peak > 0.001f);

    // micPeak() should track the mic input.
    REQUIRE(g.micPeak() > 0.1f);
}

TEST_CASE("Mic-talkbox scene: silent mic produces near-silent wet output",
          "[integration][mic][scene]") {
    AudioGraph g;
    g.prepare(48000.0, 512);

    g.setModulatorSource(AudioGraph::ModulatorSource::Mic);
    g.mixer().setDryWet(1.0f);

    constexpr std::size_t N = 48000;  // 1 second
    std::vector<float> carrier(N), out(N);
    for (std::size_t i = 0; i < N; ++i)
        carrier[i] = 0.5f * std::sin(2.0f * 3.14159265f * 220.0f * i / 48000.0f);

    std::vector<float> silentMic(512, 0.0f);

    for (std::size_t b = 0; b < N / 512; ++b) {
        g.setMicBlock(silentMic.data(), 512);
        g.process(carrier.data() + b * 512, out.data() + b * 512, 512);
    }

    // With no mic excitation, the modulator is silent → vocoder output near
    // zero. (Some bleed from the carrier-noise floor is possible — keep the
    // threshold loose.)
    float peakTail = 0.0f;
    for (std::size_t i = N - 9600; i < N; ++i)
        peakTail = std::max(peakTail, std::fabs(out[i]));
    REQUIRE(peakTail < 0.05f);
}
