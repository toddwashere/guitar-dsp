#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace guitar_dsp::audio {

// Per-grain pre-computed WORLD analysis. Built offline (message thread)
// once per grain when the bundle loads.
struct ShifterGrain {
    int                              sampleRate    = 48000;
    int                              fftSize       = 2048;
    double                           framePeriodMs = 5.0;
    std::vector<double>              timeAxis;
    std::vector<double>              f0;
    std::vector<std::vector<double>> spectrum;
    std::vector<std::vector<double>> aperiodicity;
};

// Streaming formant-preserving pitch shift over a pre-analysed grain.
// Audio-thread process() is allocation-free; it advances a synthesis
// cursor through the grain's WORLD parameters with the current ratio.
//
// Threading:
//   * setSource() is message-thread; it atomically swaps the active grain
//     pointer (cheap).
//   * setRatio() is callable from either thread.
//   * process() reads only the active-grain pointer and produces samples.
//     WORLD's Synthesis is invoked block-by-block; on a 256-sample block at
//     48 kHz, this is ~5 ms of compute budget. M1.5 verified compute fits.
class FormantShifter {
public:
    FormantShifter();
    ~FormantShifter();

    void prepare(double sampleRate, int blockSize);
    void reset();

    void setSource(std::shared_ptr<const ShifterGrain> grain) noexcept;
    void setRatio(float r) noexcept;
    void setFormantTintSemitones(float n) noexcept;

    void process(float* out, int n) noexcept;

    int latencySamples() const noexcept;

private:
    double sampleRate_ = 48000.0;
    int    blockSize_  = 256;
    int    latencySamples_ = 0;

    std::atomic<float> ratio_     {1.0f};
    std::atomic<float> tintSemi_  {0.0f};

    // Pointer swap: writer (message) uses std::atomic_store; reader (audio)
    // uses std::atomic_load. These free functions support shared_ptr in C++11
    // and are available in C++17/20. (std::atomic<shared_ptr> requires
    // additional library support not available in all Clang builds.)
    std::shared_ptr<const ShifterGrain> activeGrain_;
    std::shared_ptr<const ShifterGrain> localGrain_;
    int                                 localFrameIdx_ = 0;

    // Scratch buffers sized at prepare() — avoid heap during process().
    std::vector<double> outBufferD_;  // sized blockSize_
};

} // namespace guitar_dsp::audio
