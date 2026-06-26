#include "NoteSteppedTTSPlayer.h"

#include <cmath>

namespace guitar_dsp::audio {

NoteSteppedTTSPlayer::NoteSteppedTTSPlayer() = default;

void NoteSteppedTTSPlayer::prepare(double sampleRate, int /*blockSize*/) {
    onset_.prepare(sampleRate);
    reset();
}

void NoteSteppedTTSPlayer::reset() {
    onset_.reset();
    wordIndex_ = -1;
    playPos_ = 0;
    segEnd_ = 0;
    playing_ = false;
    currentWordIndex_.store(-1, std::memory_order_relaxed);
}

void NoteSteppedTTSPlayer::setClip(TTSClipPtr clip) {
    pendingClip_ = std::move(clip);
    newClipFlag_.store(true, std::memory_order_release);
}

void NoteSteppedTTSPlayer::setMode(WordSyncMode m) noexcept {
    // If the mode actually changes, also rewind. The segment list being
    // stepped through likely differs (words ⇄ syllables), so a carryover
    // wordIndex_ would jump mid-phrase on the next onset.
    const int prev = mode_.exchange(static_cast<int>(m), std::memory_order_relaxed);
    if (prev != static_cast<int>(m))
        pendingRewind_.store(true, std::memory_order_release);
}
WordSyncMode NoteSteppedTTSPlayer::mode() const noexcept {
    return static_cast<WordSyncMode>(mode_.load(std::memory_order_relaxed));
}

void NoteSteppedTTSPlayer::rewind() noexcept {
    pendingRewind_.store(true, std::memory_order_release);
}

void NoteSteppedTTSPlayer::setOnsetSensitivityDb(float dB) noexcept {
    const float attackLin = std::pow(10.0f, dB         * 0.05f);
    const float rearmLin  = std::pow(10.0f, (dB - 8.0f) * 0.05f);
    onset_.setAttackThreshold(attackLin);
    onset_.setRearmThreshold (rearmLin);
}

void NoteSteppedTTSPlayer::process(const float* onsetSrc, float* modOut,
                                   std::size_t numSamples) noexcept {
    if (newClipFlag_.exchange(false, std::memory_order_acquire)) {
        activeClip_ = std::move(pendingClip_);
        wordIndex_ = -1;
        playPos_ = 0;
        segEnd_ = 0;
        playing_ = false;
        onset_.reset();
        currentWordIndex_.store(-1, std::memory_order_relaxed);
    }

    if (pendingRewind_.exchange(false, std::memory_order_acquire)) {
        wordIndex_ = -1;
        playPos_ = 0;
        segEnd_ = 0;
        playing_ = false;
        onset_.reset();
        currentWordIndex_.store(-1, std::memory_order_relaxed);
    }

    const bool haveClip      = activeClip_ && !activeClip_->samples.empty();
    const auto modeNow       = static_cast<WordSyncMode>(mode_.load(std::memory_order_relaxed));
    const bool wantSyllables = (modeNow == WordSyncMode::Syllable)
                                && haveClip
                                && !activeClip_->syllables.empty();
    const std::vector<WordSegment>* segments = nullptr;
    if (haveClip)
        segments = wantSyllables ? &activeClip_->syllables : &activeClip_->words;
    const bool haveSegments  = (segments != nullptr) && !segments->empty();

    const bool loopNow = loop_.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < numSamples; ++i) {
        if (onset_.processSample(onsetSrc[i]) && haveClip) {
            const bool latchHolds = (modeNow == WordSyncMode::Latch
                                      || modeNow == WordSyncMode::Syllable)
                                    && playing_;
            if (!latchHolds) {
                if (haveSegments) {
                    const int n = static_cast<int>(segments->size());
                    int nextIndex = wordIndex_ + 1;
                    if (nextIndex >= n) {
                        if (loopNow) nextIndex = 0;
                        else         nextIndex = -1;   // sentinel: stop, don't advance
                    }
                    if (nextIndex >= 0) {
                        wordIndex_ = nextIndex;
                        playPos_ = (*segments)[static_cast<std::size_t>(wordIndex_)].startSample;
                        segEnd_  = (*segments)[static_cast<std::size_t>(wordIndex_)].endSample;
                        playing_ = true;
                        currentWordIndex_.store(wordIndex_, std::memory_order_relaxed);
                    }
                    // else: end-of-clip in non-loop mode; ignore the onset
                    // and leave wordIndex_ at the last segment. Rewind() or
                    // setClip() resets.
                } else {
                    wordIndex_ = 0;
                    playPos_ = 0;
                    segEnd_ = activeClip_->samples.size();
                    playing_ = true;
                    currentWordIndex_.store(wordIndex_, std::memory_order_relaxed);
                }
            }
        }

        float s = 0.0f;
        if (playing_ && playPos_ < segEnd_ && playPos_ < activeClip_->samples.size()) {
            s = activeClip_->samples[playPos_++];
        } else {
            playing_ = false;
        }
        modOut[i] = s;
    }
}

} // namespace guitar_dsp::audio
