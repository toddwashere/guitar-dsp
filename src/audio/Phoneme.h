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
