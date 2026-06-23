#include "SungDirectPath.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

void SungDirectPath::prepare(double sampleRate, int blockSize) {
    sampleRate_ = sampleRate;
    blockSize_  = std::max(64, blockSize);
    clipBank_.prepare(sampleRate, blockSize_);
    shifter_.prepare(sampleRate, blockSize_);
    vowelLoop_.prepare(sampleRate);
    grainOutBuf_.assign(static_cast<std::size_t>(blockSize_), 0.0f);
    smoothedRatio_   = 1.0f;
    lastSourceIdx_   = -2;
    currentAnchorHz_ = 0.0f;
}

void SungDirectPath::reset() {
    clipBank_.reset();
    shifter_.reset();
    vowelLoop_.reset();
    smoothedRatio_   = 1.0f;
    lastSourceIdx_   = -2;
    currentAnchorHz_ = 0.0f;
}

void SungDirectPath::setGrainsForBank(const std::vector<TTSClipPtr>& bank) {
    // Step 1: Run WORLD analysis for any new grains (offline, allocates freely).
    for (const auto& c : bank) {
        if (!c || c->samples.empty()) continue;
        if (analysisCache_.count(c.get())) continue;
        auto raw = analyseGrain(c->samples.data(),
                                static_cast<int>(c->samples.size()),
                                static_cast<int>(c->sampleRate));
        if (raw) analysisCache_[c.get()] = raw;
    }

    // Step 2: Pre-render ratio variants for any grains not yet in the
    // pre-rendered cache. This calls WORLD's Synthesis() kNumRatios times
    // per grain — all offline (message thread). The audio thread never
    // needs to call Synthesis() again.
    for (const auto& c : bank) {
        if (!c) continue;
        if (prerenderedCache_.count(c.get())) continue;
        auto rawIt = analysisCache_.find(c.get());
        if (rawIt == analysisCache_.end()) continue;
        auto prerendered = FormantShifter::preRenderGrain(rawIt->second);
        if (prerendered)
            prerenderedCache_[c.get()] = std::move(prerendered);
    }

    // Reset source-change tracking so process() re-fires on the first block.
    lastSourceIdx_   = -2;
    currentAnchorHz_ = 0.0f;

    clipBank_.setBank(bank);

    // Prime the shifter with the first pre-rendered grain in the bank so
    // process() produces non-silent output before any clip-index transition
    // is detected. setSource() here is a near-zero-cost atomic pointer swap
    // (the pre-rendered buffers are already populated above).
    for (const auto& c : bank) {
        if (!c) continue;
        auto it = prerenderedCache_.find(c.get());
        if (it != prerenderedCache_.end()) {
            shifter_.setSource(it->second);
            currentAnchorHz_ = c->anchorPitchHz;
            break;
        }
    }
}

void SungDirectPath::process(const float* guitarIn, float detectedHz,
                             float* wetOut, std::size_t numSamples) noexcept {
    // Publish detected pitch so ClipBankPlayer's anchor-mode selection uses it.
    clipBank_.setDetectedPitchHz(detectedHz);

    // Run ClipBankPlayer to drive onset detection + grain selection.
    // We discard its audio output; we only care about currentClipIndex().
    if (numSamples > grainOutBuf_.size())
        numSamples = grainOutBuf_.size();
    clipBank_.process(guitarIn, grainOutBuf_.data(), numSamples);

    // Detect active-clip changes and mirror them into the shifter source.
    const int idx = clipBank_.currentClipIndex();
    if (idx >= 0 && idx != lastSourceIdx_) {
        // Fetch the clip pointer from the active bank (O(1), no allocation).
        const auto clip = clipBank_.activeBankAt(idx);
        if (clip) {
            // Look up the pre-rendered grain. setSource() is a fast atomic
            // pointer swap — all Synthesis() calls happened offline in
            // setGrainsForBank(). RT-safe.
            auto it = prerenderedCache_.find(clip.get());
            if (it != prerenderedCache_.end())
                shifter_.setSource(it->second);

            // I1 fix: update anchor pitch from the new grain's metadata.
            currentAnchorHz_ = clip->anchorPitchHz;
        }
        lastSourceIdx_ = idx;
    }

    // I2 fix: per-block ratio smoothing (replaces per-sample loop).
    const float portMs = portamentoMs_.load(std::memory_order_relaxed);
    float targetRatio  = 1.0f;
    if (detectedHz > 0.0f && currentAnchorHz_ > 0.0f) {
        // I1 fix: use the active grain's anchorPitchHz rather than 220 Hz.
        targetRatio = detectedHz / currentAnchorHz_;
    } else if (detectedHz > 0.0f) {
        // Fallback for legacy bundles where anchorPitchHz is unknown.
        // 220 Hz (A3) is a conservative default that keeps output audible.
        targetRatio = detectedHz / 220.0f;
    }
    if (targetRatio < 0.25f) targetRatio = 0.25f;
    if (targetRatio > 4.0f)  targetRatio = 4.0f;

    // Alpha computed once per block — exponential portamento per I2.
    const float alphaBlock = (portMs <= 0.0f || numSamples == 0)
        ? 0.0f
        : std::exp(-1.0f / (static_cast<float>(sampleRate_) * portMs * 0.001f
                            / static_cast<float>(numSamples)));
    smoothedRatio_ = alphaBlock * smoothedRatio_ + (1.0f - alphaBlock) * targetRatio;

    shifter_.setRatio(smoothedRatio_);
    shifter_.process(wetOut, static_cast<int>(numSamples));

    // Mirror ClipBankPlayer's note-off gate onto the shifter output so a
    // released note silences the shifter in lockstep with the modulator
    // path. currentGateGain() returns 1.0 while the grain is sounding,
    // ramps to 0.0 during the 10 ms fade triggered by guitar silence.
    // Block-level multiply is sufficient — at 256-sample blocks the
    // fade still spans ~2 blocks, which is below the perceptual click
    // threshold for a smooth envelope.
    const float gate = clipBank_.currentGateGain();
    if (gate < 1.0f) {
        for (std::size_t i = 0; i < numSamples; ++i) wetOut[i] *= gate;
    }
}

} // namespace guitar_dsp::audio
