#include "audio/GspeakBundle.h"

#include <catch2/catch_approx.hpp>
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

guitar_dsp::audio::TTSClip makeV1Clip() {
    guitar_dsp::audio::TTSClip c;
    c.name = "v1-test";
    c.sampleRate = 44100.0;
    c.samples.resize(11025);  // 0.25s at 44.1
    for (std::size_t i = 0; i < c.samples.size(); ++i)
        c.samples[i] = std::sin(2.0f * 3.14159f * 220.0f
                                * (float) i / 44100.0f) * 0.5f;
    guitar_dsp::audio::WordSegment w;
    w.word = "De";  w.startSample = 0;     w.endSample = 5000;  c.syllables.push_back(w);
    w.word = "vel"; w.startSample = 5000;  w.endSample = 11025; c.syllables.push_back(w);
    w.word = "Developers"; w.startSample = 0; w.endSample = 11025;
    c.words.push_back(w);
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

TEST_CASE("GspeakBundle round-trip preserves v2 clip", "[audio][gspeak]") {
    auto temp = juce::File::createTempFile(".gspeak");
    auto orig = makeV2Clip();
    REQUIRE(guitar_dsp::audio::GspeakBundle::write(temp, orig, "I am"));

    auto loaded = guitar_dsp::audio::GspeakBundle::read(temp, 48000.0);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->isV2);
    REQUIRE(loaded->text == "I am");
    REQUIRE(loaded->clip != nullptr);
    REQUIRE(loaded->clip->sampleRate == Catch::Approx(48000.0));
    REQUIRE(loaded->clip->samples.size() == orig.samples.size());
    REQUIRE(loaded->clip->sylsV2.size() == orig.sylsV2.size());
    for (std::size_t i = 0; i < orig.sylsV2.size(); ++i) {
        REQUIRE(loaded->clip->sylsV2[i].startSample == orig.sylsV2[i].startSample);
        REQUIRE(loaded->clip->sylsV2[i].endSample   == orig.sylsV2[i].endSample);
        REQUIRE(loaded->clip->sylsV2[i].vowelNucleusSample
                == orig.sylsV2[i].vowelNucleusSample);
    }
    REQUIRE(loaded->clip->phonemes.size() == orig.phonemes.size());
    temp.deleteFile();
}

TEST_CASE("GspeakBundle::read rejects missing file", "[audio][gspeak]") {
    juce::File missing("/tmp/does-not-exist-gspeak-test.gspeak");
    REQUIRE_FALSE(guitar_dsp::audio::GspeakBundle::read(missing, 48000.0).has_value());
}

TEST_CASE("GspeakBundle::read rejects length mismatch", "[audio][gspeak]") {
    auto temp = juce::File::createTempFile(".gspeak");
    auto clip = makeV2Clip();
    REQUIRE(guitar_dsp::audio::GspeakBundle::write(temp, clip, "x"));

    // Read the existing zip, alter the manifest's lengthSamples, re-pack.
    juce::ZipFile zip(temp);
    auto manifestStream = std::unique_ptr<juce::InputStream>(
        zip.createStreamForEntry(*zip.getEntry("manifest.json")));
    REQUIRE(manifestStream != nullptr);
    auto json = juce::JSON::parse(manifestStream->readEntireStreamAsString());
    json.getDynamicObject()->setProperty("lengthSamples",
        (juce::int64)(clip.samples.size() + 1234));
    auto badManifest = juce::JSON::toString(json, true);

    juce::MemoryBlock badBytes(badManifest.toRawUTF8(),
                               badManifest.getNumBytesAsUTF8());
    juce::ZipFile::Builder builder;
    builder.addEntry(new juce::MemoryInputStream(badBytes, false), 9,
                     "manifest.json", juce::Time::getCurrentTime());
    auto wavStream = std::unique_ptr<juce::InputStream>(
        zip.createStreamForEntry(*zip.getEntry("audio.wav")));
    juce::MemoryBlock wavBytes;
    wavStream->readIntoMemoryBlock(wavBytes);
    builder.addEntry(new juce::MemoryInputStream(wavBytes, false), 0,
                     "audio.wav", juce::Time::getCurrentTime());

    temp.deleteFile();
    auto out = temp.createOutputStream();
    REQUIRE(builder.writeToStream(*out, nullptr));
    out.reset();

    REQUIRE_FALSE(guitar_dsp::audio::GspeakBundle::read(temp, 48000.0).has_value());
    temp.deleteFile();
}

TEST_CASE("GspeakBundle round-trip preserves v1 clip", "[audio][gspeak]") {
    auto temp = juce::File::createTempFile(".gspeak");
    auto orig = makeV1Clip();
    REQUIRE(guitar_dsp::audio::GspeakBundle::write(temp, orig, "Developers"));
    auto loaded = guitar_dsp::audio::GspeakBundle::read(temp, 44100.0);
    REQUIRE(loaded.has_value());
    REQUIRE_FALSE(loaded->isV2);
    REQUIRE(loaded->clip->sampleRate == Catch::Approx(44100.0));
    REQUIRE(loaded->clip->syllables.size() == orig.syllables.size());
    REQUIRE(loaded->clip->words.size()     == orig.words.size());
    REQUIRE(loaded->clip->syllables[0].word == "De");
    REQUIRE(loaded->clip->syllables[1].word == "vel");
    REQUIRE(loaded->text == "Developers");
    temp.deleteFile();
}

TEST_CASE("GspeakBundle resamples on rate mismatch", "[audio][gspeak]") {
    auto temp = juce::File::createTempFile(".gspeak");
    auto orig = makeV2Clip();  // 48000 Hz, 12000 samples
    REQUIRE(guitar_dsp::audio::GspeakBundle::write(temp, orig, "x"));
    auto loaded = guitar_dsp::audio::GspeakBundle::read(temp, 44100.0);
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->clip->sampleRate == Catch::Approx(44100.0));
    // 12000 * (44100/48000) = 11025; allow ±1 for resampler rounding
    REQUIRE(std::abs((int) loaded->clip->samples.size() - 11025) <= 1);
    // Boundary at sample 6000 in 48k -> 6000 * (44100/48000) ≈ 5512 or 5513.
    REQUIRE(std::abs((int) loaded->clip->sylsV2[0].endSample - 5513) <= 1);
    REQUIRE(loaded->clip->sylsV2.back().endSample == loaded->clip->samples.size());
    temp.deleteFile();
}
