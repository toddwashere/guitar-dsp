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
            // possible go to the *next* nucleus's onset (i.e., to THIS
            // syllable's onset). Simple impl: split the inter-nuclear
            // consonant span at floor(span/2). All-but-first go to onset.
            const int prevNuc = nuclei[n-1];
            const int gap = nucIdx - prevNuc - 1;  // # consonants between
            // gap >= 0; first ceil(gap/2) consonants become prev coda,
            // remaining floor(gap/2) become this onset.
            const int prevCodaCount = (gap + 1) / 2;
            onsetStart = prevNuc + 1 + prevCodaCount;
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
            // See above: prevCodaCount of THIS syllable goes here.
            const int nextNuc = nuclei[n+1];
            const int gap = nextNuc - nucIdx - 1;
            const int codaCount = (gap + 1) / 2;  // first half of gap
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
