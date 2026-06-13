#pragma once

#include <atomic>
#include <cstddef>

#include "OnsetDetector.h"
#include "TTSClip.h"
#include "WordSyncMode.h"

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

    // Message thread. Default is WordSyncMode::Latch.
    void setMode(WordSyncMode m) noexcept;
    WordSyncMode mode() const noexcept;

    // Message thread. Reset playback to the start of the sequence — next
    // onset plays segment 0 again. Useful when a phrase desyncs from the
    // performer (bad WordAligner output, lost a pluck, etc.). The actual
    // reset happens on the next audio block via an atomic pending flag,
    // so the call is RT-safe.
    void rewind() noexcept;

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

    std::atomic<int>  currentWordIndex_ {-1};
    std::atomic<int>  mode_ {static_cast<int>(WordSyncMode::Latch)};
    std::atomic<bool> pendingRewind_ {false};
};

} // namespace guitar_dsp::audio
