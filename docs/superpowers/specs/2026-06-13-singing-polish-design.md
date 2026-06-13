# Pitch-Singing Polish + Singing Mode — Design

**Status:** Approved direction (brainstorming) — 2026-06-13

**Goal:** Two complementary improvements to the pitch-singing carrier shipped
at `0bea39d`:

1. **Polish** — make the pitched-saw carrier sound *less scratchy* so
   intelligibility improves. Three small tuning changes.
2. **Singing mode** — a new optional layer on top of pitch-singing that
   pushes the perceived effect from "spoken-at-a-pitch" toward
   "sung". Adds vibrato + scale-quantize, toggleable independently of
   `pitchSinging`.

**Why now:** live testing of the pitch-singing scene revealed (a) the raw
sawtooth carrier is bright and rough, hurting intelligibility, and (b) the
demo would land harder if the voice noticeably *sings* — pitch-stable,
vibrato'd, in tune — rather than just speaking on a pitch.

**Out of scope (separate spec):**
- Word-sync modes for note-triggered TTS scenes — see
  [2026-06-13-word-sync-modes-design.md](2026-06-13-word-sync-modes-design.md).
- Carrier-noise save/restore race in AudioGraph (tracked as
  `task_ef36dbbd`).

---

## 1. Polish — three tuning changes

Each is small, audibly tested by ear, controlled by a defaulted constant
or live-tunable knob.

### 1.1 Lowpass-filter the pitched sawtooth at ~2 kHz

A raw sawtooth has flat -6 dB/octave content with energy out to Nyquist —
the vocoder's high bands then pass that buzz straight to the listener.
Roll off the saw before mixing into the carrier with a 1-pole IIR
lowpass at ~2 kHz. Speech intelligibility lives below ~3 kHz; the
high-band sibilance you actually want comes from the vocoder's existing
sibilance/noise generator (independent of carrier choice).

- **Where:** inside `PitchTrackedCarrier::nextSawSample(float freqHz)`,
  apply a 1-pole LPF on the post-BLEP output before returning.
- **Cutoff:** default 2000 Hz. Make it a tunable: `setSawLowpassHz(float)`.
- **Filter:** `y[n] = y[n-1] + α (x[n] - y[n-1])` with
  `α = 1 - exp(-2π · fc / sampleRate)`.

### 1.2 Drop default hold from 1000 ms → 250 ms

