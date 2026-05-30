#pragma once

#include <cstddef>

namespace guitar_dsp::tests {

// Deterministic test-signal generators used by unit and integration tests.
// All methods write `numSamples` mono samples into the provided buffer.
class SyntheticGuitar {
public:
    explicit SyntheticGuitar(double sampleRate);

    void silence(float* buffer, std::size_t numSamples);
    void dc(float value, float* buffer, std::size_t numSamples);
    void sine(float frequencyHz, float amplitude,
              float* buffer, std::size_t numSamples);
    void sweep(float startHz, float endHz, float amplitude,
               float* buffer, std::size_t numSamples);

    // Karplus-Strong plucked-string for golden-file tests.
    void pluck(float frequencyHz, float decaySeconds, float amplitude,
               float* buffer, std::size_t numSamples);

private:
    double sampleRate_;
    double phase_ = 0.0;
};

} // namespace guitar_dsp::tests
