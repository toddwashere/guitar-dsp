# Scene 11 (Vocoded Sung Vowels) + Scene 12 (Direct Formant-Shifted Singing) — Design

**Status:** Draft for review
**Author:** Todd + Claude
**Date:** 2026-06-23
**Branch target:** `main` (per [feedback_commit_often.md](../../../../.claude/projects/-Users-user-GIT-guitar-dsp/memory/feedback_commit_often.md))

## 0. Build workflow (project-level)

Per user direction (2026-06-23):

- All three milestones (M1 → M1.5 → M2) are built **sequentially in a
  single development pass**, with no manual user-test gate between
  them. Test gates between milestones are automated only (unit +
  integration suites must pass before moving to the next milestone).
- Manual user testing happens **after M2 lands**, against the full
  three-scene flow (scene 11 vocoded, scene 12 direct, scene 13
  bypass).
- Top constraint, restated: **no existing scene may regress and no
  new crash/bug may be introduced**. Sections 7 (Regression safety)
  and 8 (Testing strategy) enumerate how that's enforced.

## 1. Goal

Push the talking-guitar pipeline past "TTS triggered word-by-word" into
something an audience hears as actual *singing*. Two new scenes,
sharing one curated VocalSet source bundle, each demonstrating a
different point on the quality-vs-complexity curve:

- **Scene 11 — "Sung Vowels" (vocoded).** Drop-in upgrade to scene
  10's pipeline. Vocoder, saw carrier, sung-vowel source instead of
  TTS. Ship-ready in days, no new DSP.
- **Scene 12 — "Sung Direct" (formant-shifted).** Real human voice
  shifted in pitch to track the guitar, no vocoder coloration. The
  audible payoff. Gated by a formant-preserving pitch shifter.

The previous safety scene `11_bypass.json` moves to slot 13.

## 2. Why these two scenes (not one)

Arch B (vocoded) and Arch A (direct shift) sound categorically
different. The vocoded path always carries the saw carrier's signature
— some audiences will read it as "vocodery synth voice." The direct
path preserves the singer's character but needs a non-trivial shifter
implementation. Shipping both:

- Hedges the demo: scene 11 lands cheaply and stays as a fallback even
  if scene 12's DSP work slips.
- Creates a built-in A/B comparison that tells a story on stage
  ("here's the vocoder approach, here's the real-voice approach").
- Re-uses the same source bundle, the same pitch-aware grain
  selection, the same `VowelGrainLoop`, the same YIN.

## 3. Source material — curated VocalSet subset

VocalSet (Wilkins et al., ISMIR 2018) is CC-BY-4.0, 8 GB total, 20
professional singers, organised by vowel and technique.

The repo carries a 22 MB curated subset under
[assets/vocalset/male1/](../../../assets/vocalset/male1/):

| Subfolder | Files | Purpose |
|---|---|---|
| `long_tones/straight/` | 5 (a/e/i/o/u) | Workhorse: held vowels, no vibrato (carrier adds vibrato downstream). |
| `long_tones/forte/` | 5 (a/e/i/o/u) | Louder/edgier alternative for character. |
| `scales/slow_piano/` | 10 (C+F keys × 5 vowels) | Slow ascending+descending scales — sliced for anchor-pitch grains. |

All files: 44.1 kHz mono int16 WAV (matches `.gspeak` format exactly).
Attribution and citation in [assets/vocalset/ATTRIBUTION.md](../../../assets/vocalset/ATTRIBUTION.md).

**Singer choice:** `male1` is the primary voice. `female2`, `male3`,
`male5`, `male10`, `female8` are documented in VocalSet's official
test-singers split as held-out, polished recordings — swap candidates
if `male1`'s timbre doesn't fit.

**Anchor pitches** (male1):
- Low anchor: ~G2 (98 Hz) — covers low-E2..B3 with ±a fourth of shift.
- Mid anchor: ~D3 (147 Hz) — covers G2..A4.
- High anchor: ~A3 (220 Hz) — covers D3..E5.

