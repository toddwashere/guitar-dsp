#pragma once

#include "ClipBankPlayer.h"
#include "FormantShifter.h"
#include "GrainAnalyser.h"
#include "TTSClip.h"
#include "VowelGrainLoop.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <unordered_map>
#include <vector>

namespace guitar_dsp::audio {

class SungDirectPath {
public:
    SungDirectPath() = default;
    ~SungDirectPath();

    void prepare(double sampleRate, int blockSize);
    void reset();

    // Message thread. Kicks off a BACKGROUND thread that:
    //   1. Analyses each grain (WORLD Harvest/CheapTrick/D4C)
    //   2. Pre-renders the configured kNumRatios ratio variants per grain
    //   3. Atomically installs the resulting cache + primes the shifter
    //
    // The call itself returns immediately. Until the background work is
    // done, the audio thread sees no source on the shifter and emits
    // silence. Polling progress is via loadState() / loadProgressPercent().
    //
    // Re-calling cancels any in-flight pre-render and starts fresh.
    //
    // `bundleHash` (if non-empty) is the cache key for the on-disk .bake
    // file (see PrerenderCache). When provided, the loader checks for a
    // cached file before kicking off the WORLD render — turning the 90 s
    // first-activation wait into a sub-second mmap read on subsequent
    // activations of the same bundle.
    void setGrainsForBank(const std::vector<TTSClipPtr>& bank,
                          const std::string& bundleHash = {});

    void setPortamentoMs(float ms) noexcept       { portamentoMs_.store(ms, std::memory_order_relaxed); }
    void setFormantTintSemitones(float n) noexcept { shifter_.setFormantTintSemitones(n); }

    // Forwards to the embedded ClipBankPlayer. See ClipBankPlayer::setEnabledKeysMask.
    void setEnabledKeysMask(std::uint32_t mask) noexcept {
        clipBank_.setEnabledKeysMask(mask);
    }
    // Forwards to the embedded ClipBankPlayer's onset detector.
    void setOnsetSensitivityDb(float dB) noexcept {
        clipBank_.setOnsetSensitivityDb(dB);
    }

    // ---- Background-render load state for UI -------------------------------
    enum class LoadState { Idle, Loading, Ready };
    LoadState loadState() const noexcept {
        return static_cast<LoadState>(loadState_.load(std::memory_order_acquire));
    }
    int loadProgressPercent() const noexcept {
        return loadProgressPercent_.load(std::memory_order_relaxed);
    }

    // Audio thread.
    void process(const float* guitarIn, float detectedHz, float* wetOut,
                 std::size_t numSamples) noexcept;

private:
    using PrerenderedMap =
        std::unordered_map<const TTSClip*, std::shared_ptr<const ShifterGrain>>;

    // Background-thread machinery.
    void cancelAndJoinPreRender_();

    double          sampleRate_   = 48000.0;
    int             blockSize_    = 256;

    ClipBankPlayer  clipBank_;
    FormantShifter  shifter_;
    VowelGrainLoop  vowelLoop_;

    std::vector<float> grainOutBuf_;

    // Active pre-rendered cache: atomic shared_ptr swapped in by the
    // background thread. The audio thread reads via atomic_load and
    // outputs silence when this is null (i.e. pre-render in flight or
    // not yet started).
    std::shared_ptr<PrerenderedMap> activePrerendered_;

    // Background pre-render thread + control flags.
    std::thread       preRenderThread_;
    std::atomic<bool> cancelToken_         {false};
    std::atomic<int>  loadState_           {static_cast<int>(LoadState::Idle)};
    std::atomic<int>  loadProgressPercent_ {0};

    // Per-block: portamento + smoothed ratio.
    std::atomic<float> portamentoMs_ {40.0f};
    float              smoothedRatio_ = 1.0f;

    // Audio-thread state to detect grain-index changes.
    int   lastSourceIdx_    = -2;
    float currentAnchorHz_  = 0.0f;
};

} // namespace guitar_dsp::audio
