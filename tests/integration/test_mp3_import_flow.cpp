#include "audio/AudioFileDecoder.h"
#include "audio/TTSClip.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <memory>

namespace {

juce::File writeFixtureWav(const char* tag, double srcRate, std::size_t n) {
    auto path = juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile(juce::String{"_test_mp3_import_"} + tag + ".wav");
    path.deleteFile();
    juce::WavAudioFormat fmt;
    auto stream = std::unique_ptr<juce::FileOutputStream>(path.createOutputStream());
    REQUIRE(stream);
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        fmt.createWriterFor(stream.get(), srcRate, 1, 16, {}, 0));
    REQUIRE(writer);
    stream.release();
    juce::AudioBuffer<float> buf(1, (int) n);
    for (std::size_t i = 0; i < n; ++i)
        buf.setSample(0, (int) i, (float) std::sin(2.0 * 3.14159 * 220.0
                * (double) i / srcRate) * 0.25f);
    writer->writeFromAudioSampleBuffer(buf, 0, (int) n);
    return path;
}

} // namespace

TEST_CASE("Import flow: decode produces a single-span v1 clip",
          "[integration][import]") {
    const auto path = writeFixtureWav("decode_v1", 48000.0, 4800);

    auto decoded = guitar_dsp::audio::AudioFileDecoder::decodeMono(path, 48000.0);
    REQUIRE(decoded.has_value());

    // What the Import handler builds: a v1 clip with one full-length word/syl.
    auto clip = std::make_shared<guitar_dsp::audio::TTSClip>();
    clip->sampleRate = 48000.0;
    clip->samples    = std::move(decoded->samples);
    guitar_dsp::audio::WordSegment full{"imported", 0, clip->samples.size()};
    clip->words.push_back(full);
    clip->syllables.push_back(full);

    REQUIRE(clip->samples.size() == 4800);
    REQUIRE(clip->words.size() == 1);
    REQUIRE(clip->syllables.size() == 1);
    REQUIRE(clip->words[0].startSample == 0);
    REQUIRE(clip->words[0].endSample == clip->samples.size());
    REQUIRE(clip->sylsV2.empty());     // v1 only
    REQUIRE(clip->phonemes.empty());

    path.deleteFile();
}
