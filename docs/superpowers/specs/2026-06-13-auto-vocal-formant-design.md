# Auto-Vocal Formant (Scene 4)

**Status:** Design approved 2026-06-13. Implementation pending.

**Companion specs:**
- [Vocal Guitar — Clip Bank (Scene 2)](2026-06-13-vocal-guitar-clip-bank-design.md)
- [Mic Talkbox (Scene 3)](2026-06-13-mic-talkbox-design.md)

## Problem

A modulator-driven vocal guitar (Scenes 2 and 3) gives the most authentic
mouth-guitar quality but requires either a clip bank or a live mic. A third
mode is useful: an automatic, modulator-free formant patch that walks the
guitar through a vowel sequence, no clips, no mic, just LFO + envelope on a
filter. This is the "auto-wah but with real vowels" — less expressive than
Scenes 2/3 but bulletproof, hands-free, and instantly demo-able.

To do this well we need *smooth glides between vowels*, not three fixed
filter shapes. "Weedly weedly" requires interpolating between EE and EH 8
times per second; "neow neow" requires EE → OW glides on each pick. The
current `Formant` class is fixed to three discrete vowels (Ah, Oh, Ee) with no
interpolation — it cannot produce the target sound. This spec rewrites the
formant engine to a continuous vowel space.

## Non-goals

- Pitch-shifting formant correction. The formant filter is purely additive
  resonance; pitch handling lives in `PitchShifter` and `Harmonizer`.
- Tempo sync. The LFO is free-running in v1. Tempo sync is plausible later
  but the app has no global tempo source today.
- Replacing the existing `formantVowel` enum usage in other scenes. Static
  vowel mode must keep working for any scene that already uses it (none of
  the current scene JSON files set it, but the enum is in the schema and is
  honored by the audio path; preserve that contract).

## Approach

Rewrite `Formant` to operate on a continuous **vowel position** in a small
1-D vowel space, interpolating formant frequencies between named anchor
vowels. The position is driven by either an LFO, an onset-keyed envelope, or
a static value, selected per scene.

The vowel space is a circular ordered list of anchors:

```
positions:  0.0 ─── 0.25 ─── 0.5 ─── 0.75 ─── 1.0 (== 0.0)
            EE       EH       AH      OH       OO (wraps to EE)
```

Position is a `float` in `[0, 1)`. The active F1/F2/F3 formant frequencies and
bandwidths are linearly interpolated between the two adjacent anchors based
on the fractional position. Five anchors is enough for the Jack Black target
phrases (EE↔EH for weedly, EH↔OH for neow, AH↔OO for wow). The anchor table
is in code, not config — it's a physics constant.

For each scene the JSON declares:
- `mode`: how position is driven (`static`, `lfo`, `envelope`).
- For `lfo`: a `pattern` of position breakpoints + a rate.
- For `envelope`: a `pattern` of position breakpoints stepped per onset.
- A `depth` (the existing `amount` — wet/dry blend of the filter).

For "weedly" the LFO drives a triangle between positions 0.0 (EE) and 0.2
(EH) at 8 Hz. For "neow" the envelope-driven mode advances through anchors
[EH, OH] on each onset, with each onset retriggering a fast attack into the
next position. For sustained vowel sweeps, an LFO between 0.0 and 1.0 walks
the entire space.

## Architecture

### Modified components

#### `src/audio/Formant.{h,cpp}` — rewrite

