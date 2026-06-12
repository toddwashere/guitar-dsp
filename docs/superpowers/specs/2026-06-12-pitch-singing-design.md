# Pitch-Tracked Singing Carrier — Design

**Status:** Approved design (brainstorming) — 2026-06-12

**Goal:** Add a toggleable **pitched** carrier to the vocoder so the spoken/AI
voice **sings in the guitar's note**. When the toggle is off, the app behaves
exactly as today (broadband-noise floor in the carrier). When on, a YIN-based
pitch detector tracks the guitar's fundamental and feeds a PolyBLEP sawtooth
oscillator into the carrier path in place of the noise floor — so vowels and
sustained phonemes ring at the pitch of whatever you just played.

**Why now:** the existing broadband-noise floor solves intelligibility but
sounds atonal; a pitched-but-harmonic carrier is both intelligible *and* in
tune. The AU spec
([docs/superpowers/specs/2026-06-12-au-plugin-design.md](2026-06-12-au-plugin-design.md))
§9 documents this as a future north-star; this spec turns it into a build.

**Standing design principle — visibility & tweakability.** The toggle ships
with a live **pitch readout** (note name + cents + Hz) that runs whether the
toggle is on or off, so the audience can see "the app already knows what note
I played; flipping this switch makes it *use* that note." Continues the
pattern of `DiagToggleBar` and `VocoderPanel`.

---

## 1. Demo arc this enables

The live conference-demo flow is the design's pacing target:

1. **"Without"** — play a phrase; the speaking voice rides on the existing
   guitar+noise carrier (current behavior). Intelligible but tonally
   disconnected from your playing.
2. **Flip the toggle** — visible UI button lights; FCB switch confirms.
3. **"With"** — replay the same phrase; the voice now **sings the note you
   just played**, and continues singing the last note while the TTS finishes
   the sentence even after you release the string.

The flip must be hands-free (FCB) and audience-visible (UI button + pitch
readout).

---

## 2. Approach: YIN detector + PolyBLEP sawtooth

**Why YIN (autocorrelation-based pitch detection):**

- **Most accurate on plucked strings.** YIN's cumulative-mean-normalized
  difference function handles the case where the fundamental is weaker than
  the 2nd/3rd harmonic — common on guitar low E (~82 Hz). FFT/HPS methods
  octave-error there without explicit hardening.
- **Lowest practical latency.** 2048-sample window + 256-sample hop gives a
  usable F0 in ~30-50 ms. FFT methods need ~90 ms windows just for low-end
  bin resolution.
- **Cheap.** O(N) per hop with the standard difference-function trick.
  Comfortably <0.1 ms per frame on this hardware.
- **No dependency.** ~150 LOC, fits the project's "small focused C++"
  philosophy. (External libs like aubio use YIN under the hood; wrapping it
  in a dependency buys nothing.)

**Why a PolyBLEP sawtooth carrier:**

- Sawtooth is harmonic-rich — every formant band in the vocoder has energy
  to shape, which is the whole point of a carrier.
- PolyBLEP anti-aliasing is ~30 LOC, allocation-free, sounds clean across the
  guitar's range without per-sample FFT.
- Matches what the AU spec hint described.

**Scope:** **monophonic.** Track the dominant fundamental — chords pick the
lowest stable partial. This is sufficient for the demo arc; polyphonic
tracking is a separate, much larger build.

---

## 3. Architecture

One new audio-thread module, slotted into the existing carrier-selection
point in [src/audio/AudioGraph.cpp](../../src/audio/AudioGraph.cpp). No
changes to `ChannelVocoder`.

