# Instrument Carousel A — Design Spec (Phase 4)

**Status:** Approved for planning
**Date:** 2026-05-31
**Parent spec:** [`2026-05-29-while-my-guitar-gently-speaks-design.md`](2026-05-29-while-my-guitar-gently-speaks-design.md) §5.1, §7.2
**Supersedes for scenes 1–5:** the placeholder carousel behavior from Phase 1–3.

## 1. Goal

Give carousel scenes 1–5 real, recognizable instrument/effect timbres by
processing the **live guitar audio** through a parametric effects chain.
None of these patches require MIDI notes or pitch detection — they are pure
audio-domain transformations, exactly like guitar pedals. MIDI (FCB1010)
continues to be used *only* for scene switching, never as a note source.

This is "Carousel A": the five patches that need **no pitch-shifting**.
Pitch/harmony-based instruments (piano, choir/pad) are deferred to a future
**Phase 4b** because pitch-shifting a clean guitar convincingly while staying
under the 5 ms latency budget and allocation-free is the highest-risk DSP in
the project.

## 2. The five patches

Mapped onto the existing `assets/scenes/` files (two are repurposed away
from their pitch-dependent placeholders):

| Scene id | File | Name | Core stages |
|---|---|---|---|
| 1 | `01_carousel_organ.json` (keep) | Organ / Leslie | tube warmth + chorus (Leslie) + light reverb |
| 2 | `02_carousel_piano.json` → rename `02_carousel_distortion.json` | Distorted guitar | drive + hard waveshaper + post tone filter |
| 3 | `03_carousel_synth.json` (keep) | Synth lead | LFO-swept resonant filter + saturation + chorus + reverb |
| 4 | `04_carousel_8bit.json` (keep) | 8-bit chiptune | bit crusher + sample-rate reducer (+ light hard waveshape) |
| 5 | `05_carousel_choir.json` → rename `05_carousel_autowah.json` | Auto-wah | envelope-follower-driven resonant bandpass + drive |

Scenes 2 (piano-ish) and 5 (choir/pad) are repurposed because both need
pitch/harmony DSP; their original instruments move to **Phase 4b**.

All five derive their pitch and dynamics from whatever the performer plays;
the effect only reshapes timbre. Each carousel scene's `mixer.dryWet`
(currently `0.0`, i.e. fully dry — which is why they sound clean today) is
raised so the processed signal is actually heard; exact wet amount tuned per
patch during implementation.

## 3. DSP stage chain

A single `Carousel` module processes a mono buffer through a fixed-order
chain. Each stage is bypassed at near-zero cost when its JSON sub-block is
absent (or its amount/mix is zero). Order:

1. **Drive** — pre-gain in dB (custom, trivial).
2. **Waveshaper** — `juce::dsp::WaveShaper`, selectable curve: `tanh` (soft),
   `hardclip`, `foldback`. `amount` scales pre-shaper gain.
3. **Crusher** — custom: bit-depth quantizer (`bits`) + sample-hold
   downsampler (`downsample` factor). Both bypass when absent.
4. **Resonant filter** — `juce::dsp::StateVariableTPTFilter<float>`
   (LP / BP / HP). Modulation modes (`mod`):
   - `static` — fixed cutoff.
   - `envelope` — cutoff = base + envAmount × follower(input). Custom
     one-pole envelope follower (attack/release fixed for v1). This is the
     auto-wah engine.
   - `lfo` — cutoff modulated by a sine LFO at `lfoHz`. Custom phasor.
5. **Chorus / Leslie** — `juce::dsp::Chorus<float>` (`rateHz`, `depth`,
   `mix`). Covers the rotating-Leslie shimmer and synth width.
6. **Plate reverb** — `juce::dsp::Reverb` (`roomSize`, `wet`). Freeverb-based;
   "plate-ish" is acceptable for the demo.
7. **Output trim** — post-gain in dB.

**Explicitly NOT in Phase 4A** (deferred to 4b): pitch shifter / octaver,
multi-voice harmonizer, formant shifter, comb filter (the comb is mainly a
piano-attack tool).

### Implementation note
- DSP building blocks use JUCE's `juce::dsp` modules where they fit
  (`WaveShaper`, `StateVariableTPTFilter`, `Chorus`, `Reverb`). These are
  allocation-free and real-time-safe once `prepare()`d. Only the simple,
  bespoke bits (drive, crusher, envelope follower, LFO phasor) are
  hand-rolled.
- This requires adding `juce_dsp` to the `guitar_dsp_audio` static library's
  link list (currently it links `juce_audio_formats` only; the app already
  links `juce_dsp`).
- All `prepare(sampleRate, blockSize, channels=1)` happens on the message
  thread in `prepareToPlay`; `process()` is allocation/lock free.

## 4. Signal routing

`AudioGraph` currently is: InputStage → {ChannelVocoder | TTSClipPlayer} →
Mixer. Phase 4A adds the `Carousel` as a third "wet source" selected by scene
type:

