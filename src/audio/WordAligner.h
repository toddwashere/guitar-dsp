#pragma once

#include <string>
#include <vector>

#include "TTSClip.h"

namespace guitar_dsp::audio {

// Splits a clip's samples into one WordSegment per word using energy-gap
// segmentation: the N-1 largest inter-word silence gaps become boundaries.
// Uniform across all TTS backends (operates only on the produced PCM + the
// word list). Pure / message-thread only.
class WordAligner {
public:
    static std::vector<WordSegment> align(const std::vector<float>& samples,
                                          const std::vector<std::string>& words,
                                          double sampleRate);

    // Build per-syllable segments by:
    //   (1) running align() to get per-word boundaries from the unhyphenated
    //       words list, and
    //   (2) within each word's [startSample, endSample) range, splitting into
    //       N equal-duration sub-segments where N is the count of hyphen-bounded
    //       fragments in the corresponding hyphenated text token.
    // `hyphenatedWords` is the per-word hyphenated forms; size must match
    // `words`. If a word has no hyphen, it contributes one syllable segment
    // equal to the whole word. Returns empty vector on shape mismatch.
    static std::vector<WordSegment> alignSyllables(
        const std::vector<float>& samples,
        const std::vector<std::string>& words,
        const std::vector<std::string>& hyphenatedWords,
        double sampleRate);
};

} // namespace guitar_dsp::audio
