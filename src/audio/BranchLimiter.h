#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace guitar_dsp::audio {

class BranchLimiter {
public:
    void prepare(double sampleRate) noexcept {
        sr_ = sampleRate;
        updateRelease_();
        reset();
    }

    void setCeilingDb(float db) noexcept {
        ceiling_ = std::pow(10.0f, db / 20.0f);
    }

    void setReleaseMs(float ms) noexcept {
        releaseMs_ = ms;
        updateRelease_();
    }

    void processBlock(float* buf, std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i) {
            const float a = std::fabs(buf[i]);
            float target = (a > ceiling_) ? (ceiling_ / a) : 1.0f;
            if (target < gain_) gain_ = target;     // instant attack
            else gain_ += (target - gain_) * relCoeff_; // smooth release
            buf[i] *= gain_;
        }
    }

    void reset() noexcept { gain_ = 1.0f; }

private:
    void updateRelease_() noexcept {
        if (sr_ <= 0.0 || releaseMs_ <= 0.0f) { relCoeff_ = 1.0f; return; }
        relCoeff_ = 1.0f - std::exp(-1.0f / (float(sr_) * releaseMs_ * 0.001f));
    }

    double sr_ = 48000.0;
    float ceiling_ = 0.7079f; // -3 dBFS
    float releaseMs_ = 60.0f;
    float relCoeff_ = 0.001f;
    float gain_ = 1.0f;
};

} // namespace guitar_dsp::audio
