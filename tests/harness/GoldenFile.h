#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace guitar_dsp::tests {

class GoldenFile {
public:
    struct WavData {
        double sampleRate = 0.0;
        std::vector<float> samples;
    };

    struct DiffResult {
        float maxAbsDiff = 0.0f;
        std::size_t numDifferingSamples = 0;
        std::size_t firstDifferingIndex = 0;
    };

    // Writes a 24-bit mono WAV at the given sample rate.
    static void writeMonoWav(const std::string& path,
                             double sampleRate,
                             const float* samples,
                             std::size_t numSamples);

    // Reads a mono WAV (any bit depth). Throws std::runtime_error on failure.
    static WavData readMonoWav(const std::string& path);

    // Element-wise compare two buffers. `tolerance` is absolute; pass 0 for
    // bit-exact comparison.
    static DiffResult compareBuffers(const float* a,
                                     const float* b,
                                     std::size_t numSamples,
                                     float tolerance = 0.0f);

    // Render a buffer + compare against a golden WAV on disk. If env var
    // `GUITAR_DSP_REGENERATE_GOLDENS=1` is set, writes the new buffer as the
    // golden instead of comparing.
    static DiffResult assertMatchesGolden(const std::string& goldenPath,
                                          double sampleRate,
                                          const float* samples,
                                          std::size_t numSamples,
                                          float tolerance = 0.0f);
};

} // namespace guitar_dsp::tests
