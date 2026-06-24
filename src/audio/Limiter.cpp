#include "Limiter.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

void Limiter::prepare(double sr) noexcept {
    sampleRate_ = sr;
    // Gain smoothing coef for when the limiter ENGAGES (target gain drops
    // below current). ~0.5 ms — fast enough to catch transients without
    // distortion-inducing zipper.
    gainAttackCoef_ = std::exp(-1.0f / static_cast<float>(sr * 0.0005));
    // Envelope follower release + gain release coef. ~80 ms — musical
    // breathing, fast enough to recover between notes.
    envReleaseCoef_ = std::exp(-1.0f / static_cast<float>(sr * 0.080));
    reset();
}

void Limiter::reset() noexcept {
    envelope_    = 0.0f;
    currentGain_ = 1.0f;
}

void Limiter::setThresholdDb(float dbfs) noexcept {
    if (dbfs < -30.0f) dbfs = -30.0f;
    if (dbfs >   0.0f) dbfs =   0.0f;
    thresholdDb_.store(dbfs, std::memory_order_relaxed);
    thresholdLin_.store(std::pow(10.0f, dbfs / 20.0f),
                        std::memory_order_relaxed);
}

void Limiter::process(float* buf, int numSamples) noexcept {
    if (! enabled_.load(std::memory_order_relaxed)) {
        // Bleed any in-flight gain reduction back to unity smoothly so a
        // re-enable doesn't start with stale gain.
        if (currentGain_ < 1.0f) {
            for (int i = 0; i < numSamples; ++i) {
                currentGain_ = envReleaseCoef_ * currentGain_
                             + (1.0f - envReleaseCoef_) * 1.0f;
            }
            if (currentGain_ > 0.99999f) currentGain_ = 1.0f;
        }
        reportedGRdb_.store(0.0f, std::memory_order_relaxed);
        return;
    }

    const float threshold = thresholdLin_.load(std::memory_order_relaxed);

    // Track minimum gain seen this block for the LED.
    float minGain = currentGain_;

    for (int i = 0; i < numSamples; ++i) {
        const float absIn = std::fabs(buf[i]);

        // Peak follower — instant attack, exponential release.
        if (absIn > envelope_) {
            envelope_ = absIn;
        } else {
            envelope_ = envReleaseCoef_ * envelope_;
        }

        // Brickwall target gain.
        float targetGain = 1.0f;
        if (envelope_ > threshold) {
            targetGain = threshold / envelope_;
        }

        // Smooth gain — fast when limiting kicks in (avoids clicks),
        // slow when releasing (avoids pumping).
        if (targetGain < currentGain_) {
            currentGain_ = gainAttackCoef_ * currentGain_
                         + (1.0f - gainAttackCoef_) * targetGain;
        } else {
            currentGain_ = envReleaseCoef_ * currentGain_
                         + (1.0f - envReleaseCoef_) * targetGain;
        }

        buf[i] *= currentGain_;

        if (currentGain_ < minGain) minGain = currentGain_;
    }

    // Update reported GR once per block — cheap log10 outside the loop.
    const float grDb = (minGain >= 1.0f - 1e-6f)
        ? 0.0f
        : -20.0f * std::log10(std::max(minGain, 1e-9f));
    reportedGRdb_.store(grDb, std::memory_order_relaxed);
}

} // namespace guitar_dsp::audio
