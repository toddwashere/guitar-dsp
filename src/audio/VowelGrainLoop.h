#pragma once

#include <cstddef>

namespace guitar_dsp::audio {

// Pitch-period-synchronous grain loop for sustaining a vowel during
// note-hold. RT-safe; no allocation in next().
//
// Usage:
//   prepare(sampleRate);
//   beginLoop(clipSamples, anchorSample);
//   while (sustaining) modOut = grain.next();
class VowelGrainLoop {
public:
    void prepare(double sampleRate);
    void reset();

    // Start (or restart) a loop centered at `anchorSample` in `samples`.
    // `numSamples` is the total length of the clip buffer; the loop window
    // is clamped to stay within [0, numSamples).
    void beginLoop(const float* samples, std::size_t numSamples,
                   std::size_t anchorSample) noexcept;

    // Returns the next sample of the looped grain. Returns 0.0f if not
    // looping or if samples is null.
    float next() noexcept;

private:
    const float* samples_ = nullptr;
    std::size_t  loopStart_  = 0;    // grain region [start, start+len)
    std::size_t  loopLen_    = 1024; // ~ pitch period samples (set later)
    std::size_t  cursor_     = 0;
    std::size_t  xfadeLen_   = 240;  // ~5 ms @ 48 kHz
    double       sampleRate_ = 48000.0;
};

} // namespace guitar_dsp::audio
