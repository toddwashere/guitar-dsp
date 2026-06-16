# Talking guitar v2 — phoneme-aligned note-triggered speech (B1 → B2)

Status: design — not yet planned, not yet implemented.
Supersedes nothing. Preserves v1 (note-stepped speech, Phase 5a) as a live
demo exhibit.

## 1. Problem

Phase 5a's note-triggered speech is the demo's punchline, and it has two
recurring problems on stage:

- **Syllable boundaries are fictional.** `WordAligner` finds *word*
  boundaries by listening for silence gaps in the baked TTS clip (~30 ms
  envelope release, 15 % peak threshold) — that part is correct.
  Syllables are then proportionally split *within* each word:
  `start = wordStart + total*s/n` at
  [WordAligner.cpp:109](../../../src/audio/WordAligner.cpp). There is no
  phoneme awareness. "Au-to-mat-i-cal-ly" becomes six evenly-spaced slices
  regardless of where the /m/ or the /æ/ actually live. A pluck rarely
  lands on a real consonant attack, so the result feels mushy even when
  audiences cannot name why.
- **No sustain support.** Each pluck plays a fixed-length slice and stops.
  A held chord cannot ring the vowel out and cannot breathe — speech is
  grid-locked to the right hand even when phrasing is legato.

## 2. Goals

- Ship **Mode B1** as a new live demo scene: pluck triggers the next
  *real* syllable; sustain holds the syllable's vowel; release plays the
  consonant tail.
- Fix the Piper TTS source (it is currently broken — see §3) so that
  phoneme alignment has a working input.
- Preserve v1 bit-for-bit as the *"Speak v1 — Note-Stepped"* exhibit; the
  presentation narrative leans on the comparison.
- After B1 ships, follow up with **Mode B2** (phase-vocoder time-stretch
  on sustain) as a quality improvement on the same data model.

## 3. Non-goals

- Mode A (speech-leads / guitar-colors) — explored conceptually,
  deferred.
- Mode C (fused, source-filter, no baked clip) — see §10 for the future
  note. Not in this spec.
- **Mode B3** (concatenative phoneme synthesis with diphone DB) — see
  §10. Useful follow-on once B1+B2 are stable.
- Replacing Apple TTS. AVSpeechSynthesizer does not expose phoneme
  timings; Mode B scenes lock the TTS source to Piper. Other scenes keep
  using Apple/Prebaked.
- Streaming-during-synthesis (start playing chunk 1 while chunk 2 is
  still rendering). Sub-100 ms LLM-to-mouth response is not a v1-of-v2
  goal. Piper-on-M-series is ~150–400 ms per short phrase, which is
  imperceptible after a Say button press and hidden inside LLM response
  time on the conversation scene.
- FCB1010 mapping changes. v2 scenes ship at scene IDs 10, 11, … and are
  triggered from the UI only. The on-stage footswitch displacement
  question is deferred until B1 is proven and we re-evaluate which
  existing scenes to retire or move to `archive/`.

## 4. Piper fix (prerequisite)

The Piper TTS path is currently non-functional on a fresh clone:

- `PiperTTSSource::statusDetail()` at
  [PiperTTSSource.cpp:240–271](../../../src/audio/PiperTTSSource.cpp)
  requires three runtime dylibs next to the binary:
  `libespeak-ng.1.dylib`, `libpiper_phonemize.1.dylib`,
  `libonnxruntime.1.14.1.dylib`. `otool -L assets/piper/piper` confirms
  the binary linker-paths them via `@rpath`.
- The upstream macOS release tarball (`2023.11.14-2`) ships the binary
  *without* these dylibs. `scripts/fetch_piper.sh` faithfully copies
  what is in the tarball, producing an install whose `isReady()` returns
  false. Every Piper-source scene silently falls back to Apple or
  Prebaked; `TtsStatusBar` shows the "fell back" diagnostic.
