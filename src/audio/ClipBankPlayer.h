#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
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

    // Audio thread (same thread as process). Returns the current note-off
    // gate gain: 1.0 while a grain is sounding, ramps down during the
    // release fade triggered by guitar going silent, 0.0 once gated off.
    // SungDirectPath uses this to gate FormantShifter output in lockstep.
    //
    // While the post-onset minimum-hold window is active, returns 1.0
    // unconditionally — this lets downstream consumers (e.g. the SungDirect
    // shifter, which loops the analysed grain) sustain past the natural
    // length of the source clip without the gate clipping them.
    float currentGateGain() const noexcept {
        if (gateHoldRemaining_ > 0) return 1.0f;
        if (!playing_) return 0.0f;
        return gateFadingOut_ ? std::max(0.0f, gateFadeGain_) : 1.0f;
    }

    // Message thread. Minimum window during which the gate stays fully open
    // after each onset, regardless of guitar input amplitude. Prevents
    // staccato plucks from cutting a sustained-vowel scene short. Set to 0
    // to disable (default — preserves the legacy "track guitar amplitude
    // exactly" behaviour used by scenes 2 and 11).
    void setGateMinHoldMs(float ms) noexcept {
        gateMinHoldMs_ = std::max(0.0f, ms);
        recomputeGateTimes_();
    }
    // Message thread. Length of the smooth fade-to-silence applied once the
    // gate releases (after hold + hangover). Larger values give consecutive
    // notes a natural overlap; smaller values are tighter. Default 8 ms.
    void setGateReleaseMs(float ms) noexcept {
        gateReleaseMs_ = std::max(0.1f, ms);
        recomputeGateTimes_();
    }

    // Message thread. Onset detector attack threshold in dBFS — softer plucks
    // register when this is lower (more negative). Re-arm threshold sits 8 dB
    // below to provide hysteresis against double-triggers. Forwarded to the
    // embedded OnsetDetector.
    void setOnsetSensitivityDb(float dB) noexcept;

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

    // ---- Note-off gate: stop the current grain shortly after the guitar
    // goes silent, so a held grain doesn't keep playing into a quiet
    // passage. Works alongside the onset-triggered restart — a new pluck
    // cancels any in-progress fade and starts a fresh grain.
    //
    // Audio-thread state only — never touched from message thread.
    float gateEnv_              = 0.0f;   // peak-follower amplitude
    float gateReleaseCoef_      = 0.0f;   // 1-pole release; set in prepare()
    int   gateSilenceCounter_   = 0;      // samples spent below threshold
    int   gateHangoverSamples_  = 0;      // grace period before fade starts
    float gateFadeGain_         = 1.0f;   // current fade multiplier during stop
    float gateFadeStep_         = 0.0f;   // per-sample decrement; set in prepare()
    bool  gateFadingOut_        = false;
    int   gateMinHoldSamples_   = 0;      // length of post-onset open window
    int   gateHoldRemaining_    = 0;      // countdown during hold window
    static constexpr float kGateSilenceThreshold = 0.060f;  // ≈ −24 dBFS — catches
                                                            // typical guitar decay
                                                            // before the natural ring
                                                            // overwhelms the gate.

    // Configured gate times (ms). Defaults preserve legacy behaviour. Caller
    // tunes via setGateMinHoldMs / setGateReleaseMs; values get applied to
    // sample-domain members on the next prepare() or setter call.
    double sampleRate_     = 48000.0;
    float  gateMinHoldMs_  = 0.0f;
    float  gateReleaseMs_  = 8.0f;
    void   recomputeGateTimes_() noexcept;

public:
    // Message thread. Enabled-keys bitmask for anchor-mode rotation. Bit i
    // corresponds to uniqueKeys_[i] (first-appearance order). If a key is
    // disabled, anchor-mode cycling skips it. Default 0xFFFFFFFF — all keys
    // enabled. Has no effect in legacy round-robin mode.
    void setEnabledKeysMask(std::uint32_t mask) noexcept {
        enabledKeysMask_.store(mask, std::memory_order_relaxed);
    }

private:
    std::atomic<std::uint32_t> enabledKeysMask_ {0xFFFFFFFFu};
};

} // namespace guitar_dsp::audio
