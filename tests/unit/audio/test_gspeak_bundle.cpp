#include "audio/GspeakBundle.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_core/juce_core.h>

#include <cmath>

namespace {

guitar_dsp::audio::TTSClip makeV2Clip() {
    guitar_dsp::audio::TTSClip c;
    c.name = "v2-test";
    c.sampleRate = 48000.0;
    c.samples.resize(12000);
    for (std::size_t i = 0; i < c.samples.size(); ++i)
        c.samples[i] = std::sin(2.0f * 3.14159f * 200.0f
                                * (float) i / 48000.0f) * 0.5f;
    guitar_dsp::audio::SyllableSpan s;
    s.startSample = 0; s.endSample = 6000;
    s.vowelNucleusSample = 3000; s.attackEndSample = 1500; s.codaStartSample = 4500;
    s.nucleusIsFricative = false; s.phonemeIndices = {0};
    c.sylsV2.push_back(s);
    s.startSample = 6000; s.endSample = 12000;
    s.vowelNucleusSample = 9000; s.attackEndSample = 7500; s.codaStartSample = 10500;
    s.phonemeIndices = {1};
    c.sylsV2.push_back(s);
    guitar_dsp::audio::Phoneme p;
    p.label = "AY"; p.type = guitar_dsp::audio::Phoneme::Type::Vowel;
    p.startSample = 0; p.endSample = 6000; c.phonemes.push_back(p);
    p.label = "M"; p.type = guitar_dsp::audio::Phoneme::Type::Consonant;
    p.startSample = 6000; p.endSample = 12000; c.phonemes.push_back(p);
    return c;
}

} // namespace

TEST_CASE("GspeakBundle::write produces a valid zip", "[audio][gspeak]") {
    auto temp = juce::File::createTempFile(".gspeak");
    auto clip = makeV2Clip();
    REQUIRE(guitar_dsp::audio::GspeakBundle::write(temp, clip, "I am"));
    REQUIRE(temp.existsAsFile());
    REQUIRE(temp.getSize() > 0);

    juce::ZipFile zip(temp);
    REQUIRE(zip.getNumEntries() == 2);
    bool sawManifest = false, sawAudio = false;
    for (int i = 0; i < zip.getNumEntries(); ++i) {
        const auto* e = zip.getEntry(i);
        if (e->filename == "manifest.json") sawManifest = true;
        if (e->filename == "audio.wav")     sawAudio    = true;
    }
    REQUIRE(sawManifest);
    REQUIRE(sawAudio);
    temp.deleteFile();
}
