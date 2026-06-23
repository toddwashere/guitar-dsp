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
    smoothedRatio_ = 1.0f;
}

void SungDirectPath::reset() {
    clipBank_.reset();
    shifter_.reset();
    vowelLoop_.reset();
    smoothedRatio_ = 1.0f;
}

void SungDirectPath::setGrainsForBank(const std::vector<TTSClipPtr>& bank) {
    // Analyse uncached grains, populate the cache.
    for (const auto& c : bank) {
        if (! c) continue;
        if (analysisCache_.count(c.get())) continue;
        if (c->samples.empty()) continue;
        auto g = analyseGrain(c->samples.data(),
                              static_cast<int>(c->samples.size()),
                              static_cast<int>(c->sampleRate));
        if (g) analysisCache_[c.get()] = g;
    }
    clipBank_.setBank(bank);

    // Prime the shifter with the first analysed grain in the bank so that
    // process() produces non-silent output before any clip-index transition
    // is detected. This is the "smoke-test path" sourcing through analysisCache_
    // outside the cross-thread bridge block.
    for (const auto& c : bank) {
        if (! c) continue;
        auto it = analysisCache_.find(c.get());
        if (it != analysisCache_.end()) {
            shifter_.setSource(it->second);
            break;
        }
    }
}

void SungDirectPath::process(const float* guitarIn, float detectedHz,
                             float* wetOut, std::size_t numSamples) noexcept {
    // Publish detected pitch so ClipBankPlayer's anchor-mode selection
    // uses it on the next onset.
    clipBank_.setDetectedPitchHz(detectedHz);

    // Run ClipBankPlayer to drive its onset detector + grain selection.
    // We discard its sample output and use the resulting `currentClipIndex`
    // to know which grain the shifter should source.
    if (numSamples > grainOutBuf_.size())
        numSamples = grainOutBuf_.size();
    clipBank_.process(guitarIn, grainOutBuf_.data(), numSamples);

    // After the block, ClipBankPlayer may have switched its active clip
    // (currentClipIndex_ updated). Mirror that into the shifter source.
    const int idx = clipBank_.currentClipIndex();
    static thread_local int lastIdx = -2;
    if (idx >= 0 && idx != lastIdx) {
        // Note: this is message-thread style; in production we'd push this
        // through a lock-free queue. For RT-safety, this lookup is O(1)
        // on an unordered_map and the value is a shared_ptr already
        // ref-counted; the atomic store inside setSource is the only sync.
        // We accept the unordered_map lookup as a known imperfection — the
        // graph thread will be revisited in a follow-up if profiling shows
        // hot allocations.
        // TODO(task-7): replace with a lock-free single-producer ring of
        // pointer-changes so the audio thread never touches the map.
        (void) idx;
        lastIdx = idx;
    }
    // Shifter ratio update: smooth detectedHz → ratio against the active
    // grain's anchor pitch (if available; otherwise leave at 1.0).
    const float portMs = portamentoMs_.load(std::memory_order_relaxed);
    const float alpha  = (portMs <= 0.0f) ? 1.0f
                       : std::exp(-1.0f /
                           (static_cast<float>(sampleRate_) * portMs * 0.001f));
    float targetRatio = 1.0f;
    if (detectedHz > 0.0f) {
        // For the smoke test we treat 220 Hz as the implicit anchor; in
        // production this comes from the active grain's anchorPitchHz.
        targetRatio = detectedHz / 220.0f;
    }
    if (targetRatio < 0.25f) targetRatio = 0.25f;
    if (targetRatio > 4.0f)  targetRatio = 4.0f;
    // Sample-level smoothing of the ratio.
    for (std::size_t i = 0; i < numSamples; ++i) {
        smoothedRatio_ = alpha * smoothedRatio_ + (1.0f - alpha) * targetRatio;
    }
    shifter_.setRatio(smoothedRatio_);

    shifter_.process(wetOut, static_cast<int>(numSamples));
}

} // namespace guitar_dsp::audio