```
src/audio/PitchTrackedCarrier.{h,cpp}    NEW (~250 LOC)
  ├─ YIN detector (private)               F0 + voiced flag, 256-sample hop
  ├─ PolyBLEP sawtooth oscillator         pitched carrier signal
  └─ Hold/decay state machine             holds last F0 ~1s, then fades

src/audio/AudioGraph.{h,cpp}              EDIT
  ├─ owns one PitchTrackedCarrier
  ├─ new atomic<bool>  pitchSinging_      message-thread setter, audio-thread read
  ├─ new atomic<int>   detectedNoteMidi_  audio-thread write, UI read (-1=unvoiced)
  ├─ new atomic<float> detectedCents_     fine offset, audio→UI
  └─ new atomic<float> detectedHz_        Hz, audio→UI

src/midi/FCB1010Mapping.{h,cpp}           EDIT
  └─ new pitchSingingToggleCc_ (default CC#80, latch ≥64)
src/midi/SceneCommand.h                   EDIT
  └─ new SceneCommandType::TogglePitchSinging

src/app/PluginProcessor.{h,cpp}           EDIT
  └─ handle TogglePitchSinging via existing pending-flag + callAsync pattern

src/app/PluginState.{h,cpp}               EDIT
  └─ +1 bool field `pitchSinging` in save/restore

src/app/DiagToggleBar.{h,cpp}             EDIT
  └─ new "Pitch Sing" toggle button (groups with diag toggles visually)

src/app/NoteReadout.{h,cpp}               NEW (~80 LOC)
  └─ small JUCE component, 30 Hz timer, polls AudioGraph atomics

src/app/VocoderPanel.{h,cpp}              EDIT
  └─ host the NoteReadout child
```

**Module boundaries.**

- `PitchTrackedCarrier` is self-contained: input = guitar buffer + numSamples;
  output = pitched carrier buffer + a published `(midiNote, cents, hz, voiced)`
  state struct. Knows nothing about scenes, MIDI, or UI.
- All atomics live on `AudioGraph` (consistent with `clarity_`,
  `diagNoiseCarrier_`, etc.). UI reads atomics; UI does not reach into
  `PitchTrackedCarrier`.
- `FCB1010Mapping` keeps owning the CC→command translation. The new
  `TogglePitchSinging` flows through the existing `SceneCommand` path.

---

## 4. Components in detail

### 4.1 `PitchTrackedCarrier`

```cpp
class PitchTrackedCarrier {
public:
    void prepare(double sampleRate, int blockSize);
    void reset();

    struct State {
        float freqHz;     // 0 when unvoiced AND hold expired
        int   midiNote;   // -1 when unvoiced AND hold expired
        float cents;      // fine offset from midiNote, [-50, +50]
        bool  voiced;     // current detection (not hold state)
    };

    // Writes pitched carrier into `out`. Returns latest published state.
    State process(const float* guitarIn, float* out, std::size_t numSamples);

private:
    // YIN: 2048-sample window, 256-sample hop. Ring buffer fed sample-by-sample.
    // CMNDF + parabolic interpolation. Confidence threshold 0.15.
    // PolyBLEP saw: phase accumulator, ±1-sample BLEP at wraparound.
    // Hold-last-pitch: voiced=false → keep emitting last F0 for holdMs (default
    // ~1000 ms), then fade amplitude linearly over decayMs (default ~200 ms).
};
```

All buffers sized at `prepare`; `process` is allocation- and lock-free.

### 4.2 Mixing model

When pitch-singing is ON, the existing `carrierNoise` mix knob (`ChannelVocoder`'s
broadband-noise mix) is **bypassed**: AudioGraph pre-mixes the carrier as
`guitar + carrierNoise * pitched_saw`, passes that buffer to the vocoder with
`vocoder_.setCarrierNoise(0)` for the duration the toggle is on. When the
toggle is off, the knob recovers its original meaning. The knob's label in
`VocoderPanel` updates dynamically: **"Noise floor"** ↔ **"Pitched floor"**.

This keeps a single user-facing dial for "how much carrier floor" rather than
adding a third knob, and means the existing tuning (default 0.30) carries
over.

### 4.3 Toggle plumbing

