#pragma once

#include "TTSClip.h"

#include <cstddef>
#include <vector>

namespace guitar_dsp::audio {

// Boundary editing for v1 word-segment arrays (WordSegment-based:
// clip->words and clip->syllables). Mirrors the v2 helpers in
// Syllabifier.h (addBoundary / moveBoundary / removeBoundary) but
// operates on the simpler v1 array layout — no anchors, no phoneme
// indices, just contiguous {label, startSample, endSample} spans.
//
// Convention matches v2: boundaryIndex 1..segs.size()-1 is interior.

// Clamp newSample into [segs[idx-1].startSample + minWidth,
//                      segs[idx].endSample    - minWidth] and
// update segs[idx-1].endSample = segs[idx].startSample = newSample.
// Returns the actual clamped position. Returns 0 if boundaryIndex
// is not interior.
std::size_t moveBoundaryV1(std::vector<WordSegment>& segs,
                           std::size_t boundaryIndex,
                           std::size_t newSample,
                           std::size_t minWidthSamples = 240);

// Splits the segment containing atSample into two segments at
// atSample. Both new segments inherit the parent's word label.
// Returns false if atSample is within minWidthSamples of an existing
// boundary or outside the clip.
bool addBoundaryV1(std::vector<WordSegment>& segs,
                   std::size_t atSample,
                   std::size_t minWidthSamples = 240);

// Merges segs[boundaryIndex-1] and segs[boundaryIndex] into one
// segment. The merged segment keeps the left segment's word label.
// Returns false if boundaryIndex is not interior.
bool removeBoundaryV1(std::vector<WordSegment>& segs,
                      std::size_t boundaryIndex);

// Nudges each interior boundary in `segs` to the local minimum of the
// audio's 5 ms RMS envelope within a search window centred on the
// current boundary. Search radius = 30 % of the smaller neighbour, so
// no boundary can cross past the midpoint of its neighbour. Boundaries
// that already sit on the minimum stay put. No-op when `segs.size() < 2`.
//
// Use this as a post-process on whatever boundary set is in place — the
// equal-duration output of WordAligner::alignSyllables, hand-drag edits,
// boundaries loaded from a `.gspeak` bundle — to land each cut on a real
// quiet spot instead of an arbitrary mid-sample.
void snapBoundariesToEnergyValleysV1(std::vector<WordSegment>& segs,
                                     const std::vector<float>& audio,
                                     double sampleRate);

} // namespace guitar_dsp::audio
