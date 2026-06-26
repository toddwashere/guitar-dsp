#include "PhonemeSteppedTTSPlayer.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

PhonemeSteppedTTSPlayer::PhonemeSteppedTTSPlayer() = default;

void PhonemeSteppedTTSPlayer::prepare(double sr, int /*blockSize*/) {
    sampleRate_ = sr;
    onset_.prepare(sr);
    grain_.prepare(sr);
    reset();
}

void PhonemeSteppedTTSPlayer::reset() {
    onset_.reset();
    grain_.reset();
    sylIdx_ = -1;
    playPos_ = 0;
    state_ = State::Idle;
    sustainSamplesPlayed_ = 0;
    currentSylIdx_.store(-1, std::memory_order_relaxed);
    currentState_.store(0, std::memory_order_relaxed);
    currentPlaySample_.store(-1, std::memory_order_relaxed);
}

void PhonemeSteppedTTSPlayer::setClip(TTSClipPtr c) {
    pendingClip_ = std::move(c);
    newClipFlag_.store(true, std::memory_order_release);
}

void PhonemeSteppedTTSPlayer::setMaxSustainMs(double ms) noexcept {
    maxSustainSamples_ = static_cast<std::size_t>(ms * 0.001 * sampleRate_);
}

void PhonemeSteppedTTSPlayer::rewind() noexcept {
    pendingRewind_.store(true, std::memory_order_release);
}

void PhonemeSteppedTTSPlayer::setOnsetSensitivityDb(float dB) noexcept {
    const float attackLin = std::pow(10.0f, dB         * 0.05f);
    const float rearmLin  = std::pow(10.0f, (dB - 8.0f) * 0.05f);
    onset_.setAttackThreshold(attackLin);
    onset_.setRearmThreshold (rearmLin);
}

void PhonemeSteppedTTSPlayer::advanceToNext_() {
    if (!activeClip_) return;
    const auto& syls = activeClip_->sylsV2;
    if (syls.empty()) { state_ = State::Idle; return; }
    int next = sylIdx_ + 1;
    if (next >= static_cast<int>(syls.size())) {
        if (loop_.load(std::memory_order_relaxed)) next = 0;
        else { state_ = State::Idle; return; }
    }
    sylIdx_ = next;
    playPos_ = syls[next].startSample;
    state_ = State::Attack;
    sustainSamplesPlayed_ = 0;
    currentSylIdx_.store(sylIdx_, std::memory_order_relaxed);
    currentState_.store(static_cast<int>(state_), std::memory_order_relaxed);
}

void PhonemeSteppedTTSPlayer::enterSustain_() {
    state_ = State::Sustain;
    sustainSamplesPlayed_ = 0;
    currentState_.store(static_cast<int>(state_), std::memory_order_relaxed);
    grain_.beginLoop(activeClip_->samples.data(),
                     activeClip_->samples.size(),
                     activeClip_->sylsV2[sylIdx_].vowelNucleusSample);
}

void PhonemeSteppedTTSPlayer::enterCoda_() {
    state_ = State::Coda;
    playPos_ = activeClip_->sylsV2[sylIdx_].codaStartSample;
    currentState_.store(static_cast<int>(state_), std::memory_order_relaxed);
}

void PhonemeSteppedTTSPlayer::process(const float* onsetSrc, float* modOut,
                                      std::size_t numSamples) noexcept {
    if (newClipFlag_.exchange(false, std::memory_order_acquire)) {
        activeClip_ = std::move(pendingClip_);
        reset();
    }
    if (pendingRewind_.exchange(false, std::memory_order_acquire)) reset();

    const bool haveClip = activeClip_ && !activeClip_->sylsV2.empty();

    for (std::size_t i = 0; i < numSamples; ++i) {
        const bool onset = onset_.processSample(onsetSrc[i]) && haveClip;
        if (onset) {
            // From Idle: advance. From Attack: defer (finish current). From
            // Sustain: enter Coda (current syllable plays its tail, then
            // next onset will advance). From Coda: advance.
            if (state_ == State::Idle)    advanceToNext_();
            else if (state_ == State::Sustain) enterCoda_();
            else if (state_ == State::Coda)    advanceToNext_();
            // Attack: ignore (default "finish" policy).
        }

        float s = 0.0f;
        if (haveClip) {
            const auto& syl = activeClip_->sylsV2[sylIdx_ >= 0 ? sylIdx_ : 0];
            if (state_ == State::Attack) {
                // When Sustain is disabled (maxSustainSamples_==0), Attack extends
                // all the way to syl.endSample — playing the entire syllable
                // linearly. Without this, the state machine would jump from
                // attackEndSample to codaStartSample, skipping the entire vowel
                // body that carries most of the formant / word identity.
                const std::size_t attackBound = (maxSustainSamples_ == 0)
                                                ? syl.endSample
                                                : syl.attackEndSample;
                if (playPos_ < attackBound
                    && playPos_ < activeClip_->samples.size()) {
                    s = activeClip_->samples[playPos_++];
                } else {
                    if (maxSustainSamples_ == 0) {
                        state_ = State::Idle;
                        currentState_.store(0, std::memory_order_relaxed);
                    } else if (syl.nucleusIsFricative) {
                        enterCoda_();
                    } else {
                        enterSustain_();
                    }
                }
            } else if (state_ == State::Sustain) {
                s = grain_.next();
                ++sustainSamplesPlayed_;
                if (sustainSamplesPlayed_ >= maxSustainSamples_) enterCoda_();
            } else if (state_ == State::Coda) {
                if (playPos_ < syl.endSample
                    && playPos_ < activeClip_->samples.size()) {
                    s = activeClip_->samples[playPos_++];
                } else {
                    state_ = State::Idle;
                    currentState_.store(0, std::memory_order_relaxed);
                }
            }
        }
        modOut[i] = s;
    }

    // Publish the end-of-block play position so the UI (30 Hz polling) can
    // draw a playhead inside the active syllable. During Sustain the grain
    // loop bounces around the vowel nucleus — we park the visible playhead
    // there because the audience-facing message is "I'm holding this vowel,"
    // not the grain offset.
    int published = -1;
    if (haveClip && state_ != State::Idle && sylIdx_ >= 0) {
        if (state_ == State::Sustain)
            published = static_cast<int>(activeClip_->sylsV2[sylIdx_].vowelNucleusSample);
        else
            published = static_cast<int>(playPos_);
    }
    currentPlaySample_.store(published, std::memory_order_relaxed);
}

} // namespace guitar_dsp::audio