- The code comment at
  [PiperTTSSource.cpp:232](../../../src/audio/PiperTTSSource.cpp)
  anticipated this exact failure mode.

A second issue, separate from the dylib gap: the Piper CLI binary
(`--output-raw`) emits raw PCM only — no phoneme timings. Mode B needs
them.

### 4.1 Resolution

Two coupled deliverables:

1. **Make Piper actually launch.** Add `scripts/build_piper.sh` that
   compiles Piper from source (Piper's upstream CMake + ONNX Runtime
   release lib + libespeak-ng). The script populates `assets/piper/`
   with binary + all required dylibs. The output is byte-for-byte
   reproducible given a pinned ONNX Runtime release and Piper commit
   SHA. `fetch_piper.sh` stays for the binary-tarball path but is
   updated to either (a) detect the missing dylibs and call
   `build_piper.sh` automatically, or (b) print a clear "run
   `build_piper.sh`" error. Decide during implementation; either is
   fine.
   - Fallback if source build proves too painful for the deadline:
     vendor the three dylibs into the repo (~25 MB total, within
     reason). `git-lfs` is overkill at that size.
2. **Get phoneme timings.** Two viable subpaths:
   - **a) Call `espeak-ng -q -X` directly.** The binary is already
     present at `assets/piper/espeak-ng`. The `-X` flag emits a phoneme
     stream with per-phoneme duration estimates. Phoneme labels here
     are the same labels Piper's duration predictor was trained on
     (Piper wraps libpiper_phonemize, which wraps espeak-ng's
     phonemizer), so relative timings line up well with Piper's
     synthesized audio. Linear-rescale espeak-ng's total predicted
     duration onto the actual Piper audio length. Cheapest, lowest
     risk. **Recommended.**
   - **b) Use `libpiper_phonemize` as a library.** Higher engineering
     cost (FFI + C++ binding), exposes Piper's *exact* internal phoneme
     sequence including diacritics. Defer to v2.1 unless (a) proves
     insufficient.

### 4.2 Acceptance criteria

- `PiperTTSSource::isReady()` returns true on a fresh clone after one
  setup-script run (`fetch_piper.sh` or `build_piper.sh`).
- `PiperTTSSource::synthesize("test phrase")` returns a non-null clip
  with non-empty samples.
- New helper `PhonemeExtractor::extract("test phrase")` returns a
  `vector<Phoneme>` (label, type, durationMs) sourced from espeak-ng
  `-X` output and rescaled to the Piper audio length within ±5 % per
  phoneme on validation corpus.

## 5. Phoneme alignment & syllabification

### 5.1 PhonemeAlignedClipBuilder

New class. Inputs: text, `ITTSSource*` (must be Piper-capable). Outputs:
`PhonemeAlignedClip`. Pipeline:

1. Call `tts.synthesize(text)` to get samples + duration.
2. Call `PhonemeExtractor::extract(text)` to get the phoneme sequence
   with predicted durations.
3. Linearly rescale predicted durations so their sum equals the audio
   length.
4. Run `Syllabifier::group(phonemes)` to bucket phonemes into syllables
   using the sonority sequencing principle: vowels are nuclei,
   consonants attach to the nearest nucleus by the maximum-onset
   principle. Edge cases (syllabic consonants, glottal stops) fall back
   to nearest-vowel grouping.
5. Compute per-syllable `vowelNucleusSample` — the midpoint sample of
   the highest-sonority phoneme in the syllable. This is the grain-loop
   anchor.

### 5.2 Data model

Extend `TTSClip` in [TTSClip.h](../../../src/audio/TTSClip.h):

