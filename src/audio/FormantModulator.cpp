#include "FormantModulator.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

void FormantModulator::prepare(double sampleRate) {
    sampleRate_ = sampleRate;
    onset_.prepare(sampleRate);
    reset();
}

void FormantModulator::reset() {
    onset_.reset();
    lfoPhase_      = 0.0f;
    envIndex_      = -1;
    envCurrentPos_ = 0.0f;
    envTargetPos_  = 0.0f;
}

void FormantModulator::setMode(Mode m) noexcept { mode_ = m; }
void FormantModulator::setStaticPosition(float p) noexcept { staticPos_ = p; }
void FormantModulator::setBreakpoints(std::vector<float> bp) noexcept {
    breakpoints_ = std::move(bp);
}
void FormantModulator::setLfoRateHz(float hz) noexcept {
    lfoIncrPerSample_ = (sampleRate_ > 0.0) ? static_cast<float>(hz / sampleRate_) : 0.0f;
}
void FormantModulator::setEnvelopeAttackMs(float ms) noexcept {
    envAttackMs_ = ms;
    const float samples = static_cast<float>(sampleRate_ * ms / 1000.0);
    envRampPerSample_ = (samples > 0.0f) ? (1.0f / samples) : 1.0f;
}

void FormantModulator::process(const float* onsetSrc, float* posOut,
                                std::size_t numSamples) noexcept {
    switch (mode_) {
        case Mode::Static: {
            std::fill(posOut, posOut + numSamples, staticPos_);
            break;
        }
        case Mode::Lfo: {
            if (breakpoints_.empty()) {
                std::fill(posOut, posOut + numSamples, 0.0f);
                break;
            }
            const int n = static_cast<int>(breakpoints_.size());
            for (std::size_t i = 0; i < numSamples; ++i) {
                float phase = lfoPhase_;
                const float halfFold = (phase < 0.5f) ? (phase * 2.0f)
                                                       : ((1.0f - phase) * 2.0f);
                const float scaled = halfFold * static_cast<float>(n - 1);
                const int lo = std::min(static_cast<int>(scaled), n - 1);
                const int hi = std::min(lo + 1, n - 1);
                const float frac = scaled - static_cast<float>(lo);
                posOut[i] = (1.0f - frac) * breakpoints_[static_cast<std::size_t>(lo)]
                          + frac        * breakpoints_[static_cast<std::size_t>(hi)];

                lfoPhase_ += lfoIncrPerSample_;
                if (lfoPhase_ >= 1.0f) lfoPhase_ -= 1.0f;
            }
            break;
        }
        case Mode::Envelope: {
            if (breakpoints_.empty()) {
                std::fill(posOut, posOut + numSamples, 0.0f);
                break;
            }
            for (std::size_t i = 0; i < numSamples; ++i) {
                if (onset_.processSample(onsetSrc[i])) {
                    const int n = static_cast<int>(breakpoints_.size());
                    envIndex_ = (envIndex_ + 1) % n;
                    envTargetPos_ = breakpoints_[static_cast<std::size_t>(envIndex_)];
                }
                if (envCurrentPos_ < envTargetPos_) {
                    envCurrentPos_ += envRampPerSample_;
                    if (envCurrentPos_ > envTargetPos_) envCurrentPos_ = envTargetPos_;
                } else if (envCurrentPos_ > envTargetPos_) {
                    envCurrentPos_ -= envRampPerSample_;
                    if (envCurrentPos_ < envTargetPos_) envCurrentPos_ = envTargetPos_;
                }
                posOut[i] = envCurrentPos_;
            }
            break;
        }
    }
}

} // namespace guitar_dsp::audio
