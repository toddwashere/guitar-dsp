#include "audio/AudioFileDecoder.h"

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <filesystem>

namespace {

// Writes a synthetic mono WAV (sine, 250 Hz, 0.3 amplitude) to `path`
// at `srcRate` for `samples` samples. Returns the sample count.
std::size_t writeSineWav(const juce::File& path,
                         double srcRate,
                         std::size_t samples) {
    juce::WavAudioFormat fmt;
    path.deleteFile();
    auto stream = std::unique_ptr<juce::FileOutputStream>(path.createOutputStream());
    REQUIRE(stream);
    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        fmt.createWriterFor(stream.get(), srcRate, 1, 16, {}, 0));
    REQUIRE(writer);
    stream.release();  // writer owns it now

    juce::AudioBuffer<float> buf(1, (int) samples);
    for (std::size_t i = 0; i < samples; ++i)
        buf.setSample(0, (int) i, (float) std::sin(2.0 * 3.14159
                * 250.0 * (double) i / srcRate) * 0.3f);
    writer->writeFromAudioSampleBuffer(buf, 0, (int) samples);
    return samples;
}

juce::File tempWavPath(const char* tag) {
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile(juce::String{"_test_audio_file_decoder_"} + tag + ".wav");
}

} // namespace

TEST_CASE("decodeMono returns mono samples for a mono WAV at target SR",
          "[unit][audio-file-decoder]") {
    const auto path = tempWavPath("mono_same_sr");
    const auto n = writeSineWav(path, 48000.0, 4800);

    auto r = guitar_dsp::audio::AudioFileDecoder::decodeMono(path, 48000.0);
    REQUIRE(r.has_value());
    CHECK(r->samples.size() == n);
    CHECK(r->sampleRate == 48000.0);
    // First non-zero sample should match the sine formula within float error.
    CHECK(std::fabs(r->samples[100]
            - (float) std::sin(2.0 * 3.14159 * 250.0 * 100.0 / 48000.0) * 0.3f) < 1e-4f);

    path.deleteFile();
}

TEST_CASE("decodeMono resamples to a different target SR",
          "[unit][audio-file-decoder]") {
    const auto path = tempWavPath("resample");
    const auto n = writeSineWav(path, 44100.0, 4410);  // 0.1 s

    auto r = guitar_dsp::audio::AudioFileDecoder::decodeMono(path, 48000.0);
    REQUIRE(r.has_value());
    CHECK(r->sampleRate == 48000.0);
    // 0.1 s at 48000 Hz = 4800 samples (±1 sample for rounding).
    CHECK(std::abs((int) r->samples.size() - 4800) <= 1);

    path.deleteFile();
}

TEST_CASE("decodeMono returns nullopt for a missing file",
          "[unit][audio-file-decoder]") {
    auto r = guitar_dsp::audio::AudioFileDecoder::decodeMono(
        juce::File("/nonexistent/path/foo.wav"), 48000.0);
    CHECK_FALSE(r.has_value());
}

TEST_CASE("decodeMono returns nullopt for a malformed file",
          "[unit][audio-file-decoder]") {
    const auto path = tempWavPath("malformed");
    path.deleteFile();
    path.replaceWithText("not a real WAV header");

    auto r = guitar_dsp::audio::AudioFileDecoder::decodeMono(path, 48000.0);
    CHECK_FALSE(r.has_value());

    path.deleteFile();
}

TEST_CASE("decodeMono returns nullopt for an empty WAV",
          "[unit][audio-file-decoder]") {
    const auto path = tempWavPath("empty");
    writeSineWav(path, 48000.0, 0);

    auto r = guitar_dsp::audio::AudioFileDecoder::decodeMono(path, 48000.0);
    CHECK_FALSE(r.has_value());

    path.deleteFile();
}