```cpp
struct Phoneme {
    std::string label;        // ARPAbet or espeak label, e.g. "AE", "m"
    enum class Type { Vowel, Consonant, Silence } type;
    std::size_t startSample;
    std::size_t endSample;
};

struct SyllableSegment {
    std::string text;                 // grapheme form, e.g. "to" or "mat"
    std::size_t startSample;          // = first phoneme start
    std::size_t endSample;            // = last phoneme end
    std::size_t vowelNucleusSample;   // grain-loop anchor
    std::size_t attackEndSample;      // = vowelNucleusSample - small offset
    std::size_t codaStartSample;      // = end of last vowel phoneme
    std::vector<int> phonemeIndices;  // into TTSClip::phonemes
};

struct TTSClip {
    // ...existing fields...
    std::vector<Phoneme> phonemes;             // empty if not aligned
    std::vector<SyllableSegment> sylsV2;       // new; v1 'syllables' stays
};
```

`WordSegment` and the existing `syllables` array stay — v1 uses them.
The new `sylsV2` array is only populated by `PhonemeAlignedClipBuilder`.

### 5.3 Build-time vs run-time

The same builder runs at three different moments depending on text
source:

| Source | When | Latency |
|---|---|---|
| Demo clips (shipped) | Offline by a baking step in `tools/` | None |
| Say textbox | On-demand on the Say button | ~150–400 ms (imperceptible) |
| LLM / conversation | On-demand per streamed sentence | Hidden inside LLM latency |

The on-demand build must run on a worker thread (it shells out to Piper
+ espeak-ng). The audio thread sees an atomically-swapped
`TTSClipPtr` exactly as today.

## 6. Player

### 6.1 PhonemeSteppedTTSPlayer

New class. Parallel to `NoteSteppedTTSPlayer` —
v1's player is untouched.

Per-syllable state machine:

```
                   onset & !playing
                ┌────────────────────┐
                │                    ▼
            ┌─────┐  vowelAttackHit  ┌────────┐  release|onset|maxHold ┌──────┐
            │Idle │                  │Sustain │                        │ Coda │
            └─────┘  ◄───────────    └────────┘   ──────────────────►  └──────┘
                ▲     codaPlayedOut                                        │
                └───────────────────────────────────────────────────────────┘
```

- **Attack** (`Idle → Sustain` precursor): on onset, advance to next
  syllable; play from `startSample` toward `vowelNucleusSample` at unity
  rate.
- **Sustain**: at `vowelNucleusSample`, enter grain-loop. Looped grains
  are 20–40 ms pitch-synchronous windows from the steady-state vowel,
  crossfaded ~5 ms.
- **Coda**: triggered by (a) note release, (b) next onset, or
  (c) `maxSustainMs` timeout (config; default 1500 ms). Play from
  `codaStartSample` to `endSample` at unity rate, then return to Idle.

### 6.2 Edge cases & policy

- **Fricative-nuclei syllables** (rare; e.g. a syllabic /s/) — skip
  sustain, play through end at unity rate. Marked at build time.
- **Onset arrives mid-Coda.** The Coda finishes (≤80 ms), then the next
  syllable starts. Audible "snap" is preferable to mid-coda truncation.
- **Onset arrives during Attack** (rare — onsets fast enough that we're
  still ramping the consonant). Two policies; pick at impl time and
  expose as a toggle: (1) finish the current syllable to vowel attack,
  then advance; (2) interrupt and start the next. Default: finish.
- **End-of-clip.** Same loop/stop policy as v1 (`setLoop(bool)` from
  commit `85ba01b`).

### 6.3 Grain-loop DSP

Reuse pitch tracking infrastructure already present for pitched-carrier
mode (`pitch-singing` work, commit `5e7a23c`). Pitch period at
`vowelNucleusSample` defines grain length. Crossfade overlap-add. Held
sustains > 500 ms add slow random walk to the grain pointer (~±5 % of
loop length, smoothed) to break up the perceived "robotic loop". No
formant shifting; this is just looped playback.

## 7. Scene & UI

### 7.1 New scene: Speak v2 — Guitar-Lead (scene id 10)

JSON at `assets/scenes/10_speak_v2_guitar_lead.json`. Notable fields:

- `tts.source = "piper"` (no fallback to apple — phoneme timings
  require Piper)
