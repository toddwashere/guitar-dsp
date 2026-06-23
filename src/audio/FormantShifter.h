#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

namespace guitar_dsp::audio {

// Pre-rendered audio at a single pitch-shift ratio.
// Built offline (message thread) in FormantShifter::setSource().
struct PreRenderedRatio {
    float              ratio   = 1.0f;       // the pitch ratio this audio was synthesised at
    std::vector<float> samples;              // mono float32, full grain length at the ratio
};

// Per-grain pre-computed WORLD analysis. Built offline (message thread)
// once per grain when the bundle loads.
//
// preRendered is populated by FormantShifter::setSource() in 13 semitone steps
// from -6 to +6 (ratios 0.7071 … 1.4142). The audio thread never calls
// WORLD's Synthesis; it only reads from whichever pre-rendered buffer has
// the nearest ratio to the live target.
//
// Known limitation: continuous portamento (per-sample ratio interpolation)
// is replaced by stepped ratio selection (nearest semitone). Continuous
// portamento can be restored in a future task by running Synthesis() on a
// dedicated non-RT thread and streaming the result through a ring buffer.
struct ShifterGrain {
    int                              sampleRate    = 48000;
    int                              fftSize       = 2048;
    double                           framePeriodMs = 5.0;
    std::vector<double>              timeAxis;
    std::vector<double>              f0;
    std::vector<std::vector<double>> spectrum;
    std::vector<std::vector<double>> aperiodicity;

    // Pre-rendered audio at discrete ratios. Populated offline.
    std::vector<PreRenderedRatio>    preRendered;
};

// Formant-preserving pitch shift over a pre-analysed grain.
//
// RT-safety model:
//   * setSource() is called from the message thread. It pre-renders the
//     grain at all discrete ratios (offline, allocating freely), then
//     atomically publishes the new grain pointer.
//   * setRatio() and setFormantTintSemitones() are callable from either thread.
//   * process() is audio-thread-only. It reads the pre-rendered buffer list,
//     picks the entry whose ratio is nearest to (ratio_ * tintRatio_), and
//     copies samples from a per-grain playback cursor. Zero heap allocations.
//
// Formant tint: setFormantTintSemitones() adds a small additional pitch offset
// before ratio lookup. This is not true formant-axis warping (that would
// require per-frame CheapTrick axis remapping), but it provides an audible,
// UI-controllable effect using the same pre-rendered ratio table.
class FormantShifter {
public:
    FormantShifter();
    ~FormantShifter();

    void prepare(double sampleRate, int blockSize);
    void reset();

    // Message thread. Accepts a grain that ALREADY has preRendered populated
    // (call preRenderGrain() first). Atomically swaps the active grain pointer.
    void setSource(std::shared_ptr<const ShifterGrain> grain) noexcept;

    // Message thread. Takes a raw ShifterGrain (preRendered empty or ignored),
    // runs Synthesis() at each discrete semitone step, and returns a new
    // ShifterGrain with preRendered populated. Allocates freely; never call
    // from the audio thread.
    static std::shared_ptr<ShifterGrain>
        preRenderGrain(std::shared_ptr<const ShifterGrain> raw);
    void setRatio(float r) noexcept;
    void setFormantTintSemitones(float n) noexcept;

    void process(float* out, int n) noexcept;

    int latencySamples() const noexcept;

    // Number of semitones pre-rendered on each side of 1.0 (unity).
    static constexpr int kSemitoneRange = 6;
    // Total discrete ratios = 2*kSemitoneRange + 1 (i.e. -6 to +6 inclusive).
    static constexpr int kNumRatios = 2 * kSemitoneRange + 1;

private:
    double sampleRate_ = 48000.0;
    int    blockSize_  = 256;
    int    latencySamples_ = 0;

    std::atomic<float> ratio_     {1.0f};
    std::atomic<float> tintSemi_  {0.0f};

    // Atomic shared_ptr to the active grain (includes pre-rendered buffers).
    // Writer (message thread) uses std::atomic_store; reader (audio thread)
    // uses std::atomic_load. These free functions work with shared_ptr in
    // C++17 (std::atomic<shared_ptr> requires extra library support).
    std::shared_ptr<const ShifterGrain> activeGrain_;
    std::shared_ptr<const ShifterGrain> localGrain_;
    std::size_t                         localPlayPos_ = 0;   // sample offset into pre-rendered buffer
    int                                 localRatioIdx_ = kSemitoneRange; // index into preRendered[]
};

} // namespace guitar_dsp::audio
