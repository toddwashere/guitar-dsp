#include "PhonemeAlignedClipBuilder.h"

#include "Syllabifier.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace guitar_dsp::audio {

namespace {

// Refine a SyllableSpan's vowelNucleusSample / attackEndSample / codaStartSample
// using the actual audio energy curve in [startSample, endSample). The previously
// computed values are derived from espeak's uniform-duration placeholders and
// don't match the real Piper audio.
//
// Method:
//   1. Compute a smoothed RMS envelope (~5 ms window).
//   2. Find the peak — that's the vowel nucleus.
//   3. Walk back from the peak to find the rising 50%-of-peak point — Attack-end.
//   4. Walk forward from the peak to find the falling 50%-of-peak point — Coda-start.
void refineAnchorByEnergy(SyllableSpan& s, const std::vector<float>& audio,
                          double sampleRate) {
    const std::size_t winSamples =
        static_cast<std::size_t>(0.005 * sampleRate);  // 5 ms RMS window
    if (s.endSample <= s.startSample || s.endSample > audio.size()) return;

    auto rmsAt = [&](std::size_t center) -> float {
        const std::size_t lo = (center > winSamples/2) ? center - winSamples/2 : 0;
        const std::size_t hi = std::min(center + winSamples/2, audio.size());
        if (hi <= lo) return 0.0f;
        double sumSq = 0.0;
        for (std::size_t i = lo; i < hi; ++i) sumSq += double(audio[i]) * audio[i];
        return static_cast<float>(std::sqrt(sumSq / double(hi - lo)));
    };

    // Stride ~1 ms for the peak scan — full sample-level scan would be too slow
    // on long clips, and 1 ms is well below vowel duration.
    const std::size_t stride =
        std::max(std::size_t(1), static_cast<std::size_t>(0.001 * sampleRate));

    std::size_t peakIdx = s.startSample;
    float peakVal = 0.0f;
    for (std::size_t i = s.startSample; i < s.endSample; i += stride) {
        const float v = rmsAt(i);
        if (v > peakVal) { peakVal = v; peakIdx = i; }
    }
    if (peakVal <= 0.0f) return;  // empty syllable; leave fields alone

    s.vowelNucleusSample = peakIdx;

    // Find rising 50%-of-peak (Attack-end).
    const float half = peakVal * 0.5f;
    std::size_t attackEnd = peakIdx;
    for (std::size_t i = peakIdx; i > s.startSample; i -= stride) {
        if (rmsAt(i) < half) { attackEnd = i; break; }
        if (i < stride) break;
    }
    s.attackEndSample = attackEnd;

    // Find falling 50%-of-peak (Coda-start).
    std::size_t codaStart = peakIdx;
    for (std::size_t i = peakIdx; i < s.endSample; i += stride) {
        if (rmsAt(i) < half) { codaStart = i; break; }
    }
    s.codaStartSample = codaStart;
}

} // anonymous namespace

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

    // Build a new clip with the existing audio + phoneme + syllable maps.
    auto out = std::make_shared<TTSClip>(*clip);
    out->phonemes = std::move(rescaled);
    out->sylsV2   = std::move(syllables);
    return out;
}

} // namespace guitar_dsp::audio