Three anchors with overlap blanket the guitar's full range while
keeping per-grain shift distance small enough that formants stay
believable (Arch B is more forgiving here than Arch A).

## 4. Bundle build process

A new offline script `tools/build_sung_vowel_bundle.py` (one-shot,
re-run only when source changes) produces
`assets/clips/gspeak/scene11_sung_vowels.gspeak`.

**M1 source inputs:** `long_tones/straight/*.wav` (5 files, ~3 s
usable region each) + `scales/slow_piano/*.wav` (10 files, sliced
into 3 anchor grains per vowel). `long_tones/forte/*.wav` is
committed to the repo but reserved for a future "loud personality"
variant — *not* included in the M1 bundle.

Steps:

1. For each long_tone WAV: trim leading/trailing silence to ≤200 ms,
   normalise peak to −3 dBFS, take the central ~3 s stable region.
   Output: one grain per vowel at the file's natural pitch.
2. For each scale WAV: detect note onsets, slice into per-note
   grains, keep three (low/mid/high anchor) per vowel, label each
   grain with its detected F0 in Hz. Output: 3 grains per vowel.
3. Concatenate all grains into one `audio.wav` with 200 ms of
   silence between (standard `.gspeak` layout — no format change).
   Final count: ~4 grains per vowel × 5 vowels = ~20 grains.
4. Emit `manifest.json` extending the existing schema with new
   optional fields per phoneme entry:

   ```json
   "phonemes": [
     {
       "startSample": 0,
       "endSample": 132300,
       "label": "ah",
       "anchorPitchHz": 147.0,
       "variant": "straight",
       "bankKey": "sung_ah"
     },
     ...
   ]
   ```

   `label`, `anchorPitchHz`, `variant`, `bankKey` are additive —
   existing `GspeakBundle::read` ignores unknown fields, so loading
   this manifest with the v2 reader is forward-compatible.

`bankKey` is the join field between the scene JSON's `tts.bank`
array and the manifest's grains. One bank key (e.g. `"sung_ah"`)
maps to **multiple grains** in the bundle — anchor selection
(closest detected pitch) happens inside `ClipBankPlayer` at trigger
time.

The bundle's expected size is ≤ 4 MB compressed.

## 5. Scene 11 — "Sung Vowels" (Arch B, vocoded)

### 5.1 JSON (assets/scenes/11_sung_vowels.json)

```json
{
  "id": 11,
  "name": "Sung Vowels",
  "color": "#c84cff",
  "mixer": { "masterGainDb": -3.0, "dryWet": 0.95, "transitionMs": 30 },
  "vocoder": { "enabled": true, "bypass": false },
  "tts": {
    "source": "clipBank",
    "bank": ["sung_ah", "sung_eh", "sung_ee", "sung_oh", "sung_oo"],
    "trigger": "note",
    "wordSync": "advance",
    "clarity": 0.0
  },
  "speech": {
    "player": "noteStepped",
    "maxSustainMs": 0,
    "attackInterruptPolicy": "interrupt"
  },
  "showVocoder": true, "showSay": false, "showWordReadout": false,
  "gspeakPath": "assets/clips/gspeak/scene11_sung_vowels.gspeak",
  "gspeakAutoLoad": true
}
```

### 5.2 New code

- **`ClipBankPlayer::selectByPitchAndKey(bankKey, detectedHz)`** —
  one new selection path, ~30–50 lines. On note attack: filter the
  active bundle's grains by `bankKey`, then pick the grain whose
  `anchorPitchHz` is closest to YIN's detected pitch. Falls back to
  round-robin (existing behavior) if anchors are missing or the
  bundle pre-dates the schema. Back-compat preserved for scenes 9
  and 10.
- **`SceneLibrary` JSON loader** — accept anchor-aware bundles
  transparently (no code change — fields already pass through
  `GspeakBundle::read`).
- **`GspeakBundle` manifest reader** — parse the new optional
  fields into the existing phoneme/syllable structs (additive
  fields only).

