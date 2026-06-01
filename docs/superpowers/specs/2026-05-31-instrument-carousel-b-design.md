# Instrument Carousel B — Design Spec (Phase 4b)

**Status:** Approved for planning
**Date:** 2026-05-31
**Parent spec:** [`2026-05-29-while-my-guitar-gently-speaks-design.md`](2026-05-29-while-my-guitar-gently-speaks-design.md) §5.1
**Builds on:** [`2026-05-31-instrument-carousel-a-design.md`](2026-05-31-instrument-carousel-a-design.md) (the `audio::Carousel` chain + wet-source routing it added)

## 1. Goal

Add the two pitch/harmony-based instrument patches deferred from Phase 4a:
**choir/pad** and **piano-ish**. Both derive from the **live guitar audio**
(no MIDI notes, no pitch tracking) using fixed-ratio time-domain pitch
shifting. They extend the existing `audio::Carousel` chain with new bespoke
stages; the three Phase-4a patches that survive (distortion, 8-bit, auto-wah)
are untouched.

## 2. Scene assignment

The carousel has exactly 5 slots (scenes 1–5) and all are used. The two new
patches **replace** the two surviving sustained-harmonic Phase-4a patches,
keeping the overall set timbrally diverse:

| Scene id | Phase-4a (current) | Phase-4b (new) |
|---|---|---|
| 1 | organ / Leslie | **choir / pad** |
| 2 | distorted guitar | *(unchanged)* |
| 3 | synth lead | **piano-ish** |
| 4 | 8-bit chiptune | *(unchanged)* |
| 5 | auto-wah | *(unchanged)* |

Files `01_carousel_organ.json` → rename `01_carousel_choir.json`;
`03_carousel_synth.json` → rename `03_carousel_piano.json`. Each keeps its
`id`; only name/filename + the carousel block change.

**Why these two:** organ and synth are the two sustained-harmonic patches that
choir/pad and piano supersede. Distortion, 8-bit, and auto-wah are the
percussive/aggressive/funky members the user specifically wanted in 4a and are
kept.

## 3. New DSP stages (all bespoke, allocation-free, mono)

1. **`PitchShifter`** — time-domain granular / overlap-add pitch shift at a
   **fixed ratio** (no pitch detection). Two read pointers into a circular
   buffer, crossfaded by a windowing function, advancing at the pitch ratio.
   Grain/window size is tunable (latency vs artifact tradeoff; default chosen
   by ear). Pre-sized buffer in `prepare()`; `processSample`/`processBlock`
   allocation-free.
2. **`Harmonizer`** — a small fixed array of `PitchShifter`s (≤4 voices) at
   configured semitone intervals, each optionally detuned a few cents, summed
   with a configurable dry mix. This is what makes "choir/pad" lush.
3. **`Comb`** — short feedback comb filter (delay line + feedback gain,
   pre-sized). The piano string-resonance / attack tool (deferred from 4a).
4. **`Formant`** — a parallel bank of 2–3 fixed resonant bandpass peaks
   approximating a single vowel ("ah"/"oh") to give choir vocal character,
   blended with the input by an amount. **NOT** true LPC/pitch-independent
   formant shifting — a fixed formant-emphasis filter only (true formant
   shifting is out of scope: too high-risk for the stability mandate).

Semitone→ratio conversion: `ratio = 2^(semitones/12)`, computed at config time
(message thread), never per sample.

## 4. Chain placement

Pitch/harmony are voice generators, so they run **early**, before the existing
tone-shapers. The full Carousel chain becomes:

```
drive → pitch/harmonizer → waveshaper → comb → resonant filter → formant
      → chorus → reverb → output trim → brick-wall limiter
```

Each new stage bypasses at near-zero cost when its config sub-block is absent.
The three surviving 4a patches declare none of the new blocks, so their signal
path is byte-identical to today (guarded by the existing golden/bounded tests).
The Phase-4a brick-wall limiter at the end already protects the output.

## 5. CarouselConfig extensions

