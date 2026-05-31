#include <catch2/catch_test_macros.hpp>
#include "audio/AudioGraph.h"
#include "audio/TTSClip.h"
#include "scenes/Scene.h"
#include "harness/SyntheticGuitar.h"
#include "harness/RealtimeSentinel.h"

#include <algorithm>
#include <cmath>
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

TEST_CASE("AudioGraph: with TTS clip active + dryWet=1, output is vocoded",
          "[audio][graph][vocoder]") {
    AudioGraph graph;
    graph.prepare(48000.0, 512);
    graph.mixer().setDryWet(1.0f);
    graph.mixer().setMasterGainDb(0.0f);
    graph.mixer().reset();

    // Inject a clip: 0.5 s of 800 Hz modulator.
    auto clip = std::make_shared<guitar_dsp::audio::TTSClip>();
    clip->sampleRate = 48000.0;
    clip->samples.resize(24000);
    for (int i = 0; i < 24000; ++i) {
        clip->samples[i] = 0.6f * std::sin(2.0 * 3.14159265 * 800.0 * i / 48000.0);
    }
    graph.ttsClipPlayer().setClip(clip);

    SyntheticGuitar gen{48000.0};
    std::vector<float> in(48000), out(48000);
    gen.sine(800.0f, 0.6f, in.data(), in.size());

    // Process the full second in 512-sample blocks.
    for (std::size_t i = 0; i < in.size(); i += 512) {
        const auto n = std::min<std::size_t>(512, in.size() - i);
        graph.process(in.data() + i, out.data() + i, n);
    }

    // First half: vocoder active -> output non-silent.
    float peakFirst = 0.0f;
    for (int i = 6000; i < 12000; ++i) peakFirst = std::max(peakFirst, std::abs(out[i]));
    REQUIRE(peakFirst > 0.05f);

    // Second half: clip ended -> modulator silent -> output near silent.
    float peakSecond = 0.0f;
    for (int i = 40000; i < 48000; ++i) peakSecond = std::max(peakSecond, std::abs(out[i]));
    REQUIRE(peakSecond < 0.02f);
}

TEST_CASE("AudioGraph: carousel wet-source transforms the guitar",
          "[audio][graph][carousel]") {
    using guitar_dsp::scenes::CarouselConfig;

    AudioGraph g;
    g.prepare(48000.0, 512);
    g.mixer().setDryWet(1.0f);
    g.mixer().setMasterGainDb(0.0f);
    g.mixer().reset();

    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.shaper = CarouselConfig::Shaper::HardClip;
    cfg.drive = 24.0f;
    g.carousel().setConfig(cfg);
    g.setWetSource(AudioGraph::WetSource::Carousel);

    std::vector<float> in(512), out(512);
    for (int i = 0; i < 512; ++i)
        in[static_cast<size_t>(i)] = 0.9f * std::sin(2.0f*3.14159265f*110.0f*i/48000.0f);

    g.process(in.data(), out.data(), out.size());
    g.process(in.data(), out.data(), out.size());

    float peak = 0.0f;
    for (float x : out) peak = std::max(peak, std::fabs(x));
    REQUIRE(peak <= 1.05f);
    REQUIRE(peak > 0.3f);
}
