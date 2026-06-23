#include "FormantShifter.h"

#include "world/synthesis.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

FormantShifter::FormantShifter()  = default;
FormantShifter::~FormantShifter() = default;

void FormantShifter::prepare(double sampleRate, int blockSize) {
    sampleRate_ = sampleRate;
    blockSize_  = std::max(64, blockSize);
    outBufferD_.assign(static_cast<std::size_t>(blockSize_), 0.0);
    // M1.5 confirmed WORLD's per-block compute fits; the only latency is
    // the synthesis lookahead — for 5 ms frame period, ~1 frame = ~5 ms.
    latencySamples_ = static_cast<int>(0.005 * sampleRate_);
    reset();
}

void FormantShifter::reset() {
    localFrameIdx_ = 0;
    std::fill(outBufferD_.begin(), outBufferD_.end(), 0.0);
}

void FormantShifter::setSource(std::shared_ptr<const ShifterGrain> g) noexcept {
    std::atomic_store(&activeGrain_, std::move(g));
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

int FormantShifter::latencySamples() const noexcept { return latencySamples_; }

void FormantShifter::process(float* out, int n) noexcept {
    // Atomic load + local cache: avoid repeated atomic loads inside the loop.
    auto fresh = std::atomic_load(&activeGrain_);
    if (fresh != localGrain_) {
        localGrain_    = std::move(fresh);
        localFrameIdx_ = 0;
    }
    if (! localGrain_ || localGrain_->f0.empty()) {
        std::fill(out, out + n, 0.0f);
        return;
    }

    const float ratio = ratio_.load(std::memory_order_relaxed);
    const auto& g     = *localGrain_;

    // Build per-block scratch arrays of pointers — small (frames-per-block
    // is ~10–30 for a 256-sample block at 5 ms frame period at 48 kHz).
    const double secondsPerBlock = static_cast<double>(n) / sampleRate_;
    const int    framesPerBlock  = std::max(1, static_cast<int>(std::ceil(
        secondsPerBlock * 1000.0 / g.framePeriodMs)) + 2);
    const int    startFrame      = localFrameIdx_;
    const int    endFrame        = std::min<int>(
        startFrame + framesPerBlock, static_cast<int>(g.f0.size()));
    const int    blockFrames     = endFrame - startFrame;
    if (blockFrames <= 0) {
        std::fill(out, out + n, 0.0f);
        return;
    }

    // Stack-alloc small arrays via fixed-size; cap framesPerBlock at 128.
    constexpr int kMaxFramesPerBlock = 128;
    if (blockFrames > kMaxFramesPerBlock) {
        std::fill(out, out + n, 0.0f);
        return;
    }
    double        f0Scaled[kMaxFramesPerBlock];
    const double* specPtr  [kMaxFramesPerBlock];
    const double* apPtr    [kMaxFramesPerBlock];
    for (int i = 0; i < blockFrames; ++i) {
        f0Scaled[i] = g.f0[(std::size_t)(startFrame + i)] * static_cast<double>(ratio);
        specPtr [i] = g.spectrum    [(std::size_t)(startFrame + i)].data();
        apPtr   [i] = g.aperiodicity[(std::size_t)(startFrame + i)].data();
    }

    // WORLD's Synthesis takes (const double * const *) for spectrogram and
    // aperiodicity — our const double* arrays are compatible directly.
    Synthesis(f0Scaled, blockFrames,
              specPtr,
              apPtr,
              g.fftSize, g.framePeriodMs, g.sampleRate, n, outBufferD_.data());

    for (int i = 0; i < n; ++i)
        out[i] = static_cast<float>(outBufferD_[(std::size_t) i]);

    // Advance the frame cursor by (n samples) → time → frames.
    localFrameIdx_ += static_cast<int>(std::round(
        secondsPerBlock * 1000.0 / g.framePeriodMs));
    if (localFrameIdx_ >= static_cast<int>(g.f0.size()))
        localFrameIdx_ = static_cast<int>(g.f0.size()) - 1;
}

} // namespace guitar_dsp::audio