No DSP changes. No new audio routing. Carrier + vocoder already do
their job.

### 5.3 UI

Existing VocoderPanel applies as-is. No new panel needed for M1.

### 5.4 Success criteria

- Striking any note from E2..E5 plays back a sung vowel modulated to
  the played pitch within < 60 ms perceived latency (matches scene 10).
- Held notes sustain via `VowelGrainLoop` without audible loop seams.
- Vowel cycles through the bank on consecutive attacks (advance mode).
- Scene 11 ↔ scene 10 transitions are click-free at 30 ms dry/wet
  fade.

## 6. Scene 12 — "Sung Direct" (Arch A, formant-shifted)

### 6.1 Audio routing

A sibling wet bus to the vocoder path:

```
Guitar in ──┬─► YIN ─────────────────────────┐
            │                                ▼
            │                  ┌──► FormantShifter ──► VowelGrainLoop ──► Mixer wet
            │                  │       ▲
            └─► (dry path)     │       │ ratio = detectedHz / anchorHz
                               │
        Sung-vowel grain ──────┘
        (selected by pitch, same as scene 11)
```

Carousel and vocoder both bypass on scene 12 (the shifter occupies the
wet bus). Mutual-exclusion logic in `AudioGraph` follows the existing
carousel↔vocoder pattern.

### 6.2 JSON (assets/scenes/12_sung_direct.json)

```json
{
  "id": 12,
  "name": "Sung Direct",
  "color": "#ff5cc4",
  "mixer": { "masterGainDb": -3.0, "dryWet": 0.95, "transitionMs": 30 },
  "vocoder": { "enabled": false, "bypass": true },
  "tts": {
    "source": "clipBank",
    "bank": ["sung_ah", "sung_eh", "sung_ee", "sung_oh", "sung_oo"],
    "trigger": "note",
    "wordSync": "advance"
  },
  "directShift": {
    "enabled": true,
    "engine": "world",
    "formantPreserve": true,
    "formantTintSemitones": 0.0,
    "portamentoMs": 40.0,
    "scoopInMs": 0.0
  },
  "speech": {
    "player": "noteStepped",
    "maxSustainMs": 0,
    "attackInterruptPolicy": "interrupt"
  },
  "showVocoder": false, "showSay": false, "showWordReadout": false,
  "showSungDirectPanel": true,
  "gspeakPath": "assets/clips/gspeak/scene11_sung_vowels.gspeak",
  "gspeakAutoLoad": true
}
```

Same bundle as scene 11. Scene-12-only `directShift` block carries the
shifter's parameters.

### 6.3 New code

- **`audio/FormantShifter.{h,cpp}`** — wraps the chosen shifter engine
  behind a stable interface:

  ```cpp
  class FormantShifter {
  public:
      void prepare(double sampleRate, int blockSize);
      void reset();
      void setRatio(float r) noexcept;        // setpoint
      void setFormantTintSemitones(float n);
      void process(const float* in, float* out, int n);
      int  latencySamples() const noexcept;
  };
  ```

  The engine is selected at scene-config time so we can swap Rubber
  Band ↔ WORLD ↔ in-house without touching call sites.
- **`audio/SungDirectPath.{h,cpp}`** — new playback chain:
  ClipBankPlayer + FormantShifter + VowelGrainLoop, drained into the
  wet bus. Sibling of the existing vocoder path; selected by
  `Scene::tts.source == "clipBank"` plus `directShift.enabled`.
- **`AudioGraph` routing flag** — extend the carousel↔vocoder
  exclusion to a three-way: `{carousel, vocoder, sungDirect}`. At most
  one drives the wet bus.

### 6.4 Shifter engine choice

Tradeoffs:

| Option | License | Latency | Voice quality | Dev effort |
|---|---|---|---|---|
| **WORLD (Morise)** ← default | BSD-3 | ~20–30 ms | Excellent for voice (purpose-built) | ~3–5 days |
| Rubber Band v3 | GPLv2 / commercial | ~25–40 ms | Excellent | ~1 day (but blocks on license decision) |
| In-house TD-PSOLA + LPC | Free | ~5–10 ms | Good | ~1–2 weeks |
| SoundTouch | LGPL | ~10 ms | Mediocre on voice | ~0.5 day |