- **Instrument scene** (`carousel.enabled == true`):
  `InputStage → Carousel → Mixer.wet`
- **Speaking scene** (`vocoder.enabled == true`):
  `InputStage → ChannelVocoder(driven by TTS clip) → Mixer.wet` (unchanged)
- **Clean / panic** (neither enabled): dry only (`Mixer.dryWet → 0` wet).

The active branch is chosen from `SceneEngine`'s atomic snapshot each block
(a small enum/flag: `None | Vocoder | Carousel`). The Mixer's existing
equal-power dry/wet and `transitionMs` ramp handle click-free blending and
scene transitions; no new transition machinery is needed.

A scene is never simultaneously carousel + vocoder; if a JSON file sets both
`enabled`, carousel wins (documented; SceneLibrary can warn).

## 5. Scene JSON — `carousel` block

```json
"carousel": {
  "enabled": true,
  "drive": 6.0,
  "waveshaper": { "type": "hardclip", "amount": 0.8 },
  "crusher": { "bits": 4, "downsample": 8 },
  "filter": {
    "mode": "bandpass",
    "cutoffHz": 800,
    "resonance": 0.7,
    "mod": "envelope",
    "envAmount": 2000,
    "lfoHz": 0
  },
  "chorus": { "rateHz": 5.0, "depth": 0.3, "mix": 0.4 },
  "reverb": { "roomSize": 0.4, "wet": 0.2 },
  "outputTrimDb": 0.0
}
```

- Any absent sub-block → that stage bypasses.
- Continuous parameters ramp over the scene's existing `transitionMs`
  (default 20 ms) on activation to prevent zipper noise. v1 ramps the
  high-impact continuous params (drive, filter cutoff/resonance, reverb wet,
  output trim); discrete params (waveshaper type, crusher bits, filter mode)
  switch instantly at the block boundary (acceptable: scene changes already
  mute/duck briefly).

### The five preset files

- `01_carousel_organ.json` — organ/Leslie (tanh warmth, chorus rate≈6 Hz
  depth high, light reverb).
- `02_carousel_distortion.json` (renamed from `_piano`) — distorted guitar
  (drive high, hardclip, post LP filter static ~3 kHz).
- `03_carousel_synth.json` — synth lead (filter mod=lfo, saturation tanh,
  chorus, reverb).
- `04_carousel_8bit.json` — 8-bit chiptune (crusher bits≈4, downsample≈8,
  light hardclip).
- `05_carousel_autowah.json` (renamed from `_choir`) — auto-wah (drive
  moderate, filter mode=bandpass, mod=envelope, high resonance).

Each file keeps its existing `id`; only the name/filename and the new
`carousel` + raised `dryWet` change.

## 6. Testing strategy

- **Per-stage unit tests** (`tests/unit/audio/`): each bespoke stage verified
  against a known signal —
  - crusher: output takes only ≤ 2^bits distinct levels; downsample holds
    samples for N frames.
  - envelope follower: rises on a loud burst, decays on silence.
  - filter wrapper: attenuates a tone above LP cutoff / below HP cutoff.
  - waveshaper: hardclip bounds output to ±threshold.
  - LFO phasor: produces a bounded sine at the requested rate.
- **Carousel integration unit test**: a full preset processes a synthetic
  pluck and produces finite, non-NaN, bounded output; an all-bypassed preset
  is (near) identity.
- **RealtimeSentinel**: `Carousel::process` performs zero heap allocations.
- **Golden-file regression**: each of the 5 presets run on the standard
  synthetic guitar pluck → committed reference WAV/hash, regenerate-gated by
  `GUITAR_DSP_REGENERATE_GOLDENS=1`.
- **Scene-switch integration**: clean → each instrument → assert the output
  spectrum/energy changes and there are no NaNs/denormals across the
  transition; clean → instrument → speaking proves the branch selector swaps
  correctly.

## 7. Out of scope (this phase)

- Pitch shifter / octaver, harmonizer, formant shifter, comb filter → Phase 4b.
- Per-stage UI controls (carousel is configured via scene JSON + hot reload
  only; no on-screen knobs).
- Karaoke text / spectrogram visualization → Phase 5.
- Any MIDI note handling — out of scope for the entire project (MIDI = scene
  switching only).

## 8. Risks & mitigations

- **Presets sounding generic.** Mitigation: tune by ear during implementation
  via hot reload; the golden-file tests only guard regressions, not taste.
- **`juce::dsp::Reverb` tail not real-time-safe on parameter change.** It is
  RT-safe for `process`; parameter updates (`setParameters`) are cheap struct
  copies — apply them on the message thread / block boundary, never mid-block.
- **State-variable filter self-oscillation at high resonance** producing
  runaway output. Mitigation: clamp resonance to a safe max and hard-limit
  the carousel output stage.
