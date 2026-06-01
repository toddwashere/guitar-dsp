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
};

} // namespace guitar_dsp::audio