**Default decision: WORLD.** BSD-3 license — no purchase, no GPL
contamination of the AU plugin, no mid-implementation user-decision
gate. Designed specifically for singing voice. Latency budget fits
the < 70 ms target with margin.

Milestone 1.5's role is to **validate** that WORLD meets the bar
before committing scene 12 to it — not to re-litigate the engine
choice. The prototype harness measures latency + CPU + listens A/B
against in-house TD-PSOLA. WORLD is dropped only if it fails one of
the success criteria; in that case we escalate to user-decision for
Rubber Band commercial vs. accepting in-house TD-PSOLA's quality
floor.

### 6.5 UI — SungDirectPanel

New small JUCE panel, ~150 lines, modeled on VocoderPanel:

- **Formant tint** — semitones (−6..+6) to push formants up/down
  independently of pitch (lets the singer sound "smaller"/"larger").
- **Portamento** — ms (0..200) for pitch glide between consecutive notes.
- **Scoop-in** — ms (0..150) for an attack pitch slide up to target.
- **Grain selection** — read-only status: detected pitch, chosen
  anchor, vowel label.

Visible only on scene 12 (`showSungDirectPanel` JSON flag).

### 6.6 Success criteria

- Striking any note from E2..E5 plays back the singer's voice at that
  pitch within < 70 ms perceived latency (target — actual depends on
  M1.5 shifter choice).
- Sustained notes do not exhibit "chipmunk" or "ogre" formants
  outside ±P5 of an anchor.
- Held notes sustain via `VowelGrainLoop` without seam clicks; loop
  points computed in the shifted-output domain.
- Chord attacks (≥3 nearly-simultaneous note-ons) do not glitch:
  shifter retriggering follows a "last-detected-pitch wins" gate.
- Scene 12 ↔ scene 11 transitions are click-free at 30 ms.

## 7. Regression safety (don't break existing scenes)

This is a top-level constraint: every existing scene (0..10 + the
relocated 13 bypass) must continue to behave identically. Concrete
guarantees:

- **No changes to existing signal paths.** Scene 11's only audio-side
  addition is `ClipBankPlayer::selectByPitchAndKey()`, gated behind
  a manifest flag — round-robin remains the path for scenes 9 and 10
  whose bundles don't carry `anchorPitchHz`. Scene 12 introduces a
  new sibling wet bus selected by `directShift.enabled == true`;
  existing scenes have that field absent → false → unchanged
  routing.
- **Additive-only manifest schema.** `anchorPitchHz`, `label`,
  `variant`, `bankKey` are new optional fields. Existing `.gspeak`
  bundles (scene0, scene10) load unchanged. Reader ignores unknown
  fields today, so v3 manifests in v2 readers degrade cleanly.
- **No edits to existing scene JSONs except the bypass slot move.**
  Scenes 0..10 untouched. `11_bypass.json` is renamed to
  `13_bypass.json` with content identical apart from the `id` field
  (11 → 13).
- **No changes to existing public DSP class interfaces.**
  `VowelGrainLoop`, `PitchTrackedCarrier`, `ChannelVocoder`,
  `PitchShifter`, `Formant`, `GspeakBundle` keep their existing
  method signatures. New behavior arrives via new methods or new
  classes (`FormantShifter`, `SungDirectPath`).
- **Carousel/vocoder mutual-exclusion preserved.** The new three-way
  arbiter (`{carousel, vocoder, sungDirect}`) reduces to the
  existing two-way arbiter when `sungDirect.enabled == false`.
- **FCB1010 mapping back-compat.** Stock defaults are extended for
  slots 11/12/13; existing slots 0..10 keep their PC numbers.

