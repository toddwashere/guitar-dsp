#include "NoteSteppedTTSPlayer.h"

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
    mode_.store(static_cast<int>(m), std::memory_order_relaxed);
}
WordSyncMode NoteSteppedTTSPlayer::mode() const noexcept {
    return static_cast<WordSyncMode>(mode_.load(std::memory_order_relaxed));
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

    const bool haveClip      = activeClip_ && !activeClip_->samples.empty();
    const auto modeNow       = static_cast<WordSyncMode>(mode_.load(std::memory_order_relaxed));
    const bool wantSyllables = (modeNow == WordSyncMode::Syllable)
                                && haveClip
                                && !activeClip_->syllables.empty();
    const std::vector<WordSegment>* segments = nullptr;
    if (haveClip)
        segments = wantSyllables ? &activeClip_->syllables : &activeClip_->words;
    const bool haveSegments  = (segments != nullptr) && !segments->empty();

    for (std::size_t i = 0; i < numSamples; ++i) {
        if (onset_.processSample(onsetSrc[i]) && haveClip) {
            const bool latchHolds = (modeNow == WordSyncMode::Latch
                                      || modeNow == WordSyncMode::Syllable)
                                    && playing_;
            if (!latchHolds) {
                if (haveSegments) {
                    const int n = static_cast<int>(segments->size());
                    wordIndex_ = (wordIndex_ + 1) % n;
                    playPos_ = (*segments)[static_cast<std::size_t>(wordIndex_)].startSample;
                    segEnd_  = (*segments)[static_cast<std::size_t>(wordIndex_)].endSample;
                } else {
                    wordIndex_ = 0;
                    playPos_ = 0;
                    segEnd_ = activeClip_->samples.size();
                }
                playing_ = true;
                currentWordIndex_.store(wordIndex_, std::memory_order_relaxed);
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
