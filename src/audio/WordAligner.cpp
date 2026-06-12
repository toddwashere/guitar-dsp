#include "WordAligner.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace guitar_dsp::audio {

std::vector<WordSegment> WordAligner::align(const std::vector<float>& samples,
                                            const std::vector<std::string>& words,
                                            double sampleRate) {
    std::vector<WordSegment> out;
    const std::size_t N = words.size();
    const std::size_t len = samples.size();
    if (N == 0 || len == 0) return out;
    if (N == 1) { out.push_back({words[0], 0, len}); return out; }

    // Smoothed peak envelope (instant attack, exp release ~30 ms). The 30 ms
    // release is intentionally LONGER than typical stop-consonant gaps inside
    // a multi-syllable word ("think", "therefore", "stop"): those dips don't
    // drop the envelope below the gap threshold, so they don't show up as
    // false-positive word boundaries. Real inter-word silences (~80 ms+) decay
    // far enough below threshold to still register.
    const float coef = static_cast<float>(std::exp(-1.0 / (sampleRate * 0.030)));
    std::vector<float> env(len);
    float e = 0.0f, peak = 0.0f;
    for (std::size_t i = 0; i < len; ++i) {
        const float a = std::fabs(samples[i]);
        e = (a > e) ? a : (e * coef);
        env[i] = e;
        peak = std::max(peak, e);
    }
    const float thresh = peak * 0.15f;

    // Inter-word gap runs (env < thresh), excluding leading/trailing silence.
    // Skip implausibly short runs (< 30 ms): a real inter-word boundary in a
    // TTS clip is at least this long; anything shorter is envelope quantization
    // or a numerical dip, not a word boundary.
    const std::size_t minGapSamples =
        static_cast<std::size_t>(sampleRate * 0.030);
    std::vector<std::pair<std::size_t, std::size_t>> gaps;  // (center, length)
    std::size_t runStart = 0; bool inRun = false;
    for (std::size_t i = 0; i < len; ++i) {
        const bool silent = (env[i] < thresh);
        if (silent && !inRun) { inRun = true; runStart = i; }
        else if (!silent && inRun) {
            inRun = false;
            const std::size_t runLen = i - runStart;
            if (runStart > 0 && runLen >= minGapSamples)  // skip leading + too-short
                gaps.push_back({(runStart + i) / 2, runLen});
        }
    }
    // A run reaching the end is trailing silence — skip it (not added).

    // Keep the N-1 longest gaps; order their centers by position.
    std::sort(gaps.begin(), gaps.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    std::vector<std::size_t> boundaries;
    for (std::size_t i = 0; i < gaps.size() && boundaries.size() < N - 1; ++i)
        boundaries.push_back(gaps[i].first);
    while (boundaries.size() < N - 1)
        boundaries.push_back(len * (boundaries.size() + 1) / N);
    std::sort(boundaries.begin(), boundaries.end());

    std::size_t prev = 0;
    for (std::size_t i = 0; i < N; ++i) {
        const std::size_t end = (i < N - 1) ? boundaries[i] : len;
        out.push_back({words[i], prev, end});
        prev = end;
    }
    return out;
}

} // namespace guitar_dsp::audio
