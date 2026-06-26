#pragma once
#include <atomic>
#include <cstddef>
#include <cstring>
#include <vector>

namespace guitar_dsp::audio {

template <typename T>
class LockFreeSPSCRing {
public:
    explicit LockFreeSPSCRing(std::size_t capacity)
        : buf_(capacity + 1), cap_(capacity + 1) {} // +1 so empty/full are distinguishable

    std::size_t write(const T* src, std::size_t count) noexcept {
        const auto w = wr_.load(std::memory_order_relaxed);
        const auto r = rd_.load(std::memory_order_acquire);
        const std::size_t avail = (r + cap_ - w - 1) % cap_;
        const std::size_t n = (count < avail) ? count : avail;
        if (n == 0) return 0;
        const std::size_t first = (w + n <= cap_) ? n : cap_ - w;
        std::memcpy(&buf_[w], src, first * sizeof(T));
        if (n > first) std::memcpy(&buf_[0], src + first, (n - first) * sizeof(T));
        wr_.store((w + n) % cap_, std::memory_order_release);
        return n;
    }

    std::size_t read(T* dst, std::size_t count) noexcept {
        const auto r = rd_.load(std::memory_order_relaxed);
        const auto w = wr_.load(std::memory_order_acquire);
        const std::size_t avail = (w + cap_ - r) % cap_;
        const std::size_t n = (count < avail) ? count : avail;
        if (n == 0) return 0;
        const std::size_t first = (r + n <= cap_) ? n : cap_ - r;
        std::memcpy(dst, &buf_[r], first * sizeof(T));
        if (n > first) std::memcpy(dst + first, &buf_[0], (n - first) * sizeof(T));
        rd_.store((r + n) % cap_, std::memory_order_release);
        return n;
    }

    std::size_t available() const noexcept {
        // SPSC: only producer writes wr_, only consumer writes rd_,
        // so caller's own pointer is relaxed; foreign pointer needs acquire.
        const auto r = rd_.load(std::memory_order_relaxed);
        const auto w = wr_.load(std::memory_order_acquire);
        return (w + cap_ - r) % cap_;
    }

    std::size_t free_space() const noexcept {
        return cap_ - 1 - available();
    }

    void clear() noexcept {
        wr_.store(0, std::memory_order_relaxed);
        rd_.store(0, std::memory_order_relaxed);
    }

private:
    std::vector<T> buf_;
    std::size_t cap_;
    std::atomic<std::size_t> wr_{0};
    std::atomic<std::size_t> rd_{0};
};

} // namespace