Same shape as the existing `diagNoiseCarrier_` toggle:
- `AudioGraph::setPitchSinging(bool)` — message-thread setter.
- `AudioGraph::pitchSinging() const` — UI/audio reader.
- `AudioGraph::process` reads it once per block to choose carrier source.
- `PitchTrackedCarrier::process` runs **always** (so the pitch readout is
  live even when the toggle is off), but its output buffer is only routed
  into the carrier when the toggle is on.

### 4.4 FCB MIDI binding

Add `pitchSingingToggleCc_` to `FCB1010Mapping`, default **CC#80** (general
purpose controller, unused by stock FCB programming). Latch semantics: any
CC#80 message with value ≥ 64 emits one `TogglePitchSinging` command. The
FCB1010 doesn't send CC#80 by default — the user reprograms one switch (their
existing FCB workflow). Documented in the README; rebinding UI is out of
scope (see §8).

`PluginProcessor::processBlock` sets `pendingPitchSingingToggle_` (atomic
bool) on receipt; `callAsync` flips `AudioGraph::pitchSinging_` on the
message thread. RT-safe.

### 4.5 UI

- **`DiagToggleBar`** gains a 4th button labeled **"Pitch Sing"**. Lights when
  active. Visually groups with the three diagnostic toggles so the audience
  reads it as a runtime mode flag, not a scene attribute.
- **`NoteReadout`** (new) is hosted inside `VocoderPanel`. A 30 Hz timer
  polls `AudioGraph::detectedNoteMidi_` / `detectedCents_` / `detectedHz_`
  and renders:
  - Big: note name + octave (`"A2"`, `"C#4"`)
  - Small below: cents offset (`"+12¢"`, `"−7¢"`)
  - Small below: Hz (`"110.0 Hz"`)
  - Dim the whole readout when `midiNote == -1` (unvoiced AND hold expired).
- **VocoderPanel** label for the "carrier-noise" slider switches between
  `"Noise floor"` and `"Pitched floor"` based on `pitchSinging_`.

### 4.6 State persistence

`PluginState` grows one bool field, `pitchSinging`, saved in
`getStateInformation` and restored in `setStateInformation` alongside the
existing scene id + 3 vocoder knobs. Default `false` — demos start "without."

---

## 5. Testing

### 5.1 Unit tests

`tests/audio/PitchTrackedCarrierTests.cpp` (new):

- **Sine sweep 80–800 Hz.** Detected F0 within ±10¢ once converged (allow
  one hop for warm-up after each frequency change).
- **Recorded low-E guitar fixture.** Correct fundamental, no octave error.
  (Add a short pluck WAV to `tests/fixtures/audio/` if none exists.)
- **Silence → unvoiced.** With pure-zero input, voiced=false within one
  window after warm-up.
- **Re-lock.** After voiced→unvoiced→voiced, F0 re-locks within 2 hops.
- **Hold-last-pitch.** After a voiced→unvoiced transition, the saw amplitude
  stays at full level for `holdMs ± 1 block`, then crosses below 1% of full
  level by `holdMs + decayMs + 1 block`.
- **PolyBLEP anti-aliasing.** At F0=1000 Hz, spectral analysis of the saw
  output shows no aliased peaks ≥ −60 dB above Nyquist/2.

`tests/audio/AudioGraphPitchSingingTests.cpp` (new):

- **Toggle off → bit-identical regression.** With the toggle off, sample-equal
  output to the pre-change AudioGraph for a representative input. Pins
  default behavior.
- **Toggle on → spectral tracking.** With a pitched guitar input + a steady
  TTS modulator, wet path's dominant spectral peak tracks the input's F0
  within ±20¢.

### 5.2 Plugin/host gates

All must remain green:
- All existing tests (currently 157/157).
- `auval -v aumf GtSp TdBF` → AU VALIDATION SUCCEEDED.
- `pluginval` strictness-10 in-process tests (state save/restore, thread
  safety, no crash).
- Manual: Logic Pro session — flip toggle via FCB CC#80, confirm audible
  pitch-singing, confirm state persists across project save/reload.

---

## 6. Performance budget

