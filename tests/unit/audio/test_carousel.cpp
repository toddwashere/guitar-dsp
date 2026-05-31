#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "audio/Carousel.h"
#include "scenes/Scene.h"
#include "harness/RealtimeSentinel.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <vector>

using Catch::Matchers::WithinAbs;
using guitar_dsp::audio::Carousel;
using guitar_dsp::scenes::CarouselConfig;
using guitar_dsp::tests::RealtimeSentinel;

namespace {
std::vector<float> tone(int n, float hz) {
    std::vector<float> b(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        b[static_cast<size_t>(i)] = 0.5f * std::sin(2.0f*3.14159265f*hz*i/48000.0f);
    return b;
}
}

TEST_CASE("Carousel: disabled config is passthrough", "[audio][carousel]") {
    Carousel c;
    c.prepare(48000.0, 512);
    CarouselConfig cfg;  // enabled == false
    c.setConfig(cfg);

    auto in = tone(512, 220.0f);
    std::vector<float> out(512, 0.0f);
    c.process(in.data(), out.data(), out.size());
    for (size_t i = 0; i < out.size(); ++i)
        REQUIRE_THAT(out[i], WithinAbs(in[i], 1e-6f));
}

TEST_CASE("Carousel: hardclip waveshaper bounds output", "[audio][carousel]") {
    Carousel c;
    c.prepare(48000.0, 512);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.drive = 24.0f;
    cfg.shaper = CarouselConfig::Shaper::HardClip;
    cfg.shaperAmount = 1.0f;
    c.setConfig(cfg);

    auto in = tone(2048, 110.0f);
    std::vector<float> out(2048, 0.0f);
    c.process(in.data(), out.data(), out.size());
    c.process(in.data(), out.data(), out.size());

    float peak = 0.0f;
    for (float x : out) peak = std::max(peak, std::fabs(x));
    REQUIRE(peak <= 1.001f);
    REQUIRE(peak > 0.5f);
}

TEST_CASE("Carousel: crusher reduces distinct output levels", "[audio][carousel]") {
    Carousel c;
    c.prepare(48000.0, 1024);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.crusherBits = 3;
    cfg.crusherDownsample = 1;
    c.setConfig(cfg);

    auto in = tone(1024, 220.0f);
    std::vector<float> out(1024, 0.0f);
    c.process(in.data(), out.data(), out.size());

    std::set<int> levels;
    for (float x : out) levels.insert(static_cast<int>(std::lround(x * 100.0f)));
    REQUIRE(levels.size() <= 12);
}

TEST_CASE("Carousel: lowpass attenuates a high tone", "[audio][carousel]") {
    Carousel c;
    c.prepare(48000.0, 4096);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.filterMode = CarouselConfig::FilterMode::LowPass;
    cfg.filterMod  = CarouselConfig::FilterMod::Static;
    cfg.filterCutoffHz = 400.0f;
    cfg.filterResonance = 0.3f;
    c.setConfig(cfg);

    auto hi = tone(4096, 5000.0f);
    std::vector<float> out(4096, 0.0f);
    c.process(hi.data(), out.data(), out.size());
    c.process(hi.data(), out.data(), out.size());

    float peak = 0.0f;
    for (float x : out) peak = std::max(peak, std::fabs(x));
    REQUIRE(peak < 0.25f);
}

TEST_CASE("Carousel: envelope-modulated bandpass produces finite output (auto-wah)",
          "[audio][carousel]") {
    Carousel c;
    c.prepare(48000.0, 4096);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.filterMode = CarouselConfig::FilterMode::BandPass;
    cfg.filterMod  = CarouselConfig::FilterMod::Envelope;
    cfg.filterCutoffHz = 300.0f;
    cfg.filterResonance = 0.7f;
    cfg.filterEnvAmount = 2000.0f;
    c.setConfig(cfg);

    auto in = tone(4096, 440.0f);
    std::vector<float> out(4096, 0.0f);
    c.process(in.data(), out.data(), out.size());
    c.process(in.data(), out.data(), out.size());
    for (float x : out) { REQUIRE(std::isfinite(x)); REQUIRE(std::fabs(x) < 4.0f); }
}

TEST_CASE("Carousel: process is allocation-free", "[audio][carousel][rt]") {
    Carousel c;
    c.prepare(48000.0, 512);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.drive = 6.0f;
    cfg.shaper = CarouselConfig::Shaper::Tanh;
    cfg.filterMode = CarouselConfig::FilterMode::LowPass;
    cfg.filterMod = CarouselConfig::FilterMod::Envelope;
    cfg.chorusRateHz = 5.0f; cfg.chorusMix = 0.4f;
    cfg.reverbRoomSize = 0.4f; cfg.reverbWet = 0.2f;
    c.setConfig(cfg);

    auto in = tone(512, 220.0f);
    std::vector<float> out(512, 0.0f);
    c.process(in.data(), out.data(), out.size());  // pick up config

    RealtimeSentinel sentinel;
    sentinel.markCurrentThreadAsRealtime();
    for (int blk = 0; blk < 50; ++blk)
        c.process(in.data(), out.data(), out.size());
    sentinel.unmarkCurrentThreadAsRealtime();
    REQUIRE(sentinel.violations() == 0);
}