- `tts.fallback = "prebaked"` (graceful degradation if Piper still
  broken at app start)
- `speech.player = "phonemeStepped"` (selects new player)
- `speech.maxSustainMs = 1500`
- `speech.attackInterruptPolicy = "finish"`

### 7.2 UI changes

- **`SceneIndicator`** ([SceneIndicator.cpp:25, 56–58](../../../src/app/SceneIndicator.cpp))
  currently hard-codes a 10-slot strip. Generalize to size dynamically
  to the number of loaded scenes, capped at a reasonable max (say 16)
  with horizontal scroll beyond that. v2 scenes (ids 10+) are visible
  and clickable but not FCB-mapped.
- **`WordReadout`** highlights the currently-active syllable within the
  current word (visual cue for the operator). v1 scenes keep the
  existing word-only readout.
- **`DiagToggleBar`** gains a `Ph` pill that lights when the
  phoneme-stepped player is active. Mirrors the existing pill style.
- **`TtsStatusBar`** already shows Piper readiness; surfaces the dylib
  gap clearly. No code change needed beyond a one-line tweak to the
  error string if helpful.

### 7.3 v1 scenes — untouched

Scenes 01 ("multi syllable words") and 02 ("Text to Speech") keep their
current behavior bit-for-bit. No changes to their JSON, their player
wiring, or `NoteSteppedTTSPlayer`.

## 8. v1 preservation

- `NoteSteppedTTSPlayer`, `WordAligner` (including the proportional
  syllable split at line 109), `WordSyncSelector`, and the v1 player's
  wiring are **not modified**.
- Add a **golden-audio smoke test** under `tests/golden/v1_speech/`:
  fixed input clip + canned onset sequence → byte-equal sample output
  diffed against a committed reference. CI fails on drift. This catches
  any accidental change in v1 behavior during the v2 build.
- A pre-recorded backup audio/video of v1's quirks lives in
  `docs/presentation/media/` for use if live demo conditions hide the
  problem.

## 9. B2 plan (follow-on, scoped here, not implemented in this spec)

Once B1 ships and is stable:

- Define `IVowelStretcher` interface. Single method:
  `process(grainSourceSamples, holdDurationSamples) -> outputSamples`.
- Implement `GrainLoopStretcher` (B1's current logic, refactored behind
  the interface).
- Implement `PhaseVocoderStretcher`: STFT (1024-pt, 75 % overlap),
  phase-locked spectral lines, time-stretch on the steady-state vowel
  region.
- Scene config selects: `speech.stretcher = "grainLoop" | "phaseVocoder"`.
- Acceptance: A/B blind test on a corpus of held sustains (>500 ms)
  prefers phase-vocoder ≥70 % of the time, with measured latency ≤
  +50 ms vs grain loop.

## 10. B3 (future, not in this spec)

Once both B1 and B2 are shipped and the live demo is solid, the next
natural direction is **Mode B3 — concatenative phoneme synthesis** (no
baked clip). Sketched design:

- Offline-render a CV/CVC diphone bank from Piper for a single voice.
- Run-time: tokenize input text to a phoneme stream (espeak-ng `-X`).
- Pluck triggers the next phoneme's attack from the bank; sustain holds
  the vowel; release fires the coda.
- Prosody comes from playing dynamics rather than from Piper's duration
  model.
- Distinct scene "Speak v3 — Fused" (or whatever the project naming
  settles on by then).

Not started until B1 and B2 ship. Listed here only so the v2 design
leaves natural extension points: the `PhonemeAlignedClip` data model
already names phonemes and labels their type, which is the input the
diphone-bank path would need.

## 11. Risks & open questions

- **espeak-ng duration accuracy.** Predicted phoneme durations may
  drift > ±10 % on long phrases. Mitigation: per-phrase linear rescale
  (already planned) covers the average; energy-peak-snap on consonant
  onsets is a v2.1 polish if needed.
