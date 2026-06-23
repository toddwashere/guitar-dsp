#pragma once

#include "ClipBankPlayer.h"
#include "FormantShifter.h"
#include "GrainAnalyser.h"
#include "TTSClip.h"
#include "VowelGrainLoop.h"

#include <atomic>
#include <memory>
#include <unordered_map>
#include <vector>

namespace guitar_dsp::audio {

class SungDirectPath {
public:
    void prepare(double sampleRate, int blockSize);
    void reset();

    // Message thread. Pre-analyses each grain's WORLD parameters and caches
    // them by clip pointer identity. Re-bank == re-analyse only new grains.
    void setGrainsForBank(const std::vector<TTSClipPtr>& bank);

    void setPortamentoMs(float ms) noexcept       { portamentoMs_.store(ms, std::memory_order_relaxed); }
    void setFormantTintSemitones(float n) noexcept { shifter_.setFormantTintSemitones(n); }

    // Audio thread.
    void process(const float* guitarIn, float detectedHz, float* wetOut,
                 std::size_t numSamples) noexcept;

private:
    double          sampleRate_   = 48000.0;
    int             blockSize_    = 256;

    ClipBankPlayer  clipBank_;       // re-used for onset + grain selection
    FormantShifter  shifter_;
    VowelGrainLoop  vowelLoop_;

    std::vector<float> grainOutBuf_;  // ClipBankPlayer's per-block output (used to detect "playing")

    // Grain analysis cache: TTSClip pointer → raw ShifterGrain (WORLD params).
    // Built offline in setGrainsForBank().
    std::unordered_map<const TTSClip*, std::shared_ptr<const ShifterGrain>> analysisCache_;

    // Pre-rendered cache: TTSClip pointer → ShifterGrain with preRendered filled.
    // Built offline in setGrainsForBank() via FormantShifter::preRenderGrain().
    // Audio-thread setSource() calls always use this cache, which contains no
    // un-rendered grains — so setSource() never calls Synthesis() on the RT thread.
    std::unordered_map<const TTSClip*, std::shared_ptr<const ShifterGrain>> prerenderedCache_;

    // Per-block: last detected pitch published to shifter as ratio.
    std::atomic<float> portamentoMs_ {40.0f};
    float              smoothedRatio_ = 1.0f;

    // Tracks the last-seen active clip index so we detect changes without
    // a static thread_local (which is problematic if the audio thread
    // changes across OS context switches or tests).
    int   lastSourceIdx_    = -2;
    // Anchor pitch of the currently-active grain; updated when the grain
    // changes. Used to convert detectedHz → a ratio relative to this grain.
    float currentAnchorHz_  = 0.0f;
};

} // namespace guitar_dsp::audio
