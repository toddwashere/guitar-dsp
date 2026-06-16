#include "Syllabifier.h"

namespace guitar_dsp::audio {

namespace {
bool isNucleus(const Phoneme& p) {
    return p.type == Phoneme::Type::Vowel;
}
}

std::vector<SyllableSpan> Syllabifier::group(
        const std::vector<Phoneme>& ph) {
    std::vector<SyllableSpan> out;
    if (ph.empty()) return out;

    // 1) Find all nucleus indices (vowels). Each becomes its own syllable.
    std::vector<int> nuclei;
    for (int i = 0; i < static_cast<int>(ph.size()); ++i)
        if (isNucleus(ph[i])) nuclei.push_back(i);

    if (nuclei.empty()) return out;  // no vowels = nothing to syllabify

    // 2) For each nucleus, attach onset consonants (back to previous
    //    boundary) and coda consonants (forward to midpoint with next
    //    nucleus via max-onset principle).
    for (std::size_t n = 0; n < nuclei.size(); ++n) {
        const int nucIdx = nuclei[n];

        // Onset: from end of previous syllable (or start) up to nucIdx.
        int onsetStart;
        if (n == 0) {
            // Walk back through consonants/silences from nucIdx.
            onsetStart = nucIdx;
            while (onsetStart > 0
                   && ph[onsetStart-1].type != Phoneme::Type::Silence) {
                --onsetStart;
            }
        } else {
            // Boundary chosen by max-onset principle: as many consonants as
            // possible go to this nucleus's onset; prev syllable keeps as
            // few coda consonants as possible. Silences are boundaries —
            // skip past any silence block; onset starts after it.
            const int prevNuc = nuclei[n-1];
            // Find end of any silence run between prevNuc and nucIdx.
            int afterSilence = prevNuc + 1;
            while (afterSilence < nucIdx
                   && ph[afterSilence].type == Phoneme::Type::Silence) {
                ++afterSilence;
            }
            // If there WAS a silence, onset starts right after it.
            // If none, split the consonant span: floor(gap/2) go to this onset.
            bool hadSilence = (afterSilence > prevNuc + 1);
            if (hadSilence) {
                onsetStart = afterSilence;
            } else {
                // Walk from prevNuc+1 forward to find end of consonant span
                // (stop at silence, though silence would have been caught above).
                const int gap = nucIdx - prevNuc - 1;
                const int prevCodaCount = gap / 2;  // floor → give more to onset
                onsetStart = prevNuc + 1 + prevCodaCount;
            }
        }

        // Coda: from nucIdx+1 forward.
        int codaEnd;
        if (n + 1 == nuclei.size()) {
            // Walk forward through consonants/silences from nucIdx.
            codaEnd = nucIdx + 1;
            while (codaEnd < static_cast<int>(ph.size())
                   && ph[codaEnd].type != Phoneme::Type::Silence) {
                ++codaEnd;
            }
        } else {
            // Max-onset: prefer giving consonants to the next onset.
            // Count only non-silence consonants before the next nucleus.
            const int nextNuc = nuclei[n+1];
            // Walk from nucIdx+1 forward; stop at first silence.
            int gapEnd = nucIdx + 1;
            while (gapEnd < nextNuc
                   && ph[gapEnd].type != Phoneme::Type::Silence) {
                ++gapEnd;
            }
            const int gap = gapEnd - (nucIdx + 1);  // # consonants (no silences)
            // floor(gap/2): give the larger share to the next onset.
            const int codaCount = gap / 2;
            codaEnd = nucIdx + 1 + codaCount;
        }

        SyllableSpan s;
        for (int i = onsetStart; i < codaEnd; ++i) s.phonemeIndices.push_back(i);
        if (s.phonemeIndices.empty()) continue;

        s.startSample = ph[onsetStart].startSample;
        s.endSample   = ph[codaEnd - 1].endSample;
        const auto& nuc = ph[nucIdx];
        s.vowelNucleusSample = (nuc.startSample + nuc.endSample) / 2;
        s.attackEndSample    = nuc.startSample + nuc.lengthSamples() / 4;
        s.codaStartSample    = nuc.endSample;
        s.nucleusIsFricative = (phonemeSonority(nuc.label) <= 2);
        out.push_back(s);
    }
    return out;
}

} // namespace guitar_dsp::audio