- **Grain-loop artifacts on fricatives.** Sibilants (s, sh, f) sound
  bad looped. Mitigation: detect vowel-nucleus phoneme type at build
  time; skip Sustain when nucleus is a fricative (rare). Document in
  player code.
- **Piper-from-source build friction.** Adds dev-env setup cost on
  first clone. Mitigation: cache the built artifacts under
  `assets/piper/`, .gitignored; README explicit; build script verified
  on a clean macOS install before merge.
- **AU plugin path.** Phoneme extraction shells out to `piper` and
  `espeak-ng` binaries — both subprocess calls must run on a worker
  thread and never block the audio thread. Already true for the current
  Piper synthesis path; same pattern carries over.
- **What if Piper's duration predictor differs from espeak-ng's?**
  Linear rescaling will be off in the *distribution* across phonemes,
  even if total length matches. If audible, fall back to a forced
  alignment pass (energy correlation against per-phoneme target
  envelopes). Plan B, not v1 of v2.

## 12. Test plan

### Unit

- `PhonemeExtractor::extract` — text fixtures with expected
  phoneme labels and approximate durations. ±15 % tolerance per
  phoneme.
- `Syllabifier::group` — text fixtures with expected syllable bounds
  (sonority-peak grouping). Includes "automatically", "strawberry",
  "rhythm" (syllabic consonant), "the" (schwa nucleus).
- `PhonemeSteppedTTSPlayer` state machine — synthetic onset/release
  sequences, assert sample output matches expected
  attack/sustain/coda concatenation. RT-safety: no allocation,
  no lock on the `process()` path.

### Golden

- `tests/golden/v1_speech/` — fixed clip + onset sequence, byte-equal
  reference diff. Drift-detector for v1 preservation.
- `tests/golden/v2_speech/` — same idea for v2 (regression catcher
  once B1 lands; not required before merge).

### Integration

- Say-textbox round-trip on the v2 scene: type → Say → produce
  `PhonemeAlignedClip` → play through `PhonemeSteppedTTSPlayer` with
  scripted onsets → assert syllables advance correctly.
- LLM/conversation round-trip: mocked LLM reply → produce clip →
  play. Confirms the on-demand build path works without blocking the
  audio thread.

### Manual

- Full demo sentence ("While my guitar gently weeps") played:
  - One pluck per syllable (textbook case).
  - Fast strum (more onsets than syllables): syllables advance,
    don't gap or stack.
  - Sustained chord on a single syllable: vowel holds cleanly for
    1+ seconds without obvious robot-loop.
  - Released mid-vowel: clean coda playback.

## 13. Appendix — referenced files

- v1 player: [src/audio/NoteSteppedTTSPlayer.cpp](../../../src/audio/NoteSteppedTTSPlayer.cpp)
- v1 aligner: [src/audio/WordAligner.cpp](../../../src/audio/WordAligner.cpp)
- v1 clip type: [src/audio/TTSClip.h](../../../src/audio/TTSClip.h)
- Piper source: [src/audio/PiperTTSSource.cpp](../../../src/audio/PiperTTSSource.cpp)
- Piper fetch script: [scripts/fetch_piper.sh](../../../scripts/fetch_piper.sh)
- Scene loader: [src/scenes/SceneLibrary.cpp](../../../src/scenes/SceneLibrary.cpp)
- Scene UI strip: [src/app/SceneIndicator.cpp](../../../src/app/SceneIndicator.cpp)
- FCB mapping: [assets/midi/fcb1010.json](../../../assets/midi/fcb1010.json)
- Prior word-sync spec: [docs/superpowers/specs/2026-06-13-word-sync-modes-design.md](2026-06-13-word-sync-modes-design.md)
- Note-triggered speech spec: [docs/superpowers/specs/2026-05-31-note-triggered-speech-design.md](2026-05-31-note-triggered-speech-design.md)
- Presentation notes (v2 section): [docs/presentation/while-my-guitar-gently-speaks.md](../../presentation/while-my-guitar-gently-speaks.md) §10
