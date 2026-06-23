#include "FormantShifter.h"

#include "world/synthesis.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace guitar_dsp::audio {

FormantShifter::FormantShifter()  = default;
FormantShifter::~FormantShifter() = default;

void FormantShifter::prepare(double sampleRate, int blockSize) {
    sampleRate_ = sampleRate;
    blockSize_  = std::max(64, blockSize);
    // For 5 ms frame period, latency is ~1 frame = ~5 ms.
    latencySamples_ = static_cast<int>(0.005 * sampleRate_);
    reset();
}

void FormantShifter::reset() {
    localPlayPos_   = 0;
    localRatioIdx_  = kSemitoneRange;  // unity ratio index
}

int FormantShifter::latencySamples() const noexcept { return latencySamples_; }

// -----------------------------------------------------------------------
// preRenderGrain — message thread only. Allocates freely.
// Runs WORLD's Synthesis() at each of the kNumRatios discrete semitone
// steps and stores the resulting float samples in ShifterGrain::preRendered.
// -----------------------------------------------------------------------
/*static*/
std::shared_ptr<ShifterGrain>
FormantShifter::preRenderGrain(std::shared_ptr<const ShifterGrain> raw) {
    if (!raw || raw->f0.empty()) return nullptr;

    // Build a copy with the same WORLD params; we'll populate preRendered.
    auto g = std::make_shared<ShifterGrain>(*raw);
    g->preRendered.clear();
    g->preRendered.reserve(static_cast<std::size_t>(kNumRatios));

    const int    f0Len      = static_cast<int>(g->f0.size());
    const int    fftSize    = g->fftSize;
    const double framePeriod = g->framePeriodMs;
    const int    fs         = g->sampleRate;

    // y_length: total output samples for the full grain.
    const int yLengthBase = static_cast<int>(
        std::round(static_cast<double>(f0Len) * framePeriod * 0.001 * fs));

    if (yLengthBase <= 0) return nullptr;

    // Build const-pointer arrays for WORLD (points into g's storage).
    std::vector<const double*> specPtr(static_cast<std::size_t>(f0Len));
    std::vector<const double*> apPtr  (static_cast<std::size_t>(f0Len));
    for (int i = 0; i < f0Len; ++i) {
        specPtr[static_cast<std::size_t>(i)] =
            g->spectrum    [static_cast<std::size_t>(i)].data();
        apPtr  [static_cast<std::size_t>(i)] =
            g->aperiodicity[static_cast<std::size_t>(i)].data();
    }

    std::vector<double> f0Scaled(static_cast<std::size_t>(f0Len));
    std::vector<double> yD(static_cast<std::size_t>(yLengthBase));

    for (int step = -kSemitoneRange; step <= kSemitoneRange; ++step) {
        const float ratio = static_cast<float>(std::pow(2.0, step / 12.0));

        // Scale F0 by ratio. Voiced: multiply; unvoiced (f0==0): leave 0.
        for (int i = 0; i < f0Len; ++i) {
            f0Scaled[static_cast<std::size_t>(i)] =
                g->f0[static_cast<std::size_t>(i)] * static_cast<double>(ratio);
        }

        std::fill(yD.begin(), yD.end(), 0.0);
        Synthesis(f0Scaled.data(), f0Len,
                  specPtr.data(),
                  apPtr.data(),
                  fftSize, framePeriod, fs, yLengthBase, yD.data());

        PreRenderedRatio pr;
        pr.ratio = ratio;
        pr.samples.resize(static_cast<std::size_t>(yLengthBase));
        for (int i = 0; i < yLengthBase; ++i)
            pr.samples[static_cast<std::size_t>(i)] =
                static_cast<float>(yD[static_cast<std::size_t>(i)]);

        g->preRendered.push_back(std::move(pr));
    }

    return g;
}

// -----------------------------------------------------------------------
// setSource — message thread only. Grain must have preRendered populated.
// Atomically swaps the active grain pointer; no allocation on audio thread.
// -----------------------------------------------------------------------
void FormantShifter::setSource(std::shared_ptr<const ShifterGrain> grain) noexcept {
    std::atomic_store(&activeGrain_, std::move(grain));
    // Reset local cache so process() detects the swap on the next call.
    localGrain_.reset();
    localPlayPos_  = 0;
    localRatioIdx_ = kSemitoneRange;
}

void FormantShifter::setRatio(float r) noexcept {
    if (r < 0.25f) r = 0.25f;
    if (r > 4.0f)  r = 4.0f;
    ratio_.store(r, std::memory_order_relaxed);
}

void FormantShifter::setFormantTintSemitones(float n) noexcept {
    if (n < -6.0f) n = -6.0f;
    if (n > 6.0f)  n = 6.0f;
    tintSemi_.store(n, std::memory_order_relaxed);
}

// -----------------------------------------------------------------------
// process — audio thread only. Zero heap allocations.
// -----------------------------------------------------------------------
void FormantShifter::process(float* out, int n) noexcept {
    // Detect grain swap (single atomic load is the only cross-thread touch).
    auto fresh = std::atomic_load(&activeGrain_);
    if (fresh != localGrain_) {
        localGrain_    = std::move(fresh);
        localPlayPos_  = 0;
        localRatioIdx_ = kSemitoneRange;
    }

    if (!localGrain_ || localGrain_->preRendered.empty()) {
        std::fill(out, out + n, 0.0f);
        return;
    }

    // Compute target ratio including formant tint.
    // Formant tint: adds semitones via ratio multiplication. This is not true
    // formant-axis warping, but it gives a visible, audible effect per C3(b).
    const float baseRatio = ratio_.load(std::memory_order_relaxed);
    const float tintSemi  = tintSemi_.load(std::memory_order_relaxed);
    const float tintRatio = (tintSemi == 0.0f)
        ? 1.0f
        : static_cast<float>(std::pow(2.0, static_cast<double>(tintSemi) / 12.0));
    const float targetRatio = baseRatio * tintRatio;

    // Find nearest pre-rendered ratio. preRendered is sorted ascending by ratio
    // (step -kSemitoneRange to +kSemitoneRange).
    const auto& pr = localGrain_->preRendered;
    int best = localRatioIdx_;
    float bestDist = std::fabs(pr[static_cast<std::size_t>(best)].ratio - targetRatio);
    for (int i = 0; i < static_cast<int>(pr.size()); ++i) {
        const float d = std::fabs(pr[static_cast<std::size_t>(i)].ratio - targetRatio);
        if (d < bestDist) { bestDist = d; best = i; }
    }
    // On ratio change, reset playback cursor to start of the new buffer.
    if (best != localRatioIdx_) {
        localRatioIdx_ = best;
        localPlayPos_  = 0;
    }

    const auto& buf    = pr[static_cast<std::size_t>(localRatioIdx_)].samples;
    const std::size_t bufLen = buf.size();
    if (bufLen == 0) {
        std::fill(out, out + n, 0.0f);
        return;
    }

    // One-shot per source set: play through the pre-rendered grain once,
    // then output silence. Each new note attack triggers a fresh
    // setSource() upstream, which resets localPlayPos_ to 0 — so notes
    // re-trigger naturally without the previous bug where the grain
    // looped forever and produced endless audio after the host stopped
    // sending input.
    for (int i = 0; i < n; ++i) {
        if (localPlayPos_ >= bufLen) {
            for (; i < n; ++i) out[i] = 0.0f;
            return;
        }
        out[i] = buf[localPlayPos_++];
    }
}

} // namespace guitar_dsp::audio
