#include "Phoneme.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>

namespace guitar_dsp::audio {

namespace {

constexpr std::array<std::string_view, 18> kVowels = {
    "a","A","e","E","i","I","o","O","u","U","V","@","3","6","Q","aI","aU","OI"
};
constexpr std::array<std::string_view, 8> kSilenceLabels = {
    "_","__"," ","-","\n","\t","",","
};

template <typename Set>
bool isOneOf(const std::string& s, const Set& set) {
    return std::any_of(set.begin(), set.end(),
                       [&](std::string_view e){ return e == s; });
}
} // namespace

Phoneme::Type phonemeType(const std::string& label) noexcept {
    if (label.empty() || isOneOf(label, kSilenceLabels))
        return Phoneme::Type::Silence;
    // espeak's vowel labels are mostly lowercase IPA-ish; also uppercase
    // diphthongs (aI, aU, OI). Heuristic: if first char is in kVowels set,
    // treat as vowel. Edge cases (syllabic n/l/m) fall through as consonant.
    const std::string first(1, label[0]);
    if (isOneOf(first, kVowels)) return Phoneme::Type::Vowel;
    if (isOneOf(label, kVowels)) return Phoneme::Type::Vowel;
    return Phoneme::Type::Consonant;
}

int phonemeSonority(const std::string& label) noexcept {
    const auto t = phonemeType(label);
    if (t == Phoneme::Type::Vowel)   return 6;
    if (t == Phoneme::Type::Silence) return -1;
    // Consonant sub-ranks: liquids/glides 4, nasals 3, fricatives 2,
    // affricates 1, stops 0.
    if (label.find_first_of("rlwjy") != std::string::npos) return 4;
    if (label.find_first_of("mnN")   != std::string::npos) return 3;
    if (label.find_first_of("szSZfvTDhx") != std::string::npos) return 2;
    if (label.find_first_of("tSdZ")  != std::string::npos) return 1;
    return 0;  // stops: p, b, t, d, k, g
}

} // namespace guitar_dsp::audio
