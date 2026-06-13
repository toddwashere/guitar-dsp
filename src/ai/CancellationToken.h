#pragma once
#include <atomic>

namespace guitar_dsp::ai {

class CancellationToken {
public:
    void cancel()                 noexcept { flag_.store(true,  std::memory_order_release); }
    void reset()                  noexcept { flag_.store(false, std::memory_order_release); }
    bool isCancelled() const      noexcept { return flag_.load(std::memory_order_acquire); }

private:
    std::atomic<bool> flag_ {false};
};

} // namespace guitar_dsp::ai
