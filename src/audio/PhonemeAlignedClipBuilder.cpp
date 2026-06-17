#include "PhonemeAlignedClipBuilder.h"

#include "Syllabifier.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace guitar_dsp::audio {


PhonemeAlignedClipBuilder::PhonemeAlignedClipBuilder(
        ITTSSource* tts, const PhonemeExtractor* phex)
    : tts_(tts), phex_(phex) {}

TTSClipPtr PhonemeAlignedClipBuilder::build(const std::string& text) const {
    if (text.empty() || tts_ == nullptr || phex_ == nullptr) return nullptr;

    auto clip = tts_->synthesize(text);
    if (!clip || clip->samples.empty()) {
        std::cerr << "[PhonemeAlignedClipBuilder] TTS produced empty clip\n";
        return nullptr;
    }

    auto phonemes = phex_->extract(text, clip->sampleRate);
    if (phonemes.empty()) {
        std::cerr << "[PhonemeAlignedClipBuilder] PhonemeExtractor empty; "
                     "returning clip with no phoneme map\n";
        return clip;  // v1-compatible: no sylsV2 means v1 fallback wiring.
    }

    // Rescale phoneme times so total matches actual audio length.
    const std::size_t actualLen = clip->samples.size();
    const std::size_t predictedLen = phonemes.back().endSample;
    if (predictedLen == 0) return clip;
    const double scale = static_cast<double>(actualLen)
                       / static_cast<double>(predictedLen);

    std::vector<Phoneme> rescaled;
    rescaled.reserve(phonemes.size());
    std::size_t cursor = 0;
    for (const auto& p : phonemes) {
        Phoneme q = p;
        q.startSample = cursor;
        q.endSample   = static_cast<std::size_t>(
            static_cast<double>(p.endSample) * scale);
        if (q.endSample > actualLen) q.endSample = actualLen;
        if (q.endSample < q.startSample) q.endSample = q.startSample;
        cursor = q.endSample;
        rescaled.push_back(q);
    }
    if (!rescaled.empty()) rescaled.back().endSample = actualLen;

    auto syllables = Syllabifier::group(rescaled);

    // Override the Syllabifier's phoneme-midpoint anchors with energy-peak
    // positions derived from the actual audio. Espeak's uniform-80ms phoneme
    // durations don't match Piper's real formant positions, so the midpoint
    // often lands on a consonant or transition rather than the vowel peak.
    for (auto& syl : syllables) {
        refineAnchorByEnergy(syl, clip->samples, clip->sampleRate);
    }

    // Snap each interior boundary to the local RMS minimum between the two
    // flanking vowel nuclei. This moves cuts from vowel peaks (wrong) into
    // the quiet gaps between syllables (correct). Re-refines all anchors
    // afterward so attackEnd/codaStart stay coherent with the new bounds.
    snapBoundariesToEnergyValleys(syllables, clip->samples, clip->sampleRate);

    // Build a new clip with the existing audio + phoneme + syllable maps.
    auto out = std::make_shared<TTSClip>(*clip);
    out->phonemes = std::move(rescaled);
    out->sylsV2   = std::move(syllables);
    return out;
}

} // namespace guitar_dsp::audio
