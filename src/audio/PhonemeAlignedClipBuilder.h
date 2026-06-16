#pragma once

#include <memory>
#include <string>

#include "ITTSSource.h"
#include "PhonemeExtractor.h"
#include "TTSClip.h"

namespace guitar_dsp::audio {

// Synthesizes audio via an ITTSSource (Piper), extracts phonemes via
// PhonemeExtractor, rescales phoneme durations to the actual audio
// length, syllabifies, and returns a fully-populated TTSClipPtr.
//
// Call from a worker thread. Returns nullptr on failure (TTS source not
// ready, phoneme extractor not ready, empty text).
class PhonemeAlignedClipBuilder {
public:
    PhonemeAlignedClipBuilder(ITTSSource* tts,
                              const PhonemeExtractor* phex);

    TTSClipPtr build(const std::string& text) const;

private:
    ITTSSource* tts_;
    const PhonemeExtractor* phex_;
};

} // namespace guitar_dsp::audio
