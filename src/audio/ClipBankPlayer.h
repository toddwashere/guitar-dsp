#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

#include "OnsetDetector.h"
#include "TTSClip.h"

namespace guitar_dsp::audio {

// Plays a bank of short audio clips, advancing one clip per detected guitar
// onset. Each clip is an atomic unit — no internal segmentation. After the
// active clip finishes, output is silence until the next onset.
//
// RT-safe in process(); allocates only in setBank() (message thread).
class ClipBankPlayer {
public:
    ClipBankPlayer();

    void prepare(double sampleRate, int blockSize);
    void reset();

    // Message thread. Swap the active bank atomically. Pass an empty vector
    // to clear. Bank order is the playback order.
    void setBank(std::vector<TTSClipPtr> clips);

    // Message thread. Reset cursor to "before the first clip"; next onset
    // plays clip 0. RT-safe via pending flag.
    void rewind() noexcept;

    // Audio thread.
    //   onsetSrc = clean guitar (drives OnsetDetector)
    //   modOut   = vocoder modulator output for this block
    void process(const float* onsetSrc, float* modOut, std::size_t numSamples) noexcept;

    // UI: current bank cursor (clip index), or -1 if idle / no clip yet played.
    int currentClipIndex() const noexcept {
        return currentClipIndex_.load(std::memory_order_relaxed);
    }
    int bankSize() const noexcept {
        return bankSize_.load(std::memory_order_relaxed);
    }

private:
    OnsetDetector onset_;

    std::atomic<bool> newBankFlag_ {false};
    std::vector<TTSClipPtr> pendingBank_;
    std::vector<TTSClipPtr> activeBank_;

    int         cursor_     = -1;   // last-triggered clip index
    std::size_t playPos_    = 0;    // sample offset within active clip
    bool        playing_    = false;

    std::atomic<int>  currentClipIndex_ {-1};
    std::atomic<int>  bankSize_         {0};
    std::atomic<bool> pendingRewind_    {false};
};

} // namespace guitar_dsp::audio