```cpp
namespace guitar_dsp::audio {

// Continuous vowel-space formant filter. Operates on a 1-D vowel position
// in [0, 1) that interpolates between 5 anchor vowels (EE, EH, AH, OH, OO).
// Three resonant peaks (F1, F2, F3) tuned per position; mix is controlled
// by `amount`.
//
// Position can be set every sample (or every N samples for cheaper
// modulation). Coefficients are recomputed lazily when position changes by
// more than a small threshold to bound CPU.
class Formant {
public:
    void prepare(double sampleRate);
    void reset();

    // Position in [0, 1). Out-of-range is wrapped.
    void setPosition(float p) noexcept;

    // 0..1 wet/dry blend with the input.
    void setAmount(float a) noexcept { amount_ = a; }

    float processSample(float x) noexcept;

    // --- Compatibility shim for existing static-vowel callers ---
    // Maps the existing 4-value enum onto fixed positions:
    //   None → amount_ = 0 (bypass)
    //   Ah   → position 0.5
    //   Oh   → position 0.75
    //   Ee   → position 0.0
    // Existing scene JSON keeps working unchanged.
    void setVowel(scenes::CarouselConfig::Vowel v) noexcept;

private:
    struct Anchor { float f1, f2, f3, bw1, bw2, bw3; };
    static constexpr std::array<Anchor, 5> kAnchors = {{
        // EE, EH, AH, OH, OO — formant frequencies in Hz, bandwidths in Hz.
        // Concrete values populated in implementation from standard tables
        // (e.g. Peterson-Barney 1952 male-average). Reasonable starting:
        // EE: 270/2290/3010, EH: 530/1840/2480, AH: 730/1090/2440,
        // OH: 570/840/2410,  OO: 300/870/2240. Bandwidths ~60/90/120.
    }};

    void recomputeCoefs(float position) noexcept;

    juce::dsp::StateVariableTPTFilter<float> peaks_[3];
    double sampleRate_ = 48000.0;
    float position_       = 0.0f;
    float lastComputedPos_ = -1.0f;  // forces first recompute
    float amount_         = 0.0f;
};

} // namespace guitar_dsp::audio
```

Position-driven recompute is gated by a small threshold (~0.005) to bound
CPU — the SVTPT filter is cheap but recomputing 3 of them every sample for
every voice is wasteful when the LFO moves slowly.

#### `src/audio/FormantModulator.{h,cpp}` (NEW)

```cpp
namespace guitar_dsp::audio {

// Drives a Formant's vowel position from a sequence of breakpoints + a
// timebase. Two modes:
//   - Lfo: position is a linear walk through breakpoints at `rateHz`,
//     wrapping after the last. The walk shape is "ramp through
//     breakpoint list in order, hold each for 1/N of the cycle period."
//   - Envelope: position advances one breakpoint per detected onset, with
//     a fast attack into the new position (linear ~30 ms ramp).
class FormantModulator {
public:
    enum class Mode { Static, Lfo, Envelope };

    void prepare(double sampleRate);
    void reset();

    void setMode(Mode m) noexcept;
    void setStaticPosition(float p) noexcept;     // for Mode::Static
    void setBreakpoints(std::vector<float> bp) noexcept;
    void setLfoRateHz(float hz) noexcept;          // for Mode::Lfo
    void setEnvelopeAttackMs(float ms) noexcept;   // for Mode::Envelope

    // Audio thread. onsetSrc is required only in Envelope mode (passes
    // through OnsetDetector); ignored otherwise. Returns the position at
    // each sample so caller can drive Formant::setPosition (or compute it
    // once per block for cheaper updates).
    void process(const float* onsetSrc, float* posOut, std::size_t numSamples) noexcept;

private:
    Mode mode_ = Mode::Static;
    std::vector<float> breakpoints_; // wrapped circularly
    float staticPos_ = 0.0f;
    float lfoPhase_ = 0.0f;
    float lfoIncrPerSample_ = 0.0f;
    int   envIndex_ = 0;
    float envCurrentPos_ = 0.0f;
    float envTargetPos_ = 0.0f;
    float envRampPerSample_ = 0.0f;
    OnsetDetector onset_;
};

} // namespace guitar_dsp::audio
```

For block-rate updates, callers can either read `posOut[0]` per block
(cheap, no audible artifact at LFO rates below ~20 Hz) or read each sample
(needed for envelope-mode attack ramps).

#### `src/audio/CarouselMod.{cpp,h}` — wire FormantModulator into the chain

