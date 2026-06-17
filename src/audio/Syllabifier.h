#pragma once

#include <cstddef>
#include <vector>

#include "Phoneme.h"

namespace guitar_dsp::audio {

// A single syllable: phoneme indices into the input vector, plus pre-
// computed sample ranges and the vowel-nucleus anchor for grain looping.
struct SyllableSpan {
    std::vector<int> phonemeIndices;   // into input Phonemes
    std::size_t startSample = 0;
    std::size_t endSample   = 0;
    std::size_t vowelNucleusSample = 0;  // midpoint of nucleus vowel
    std::size_t attackEndSample = 0;     // approx vowel attack end
    std::size_t codaStartSample = 0;     // start of coda consonants
    bool        nucleusIsFricative = false;  // skip Sustain if true
};

class Syllabifier {
public:
    // Groups phonemes into syllables via the sonority sequencing principle:
    // every vowel (or fricative/silence-bordered sonority peak) is a
    // nucleus; preceding consonants attach as onset by max-onset principle,
    // following consonants as coda. Silences (Phoneme::Type::Silence)
    // end the current syllable.
    static std::vector<SyllableSpan> group(const std::vector<Phoneme>& phonemes);
};

// ---------------------------------------------------------------------------
// Free functions for post-construction slice editing
// ---------------------------------------------------------------------------

// Refines a SyllableSpan's vowelNucleusSample / attackEndSample /
// codaStartSample by scanning the actual audio energy envelope inside
// [startSample, endSample). Use after any boundary edit so the
// per-syllable anchors stay coherent with the new bounds.
void refineAnchorByEnergy(SyllableSpan& s, const std::vector<float>& audio,
                          double sampleRate);

// Returns the new boundary position clamped within
// [syls[boundaryIndex-1].startSample + minWidthSamples,
//  syls[boundaryIndex].endSample   - minWidthSamples].
// boundaryIndex must be in [1, syls.size()-1] (interior boundaries only);
// index 0 and syls.size() are the clip start/end and cannot be moved.
// After clamping, updates syls[boundaryIndex-1].endSample and
// syls[boundaryIndex].startSample, then re-runs refineAnchorByEnergy on
// both affected syllables. Returns the actual new position.
std::size_t moveBoundary(std::vector<SyllableSpan>& syls,
                         std::size_t boundaryIndex,
                         std::size_t newSample,
                         const std::vector<float>& audio,
                         double sampleRate,
                         std::size_t minWidthSamples = 240);

// Splits the syllable that contains atSample into two at atSample. Returns
// true on success, false if atSample is too close to an existing boundary
// (within minWidthSamples). Refines both new syllables' anchors.
bool addBoundary(std::vector<SyllableSpan>& syls,
                 std::size_t atSample,
                 const std::vector<float>& audio,
                 double sampleRate,
                 std::size_t minWidthSamples = 240);

// Removes the boundary at boundaryIndex (1 <= boundaryIndex <= syls.size()-1),
// merging syls[boundaryIndex-1] and syls[boundaryIndex] into one syllable.
// Refines the merged syllable's anchor. Returns true on success.
bool removeBoundary(std::vector<SyllableSpan>& syls,
                    std::size_t boundaryIndex,
                    const std::vector<float>& audio,
                    double sampleRate);

// Snaps each interior boundary in `syls` to the local RMS minimum between
// the two flanking vowel nuclei. Re-runs energy-anchor refinement on every
// affected syllable so the per-syllable anchors stay coherent.
//
// Must run AFTER refineAnchorByEnergy (so vowelNucleusSample reflects the
// actual peak, not the linearly-rescaled phoneme position).
void snapBoundariesToEnergyValleys(std::vector<SyllableSpan>& syls,
                                   const std::vector<float>& audio,
                                   double sampleRate);

} // namespace guitar_dsp::audio
