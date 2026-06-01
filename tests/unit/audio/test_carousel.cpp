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

TEST_CASE("Carousel: reverb adds a decaying tail after input stops",
          "[audio][carousel]") {
    Carousel c;
    c.prepare(48000.0, 4096);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.reverbRoomSize = 0.7f;
    cfg.reverbWet = 0.6f;
    c.setConfig(cfg);

    auto in = tone(4096, 440.0f);
    std::vector<float> out(4096, 0.0f);
    c.process(in.data(), out.data(), out.size());

    std::vector<float> silence(4096, 0.0f), tail(4096, 0.0f);
    c.process(silence.data(), tail.data(), tail.size());
    float tailPeak = 0.0f;
    for (float x : tail) tailPeak = std::max(tailPeak, std::fabs(x));
    REQUIRE(tailPeak > 1e-3f);
}

TEST_CASE("Carousel: full preset stays finite and bounded on a pluck",
          "[audio][carousel]") {
    Carousel c;
    c.prepare(48000.0, 512);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.drive = 9.0f;
    cfg.shaper = CarouselConfig::Shaper::Tanh;
    cfg.shaperAmount = 1.2f;
    cfg.crusherBits = 6; cfg.crusherDownsample = 2;
    cfg.filterMode = CarouselConfig::FilterMode::BandPass;
    cfg.filterMod  = CarouselConfig::FilterMod::Envelope;
    cfg.filterCutoffHz = 500.0f; cfg.filterResonance = 0.85f;
    cfg.filterEnvAmount = 2500.0f;
    cfg.chorusRateHz = 4.0f; cfg.chorusDepth = 0.4f; cfg.chorusMix = 0.5f;
    cfg.reverbRoomSize = 0.5f; cfg.reverbWet = 0.3f;
    cfg.outputTrimDb = -2.0f;
    c.setConfig(cfg);

    std::vector<float> in(512), out(512);
    for (int blk = 0; blk < 100; ++blk) {
        const float amp = 0.8f * std::exp(-blk / 30.0f);
        for (int i = 0; i < 512; ++i)
            in[static_cast<size_t>(i)] =
                amp * std::sin(2.0f*3.14159265f*196.0f*(blk*512+i)/48000.0f);
        c.process(in.data(), out.data(), out.size());
        for (float x : out) {
            REQUIRE(std::isfinite(x));
            REQUIRE(std::fabs(x) <= 1.0f);   // brick-wall limiter ceiling
        }
    }
}

TEST_CASE("Carousel: output limiter bounds extreme presets to [-1,1]",
          "[audio][carousel]") {
    Carousel c;
    c.prepare(48000.0, 512);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.drive = 36.0f;          // absurd boost
    cfg.shaper = CarouselConfig::Shaper::None;   // no shaper to self-bound
    cfg.filterMode = CarouselConfig::FilterMode::BandPass;
    cfg.filterMod  = CarouselConfig::FilterMod::Static;
    cfg.filterCutoffHz = 440.0f;
    cfg.filterResonance = 0.95f; // max resonance
    cfg.reverbRoomSize = 0.9f; cfg.reverbWet = 0.9f;
    cfg.outputTrimDb = 24.0f;    // and crank the trim too
    c.setConfig(cfg);

    auto in = tone(512, 440.0f);
    std::vector<float> out(512, 0.0f);
    for (int blk = 0; blk < 20; ++blk) {
        c.process(in.data(), out.data(), out.size());
        for (float x : out) {
            REQUIRE(std::isfinite(x));
            REQUIRE(std::fabs(x) <= 1.0f);
        }
    }
}

TEST_CASE("Carousel: harmonizer raises pitched energy (choir-ish)",
          "[audio][carousel][pitch]") {
    Carousel c;
    c.prepare(48000.0, 512);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.harmVoiceCount = 2;
    cfg.harmSemitones[0] = 12; cfg.harmSemitones[1] = 7;
    cfg.harmMix = 0.9f;
    c.setConfig(cfg);

    auto in = tone(4096, 196.0f);
    std::vector<float> out(4096, 0.0f);
    for (int blk = 0; blk < 4; ++blk) c.process(in.data(), out.data(), out.size());
    for (float x : out) { REQUIRE(std::isfinite(x)); REQUIRE(std::fabs(x) <= 1.0f); }
    float peak = 0.0f;
    for (float x : out) peak = std::max(peak, std::fabs(x));
    REQUIRE(peak > 0.05f);
}

TEST_CASE("Carousel: comb adds resonant ring (piano-ish)", "[audio][carousel]") {
    Carousel c;
    c.prepare(48000.0, 512);
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.combFreqHz = 220.0f;
    cfg.combFeedback = 0.7f;
    cfg.combMix = 0.8f;
    c.setConfig(cfg);

    auto in = tone(512, 220.0f);
    std::vector<float> out(512, 0.0f);
    c.process(in.data(), out.data(), out.size());

    std::vector<float> silence(512, 0.0f), tail(512, 0.0f);
    c.process(silence.data(), tail.data(), tail.size());
    float tailPeak = 0.0f;
    for (float x : tail) tailPeak = std::max(tailPeak, std::fabs(x));
    REQUIRE(tailPeak > 1e-3f);
    for (float x : tail) { REQUIRE(std::isfinite(x)); REQUIRE(std::fabs(x) <= 1.0f); }
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