`CarouselMod` currently calls `Formant::setVowel` once at scene activation.
After the rewrite, it instead:

1. Constructs a `FormantModulator` based on `CarouselConfig::formantMode` and
   related fields.
2. On each audio block, calls `formantMod_.process(carrierBuffer, posBuffer)`
   and applies `formant_.setPosition(posBuffer[i])` per-sample (or per-block
   for cheaper modes — implementation detail).
3. Static mode falls through to `formant_.setPosition(staticPos)` once, then
   processes normally.

#### `src/scenes/Scene.h` — `CarouselConfig` extensions

```cpp
struct CarouselConfig {
    // existing fields…

    enum class Vowel { None, Ah, Oh, Ee };  // KEEP for back-compat
    Vowel formantVowel  = Vowel::None;
    float formantAmount = 0.0f;

    // NEW (Phase C):
    enum class FormantMode { Static, Lfo, Envelope };
    FormantMode formantMode = FormantMode::Static;

    // Vowel position breakpoints in [0,1). 0.0=EE, 0.25=EH, 0.5=AH,
    // 0.75=OH, ~1.0 wraps back to EE. Empty in Static mode.
    std::vector<float> formantBreakpoints;

    float formantLfoHz       = 0.0f;   // Lfo mode
    float formantEnvAttackMs = 30.0f;  // Envelope mode

    // existing outputTrimDb etc.
};
```

The existing `formantVowel` enum stays as a compatibility surface for any
caller that wants the old static-vowel behavior; the `Formant::setVowel`
shim translates it to a static position. New scenes use the breakpoint API.

#### `src/scenes/SceneLibrary.cpp`

Extend the `formant` JSON parser block:

```jsonc
"formant": {
  "vowel": "ah",            // EXISTING — still honored for static mode
  "amount": 0.8,            // EXISTING
  // NEW:
  "mode": "lfo",            // "static" | "lfo" | "envelope"
  "breakpoints": [0.0, 0.2],
  "lfoHz": 8.0,
  "envAttackMs": 25.0
}
```

Backward compatibility: a scene with just `vowel` + `amount` continues to
work via the shim (`mode` defaults to `static`).

### Scene 4 JSON

Replace `assets/scenes/04_carousel_8bit.json` with:

```json
{
  "id": 4,
  "name": "Auto-Vocal — weedly",
  "color": "#ff7f24",
  "mixer": { "masterGainDb": -5.0, "dryWet": 0.95, "transitionMs": 20 },
  "carousel": {
    "enabled": true,
    "drive": 12.0,
    "waveshaper": { "type": "tanh", "amount": 1.5 },
    "filter": { "mode": "lowpass", "cutoffHz": 6000, "resonance": 0.3, "mod": "static" },
    "formant": {
      "amount": 0.85,
      "mode": "lfo",
      "breakpoints": [0.0, 0.18],
      "lfoHz": 9.0
    },
    "reverb": { "roomSize": 0.2, "wet": 0.08 },
    "outputTrimDb": -3.0
  }
}
```

This is the "weedly weedly" preset — fast LFO bouncing between EE (0.0) and
EH (0.18) at 9 Hz on a driven guitar. A second/third scene preset ("neow"
envelope-mode, "yowwww" slow full-range LFO) can be added once the engine
works, but only Scene 4's slot is claimed by this spec.

## Data flow (per audio block)

```
clean guitar ──► [Carousel chain: drive → shaper → filter] ──►
                                                                 │
                                                                 ▼
                                                              Formant
                                                                 ▲
                                                                 │
                                       FormantModulator ─────────┘
                                              ▲
                                              │
                              (Envelope mode) clean guitar ──► OnsetDetector
                              (Lfo mode)      free-running phase accumulator
                              (Static mode)   constant position
```

In Lfo and Static modes, no onset detection runs — the formant sweep is
guitar-input-agnostic (it modulates whatever spectrum the carousel chain
produces). In Envelope mode, picking is the trigger.

