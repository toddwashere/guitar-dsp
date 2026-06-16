#include "PhonemeSteppedTTSPlayer.h"

#include <algorithm>

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
                if (playPos_ < syl.attackEndSample
                    && playPos_ < activeClip_->samples.size()) {
                    s = activeClip_->samples[playPos_++];
                } else {
                    if (syl.nucleusIsFricative) enterCoda_();
                    else enterSustain_();
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
}

} // namespace guitar_dsp::audio
