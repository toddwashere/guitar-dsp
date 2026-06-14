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

TEST_CASE("Formant: setPosition(0.0) matches setVowel(Ee) at first formant",
          "[audio][formant][position]") {
    auto in = noise(48000);

    Formant ee; ee.prepare(48000.0); ee.setVowel(CarouselConfig::Vowel::Ee);
    ee.setAmount(1.0f);
    Formant pos; pos.prepare(48000.0); pos.setPosition(0.0f); pos.setAmount(1.0f);

    float energyEe  = 0.0f, energyPos = 0.0f;
    for (float s : in) {
        const float a = ee.processSample(s);
        const float b = pos.processSample(s);
        energyEe  += a * a;
        energyPos += b * b;
    }
    REQUIRE(std::fabs(energyEe - energyPos) / energyEe < 0.01f);
}

TEST_CASE("Formant: setPosition midway between EE and EH interpolates F1",
          "[audio][formant][position]") {
    // At position 0.125, F1 interpolates halfway between EE(270 Hz) and
    // EH(530 Hz) → ~400 Hz.  At position 0.0 (pure EE), F1 = 270 Hz.
    // The midpoint filter should therefore emphasise 400 Hz MORE than
    // the pure-EE filter does (because 400 Hz is closer to its resonance).
    auto in = noise(48000);

    Formant posEE; posEE.prepare(48000.0); posEE.setPosition(0.0f); posEE.setAmount(1.0f);
    std::vector<float> outEE(in.size());
    for (size_t i = 0; i < in.size(); ++i) outEE[i] = posEE.processSample(in[i]);

    Formant posMid; posMid.prepare(48000.0); posMid.setPosition(0.125f); posMid.setAmount(1.0f);
    std::vector<float> outMid(in.size());
    for (size_t i = 0; i < in.size(); ++i) outMid[i] = posMid.processSample(in[i]);

    // Midpoint (F1≈400) should have more relative energy at 400 Hz than pure EE (F1=270).
    const float e400ee  = bandEnergy(outEE,  400.0f, 48000.0);
    const float e400mid = bandEnergy(outMid, 400.0f, 48000.0);
    REQUIRE(e400mid > e400ee * 1.1f);
}

TEST_CASE("Formant: setPosition then setVowel(None) restores bypass",
          "[audio][formant][position]") {
    Formant f; f.prepare(48000.0);
    f.setPosition(0.5f);
    f.setAmount(1.0f);
    f.setVowel(CarouselConfig::Vowel::None);
    REQUIRE(f.processSample(0.31f) == 0.31f);
}
