#include <catch2/catch_test_macros.hpp>

#include "audio/Carousel.h"
#include "scenes/Scene.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::Carousel;
using guitar_dsp::scenes::CarouselConfig;

TEST_CASE("Auto-Vocal Scene 4: LFO formant + drive produces audible 'weedly' character",
          "[integration][formant][scene]") {
    CarouselConfig cfg;
    cfg.enabled = true;
    cfg.drive   = 12.0f;
    cfg.shaper  = CarouselConfig::Shaper::Tanh;
    cfg.shaperAmount = 1.5f;
    cfg.filterMode      = CarouselConfig::FilterMode::LowPass;
    cfg.filterCutoffHz  = 6000.0f;
    cfg.filterResonance = 0.3f;
    cfg.formantAmount = 0.85f;
    cfg.formantMode   = CarouselConfig::FormantMode::Lfo;
    cfg.formantBreakpoints = { 0.0f, 0.18f };
    cfg.formantLfoHz = 9.0f;
    cfg.reverbRoomSize = 0.2f;
    cfg.reverbWet      = 0.08f;
    cfg.outputTrimDb   = -3.0f;

    Carousel c;
    c.prepare(48000.0, 512);
    c.setConfig(cfg);

    constexpr std::size_t N = 48000;
    std::vector<float> in(N), out(N);
    for (std::size_t i = 0; i < N; ++i) {
        const float t = static_cast<float>(i) / 48000.0f;
        in[i] = 0.4f * (std::sin(2.0f * 3.14159265f * 220.0f * t)
                       + 0.5f * std::sin(2.0f * 3.14159265f * 440.0f * t)
                       + 0.25f * std::sin(2.0f * 3.14159265f * 660.0f * t));
    }
    for (std::size_t i = 0; i < N; i += 512) {
        const std::size_t n = std::min<std::size_t>(512, N - i);
        c.process(in.data() + i, out.data() + i, n);
    }

    float peak = 0.0f;
    for (float v : out) peak = std::max(peak, std::fabs(v));
    REQUIRE(peak > 0.01f);
    REQUIRE(peak < 5.0f);
}