- YIN @ 2048-window / 256-hop @ 44.1 kHz: ~170 hops/sec, well under 0.1 ms
  per hop measured = <2% of one core's audio-thread budget for this stage.
- PolyBLEP saw: ~10 ops/sample, negligible.
- Total addition to `AudioGraph::process` worst case: <3% of audio-thread
  budget at typical buffer sizes (256–512 samples).

No `prepare`-time allocation in `process`. Ring buffer + scratch sized once.

---

## 7. Failure modes and graceful degradation

- **Unpitched / noisy input.** Confidence threshold drops voiced flag;
  hold-decay takes over; readout dims after holdMs+decayMs.
- **Sub-bass / out-of-range input.** Clamp detected F0 to [40 Hz, 2 kHz];
  outside, treat as unvoiced.
- **Toggle flipped mid-phrase.** Carrier source swap is per-block; no
  zipper noise expected (the saw's amplitude envelope smooths
  attack/release). Validated in the AudioGraph toggle test.
- **State restore with `pitchSinging=true`.** UI toggle button reflects
  state; readout starts producing pitch within one block.

---

## 8. Out of scope (this spec)

- **Polyphony.** Mono only. Future iteration if demos demand it.
- **Per-scene defaults.** Toggle is a global runtime flag, not a scene
  attribute. A future spec could make it a per-scene field.
- **MIDI CC rebinding UI.** CC#80 is the documented default; users reprogram
  the FCB switch. A general "MIDI learn" / rebinding UI is its own future
  work.
- **Pitch quantization to a scale.** Saw plays raw detected pitch. No
  "snap to nearest semitone" yet.
- **Replacing the broadband-noise floor permanently.** Toggle off still uses
  today's noise behavior — preserves the "demo without" half of the arc.
- **Carousel scenes (piano/harmony).** They already drive pitch themselves;
  the pitch-singing toggle only affects the vocoder path.
- **Pitch-following on the modulator (TTS) side.** Pitch is detected from
  the guitar only. Resynthesizing TTS pitch would be a separate vocoder
  redesign.

---

## 9. File-touch summary

**New:**
- `src/audio/PitchTrackedCarrier.{h,cpp}` — YIN + PolyBLEP + hold/decay.
- `src/app/NoteReadout.{h,cpp}` — JUCE pitch readout component.
- `tests/audio/PitchTrackedCarrierTests.cpp` — detector + saw unit tests.
- `tests/audio/AudioGraphPitchSingingTests.cpp` — toggle + regression tests.
- `tests/fixtures/audio/lowE_pluck.wav` (if no comparable fixture exists).

**Edited:**
- `src/audio/AudioGraph.{h,cpp}` — own `PitchTrackedCarrier`, new atomics,
  carrier-source swap in `process`.
- `src/midi/SceneCommand.h` — `TogglePitchSinging` enum case.
- `src/midi/FCB1010Mapping.{h,cpp}` — `pitchSingingToggleCc_`, stockDefaults
  CC#80, JSON load/save, `translate()` adds the latch.
- `src/app/PluginProcessor.{h,cpp}` — pending-toggle + callAsync, route
  `TogglePitchSinging` to `AudioGraph::setPitchSinging`.
- `src/app/PluginState.{h,cpp}` — `pitchSinging` bool field.
- `src/app/DiagToggleBar.{h,cpp}` — 4th toggle button.
- `src/app/VocoderPanel.{h,cpp}` — host `NoteReadout`, swap carrier-noise
  slider label.
- `src/app/CMakeLists.txt` — add the two new sources.
- `tests/CMakeLists.txt` — add the two new test files.
- `README.md` — document CC#80 default + FCB programming note.

---

## 10. Verification before "done"

- All existing tests pass + the two new test files pass.
- `auval -v aumf GtSp TdBF` succeeds after rebuild.
- `pluginval` strictness-10 (in-process tests) succeeds.
- Manual gate (the actual goal): in Logic Pro, the demo arc above is
  audibly correct — same phrase sounds atonal with toggle off, sings the
  played note with toggle on, holds the last note while TTS finishes.
