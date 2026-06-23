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

    // Grain analysis cache: TTSClip pointer → ShifterGrain.
    std::unordered_map<const TTSClip*, std::shared_ptr<const ShifterGrain>> analysisCache_;

    // Per-block: last detected pitch published to shifter as ratio.
    std::atomic<float> portamentoMs_ {40.0f};
    float              smoothedRatio_ = 1.0f;
};

} // namespace guitar_dsp::audio
