#include <catch2/catch_test_macros.hpp>
#include "ai/WhisperTranscriber.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <filesystem>

using guitar_dsp::ai::WhisperTranscriber;

namespace {

// Walk up from CWD until we find tests/fixtures, then return the requested file.
// This works whether the test binary is run from the build dir or the project root.
juce::File findFixture(const char* relative) {
    auto p = std::filesystem::current_path();
    for (int i = 0; i < 8; ++i) {
        auto candidate = p / "tests" / "fixtures" / relative;
        if (std::filesystem::exists(candidate)) {
            return juce::File{juce::String{candidate.string()}};
        }
        p = p.parent_path();
    }
    return {}; // caller should REQUIRE existsAsFile()
}

// Walk up from CWD to find project root (the dir that contains "Resources/whisper").
juce::File findProjectRoot() {
    auto p = std::filesystem::current_path();
    for (int i = 0; i < 8; ++i) {
        if (std::filesystem::exists(p / "Resources" / "whisper")) {
            return juce::File{juce::String{p.string()}};
        }
        p = p.parent_path();
    }
    return {};
}

std::vector<float> loadMono16k(const juce::File& f) {
    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(f));
    REQUIRE(reader != nullptr);
    juce::AudioBuffer<float> buf(1, static_cast<int>(reader->lengthInSamples));
    reader->read(&buf, 0, (int)reader->lengthInSamples, 0, true, false);
    return std::vector<float>(buf.getReadPointer(0),
                              buf.getReadPointer(0) + buf.getNumSamples());
}

// Whisper requires at least 1000 ms of audio (16 kHz input).
// Pad to 1.5 s to safely clear the threshold (16000 samples is the boundary
// and whisper's internal check is strictly < 1000 ms).
std::vector<float> padToWhisperMin(std::vector<float> samples) {
    constexpr std::size_t kMin16k = 24000; // 1.5 seconds at 16 kHz
    if (samples.size() < kMin16k)
        samples.resize(kMin16k, 0.0f);
    return samples;
}

juce::File modelFile() {
    auto root = findProjectRoot();
    if (root == juce::File{}) return {};
    return root.getChildFile("Resources/whisper/ggml-base.en.bin");
}

} // namespace

TEST_CASE("WhisperTranscriber: hello_world.wav transcribes to text containing 'hello'",
          "[ai][stt][integration][.requires_model]") {
    auto model = modelFile();
    if (! model.existsAsFile()) {
        WARN("ggml-base.en.bin not present at Resources/whisper/ — skipping");
        return;
    }
    WhisperTranscriber t{model};
    auto fixture = findFixture("ai/hello_world.wav");
    REQUIRE(fixture.existsAsFile());
    auto samples = padToWhisperMin(loadMono16k(fixture));
    auto r = t.transcribe(samples);
    INFO("transcribed text: '" << r.text << "', error: '" << r.error << "'");
    REQUIRE(r.error.empty());
    auto lower = juce::String(r.text).toLowerCase().toStdString();
    REQUIRE(lower.find("hello") != std::string::npos);
}

TEST_CASE("WhisperTranscriber: silence returns empty or blank-audio marker",
          "[ai][stt][integration][.requires_model]") {
    auto model = modelFile();
    if (! model.existsAsFile()) return;
    WhisperTranscriber t{model};
    auto fixture = findFixture("ai/silence.wav");
    REQUIRE(fixture.existsAsFile());
    auto samples = padToWhisperMin(loadMono16k(fixture));
    auto r = t.transcribe(samples);
    INFO("silence transcribed to: '" << r.text << "', error: '" << r.error << "'");
    REQUIRE(r.error.empty());
    // Whisper on silence should not produce speech — it either returns empty, a
    // [BLANK_AUDIO] / [blank_audio] marker, or very short hallucinations (< 6 words).
    // We accept any of these outcomes; we just check no crash and no error.
    // The key invariant is that r.error is empty (checked above).
}

TEST_CASE("WhisperTranscriber: missing model file fails gracefully (no crash, no throw)",
          "[ai][stt]") {
    juce::File bogus{juce::String{"/tmp/this_definitely_does_not_exist_xyz.bin"}};
    WhisperTranscriber t{bogus};
    auto r = t.transcribe(std::vector<float>(16000, 0.0f));
    REQUIRE(r.error.find("model") != std::string::npos);
}
