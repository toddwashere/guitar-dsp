#include "GoldenFile.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace guitar_dsp::tests {

void GoldenFile::writeMonoWav(const std::string& path,
                              double sampleRate,
                              const float* samples,
                              std::size_t numSamples) {
    juce::File outFile(path);
    outFile.deleteFile();
    juce::WavAudioFormat format;
    auto stream = std::unique_ptr<juce::FileOutputStream>(outFile.createOutputStream().release());
    if (!stream) throw std::runtime_error("GoldenFile: cannot open " + path);

    auto writer = std::unique_ptr<juce::AudioFormatWriter>(
        format.createWriterFor(stream.get(), sampleRate, 1, 24, {}, 0));
    if (!writer) throw std::runtime_error("GoldenFile: cannot create writer for " + path);
    stream.release();

    juce::AudioBuffer<float> buffer(1, static_cast<int>(numSamples));
    std::memcpy(buffer.getWritePointer(0), samples, numSamples * sizeof(float));
    if (!writer->writeFromAudioSampleBuffer(buffer, 0, static_cast<int>(numSamples)))
        throw std::runtime_error("GoldenFile: write failed for " + path);
}

GoldenFile::WavData GoldenFile::readMonoWav(const std::string& path) {
    juce::File inFile(path);
    if (!inFile.existsAsFile()) throw std::runtime_error("GoldenFile: missing " + path);

    juce::WavAudioFormat format;
    auto reader = std::unique_ptr<juce::AudioFormatReader>(
        format.createReaderFor(inFile.createInputStream().release(), true));
    if (!reader) throw std::runtime_error("GoldenFile: cannot read " + path);

    WavData out;
    out.sampleRate = reader->sampleRate;
    out.samples.resize(static_cast<std::size_t>(reader->lengthInSamples));

    juce::AudioBuffer<float> buffer(1, static_cast<int>(reader->lengthInSamples));
    reader->read(&buffer, 0, static_cast<int>(reader->lengthInSamples), 0, true, false);
    std::memcpy(out.samples.data(), buffer.getReadPointer(0), out.samples.size() * sizeof(float));
    return out;
}

GoldenFile::DiffResult GoldenFile::compareBuffers(const float* a,
                                                  const float* b,
                                                  std::size_t numSamples,
                                                  float tolerance) {
    DiffResult result;
    bool foundFirst = false;
    for (std::size_t i = 0; i < numSamples; ++i) {
        const float d = std::abs(a[i] - b[i]);
        if (d > tolerance) {
            if (!foundFirst) { result.firstDifferingIndex = i; foundFirst = true; }
            ++result.numDifferingSamples;
            if (d > result.maxAbsDiff) result.maxAbsDiff = d;
        }
    }
    return result;
}

GoldenFile::DiffResult GoldenFile::assertMatchesGolden(const std::string& goldenPath,
                                                      double sampleRate,
                                                      const float* samples,
                                                      std::size_t numSamples,
                                                      float tolerance) {
    const char* regen = std::getenv("GUITAR_DSP_REGENERATE_GOLDENS");
    if (regen && std::string(regen) == "1") {
        writeMonoWav(goldenPath, sampleRate, samples, numSamples);
        return DiffResult{};
    }
    auto golden = readMonoWav(goldenPath);
    if (golden.samples.size() != numSamples)
        throw std::runtime_error("GoldenFile: length mismatch for " + goldenPath);
    return compareBuffers(golden.samples.data(), samples, numSamples, tolerance);
}

} // namespace guitar_dsp::tests
