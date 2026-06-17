#include "MicScopeBuffer.h"

#include <algorithm>
#include <cstring>

namespace guitar_dsp::audio {

MicScopeBuffer::MicScopeBuffer(std::size_t capacity)
    : samples_(capacity, 0.0f) {}

void MicScopeBuffer::push(const float* samples, std::size_t numSamples) noexcept {
    const std::size_t cap = samples_.size();
    if (cap == 0 || numSamples == 0) return;

    // Only keep the last `cap` samples if the block is larger.
    const std::size_t offset = (numSamples > cap) ? (numSamples - cap) : 0;
    const std::size_t toCopy = numSamples - offset;

    std::size_t idx = writeIdx_.load(std::memory_order_relaxed);

    for (std::size_t i = 0; i < toCopy; ++i) {
        samples_[(idx % cap)] = samples[offset + i];
        ++idx;
    }

    writeIdx_.store(idx, std::memory_order_release);
}

std::size_t MicScopeBuffer::copyMostRecent(float* out,
                                            std::size_t numSamples) const noexcept {
    const std::size_t cap = samples_.size();
    if (cap == 0 || numSamples == 0) return 0;

    const std::size_t n = std::min(numSamples, cap);
    // Snapshot the write cursor. Acquire pairs with the release in push().
    const std::size_t writeIdx = writeIdx_.load(std::memory_order_acquire);

    // Walk backwards n slots from writeIdx. Because writeIdx is monotonic,
    // writeIdx - n gives us the oldest sample we want.
    // Guard against underflow when fewer than n samples have been written.
    const std::size_t start = (writeIdx >= n) ? (writeIdx - n) : 0;
    const std::size_t actual = writeIdx - start;   // <= n

    for (std::size_t i = 0; i < actual; ++i) {
        out[i] = samples_[(start + i) % cap];
    }
    // Zero-fill any leading slots not yet written (very first second of audio).
    if (actual < n) {
        std::fill_n(out + actual, n - actual, 0.0f);
    }
    return n;
}

} // namespace guitar_dsp::audio