The regression contract is enforced by tests (Section 8). If any
test in [tests/integration/](../../../tests/integration/) or [tests/unit/scenes/](../../../tests/unit/scenes/)
fails on `main` after this work, the change is rejected before
commit.

## 8. Testing strategy

Tests are written in lockstep with the implementation work in each
milestone — not as a follow-up phase. Each milestone's "done"
definition requires its new tests to pass and the full pre-existing
suite to still pass.

### 8.1 Unit tests — new files

| File | Milestone | Coverage |
|---|---|---|
| `tests/unit/audio/test_clip_bank_player.cpp` (extend) | M1 | `selectByPitchAndKey`: filters by `bankKey`, picks nearest `anchorPitchHz`; fallback to round-robin when anchors absent; deterministic on tie; out-of-range pitches clamp to nearest. |
| `tests/unit/audio/test_gspeak_bundle.cpp` (extend) | M1 | Round-trips new optional fields (`anchorPitchHz`, `label`, `variant`, `bankKey`); v2 bundle reads cleanly with v3 reader (back-compat); v3 bundle reads cleanly with old fields populated only. |
| `tests/unit/scenes/test_scene_library_sung_vowels.cpp` (new) | M1 | Loads `11_sung_vowels.json`; `directShift` absent → default-disabled; `gspeakAutoLoad == true`; vocoder enabled. |
| `tests/unit/scenes/test_scene_library_sung_direct.cpp` (new) | M2 | Loads `12_sung_direct.json`; `directShift.enabled == true`; engine/portamento/tint fields parsed; vocoder disabled. |
| `tests/unit/scenes/test_scene_library.cpp` (extend) | M1 | Bypass relocated from slot 11 → slot 13; slot 11 is now Sung Vowels. |
| `tests/unit/audio/test_formant_shifter.cpp` (new) | M2 | `prepare/reset` allocate-free; `setRatio` clamps to [0.25, 4.0]; `latencySamples()` returns finite int; processing silence yields silence after settling; ratio = 1.0 preserves input within < −60 dB error. |
| `tests/unit/audio/test_sung_direct_path.cpp` (new) | M2 | Wires ClipBank + FormantShifter + VowelGrainLoop; mutes when no note attack pending; ratio updates follow injected pitch envelope; no allocations in `process()`. |

### 8.2 Integration tests — new and extended

| File | Milestone | Coverage |
|---|---|---|
| `tests/integration/test_scene_switch.cpp` (extend) | M1 | Existing test extended to cycle PC 0..13. All scenes activate without throwing; mixer params update correctly. Catches accidental scene-ID gaps from the bypass move. |
| `tests/integration/test_sung_vowels_scene.cpp` (new) | M1 | Load scene 11 + its bundle; feed synthetic guitar (sustained sine at A2, then chord-attack burst); assert wet bus produces non-zero output of expected energy; assert no NaN/Inf; assert RT-safety sentinel. |
| `tests/integration/test_sung_direct_scene.cpp` (new) | M2 | Same shape as above for scene 12; additionally assert the formant-shifter ratio tracks YIN output within 2 cents over a 4-second envelope. |
| `tests/integration/test_realtime_safety.cpp` (extend) | M1, M2 | Add scenes 11 + 12 to the no-allocation-in-process audit. |
| `tests/integration/test_scene_hot_reload.cpp` (extend) | M1 | Add the new scene JSONs to the hot-reload sweep. |

### 8.3 Regression tests — explicit "we didn't break X" checks

| File | Milestone | Coverage |
|---|---|---|
| `tests/integration/test_vocal_guitar_clip_bank_scene.cpp` (no change, must still pass) | M1, M2 | Existing scenes that use ClipBankPlayer still trigger correctly with the new `selectByPitchAndKey` code present (gated off). |
| `tests/integration/test_speaking_scene.cpp` + `test_apple_speaking_scene.cpp` (no change) | M1, M2 | TTS pipeline still functions. |
| `tests/integration/test_carousel_scene.cpp` (no change) | M2 | Carousel still owns the wet bus when active; three-way arbiter doesn't steal it. |
| `tests/integration/test_mic_talkbox_scene.cpp` + `test_auto_vocal_formant_scene.cpp` (no change) | M2 | Vocoder paths unchanged. |
| `tests/unit/audio/test_v1_golden.cpp` + `test_pitch_shifter.cpp` (no change) | All | Existing DSP output is bit-for-bit unchanged. |

