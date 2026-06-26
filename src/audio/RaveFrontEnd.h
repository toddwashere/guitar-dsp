#pragma once
#include <algorithm>
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
        hpf_.reset(); peak_.reset(); shelf_.reset();
        currentPresence_ = -1.0f;  // force coefficient recompute on first block
    }

    void setGateDb(float db) noexcept {
        gateLin_ = std::pow(10.0f, db / 20.0f);
    }

    void setPresence(float amount) noexcept {
        presence_ = std::clamp(amount, 0.0f, 1.0f);
        // NOTE: do not recompute coefficients here. processBlock notices the
        // change and recomputes inline — keeps setPresence callable from the
        // audio thread with zero allocation.
    }

    void processBlockEqOnly(float* buf, std::size_t n) noexcept {
        if (presence_ != currentPresence_) {
            updateEqCoeffs_();
            currentPresence_ = presence_;
        }
        for (std::size_t i = 0; i < n; ++i) {
            float x = buf[i];
            x = hpf_.process(x);
            x = peak_.process(x);
            x = shelf_.process(x);
            buf[i] = x;
        }
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
    struct Biquad {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
        float z1 = 0.0f, z2 = 0.0f;
        inline float process(float x) noexcept {
            // Direct Form II Transposed
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() noexcept { z1 = z2 = 0.0f; }
    };

    double sr_ = 48000.0;
    float attackCoeff_ = 0.0f;
    float releaseCoeff_ = 0.0f;

    // Gate state
    float gateLin_ = 0.01f;
    float env_ = 0.0f;
    float gateGain_ = 0.0f;

    // EQ state
    Biquad hpf_, peak_, shelf_;
    float presence_ = 0.5f;
    float currentPresence_ = -1.0f;   // forces first-block recompute

    static void makeHighPass(Biquad& b, float fs, float f0) noexcept {
        const float w0 = 6.2831853f * f0 / fs;
        const float cw = std::cos(w0), sw = std::sin(w0);
        const float Q  = 0.7071f;
        const float al = sw / (2.0f * Q);
        const float a0 =  1.0f + al;
        b.b0 = (1.0f + cw) / 2.0f / a0;
        b.b1 = -(1.0f + cw)        / a0;
        b.b2 = (1.0f + cw) / 2.0f / a0;
        b.a1 = -2.0f * cw          / a0;
        b.a2 = (1.0f - al)         / a0;
    }
    static void makePeak(Biquad& b, float fs, float f0, float Q, float gainDb) noexcept {
        const float A  = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 6.2831853f * f0 / fs;
        const float cw = std::cos(w0), sw = std::sin(w0);
        const float al = sw / (2.0f * Q);
        const float a0 =  1.0f + al / A;
        b.b0 = ( 1.0f + al * A) / a0;
        b.b1 = (-2.0f * cw)     / a0;
        b.b2 = ( 1.0f - al * A) / a0;
        b.a1 = (-2.0f * cw)     / a0;
        b.a2 = ( 1.0f - al / A) / a0;
    }
    static void makeHighShelf(Biquad& b, float fs, float f0, float gainDb) noexcept {
        const float A  = std::pow(10.0f, gainDb / 40.0f);
        const float w0 = 6.2831853f * f0 / fs;
        const float cw = std::cos(w0), sw = std::sin(w0);
        const float S  = 1.0f; // shelf slope
        const float al = sw / 2.0f * std::sqrt((A + 1.0f/A) * (1.0f/S - 1.0f) + 2.0f);
        const float sqrtA2al = 2.0f * std::sqrt(A) * al;
        const float a0 =      (A + 1.0f) - (A - 1.0f) * cw + sqrtA2al;
        b.b0 =  A * ((A + 1.0f) + (A - 1.0f) * cw + sqrtA2al) / a0;
        b.b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw)   / a0;
        b.b2 =  A * ((A + 1.0f) + (A - 1.0f) * cw - sqrtA2al) / a0;
        b.a1 =  2.0f * ((A - 1.0f) - (A + 1.0f) * cw)        / a0;
        b.a2 =      ((A + 1.0f) - (A - 1.0f) * cw - sqrtA2al) / a0;
    }

    void updateEqCoeffs_() noexcept {
        makeHighPass(hpf_,   float(sr_), 100.0f);
        makePeak(peak_,      float(sr_), 2500.0f, 1.0f, presence_ *  6.0f);
        makeHighShelf(shelf_,float(sr_), 8000.0f,        presence_ * -3.0f);
    }
};

} // namespace guitar_dsp::audio
