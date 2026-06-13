#include "audio/MicCapture.h"
#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

void MicCapture::prepare(double sr, int) {
    sampleRate_ = sr;
    // Allocate exactly kMaxDurationSec worth of samples (+ small guard).
    // maxWriteIdx_ enforces the logical 30 s boundary in appendFromAudioBlock.
    const int logical = (int)std::round(sr * kMaxDurationSec);
    staging_.assign(static_cast<size_t>(logical + 64), 0.0f);  // allocate once (outside capture)
    writeIdx_.store(0);
    maxWriteIdx_ = logical;
}

void MicCapture::beginCapture() {
    writeIdx_.store(0);
    peakLin_.store(0.0f);
    tooShort_  = false;
    truncated_ = false;
    capturing_.store(true);
}

void MicCapture::appendFromAudioBlock(const float* mono, int n) {
    if (! capturing_.load(std::memory_order_acquire)) return;
    const int idx = writeIdx_.load(std::memory_order_relaxed);
    const int cap = maxWriteIdx_;                               // logical 30 s cap
    if (idx >= cap) { truncated_ = true; return; }

    const int writeN = std::min(n, cap - idx);
    float peak = peakLin_.load(std::memory_order_relaxed);
    for (int i = 0; i < writeN; ++i) {
        const float s = mono[i];
        staging_[static_cast<size_t>(idx + i)] = s;
        const float a = std::fabs(s);
        if (a > peak) peak = a;
    }
    peakLin_.store(peak, std::memory_order_relaxed);
    writeIdx_.store(idx + writeN, std::memory_order_release);
    if (writeN < n) truncated_ = true;
}

float MicCapture::currentPeakDbfs() const noexcept {
    const float p = peakLin_.load(std::memory_order_relaxed);
    if (p <= 1e-6f) return -120.0f;
    return 20.0f * std::log10(p);
}

namespace {
// Simple linear resampler.
std::vector<float> resampleLinear(const float* in, int n,
                                  double srcRate, double dstRate) {
    if (srcRate == dstRate) return std::vector<float>(in, in + n);
    const double ratio = dstRate / srcRate;
    const int outN = static_cast<int>(std::round(n * ratio));
    std::vector<float> out(outN);
    for (int i = 0; i < outN; ++i) {
        const double t = i / ratio;
        const int    j = static_cast<int>(t);
        const double f = t - j;
        const float  a = j     < n ? in[j]     : 0.0f;
        const float  b = j + 1 < n ? in[j + 1] : 0.0f;
        out[static_cast<size_t>(i)] = static_cast<float>(a + (b - a) * f);
    }
    return out;
}
}

std::vector<float> MicCapture::endCapture() {
    capturing_.store(false, std::memory_order_release);
    const int n = writeIdx_.load(std::memory_order_acquire);
    auto resampled = resampleLinear(staging_.data(), n,
                                    sampleRate_, static_cast<double>(kTargetRate));
    const double seconds = static_cast<double>(resampled.size())
                           / static_cast<double>(kTargetRate);
    if (seconds < kMinDurationSec) tooShort_ = true;
    return resampled;
}

} // namespace guitar_dsp::audio
