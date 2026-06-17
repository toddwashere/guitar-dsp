#pragma once

#include <atomic>
#include <cstddef>
#include <memory>

#include "OnsetDetector.h"
#include "TTSClip.h"
#include "VowelGrainLoop.h"

namespace guitar_dsp::audio {

// Plays a phoneme-aligned TTSClip one syllable per onset, with vowel
// sustain via VowelGrainLoop. Parallel to NoteSteppedTTSPlayer; does
// NOT replace it.
//
// Allocation-free in process().
class PhonemeSteppedTTSPlayer {
public:
    enum class State { Idle, Attack, Sustain, Coda };

    PhonemeSteppedTTSPlayer();
    void prepare(double sampleRate, int blockSize);
    void reset();

    void setClip(TTSClipPtr clip);                     // message thread
    void setMaxSustainMs(double ms) noexcept;          // message thread
    void setLoop(bool on) noexcept { loop_.store(on, std::memory_order_relaxed); }
    void rewind() noexcept;

    void process(const float* onsetSrc, float* modOut,
                 std::size_t numSamples) noexcept;     // audio thread

    int currentSyllableIndex() const noexcept {
        return currentSylIdx_.load(std::memory_order_relaxed);
    }
    // Latest sample position into the active clip (-1 when idle).
    // Published once per process block; safe to read from the message thread.
    // During Sustain (grain loop), reports the syllable's vowel-nucleus
    // sample — the playhead "parks" at the vowel while the grain loops.
    int currentPlaySample() const noexcept {
        return currentPlaySample_.load(std::memory_order_relaxed);
    }
    // Number of syllables in the currently active clip (0 if no clip or empty).
    // Safe from the message thread — the shared_ptr count is stable once set.
    std::size_t syllableCount() const noexcept {
        if (!activeClip_) return 0;
        return activeClip_->sylsV2.size();
    }
    State currentState() const noexcept {
        return static_cast<State>(currentState_.load(std::memory_order_relaxed));
    }

private:
    void advanceToNext_();   // RT-safe
    void enterSustain_();
    void enterCoda_();

    OnsetDetector onset_;
    VowelGrainLoop grain_;

    std::atomic<bool> newClipFlag_{false};
    std::atomic<bool> pendingRewind_{false};
    TTSClipPtr pendingClip_;
    TTSClipPtr activeClip_;

    int   sylIdx_   = -1;
    std::size_t playPos_ = 0;
    State state_ = State::Idle;
    std::size_t sustainSamplesPlayed_ = 0;
    std::size_t maxSustainSamples_ = 72000;  // 1.5 s @ 48 kHz, default
    double sampleRate_ = 48000.0;

    std::atomic<int>  currentSylIdx_{-1};
    std::atomic<int>  currentState_{0};
    std::atomic<int>  currentPlaySample_{-1};
    std::atomic<bool> loop_{true};
};

} // namespace guitar_dsp::audio
