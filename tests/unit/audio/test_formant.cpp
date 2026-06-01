#include <catch2/catch_test_macros.hpp>
#include "audio/Formant.h"
#include "scenes/Scene.h"

#include <cmath>
#include <vector>

using guitar_dsp::audio::Formant;
using guitar_dsp::scenes::CarouselConfig;

namespace {
float bandEnergy(const std::vector<float>& x, float f, double sr) {
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        const double ph = 2.0 * 3.14159265358979 * f * i / sr;
        re += x[i] * std::cos(ph);
        im += x[i] * std::sin(ph);
    }
    return static_cast<float>(std::sqrt(re*re + im*im) / x.size());
}
std::vector<float> noise(int n) {
    std::vector<float> b(static_cast<size_t>(n));
    unsigned s = 0x12345u;
    for (int i = 0; i < n; ++i) { s = s*1664525u + 1013904223u;
        b[static_cast<size_t>(i)] = (static_cast<float>(s >> 9) / 8388608.0f) - 1.0f; }
    return b;
}
}

TEST_CASE("Formant: 'ah' emphasizes its first formant band vs none",
          "[audio][formant]") {
    auto in = noise(48000);

    Formant flat; flat.prepare(48000.0); flat.setVowel(CarouselConfig::Vowel::None);
    flat.setAmount(1.0f);
    std::vector<float> outFlat(in.size());
    for (size_t i = 0; i < in.size(); ++i) outFlat[i] = flat.processSample(in[i]);

    Formant ah; ah.prepare(48000.0); ah.setVowel(CarouselConfig::Vowel::Ah);
    ah.setAmount(1.0f);
    std::vector<float> outAh(in.size());
    for (size_t i = 0; i < in.size(); ++i) outAh[i] = ah.processSample(in[i]);

    const float e700flat = bandEnergy(outFlat, 700.0f, 48000.0);
    const float e700ah   = bandEnergy(outAh,   700.0f, 48000.0);
    REQUIRE(e700ah > e700flat * 1.2f);
}

TEST_CASE("Formant: None vowel is bypass", "[audio][formant]") {
    Formant f; f.prepare(48000.0);
    f.setVowel(CarouselConfig::Vowel::None);
    f.setAmount(1.0f);
    REQUIRE(f.processSample(0.31f) == 0.31f);
}

TEST_CASE("Formant: output finite + bounded", "[audio][formant]") {
    Formant f; f.prepare(48000.0);
    f.setVowel(CarouselConfig::Vowel::Ee);
    f.setAmount(0.8f);
    auto in = noise(8192);
    for (float s : in) {
        const float y = f.processSample(s);
        REQUIRE(std::isfinite(y));
        REQUIRE(std::fabs(y) < 8.0f);
    }
}