### 8.4 Test data

A synthetic mini-bundle at `tests/fixtures/sung_vowels_test.gspeak`
— 5 grains, all 1 second at 440 Hz, manifest with anchors at 110,
220, 440, 880, 1760 Hz. Used by unit tests so they don't depend on
the production bundle (which is built by an offline script).

### 8.5 Verification before claiming a milestone done

Per [verification-before-completion](../../../.claude/plugins/cache/claude-plugins-official/superpowers/6.0.2/skills/verification-before-completion/SKILL.md):

1. Full test suite passes from a clean build: `cd build-tests && ctest --output-on-failure`.
2. Standalone app launches and scene cycle PC 0..13 produces audible output on each scene that should be audible (M1 covers 0..11 + 13; M2 covers 12).
3. No new compiler warnings.
4. RT-safety sentinel reports zero allocations/locks during a 30-second guitar input simulation.

## 9. Milestone breakdown

### Milestone 1 — Scene 11 ships

1. **Tests first** — add `test_sung_vowels_test.gspeak` fixture +
   skeleton unit tests for `selectByPitchAndKey`,
   `GspeakBundle` schema additions, scene 11 JSON loader. These
   start as red.
2. Extend `GspeakBundle` manifest schema with `anchorPitchHz`,
   `label`, `variant`, `bankKey` (additive; back-compat). Tests
   for `test_gspeak_bundle.cpp` go green.
3. Add `ClipBankPlayer::selectByPitchAndKey()`. Tests for
   `test_clip_bank_player.cpp` go green.
4. Write `tools/build_sung_vowel_bundle.py` and produce
   `assets/clips/gspeak/scene11_sung_vowels.gspeak` from the
   curated `assets/vocalset/male1/` subset.
5. Author `assets/scenes/11_sung_vowels.json`. Tests for
   `test_scene_library_sung_vowels.cpp` go green.
6. Move `11_bypass.json` → `13_bypass.json` (rename + change `id`
   field 11 → 13). Update `test_scene_library.cpp` expectations.
7. Update FCB1010 stock defaults to map slot 11 → Sung Vowels,
   slot 13 → Bypass.
8. Add `test_sung_vowels_scene.cpp` integration test.
9. Extend `test_scene_switch.cpp` to cycle PC 0..13.
10. Extend `test_realtime_safety.cpp` to include scene 11.
11. **Verify:** full suite green, no new warnings, RT-safety
    sentinel passes for scene 11.

### Milestone 1.5 — Shifter validation

1. Vendor WORLD (Morise) into `external/world/` via CMake
   `FetchContent` or submodule.
2. Build a CLI harness `tools/shift_test.cpp` that loads a source
   grain + a ratio envelope and writes output WAV.
3. Run the harness on three source grains from the curated subset
   (low/mid/high anchors of `male1` `ah`), ratio sweeps from 0.5
   to 2.0.
4. Measure: latency added (samples + ms), CPU per 256-sample
   block, formant preservation by listening A/B against the
   anchor.
5. **Outcome A (expected):** WORLD passes — latency < 30 ms, CPU
   < 5% of one core at 48 kHz, voice character preserved. M2
   proceeds with WORLD.
6. **Outcome B (fallback):** WORLD fails some criterion — pause,
   summarize the data, ask user to choose between commercial
   Rubber Band purchase or in-house TD-PSOLA effort.

Outcome B is the only point in the three-milestone arc that
intentionally pauses for user input. Outcome A is the planned
path.

### Milestone 2 — Scene 12 ships

1. **Tests first** — add skeleton unit tests for `FormantShifter`,
   `SungDirectPath`, and scene 12 JSON loader. Start red.