New optional sub-blocks on `scenes::CarouselConfig`, parsed defensively in the
same style as the 4a blocks (absent block / key → default, no throw):

```json
"pitch":      { "semitones": 12, "mix": 1.0, "grainMs": 40 },
"harmonizer": { "intervals": [12, 7, 0], "detuneCents": [0, 0, 6], "mix": 0.7 },
"comb":       { "freqHz": 220, "feedback": 0.6, "mix": 0.5 },
"formant":    { "vowel": "ah", "amount": 0.6 }
```

- `pitch` — single-voice fixed shift. `harmonizer` — multi-voice (supersedes
  `pitch` when both present; piano uses `pitch`, choir uses `harmonizer`).
- `intervals` / `detuneCents` are parallel arrays (≤4 entries); extra entries
  ignored, missing `detuneCents` defaults to 0.
- `vowel` ∈ {`ah`, `oh`, `ee`} maps to a fixed 2–3-peak formant table.

Struct fields are fixed-size (no heap): e.g. `int harmIntervals[4]`,
`int harmVoiceCount`, etc., so the config stays trivially-copyable for the
existing audio-thread atomic swap.

## 6. Preset sketches (tuned by ear during implementation)

- **Choir / pad** (`01_carousel_choir.json`): harmonizer
  `[+12, +7, 0]` with a few cents of detune → formant "ah" amount ~0.6 →
  light filter → big reverb (roomSize ~0.7, wet ~0.4) → slow `transitionMs`.
- **Piano-ish** (`03_carousel_piano.json`): `pitch +12` blended ~0.5 with dry
  → comb (feedback ~0.6) → resonant lowpass with quick envelope decay → light
  reverb. Honest target: "plucky electric-piano-ish," not a concert grand.

## 7. Testing

- **Per-stage unit tests**:
  - `PitchShifter`: feed a sine at f; output's dominant period matches
    `f * ratio` within a tolerance (autocorrelation or zero-cross estimate).
    Bypass (ratio 1.0) ≈ identity within window latency.
  - `Harmonizer`: with 1 voice at interval 0 + dry, output ≈ scaled input;
    with multiple voices, output is finite/bounded and energy increases.
  - `Comb`: an impulse produces decaying echoes spaced by the delay period.
  - `Formant`: emphasizes the target vowel's peak bands vs a flat input.
- **RealtimeSentinel**: every new stage + the full extended chain allocate
  nothing in `process`.
- **Full-chain integrity**: choir + piano presets stay finite and within
  ±1.0 (the brick-wall limiter), no NaN/Inf, across a sustained pluck.
- **Scene-switch integration**: activating choir / piano transforms the guitar
  vs clean and stays finite; switching among 4a patches + 4b patches + speaking
  scenes routes correctly.

## 8. Out of scope (this phase)

- True LPC / pitch-independent formant shifting (4b uses a fixed
  formant-emphasis filter only).
- Pitch detection / tracked diatonic harmony (intervals are fixed).
- Polyphonic pitch shifting (the input is treated as monophonic guitar).
- Any scene-bank / second-page system (we replace 2 slots, not add slots).
- Visualization (Phase 5), hardening (Phase 6).

## 9. Risks & mitigations

- **Latency.** Granular pitch shifting adds roughly one grain of delay
  (~20–40 ms at the default window). Acceptable for the sustained choir/pad;
  **noticeable on percussive piano** — piano may feel slightly laggy.
  Mitigation: tune grain size by ear (smaller = lower latency, more warble);
  document the tradeoff.
- **Piano realism.** "Guitar → piano" is the weakest target in the project;
  the octave-layer + comb approach lands at "electric-piano-ish," not a
  concert grand. Expectation set explicitly; the golden tests only guard
  regression, not timbral fidelity.
- **Granular artifacts.** Warble on sustained notes; masked for choir by
  detuned layering + reverb, more exposed on piano. Mitigation: crossfade
  windowing + per-preset grain tuning.
- **Self-oscillation in the comb** at high feedback. Mitigation: clamp
  feedback < 1.0 and rely on the existing output limiter as a backstop.
