#pragma once

#include <cstddef>
#include <string>

namespace guitar_dsp::audio {

struct Phoneme {
    enum class Type { Vowel, Consonant, Silence };

    std::string  label;          // espeak label, e.g. "AE", "m", "_" (silence)
    Type         type = Type::Consonant;
    std::size_t  startSample = 0;  // inclusive
    std::size_t  endSample   = 0;  // exclusive

    // Per-grain metadata (optional; populated by GspeakBundle::read for v2
    // multi-grain bundles where each phoneme entry carries its own bankKey
    // and anchorPitchHz). Empty / 0.0f = legacy clip, use TTSClip-level fields.
    std::string bankKey;          // e.g. "sung_ah" — identifies the grain's vowel bank
    float       anchorPitchHz = 0.0f;  // 0 = unknown / not stored in bundle

    std::size_t lengthSamples() const noexcept {
        return endSample > startSample ? endSample - startSample : 0;
    }
};

// Sonority rank of a phoneme label. Higher = more sonorous.
// Used by Syllabifier::group. Open vowels = 6; nasals = 3; stops = 0.
int phonemeSonority(const std::string& label) noexcept;

// Classifies an espeak label as vowel / consonant / silence.
Phoneme::Type phonemeType(const std::string& label) noexcept;

} // namespace guitar_dsp::audio