2. Implement `audio/FormantShifter.{h,cpp}` wrapping WORLD (or
   M1.5's chosen engine). Tests for `test_formant_shifter.cpp`
   go green.
3. Implement `audio/SungDirectPath.{h,cpp}` (ClipBank + shifter +
   VowelGrainLoop). Tests for `test_sung_direct_path.cpp` go
   green.
4. Extend `AudioGraph` wet-bus routing to the three-way arbiter
   `{carousel, vocoder, sungDirect}` with the existing
   mutual-exclusion semantics. No existing routing changes.
5. Author `assets/scenes/12_sung_direct.json`. Tests for
   `test_scene_library_sung_direct.cpp` go green.
6. Implement `SungDirectPanel` JUCE component, gated by the
   `showSungDirectPanel` scene flag.
7. Add `test_sung_direct_scene.cpp` integration test.
8. Extend `test_realtime_safety.cpp` to include scene 12.
9. Update FCB1010 stock defaults to map slot 12 → Sung Direct.
10. **Verify:** full suite green (M1 tests still pass + new
    M2 tests pass), no new warnings, RT-safety sentinel passes
    for scene 12, chord-attack debounce gate verified by a
    chord-burst integration test.

## 10. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Rubber Band's added latency pushes scene 12 over a playable threshold | M1.5 prototype gates the choice; in-house TD-PSOLA is the low-latency fallback. |
| Chord-attack thrashing (6 retriggers in ms) glitches the shifter | "Last-detected-pitch wins" gate, debounce window ~20 ms. |
| Loop-seam clicks on shifted vowels | Compute `VowelGrainLoop` boundaries in shifted-output sample domain, not source. |
| Audience hears scene 11 as worse after scene 12 | Demo ordering: vocoded → direct, with verbal framing. |
| Licensing slip with Rubber Band in AU plugin | Decision committed in M1.5 — either pay for commercial license or drop to WORLD/in-house *before* writing call-site code. |
| FCB1010 footswitch mapping breaks on scene renumber | Single config edit, smoke-test on the FCB1010 before next show. |

## 11. Out of scope (for these milestones)

- DDSP / RAVE / RTNeural — neural timbre transfer. Documented in the
  prior chat thread as the long-term direction; explicitly *not* the
  next step.
- Multi-voice (male+female) bundles. Defer until M2 ships and male1
  is validated on stage.
- Consonant-onset bank (la/da/na syllables) — vowels-only for v1.
- Lyric-driven phoneme sequencing — keep this scene non-lyrical.
- Personal recordings — VocalSet is the source for v1; user
  recordings are a later asset swap.

## 12. References

- [VocalSet on Zenodo](https://zenodo.org/records/1442513)
- [VocalSet ISMIR 2018 paper (PDF)](http://ismir2018.ircam.fr/doc/pdfs/114_Paper.pdf)
- [docs/superpowers/specs/2026-06-17-gspeak-clip-bundle-design.md](2026-06-17-gspeak-clip-bundle-design.md) — `.gspeak` bundle format
- [docs/superpowers/specs/2026-06-13-vocal-guitar-clip-bank-design.md](2026-06-13-vocal-guitar-clip-bank-design.md) — ClipBankPlayer baseline
- [docs/superpowers/specs/2026-06-13-singing-polish-design.md](2026-06-13-singing-polish-design.md) — carrier vibrato + quantize
- [src/audio/VowelGrainLoop.h](../../../src/audio/VowelGrainLoop.h) — sustain loop primitive (reused by both scenes)
- [src/audio/PitchTrackedCarrier.h](../../../src/audio/PitchTrackedCarrier.h) — YIN + carrier (reused by scene 11)
- [src/audio/PitchShifter.cpp](../../../src/audio/PitchShifter.cpp) — current PSOLA (replaced for scene 12 by formant-preserving shifter)
- [src/audio/GspeakBundle.h](../../../src/audio/GspeakBundle.h) — bundle reader/writer (extended for `anchorPitchHz` field)
