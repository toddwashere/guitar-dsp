#include "V1BoundaryEdits.h"

#include <algorithm>

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

} // namespace guitar_dsp::audio
