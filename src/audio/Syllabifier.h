#pragma once

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

} // namespace guitar_dsp::audio
