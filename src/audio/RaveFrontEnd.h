#pragma once
#include <cmath>
#include <cstddef>

namespace guitar_dsp::audio {

class RaveFrontEnd {
public:
    void prepare(double sampleRate) noexcept {
        sr_ = sampleRate;
        attackCoeff_ = 1.0f - std::exp(-1.0f / (float(sr_) * 0.005f));   // 5 ms
        releaseCoeff_ = 1.0f - std::exp(-1.0f / (float(sr_) * 0.080f));  // 80 ms
        env_ = 0.0f;
        gateGain_ = 0.0f;
    }

    void setGateDb(float db) noexcept {
        gateLin_ = std::pow(10.0f, db / 20.0f);
    }

    void processBlockGateOnly(float* buf, std::size_t n) noexcept {
        for (std::size_t i = 0; i < n; ++i) {
            const float a = std::fabs(buf[i]);
            const float c = (a > env_) ? attackCoeff_ : releaseCoeff_;
            env_ += (a - env_) * c;
            const float target = (env_ > gateLin_) ? 1.0f : 0.0f;
            const float gc = (target > gateGain_) ? attackCoeff_ : releaseCoeff_;
            gateGain_ += (target - gateGain_) * gc;
            buf[i] *= gateGain_;
        }
    }

protected:
    double sr_ = 48000.0;
    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;

    // Gate state
    float gateLin_ = 0.01f;
    float env_ = 0.0f;
    float gateGain_ = 0.0f;
};

} // namespace guitar_dsp::audio
