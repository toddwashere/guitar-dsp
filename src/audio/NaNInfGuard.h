#pragma once
#include <cmath>
#include <cstddef>
#include <cstring>

namespace guitar_dsp::audio {

class NaNInfGuard {
public:
    bool processBlock(float* buf, std::size_t n) noexcept {
        bool clean = true;
        for (std::size_t i = 0; i < n; ++i) {
            if (!std::isfinite(buf[i])) { clean = false; break; }
        }
        if (!clean) {
            std::memset(buf, 0, n * sizeof(float));
            badCount_++;
            if (badCount_ >= 3) stalled_ = true;
        } else {
            badCount_ = 0;
            stalled_ = false;
        }
        return clean;
    }

    bool stalled() const noexcept { return stalled_; }

    void reset() noexcept { badCount_ = 0; stalled_ = false; }

private:
    int badCount_ = 0;
    bool stalled_ = false;
};

} // namespace guitar_dsp::audio