## Testing

**Unit tests** (`tests/`):
- `Formant_PositionInterpolatesBetweenAnchors` — set position 0.5 (AH),
  feed an impulse, FFT the response, assert peaks within ±5% of AH formant
  frequencies. Repeat for position 0.0 (EE) and 0.75 (OH).
- `Formant_PositionMidwayBetweenAnchorsInterpolates` — set position 0.125
  (halfway EE↔EH), assert peak frequencies are roughly the average.
- `Formant_StaticVowelShimMapsToPosition` — call setVowel(Ee), assert
  internal position == 0.0; setVowel(Ah) → 0.5; setVowel(None) → amount = 0.
- `FormantModulator_LfoCyclesBreakpoints` — set breakpoints [0.0, 0.2],
  rate 1 Hz, prepare 48 kHz, process 1 second, assert posOut traces a
  triangle (or selected shape) between 0.0 and 0.2.
- `FormantModulator_EnvelopeAdvancesOnOnset` — set breakpoints [0.0, 0.5,
  0.75], feed three onsets, assert posOut steps through each (with attack
  ramp).
- `SceneLibrary_ParsesFormantMode` — JSON with new `mode`+`breakpoints`
  populates `CarouselConfig::formantMode` and `formantBreakpoints`.
- `SceneLibrary_BackwardCompatVowelStillWorks` — JSON with only the old
  `vowel`+`amount` fields produces a static-mode CarouselConfig.

**Manual / demo verification:**
- Activate Scene 4, pick any note: hear an unmistakable "weedly weedly"
  vowel oscillation on top of the guitar tone.
- Hold a sustained note: the weedly continues at LFO rate, independent of
  pick attacks (Lfo mode characteristic).
- Switch from Scene 2 to Scene 4 to Scene 6: no audio glitch, no parameter
  bleed between formant scenes and clip-bank scenes.
- Load an older scene that uses the deprecated `formantVowel` field
  (manually edit one for the test): confirm it still produces a static
  vowel (shim works).

## Risk / open questions

- **Formant tables are rough.** Standard formant frequency tables vary by
  speaker (male vs. female vs. child average) and by source. The values
  in the spec are male-average starting points; implementation should
  expose tunable constants and iterate based on listening. There is no
  "correct" answer; the goal is "sounds cartoonishly vowel-y," not
  scientific accuracy.
- **Filter bandwidth tradeoff.** Narrower bandwidth = more pronounced vowel
  but more ringing artifacts. Wider = smoother but more washed-out. The
  initial bandwidths (~60/90/120 Hz) lean toward pronounced; expect to
  retune.
- **Per-sample vs per-block position updates.** Per-sample is correct for
  envelope-mode attack ramps; per-block is fine for slow LFOs. Implementer
  picks one consistent strategy or uses both (per-block for Static/Lfo,
  per-sample for Envelope).
- **Schema versioning.** Adding `formantMode` + `formantBreakpoints` while
  keeping `formantVowel` honored is mild schema bloat. Acceptable in v1;
  if scenes proliferate, consider a `Scene.h` cleanup pass later.

## Deletes

- `assets/scenes/04_carousel_8bit.json` (replaced by new Scene 4 JSON
  above).
- The existing `Formant::setVowel` implementation in `Formant.cpp` is
  rewritten; the `Vowel` enum stays in `Scene.h` for back-compat (do not
  delete it).
- No deletion of `Crusher.cpp` / `Crusher.h` — after Scene 4 is replaced, no
  current scene uses the bit-crusher stage, but the crusher is part of the
  generic carousel effect rack (`CarouselConfig::crusherBits`,
  `crusherDownsample`) and is wired through `CarouselMod`. Leaving it in
  place preserves the rack's surface area for future scene authoring.
  Deleting it would require removing the schema fields, parsing in
  `SceneLibrary`, and rack wiring — a wider blast radius than the design
  intent of "replace three scene JSONs."
