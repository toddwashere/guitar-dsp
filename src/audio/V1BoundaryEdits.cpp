#include "V1BoundaryEdits.h"

#include <algorithm>
#include <cmath>

namespace guitar_dsp::audio {

std::size_t moveBoundaryV1(std::vector<WordSegment>& segs,
                           std::size_t boundaryIndex,
                           std::size_t newSample,
                           std::size_t minWidthSamples) {
    if (boundaryIndex == 0 || boundaryIndex >= segs.size()) return 0;
    const auto lo = segs[boundaryIndex - 1].startSample + minWidthSamples;
    const auto hi = segs[boundaryIndex].endSample       - minWidthSamples;
    const auto clamped = std::clamp(newSample, lo, hi);
    segs[boundaryIndex - 1].endSample   = clamped;
    segs[boundaryIndex].startSample     = clamped;
    return clamped;
}

bool addBoundaryV1(std::vector<WordSegment>& segs,
                   std::size_t atSample,
                   std::size_t minWidthSamples) {
    for (std::size_t i = 0; i < segs.size(); ++i) {
        const auto& s = segs[i];
        if (atSample <= s.startSample || atSample >= s.endSample) continue;
        if (atSample - s.startSample < minWidthSamples) return false;
        if (s.endSample - atSample   < minWidthSamples) return false;
        WordSegment right{s.word, atSample, s.endSample};
        segs[i].endSample = atSample;
        segs.insert(segs.begin() + (std::ptrdiff_t)(i + 1), right);
        return true;
    }
    return false;
}

bool removeBoundaryV1(std::vector<WordSegment>& segs,
                      std::size_t boundaryIndex) {
    if (boundaryIndex == 0 || boundaryIndex >= segs.size()) return false;
    segs[boundaryIndex - 1].endSample = segs[boundaryIndex].endSample;
    segs.erase(segs.begin() + (std::ptrdiff_t) boundaryIndex);
    return true;
}

void snapBoundariesToEnergyValleysV1(std::vector<WordSegment>& segs,
                                     const std::vector<float>& audio,
                                     double sampleRate) {
    if (segs.size() < 2 || audio.empty() || sampleRate <= 0.0) return;

    const std::size_t winSamples =
        static_cast<std::size_t>(0.005 * sampleRate);   // 5 ms RMS window
    const std::size_t stride =
        std::max<std::size_t>(1,
            static_cast<std::size_t>(0.001 * sampleRate));  // 1 ms scan stride
    const std::size_t safety =
        static_cast<std::size_t>(0.005 * sampleRate);   // 5 ms hard edge guard

    auto rmsAt = [&](std::size_t center) -> float {
        const std::size_t halfWin = winSamples / 2;
        const std::size_t lo = (center > halfWin) ? center - halfWin : 0;
        const std::size_t hi = std::min(center + halfWin, audio.size());
        if (hi <= lo) return 0.0f;
        double sumSq = 0.0;
        for (std::size_t i = lo; i < hi; ++i)
            sumSq += double(audio[i]) * double(audio[i]);
        return static_cast<float>(std::sqrt(sumSq / double(hi - lo)));
    };

    for (std::size_t i = 1; i < segs.size(); ++i) {
        auto& left  = segs[i - 1];
        auto& right = segs[i];
        if (right.endSample <= left.startSample) continue;

        const std::size_t leftLen  = left.endSample  - left.startSample;
        const std::size_t rightLen = right.endSample - right.startSample;
        // Cap search radius at 30 % of the smaller neighbour so a snap can
        // never cross past that neighbour's midpoint.
        const std::size_t radius = std::min(leftLen, rightLen) * 30 / 100;
        if (radius == 0) continue;

        const std::size_t boundary = left.endSample;
        const std::size_t loBound  = left.startSample + safety;
        const std::size_t hiBound  = right.endSample > safety
                                       ? right.endSample - safety : 0;
        const std::size_t lo = (boundary > radius && boundary - radius > loBound)
                                  ? boundary - radius : loBound;
        const std::size_t hi = std::min(boundary + radius, hiBound);
        if (lo >= hi) continue;

        std::size_t bestIdx = boundary;
        float bestVal = rmsAt(boundary);
        for (std::size_t s = lo; s < hi; s += stride) {
            const float v = rmsAt(s);
            if (v < bestVal) { bestVal = v; bestIdx = s; }
        }
        left.endSample    = bestIdx;
        right.startSample = bestIdx;
    }
}

} // namespace guitar_dsp::audio
