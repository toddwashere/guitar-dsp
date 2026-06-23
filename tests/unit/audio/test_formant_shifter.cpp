#include <catch2/catch_test_macros.hpp>
#include "audio/FormantShifter.h"
#include "audio/GrainAnalyser.h"
#include <cmath>
#include <memory>
#include <vector>

using guitar_dsp::audio::FormantShifter;
using guitar_dsp::audio::ShifterGrain;

namespace {
// A trivial 1-second 440 Hz sine ShifterGrain with hand-rolled spectrum.
// Real grains come from the offline analysis path (Task 3).
std::shared_ptr<ShifterGrain> makeSineGrain(int sr = 48000) {
    auto g = std::make_shared<ShifterGrain>();
    g->sampleRate    = sr;
    g->fftSize       = 2048;
    g->framePeriodMs = 5.0;
    const int frames = static_cast<int>(1000.0 / g->framePeriodMs);
    const int bins   = g->fftSize / 2 + 1;
    g->timeAxis.resize(frames);
    g->f0.resize(frames, 440.0);
    g->spectrum.assign(frames, std::vector<double>(bins, 1e-9));
    g->aperiodicity.assign(frames, std::vector<double>(bins, 0.1));
    for (int i = 0; i < frames; ++i) g->timeAxis[i] = i * g->framePeriodMs / 1000.0;
    return g;
}
}

TEST_CASE("FormantShifter ratio=1.0 over a stable grain produces non-silent output",
          "[formant-shifter]") {
    FormantShifter sh;
    sh.prepare(48000.0, 256);
    // C1 fix: setSource() now requires a pre-rendered grain (preRendered populated).
    // Call preRenderGrain() first (message-thread helper, allocates freely).
    sh.setSource(FormantShifter::preRenderGrain(makeSineGrain()));
    sh.setRatio(1.0f);
    std::vector<float> out(256, 0.0f);
    sh.process(out.data(), 256);
    double energy = 0.0;
    for (float v : out) {
        REQUIRE(! std::isnan(v));
        REQUIRE(! std::isinf(v));
        energy += static_cast<double>(v) * v;
    }
    CHECK(energy > 0.0);
}

TEST_CASE("FormantShifter clamps setRatio outside [0.25, 4.0]",
          "[formant-shifter]") {
    FormantShifter sh;
    sh.prepare(48000.0, 256);
    sh.setRatio(0.0f);
    sh.setRatio(10.0f);
    sh.setRatio(-5.0f);
    // No assertion on internal state; the contract is "no crash, no NaN".
    sh.setSource(FormantShifter::preRenderGrain(makeSineGrain()));
    std::vector<float> out(256, 0.0f);
    sh.process(out.data(), 256);
    for (float v : out) REQUIRE(! std::isnan(v));
}

TEST_CASE("FormantShifter reports finite latencySamples",
          "[formant-shifter]") {
    FormantShifter sh;
    sh.prepare(48000.0, 256);
    const int lat = sh.latencySamples();
    REQUIRE(lat >= 0);
    REQUIRE(lat < 48000);  // < 1 s
}

TEST_CASE("analyseGrain produces a non-empty ShifterGrain over a sine",
          "[grain-analyser]") {
    using namespace guitar_dsp::audio;
    const int sr = 48000;
    const int n  = sr;  // 1 s
    std::vector<float> samples(static_cast<std::size_t>(n), 0.0f);
    for (int i = 0; i < n; ++i)
        samples[i] = 0.3f * std::sin(2.0 * M_PI * 220.0 * i / sr);
    auto g = analyseGrain(samples.data(), n, sr);
    REQUIRE(g);
    CHECK(g->sampleRate == sr);
    CHECK(g->f0.size() > 0);
    CHECK(g->spectrum.size() == g->f0.size());
    CHECK(g->aperiodicity.size() == g->f0.size());
    // Expect detected F0 near 220 Hz on at least one frame.
    bool foundNear = false;
    for (double f : g->f0) if (std::fabs(f - 220.0) < 20.0) { foundNear = true; break; }
    CHECK(foundNear);
}