The 1-second hold causes the saw to drone at the old pitch across word
boundaries during TTS sentences (audible as "the carrier doesn't catch
up to the next phrase"). 250 ms preserves the "voice keeps singing the
chord you played" feel without the drone.

- **Where:** `PitchTrackedCarrier`'s `holdMs_` default value.
- **No new tests** — existing hold/decay tests use `setHoldMs(N)`
  explicitly; they continue to pass.

### 1.3 Slower vocoder envelope follower (15 ms → 25 ms)

`ChannelVocoder` uses a 1-pole envelope follower with ~15 ms time
constant per band. Bumping to ~25 ms softens consonant edges and
sustains vowel energy slightly — more "sung," less "robotic," and easier
on the ear with the new pitched carrier.

- **Where:** `ChannelVocoder::prepare` (constant ~`0.015` → `~0.025`).
- **Risk:** consonant clarity could degrade. Mitigation: the change is
  small (10 ms) and is well within the natural range for "soft" vocoders.
  If demos sound worse, revert to 15 ms.

---

## 2. Singing mode — vibrato + pitch quantize

A new **Singing** toggle, independent of `pitchSinging`, that layers two
modulations on the detected F0 before the saw oscillator consumes it:

1. **Vibrato LFO** — sine wave at default 5 Hz, ±20 cents depth.
2. **Pitch quantize** — snap the (post-detected, pre-vibrato) F0 to the
   nearest chromatic semitone.

Order: quantize first (gives a stable scale-tone), THEN vibrato (adds
expressive wobble *around* the quantized tone).

**Toggle independence:** `singing` can be ON whether `pitchSinging` is ON
or OFF. When `pitchSinging` is OFF, `singing` has no audible effect (the
saw isn't routed). When both are ON, you get the sung effect. The
toggle is wired this way (rather than gated under `pitchSinging`) so a
future scene that uses the pitched carrier in a different routing
(carousel patches, for example) inherits singing-mode automatically.

### 2.1 API

`PitchTrackedCarrier`:

```cpp
void setSinging(bool on) noexcept;
bool singing() const noexcept;

void setVibratoHz(float hz) noexcept;       // default 5.0
void setVibratoCents(float depth) noexcept;  // default 20.0
void setPitchQuantize(bool on) noexcept;     // default true; falls under setSinging
```

The two sub-knobs (vibrato rate, vibrato depth, quantize on/off) start
with the recommended defaults; the only message-thread surface the UI
must wire is `setSinging(bool)`. The sub-knobs are exposed so power
users can tune later if needed (no UI sliders in v1).

### 2.2 Vibrato implementation

```cpp
// Per-sample (only when singing_ is true and a non-zero frequency is
// emitting):
vibratoPhase_ += 2.0 * M_PI * vibratoHz_ / sampleRate_;
if (vibratoPhase_ >= 2.0 * M_PI) vibratoPhase_ -= 2.0 * M_PI;
const float vibratoOffset = vibratoCents_ * std::sin(vibratoPhase_);
const float modulatedHz = freq * std::pow(2.0f, vibratoOffset / 1200.0f);
out[i] = amp * nextSawSample(modulatedHz);
```

`vibratoPhase_` is a new private member. Allocation-free.

### 2.3 Pitch quantize implementation

Applied in `process()` between the YIN result and the cached
`currentFreqHz_`:

```cpp
if (singing_ && pitchQuantize_) {
    const float midi = 69.0f + 12.0f * std::log2(f0 / 440.0f);
    const float quantized = std::round(midi);
    f0 = 440.0f * std::pow(2.0f, (quantized - 69.0f) / 12.0f);
}
```

When `pitchQuantize_` is true AND `singing_` is true, the saw locks to
chromatic-semitone steps. Note name in the readout reflects the
quantized note (clear visual: "you played slightly flat A4, the singer
is singing exactly A4").

When `singing_` is true but `pitchQuantize_` is false, vibrato applies
to the raw detected F0 (useful for "expressive but not quantized"
demos).

### 2.4 UI

One new toggle on `DiagToggleBar`:

- 5th pill labeled **"S  Sing"** (the existing pills V/N/S/P become
  V/N/Sib/P/Sing — the existing "Sibilance off" pill should be
  relabeled to "Sib off" to free up the "S" prefix; OR the new pill
  uses a different prefix letter).

**Decision:** use letter **"M"** for the new pill ("M  Modulate sing"
or "M  Sing mode") to avoid the S-prefix collision with Sibilance off.
The pill body shows just "Sing" — letter is keyboard-shortcut hint only.

Color: green-cyan to distinguish from the magenta P pill.

State persistence: add `bool singing = false;` to `PluginStateData`,
saved/restored alongside `pitchSinging`.

---

## 3. Visibility / NoteReadout updates

The `NoteReadout` shows the detected note name based on the published
`detectedNoteMidi_` atomic. With quantize ON, that atomic should reflect
the *quantized* note (the listener hears the quantized pitch; the
readout should match).

When quantize is OFF, the readout shows the raw detected note (current
behavior).

Optional additive UI: a small badge ("quantized" / "raw") on the
readout when Sing mode is on, so the operator can see at a glance
whether quantization is engaging. Defer to v2 unless trivial.

---

## 4. Testing

### 4.1 PitchTrackedCarrier unit tests (new)

`tests/unit/audio/test_pitch_tracked_carrier.cpp` (extend):

- **Saw LPF**: at F0=1000 Hz, the spectral energy above 4 kHz is at
  least 12 dB below the energy near 1 kHz. Goertzel-based.
- **Vibrato presence**: with `singing=true`, vibrato 5 Hz / 20 cents on
  a 220 Hz input, the saw output's instantaneous frequency oscillates
  through detectable highs and lows (zero-crossing density modulates
  at ~5 Hz). Simpler check: peak instantaneous frequency over 1 s
  differs from the nominal F0 by at least 10 cents.
- **Pitch quantize**: feeding a 226 Hz sine (slightly sharp A3 at
  220 Hz baseline), with `singing=true` and `pitchQuantize=true`, the
  saw output's dominant peak is at 220 Hz (±5 cents), not 226 Hz.
- **Singing off → raw behavior**: with `singing=false`, all existing
  PitchTrackedCarrier tests pass unchanged (regression guard).

### 4.2 PluginState (extend)

- `singing` round-trips through JSON.

### 4.3 ChannelVocoder (extend)

- The 25 ms time constant change is verified by extending the existing
  envelope-related test (if any) or adding a step-response test: a
  step input modulator decays to e^-1 of full level after
  ~25 ms (±5 ms).

### 4.4 Manual / by-ear

- Logic Pro: enable pitch-singing on scene 8; play the demo phrase
  with Sing OFF — note the scratchy buzz baseline. Toggle Sing ON —
  note the smoother, more in-tune feel. Toggle quantize OFF (via
  setter or temporarily wired UI) — note the expressive but
  un-quantized version.

---

## 5. File-touch summary

**Edited:**
- `src/audio/PitchTrackedCarrier.{h,cpp}` — saw LPF, holdMs_ default,
  Sing toggle, vibrato, pitch quantize. Largest change in this spec.
- `src/audio/ChannelVocoder.{h,cpp}` — envelope time constant 15 → 25 ms.
- `src/audio/AudioGraph.h` — forward `singing` setters/getters (so the
  toggle reaches `PitchTrackedCarrier` through the existing AudioGraph
  surface).
- `src/audio/AudioGraph.cpp` — no functional change to `process()`; just
  setter forwards.
- `src/app/PluginProcessor.{h,cpp}` — forward `singing` accessors +
  toggle; persist in get/setStateInformation.
- `src/app/PluginState.{h,cpp}` — `bool singing` field + JSON.
- `src/app/DiagToggleBar.{h,cpp}` — 5-pill layout (4 → 5).
- `tests/unit/audio/test_pitch_tracked_carrier.cpp` — new tests above.
- `tests/unit/app/test_plugin_state.cpp` — `singing` round-trip.

**New:** none.

---

## 6. Out of scope (this spec)

- **Vibrato shape options** (sawtooth, square LFO). Sine only in v1.
- **Vibrato auto-start delay** (singers often delay vibrato 100-200 ms
  after attack). Constant-on in v1.
- **Scale-aware quantize** (snap to a key/scale, not just chromatic).
  Chromatic only in v1. Add a `setKey(int rootMidi, int mode)` later
  if demands it.
- **UI sliders for vibrato rate/depth.** Defaults are baked; tune in
  code if needed.
- **Forward-looking note:** when the conversational AI path lands
  (separate spec), Sing mode is intentionally independent of the AI
  layer — it operates on whatever F0 the detector publishes, regardless
  of source.

---

## 7. Verification before "done"

- New + existing tests pass (target: 293 + ~5 new / 296 + 5 = 298/301).
- `auval -v aumf GtSp TdBF` succeeds after rebuild.
- `pluginval` strictness-10 in-process tests succeed.
- Manual ear check in Logic Pro: scratchy noticeably reduced; Sing mode
  audibly transforms speech → speech-with-tune.
