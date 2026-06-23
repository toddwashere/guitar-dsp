#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>
#include "Syllabifier.h"
#include "Phoneme.h"

namespace guitar_dsp::audio {

// One spoken word and the sample range it occupies within TTSClip::samples.
struct WordSegment {
    std::string word;
    std::size_t startSample = 0;   // inclusive
    std::size_t endSample   = 0;   // exclusive
};

// In-memory mono TTS clip. Owned by an `ITTSSource`, consumed by
// `TTSClipPlayer` on the audio thread. The samples vector is resized
// only off the audio thread.
struct TTSClip {
    std::string  name;             // for debug/UI
    double       sampleRate = 48000.0;
    std::vector<float> samples;    // mono float32, at sampleRate
    std::vector<WordSegment> words;   // empty = not segmented
    std::vector<WordSegment> syllables;  // optional; empty = no syllable map

    // v2: phoneme-aligned syllable map. Populated by
    // PhonemeAlignedClipBuilder; empty for v1 clips. v1's `syllables`
    // array above stays for the v1 player.
    std::vector<Phoneme>       phonemes;
    std::vector<SyllableSpan>  sylsV2;

    // Optional grain-metadata. Populated when this TTSClip is one grain
    // sliced from a sung-vowel bundle. Empty/zero for all legacy clips.
    std::string bankKey;             // e.g. "sung_ah" — joins to Scene::tts.bank entries.
    float       anchorPitchHz = 0.0f; // 0 = legacy clip (no anchor pitch known).
    std::string variantTag;          // e.g. "straight", "forte" — informational.

    bool empty() const noexcept { return samples.empty(); }
    std::size_t lengthSamples() const noexcept { return samples.size(); }
    double durationSeconds() const noexcept {
        return sampleRate > 0.0 ? samples.size() / sampleRate : 0.0;
    }
};

using TTSClipPtr = std::shared_ptr<const TTSClip>;

} // namespace guitar_dsp::audio
