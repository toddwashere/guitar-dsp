#include <catch2/catch_test_macros.hpp>

#include "audio/AudioGraph.h"
#include "scenes/Scene.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::AudioGraph;
using guitar_dsp::scenes::CarouselConfig;

namespace {
std::vector<float> pluckish(int n) {
    std::vector<float> b(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const float amp = 0.8f * std::exp(-i / 6000.0f);
        b[static_cast<size_t>(i)] = amp * std::sin(2.0f*3.14159265f*146.83f*i/48000.0f);
    }
    return b;
}
float energy(const std::vector<float>& x) {
    double a = 0.0; for (float v : x) a += static_cast<double>(v)*v;
    return static_cast<float>(a);
}
}

TEST_CASE("integration: each carousel preset transforms the guitar, stays finite",
          "[integration][carousel]") {
    AudioGraph g;
    g.prepare(48000.0, 1024);

    auto in = pluckish(1024);

    g.setWetSource(AudioGraph::WetSource::Vocoder);
    g.mixer().setDryWet(0.0f); g.mixer().setMasterGainDb(0.0f); g.mixer().reset();
    std::vector<float> clean(1024, 0.0f);
    g.process(in.data(), clean.data(), clean.size());

    struct Preset { CarouselConfig cfg; const char* name; };
    std::vector<Preset> presets;
    {
        CarouselConfig c; c.enabled = true; c.crusherBits = 4; c.crusherDownsample = 8;
        c.shaper = CarouselConfig::Shaper::HardClip; presets.push_back({c, "8bit"});
    }
    {
        CarouselConfig c; c.enabled = true; c.drive = 18.0f;
        c.shaper = CarouselConfig::Shaper::HardClip; presets.push_back({c, "distortion"});
    }
    {
        CarouselConfig c; c.enabled = true; c.filterMode = CarouselConfig::FilterMode::BandPass;
        c.filterMod = CarouselConfig::FilterMod::Envelope; c.filterCutoffHz = 350.0f;
        c.filterResonance = 0.8f; c.filterEnvAmount = 2200.0f; presets.push_back({c, "autowah"});
    }

    for (auto& p : presets) {
        g.carousel().setConfig(p.cfg);
        g.setWetSource(AudioGraph::WetSource::Carousel);
        g.mixer().setDryWet(1.0f); g.mixer().reset();

        std::vector<float> out(1024, 0.0f);
        g.process(in.data(), out.data(), out.size());
        g.process(in.data(), out.data(), out.size());

        for (float x : out) REQUIRE(std::isfinite(x));
        INFO("preset = " << p.name);
        REQUIRE(energy(out) != energy(clean));
    }
}
