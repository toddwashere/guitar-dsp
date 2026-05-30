#include "SyntheticGuitar.h"

#include <cmath>
#include <cstring>
#include <random>
#include <vector>

namespace guitar_dsp::tests {

namespace {
    constexpr double kTwoPi = 6.28318530717958647692;
}

SyntheticGuitar::SyntheticGuitar(double sampleRate) : sampleRate_(sampleRate) {}

void SyntheticGuitar::silence(float* buffer, std::size_t numSamples) {
    std::memset(buffer, 0, numSamples * sizeof(float));
}

void SyntheticGuitar::dc(float value, float* buffer, std::size_t numSamples) {
    for (std::size_t i = 0; i < numSamples; ++i) buffer[i] = value;
}

void SyntheticGuitar::sine(float frequencyHz, float amplitude,
                           float* buffer, std::size_t numSamples) {
    const double inc = kTwoPi * frequencyHz / sampleRate_;
    for (std::size_t i = 0; i < numSamples; ++i) {
        buffer[i] = static_cast<float>(amplitude * std::sin(phase_));
        phase_ += inc;
        if (phase_ > kTwoPi) phase_ -= kTwoPi;
    }
}

void SyntheticGuitar::sweep(float startHz, float endHz, float amplitude,
                            float* buffer, std::size_t numSamples) {
    // Linear frequency sweep.
    double localPhase = 0.0;
    for (std::size_t i = 0; i < numSamples; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(numSamples);
        const double freq = startHz + (endHz - startHz) * t;
        localPhase += kTwoPi * freq / sampleRate_;
        if (localPhase > kTwoPi) localPhase -= kTwoPi;
        buffer[i] = static_cast<float>(amplitude * std::sin(localPhase));
    }
}

void SyntheticGuitar::pluck(float frequencyHz, float decaySeconds, float amplitude,
                            float* buffer, std::size_t numSamples) {
    // Karplus-Strong with simple 2-tap lowpass feedback.
    const int delayLen = std::max(2, static_cast<int>(sampleRate_ / frequencyHz));
    std::vector<float> line(static_cast<std::size_t>(delayLen));
    // Initial noise burst, deterministic across runs so pluck() output is
    // reproducible (matters once a golden-file test consumes it).
    std::mt19937 rng{0x706C7563u}; // "pluc"
    std::uniform_real_distribution<float> dist{-amplitude, amplitude};
    for (auto& s : line) s = dist(rng);

    const float decayFactor = static_cast<float>(
        std::pow(0.5, 1.0 / (decaySeconds * sampleRate_ / delayLen)));

    std::size_t idx = 0;
    for (std::size_t i = 0; i < numSamples; ++i) {
        const float out = line[idx];
        const std::size_t nextIdx = (idx + 1) % line.size();
        const float averaged = 0.5f * (line[idx] + line[nextIdx]) * decayFactor;
        line[idx] = averaged;
        idx = nextIdx;
        buffer[i] = out;
    }
}

} // namespace guitar_dsp::tests
