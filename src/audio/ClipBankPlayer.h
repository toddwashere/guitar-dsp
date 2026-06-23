#pragma once

#include <atomic>
#include <cstddef>
#include <string>
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

    // Message or audio thread. Latest YIN-detected pitch in Hz, used by
    // anchor-aware selection. 0 means "unknown" — selection falls back to
    // the first grain of the current bank key.
    void setDetectedPitchHz(float hz) noexcept {
        detectedPitchHz_.store(hz, std::memory_order_relaxed);
    }

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

    // Audio thread. Returns the clip at `idx` in the active bank, or nullptr
    // if idx is out of range. Used by SungDirectPath to mirror source changes
    // into the FormantShifter without touching the pending bank.
    TTSClipPtr activeBankAt(int idx) const noexcept {
        if (idx < 0 || idx >= static_cast<int>(activeBank_.size())) return {};
        return activeBank_[static_cast<std::size_t>(idx)];
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

    std::atomic<float> detectedPitchHz_ {0.0f};

    // Anchor mode state. Engaged when activeBank_'s first clip has bankKey != "".
    bool                     anchorMode_   = false;
    std::vector<std::string> uniqueKeys_;     // ordered, first-appearance
    int                      keyCursor_    = -1;
};

} // namespace guitar_dsp::audio
