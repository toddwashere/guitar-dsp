#pragma once

#include <string>
#include <vector>

#include "Phoneme.h"

namespace guitar_dsp::audio {

// Shells out to `espeak-ng -q -x --sep=" " -v en-us "<text>"` and parses the
// space-separated phoneme mnemonic labels.
//
// Phoneme times in the result are UNIFORM estimates (kDefaultPhonemeMs each),
// in milliseconds, converted to sample positions at `targetSampleRate`.
// Callers (PhonemeAlignedClipBuilder) rescale these to match actual
// Piper audio length; the absolute values here are placeholder proportions.
//
// Call from a worker thread. The subprocess takes ~30-80 ms per short
// phrase on M-series. Returns empty vector on failure.
class PhonemeExtractor {
public:
    PhonemeExtractor(std::string binaryPath, std::string voice = "en-us");

    std::vector<Phoneme> extract(const std::string& text,
                                 double targetSampleRate) const;

    // Path to the espeak-ng binary (e.g. "assets/piper/espeak-ng").
    bool isReady() const;
    std::string statusDetail() const;

private:
    std::string binaryPath_;
    std::string voice_;
};

} // namespace guitar_dsp::audio
