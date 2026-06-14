#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>

#include "scenes/Scene.h"

namespace guitar_dsp::audio {

// Vowel-emphasis filter: 3 parallel resonant bandpass peaks at a vowel's
// formant frequencies, summed and blended with the input by `amount`.
//
// Two control surfaces, both supported simultaneously:
//   1. `setVowel(Vowel)` — back-compat for the original static-vowel enum.
//      None bypasses (output == input exactly). Ee/Ah/Oh map to fixed
//      positions in the continuous vowel space (see kAnchors).
//   2. `setPosition(float)` — continuous position in [0, 1) interpolating
//      between 5 anchors (EE/EH/AH/OH/OO). Out-of-range wraps.
//
// Calling `setPosition` clears the static-bypass state set by
// `setVowel(None)`. Calling `setVowel(None)` re-enables bypass.
class Formant {
public:
    void prepare(double sampleRate);
    void reset();

    // Static-vowel API (back-compat). Vowel::None = bypass.
    void setVowel(scenes::CarouselConfig::Vowel v) noexcept;

    // Continuous-position API.
    void setPosition(float p) noexcept;

    void setAmount(float a) noexcept { amount_ = a; }

    float processSample(float x) noexcept;

private:
    struct Anchor { float f1, f2, f3; };
    // EE / EH / AH / OH / OO — formant frequencies in Hz. The EE/AH/OH
    // values match the previous static-vowel table bit-for-bit so existing
    // [formant] and [carousel] tests keep passing. EH and OO are new.
    static constexpr std::array<Anchor, 5> kAnchors = {{
        {  270.0f, 2300.0f, 3000.0f },  // 0.00  EE  (matches old peaksFor(Ee))
        {  530.0f, 1840.0f, 2480.0f },  // 0.25  EH  (new)
        {  700.0f, 1220.0f, 2600.0f },  // 0.50  AH  (matches old peaksFor(Ah))
        {  500.0f, 1000.0f, 2400.0f },  // 0.75  OH  (matches old peaksFor(Oh))
        {  300.0f,  870.0f, 2240.0f },  // ~1.00 OO  (new)
    }};

    static constexpr int kPeaks = 3;

    void recomputeCoefs(float position) noexcept;

    juce::dsp::StateVariableTPTFilter<float> peaks_[kPeaks];
    double sampleRate_      = 48000.0;
    float  position_        = 0.0f;
    float  lastComputedPos_ = -1.0f;
    float  amount_          = 0.0f;
    bool   bypass_          = true;   // setVowel(None) sets; setVowel(other) or setPosition() clears
};

} // namespace guitar_dsp::audio
