#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace guitar_dsp::audio {

// Lock-free ring buffer for the most recent ~1 second of mic samples,
// used by MicScopeView to draw a scrolling live-input waveform.
//
// Single producer (audio thread) / single consumer (UI thread). The
// producer increments writeIdx_ atomically; the consumer takes a snapshot
// by reading writeIdx_ and copying samples_ backwards from there.
//
// Capacity is fixed at construction. 1 second at 48 kHz is enough for the
// live-scope use case.
class MicScopeBuffer {
public:
    explicit MicScopeBuffer(std::size_t capacity = 48000);

    // Audio thread. Lock-free, allocation-free.
    void push(const float* samples, std::size_t numSamples) noexcept;

    // UI thread. Copies the last `numSamples` (up to capacity) into `out`.
    // Returns the number actually written.
    std::size_t copyMostRecent(float* out, std::size_t numSamples) const noexcept;

    std::size_t capacity() const noexcept { return samples_.size(); }

private:
    std::vector<float>       samples_;
    std::atomic<std::size_t> writeIdx_{0};  // total samples written (monotonic)
};

} // namespace guitar_dsp::audio
