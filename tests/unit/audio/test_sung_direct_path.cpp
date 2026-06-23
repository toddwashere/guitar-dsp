#include <catch2/catch_test_macros.hpp>
#include "audio/SungDirectPath.h"
#include "audio/TTSClip.h"
#include <cmath>
#include <vector>

using guitar_dsp::audio::SungDirectPath;
using guitar_dsp::audio::TTSClip;

TEST_CASE("SungDirectPath produces non-silent output after an onset",
          "[sung-direct-path]") {
    const double sr = 48000.0;
    SungDirectPath p;
    p.prepare(sr, 256);

    // Build a tiny bank with one grain (1 s of a 220 Hz sine).
    auto c = std::make_shared<TTSClip>();
    c->sampleRate = sr;
    c->samples.assign(static_cast<std::size_t>(sr), 0.0f);
    for (int i = 0; i < (int) c->samples.size(); ++i)
        c->samples[(std::size_t) i] = 0.3f * std::sin(2.0 * M_PI * 220.0 * i / sr);
    c->bankKey       = "sung_ah";
    c->anchorPitchHz = 220.0f;
    p.setGrainsForBank({ std::const_pointer_cast<const TTSClip>(c) });

    // Drain bank-swap, then strike.
    std::vector<float> in(256, 0.0f), out(256, 0.0f);
    p.process(in.data(), 220.0f, out.data(), 256);
    in[0] = 1.0f;
    double energy = 0.0;
    for (int b = 0; b < 20; ++b) {
        p.process(in.data(), 220.0f, out.data(), 256);
        for (float v : out) {
            REQUIRE(! std::isnan(v));
            REQUIRE(! std::isinf(v));
            energy += static_cast<double>(v) * v;
        }
        in[0] = 0.0f;  // single onset
    }
    CHECK(energy > 1e-4);
}
