#include "Syllabifier.h"

#include <algorithm>
#include <cmath>

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

// ---------------------------------------------------------------------------
// Free functions — refine + slice editing
// ---------------------------------------------------------------------------

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

std::size_t moveBoundary(std::vector<SyllableSpan>& syls,
                         std::size_t boundaryIndex,
                         std::size_t newSample,
                         const std::vector<float>& audio,
                         double sampleRate,
                         std::size_t minWidthSamples) {
    // Boundary 0 = clip start, syls.size() = clip end — non-editable.
    if (boundaryIndex == 0 || boundaryIndex >= syls.size()) {
        // Return the current position (unchanged).
        if (boundaryIndex == 0 && !syls.empty())
            return syls[0].startSample;
        if (!syls.empty())
            return syls.back().endSample;
        return 0;
    }

    auto& left  = syls[boundaryIndex - 1];
    auto& right = syls[boundaryIndex];

    const std::size_t lo = left.startSample  + minWidthSamples;
    const std::size_t hi = right.endSample   - minWidthSamples;
    // If the syllable is shorter than 2*minWidth we can't move — clamp to
    // current position so the caller still gets a valid value.
    if (lo > hi) return left.endSample;

    const std::size_t clamped = std::clamp(newSample, lo, hi);
    left.endSample   = clamped;
    right.startSample = clamped;

    refineAnchorByEnergy(left,  audio, sampleRate);
    refineAnchorByEnergy(right, audio, sampleRate);

    return clamped;
}

bool addBoundary(std::vector<SyllableSpan>& syls,
                 std::size_t atSample,
                 const std::vector<float>& audio,
                 double sampleRate,
                 std::size_t minWidthSamples) {
    // Find which syllable contains atSample.
    std::size_t k = syls.size();  // sentinel
    for (std::size_t i = 0; i < syls.size(); ++i) {
        if (atSample >= syls[i].startSample && atSample < syls[i].endSample) {
            k = i;
            break;
        }
    }
    if (k >= syls.size()) return false;  // outside all syllables

    const auto& syl = syls[k];
    // Check distance from both existing boundaries of the host syllable.
    if (atSample < syl.startSample + minWidthSamples) return false;
    if (atSample + minWidthSamples > syl.endSample)   return false;

    // Split: left half inherits syl's start; right half inherits syl's end.
    SyllableSpan leftHalf  = syl;
    SyllableSpan rightHalf = syl;
    leftHalf.endSample    = atSample;
    rightHalf.startSample = atSample;
    // Clear phonemeIndices — they are meaningless after a boundary edit.
    leftHalf.phonemeIndices.clear();
    rightHalf.phonemeIndices.clear();

    refineAnchorByEnergy(leftHalf,  audio, sampleRate);
    refineAnchorByEnergy(rightHalf, audio, sampleRate);

    syls[k] = leftHalf;
    syls.insert(syls.begin() + static_cast<std::ptrdiff_t>(k) + 1,
                rightHalf);
    return true;
}

bool removeBoundary(std::vector<SyllableSpan>& syls,
                    std::size_t boundaryIndex,
                    const std::vector<float>& audio,
                    double sampleRate) {
    // Legal interior boundary: 1 .. syls.size()-1
    if (boundaryIndex == 0 || boundaryIndex >= syls.size()) return false;

    auto& left  = syls[boundaryIndex - 1];
    const auto& right = syls[boundaryIndex];

    // Merge right into left.
    left.endSample = right.endSample;
    // Phoneme indices become stale after boundary edits — clear.
    left.phonemeIndices.clear();

    refineAnchorByEnergy(left, audio, sampleRate);

    syls.erase(syls.begin() + static_cast<std::ptrdiff_t>(boundaryIndex));
    return true;
}

} // namespace guitar_dsp::audio
