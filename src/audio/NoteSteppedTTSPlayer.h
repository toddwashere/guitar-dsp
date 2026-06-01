#pragma once

#include <atomic>
#include <cstddef>

#include "OnsetDetector.h"
#include "TTSClip.h"

namespace guitar_dsp::audio {

// Plays a segmented TTSClip one word per guitar-note onset. An internal
// OnsetDetector watches the clean guitar (passed to process as onsetSrc); each
// onset advances to the next word (wrapping after the last) and plays that
// word's segment as the vocoder modulator. setClip uses the same atomic swap
// as TTSClipPlayer. Allocation-free in process().
class NoteSteppedTTSPlayer {
public:
    NoteSteppedTTSPlayer();

    void prepare(double sampleRate, int blockSize);
    void reset();

    // Message thread. Pass nullptr to clear.
    void setClip(TTSClipPtr clip);

    // Audio thread. onsetSrc = clean guitar; writes the modulator to modOut.
    void process(const float* onsetSrc, float* modOut, std::size_t numSamples) noexcept;

    // UI: current spoken word index, or -1 when idle.
    int currentWordIndex() const noexcept {
        return currentWordIndex_.load(std::memory_order_relaxed);
    }

private:
    OnsetDetector onset_;

    std::atomic<bool> newClipFlag_ {false};
    TTSClipPtr        pendingClip_;
    TTSClipPtr        activeClip_;

    int         wordIndex_  = -1;
    std::size_t playPos_    = 0;
    std::size_t segEnd_     = 0;
    bool        playing_    = false;

    std::atomic<int> currentWordIndex_ {-1};
};

} // namespace guitar_dsp::audio
