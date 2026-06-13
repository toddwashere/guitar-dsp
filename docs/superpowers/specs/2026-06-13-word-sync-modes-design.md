# Word-Sync Modes — Design

**Status:** Approved direction (brainstorming) — 2026-06-13

**Goal:** Fix the note-triggered speech bug where (a) multi-syllable
words get cut off when a fast onset arrives mid-word, and (b) double-
detected onsets play two or three words per single guitar pluck. Replace
the current single behavior with **three user-selectable modes**:

- **Latch (default)** — one word per onset, hard latch. The current word
  plays to completion; onsets received while a word is playing are
  ignored. Bulletproof 1:1 mapping; the cost is that fast playing
  doesn't queue and slow playing leaves silence between words.
- **Advance** (the current behavior, kept as opt-in) — every onset
  advances to the next word and restarts it from sample zero. Most
  responsive; can cut words mid-syllable.
- **Syllable** — one syllable per onset. Requires the scene's text to
  include hyphen markers (`"gui-tar gent-ly"`). Each hyphen-bounded
  fragment becomes one syllable segment; onsets advance through the
  syllable list. Within a word, syllables are timed by equal subdivision
  of the word's measured duration.

The mode is a runtime toggle; a per-scene JSON field overrides the
global setting for that scene.

**Why now:** the demo's "you play, the guitar speaks" effect breaks when
syllables are clipped or doubled. Even a 1:1 word-to-note guarantee
would be a big win; syllable-level is the eventual target for lyrical
phrasing.

**Out of scope (separate spec):**
- Polish + Sing mode (saw LPF, hold time, vibrato, quantize) —
  see [2026-06-13-singing-polish-design.md](2026-06-13-singing-polish-design.md).

---

## 1. The current bug — what changes

Today's `NoteSteppedTTSPlayer::process` does on every detected onset:

```cpp
wordIndex_ = (wordIndex_ + 1) % n;
playPos_ = activeClip_->words[wordIndex_].startSample;
segEnd_  = activeClip_->words[wordIndex_].endSample;
playing_ = true;
```

Two consequences observed:
- A new onset during playback restarts at the next word's `startSample`,
  cutting whatever is currently sounding.
- The `OnsetDetector` can fire 2-3 times on a single hard pluck (no
  minimum-interval gate currently visible), advancing the word index
  multiple times for one note.

We address both with mode selection AND a tighter onset minimum-interval.

---

## 2. The three modes

### 2.1 Latch (recommended default)

```cpp
if (onset_detected && !playing_) {
    advance_to_next_segment();
    playing_ = true;
}
// else: ignore onsets received during playback
```

Once a word starts, the next onset is ignored until the word's segment
finishes playing. The very next onset *after* `playing_` becomes false
advances + plays.

Pros: 1:1 deterministic. No words clipped. No 3-words-per-onset.
Cons: rapid plucking queues no words; slow plucking leaves silence.

### 2.2 Advance (current behavior, kept)

```cpp
if (onset_detected) {
    advance_to_next_segment();
    playing_ = true;
}
```

Identical to today. Available as an opt-in for users who want raw
responsiveness over predictability.

### 2.3 Syllable

Storage: `TTSClip` gains an optional second segmentation list,
`std::vector<WordSegment> syllables`. When non-empty, advances index
through `syllables` instead of `words`.

`syllables` is populated at clip-load time when the source text contains
hyphen markers. Algorithm (in `WordAligner::alignSyllables`):

1. Tokenize input text on whitespace → words.
2. For each word, split on `-` → syllable strings.
3. Use the standard `align()` to get word boundaries (energy-gap
   segmentation works as today).
4. Within each word's audio range, subdivide into N equal-duration
   syllable segments where N is the syllable count for that word.

Equal subdivision is intentional: it's deterministic, decent quality
for most demo phrases, and avoids a phoneme aligner dependency.

Pros: lyrical 1:1 syllable-to-note. Demo lines like "gui-tar gent-ly
speaks for me" map cleanly to 7 picked notes.
Cons: stress is sometimes mistimed within a word (equal subdivision
doesn't know "guh-TAR" stress). Requires hyphen authoring in the scene
text.

---

## 3. UI

A new control row in `VocoderPanel`, hosting a 3-way mode selector —
three small pills laid out in a row. DiagToggleBar is rejected here
because (a) the 3-way mode is more "configuration" than "diagnostic
toggle," (b) DiagToggleBar already carries V/N/S/P and is visually
crowded, and (c) VocoderPanel's row-based layout extends naturally:

- **[ Latch ]  [ Advance ]  [ Syllable ]**

Active mode is highlighted. Clicking switches modes; effective
immediately for the next onset.

Keyboard shortcut: cycle modes with **W** (no current binding to
collide with).

Persistence: `WordSyncMode` enum (int) added to `PluginStateData`,
default `Latch`. Saved/restored via JSON.

---

## 4. Per-scene override

Each scene's TTS config in `assets/scenes/*.json` may include an
optional `"wordSync"` field. When present, it overrides the global UI
selection for the duration of that scene.

```json
{
  "tts": {
    "source": "prebaked",
    "clip": "08_gently_weeps",
    "text": "And now my gui-tar gent-ly speaks for me.",
    "trigger": "note",
    "wordSync": "syllable",
    "clarity": 0.5
  }
}
```

Allowed values: `"latch"` | `"advance"` | `"syllable"` | `"global"`
(default — defer to UI selection).

When a scene's `wordSync` is `"syllable"` but its text has no hyphens,
the player falls back to word-level **Latch** behavior. The UI shows a
small warning indicator on the scene readout: "Syllable mode requested
but text has no hyphens — using Latch." Avoids silent confusion.

---

## 5. Onset detector tightening

Independent of mode, raise the `OnsetDetector` minimum-interval from
its current value (whatever it is) to ~80 ms. Most musical playing
won't trigger two genuine onsets in under 80 ms; this kills the most
common false-double-trigger.

- **Where:** `OnsetDetector::prepare` or a new `setMinIntervalMs`.
- **Default:** 80 ms.
- **Risk:** rapid tremolo playing might get throttled. Acceptable for the
  demo use case; can be lowered via a future UI knob if needed.

---

## 6. Hyphenation — who supplies the markers

Three input sources, three policies:

1. **Scene JSON** — author hyphenates in `text` field. Authority for
   pre-built scenes. Author tooling (just a JSON file edit) is trivial.
2. **`SayPanel` typed text** — user types `"hello world"` (no
   hyphens), gets word-level segmentation; or types
   `"hel-lo world"` for explicit syllable boundaries. No automatic
   syllabification (avoids unpredictable mis-segmentation surprises
   in a live demo). A small status hint appears under the input when
   Syllable mode is active: "Add hyphens for syllable steps:
   `gui-tar gent-ly`".
3. **Future conversational-AI LLM output** — the AI flow is not yet
   built; when it lands, its system prompt must include an
   instruction along the lines of: *"When syllable-sync mode is active
   for the response output, hyphenate any multi-syllable word in your
   response: `guitar` → `gui-tar`, `gently` → `gent-ly`. The user is
   triggering each syllable with a guitar note, so accurate syllable
   breaks make the spoken response feel sung in time with their
   playing."*  This is forward-looking documentation; nothing built
   here.

---

## 7. Architecture

`WordSyncMode` enum (new, in `src/audio/`):

```cpp
enum class WordSyncMode {
    Latch,      // recommended default
    Advance,    // current behavior
    Syllable,   // requires hyphenated text
};
```

`NoteSteppedTTSPlayer`:
- Gains `void setMode(WordSyncMode)` + `WordSyncMode mode() const`.
- Gains `void setSegmentation(Segmentation)` where Segmentation is
  either Words or Syllables — selects which list to step through
  (decided per-clip at load time; see §6).
- `process()` gains the mode branch — Latch vs Advance — and reads
  from the selected segmentation list.

`TTSClip`:
- Adds `std::vector<WordSegment> syllables;` (empty by default).

`WordAligner`:
- Adds `static std::vector<WordSegment> alignSyllables(samples, hyphenated_words, sampleRate)`.

`SceneEngine` / scene loader:
- Parses the optional `wordSync` JSON field; clamp to `"global"` if
  unknown.
- When applying a scene with `wordSync: syllable`, calls
  `noteSteppedPlayer_.setMode(Syllable)` and ensures the prepared clip
  has its `syllables` populated (calling `alignSyllables` if needed).

`AudioGraph`:
- Forwards `setWordSyncMode(WordSyncMode)` / `wordSyncMode()` to
  `noteSteppedPlayer_`. Atomic int storage for thread-safety.

`PluginState`:
- Adds `WordSyncMode wordSyncMode = Latch` (as int in JSON).

`PluginProcessor`:
- Forwards mode setter/getter.
- Persists mode in get/setStateInformation.

UI (`VocoderPanel` or `DiagToggleBar`):
- 3-way mode selector + status warning when scene requests Syllable
  but text isn't hyphenated.

---

## 8. Testing

### 8.1 `NoteSteppedTTSPlayer` unit tests (extend)

`tests/unit/audio/test_note_stepped_player.cpp`:

- **Latch ignores onsets during playback**: feed two onsets 50 ms apart
  with a clip whose first word is 500 ms long; assert `currentWordIndex_`
  remains 0 throughout (didn't advance to 1).
- **Latch advances after current word finishes**: feed an onset, let
  500 ms pass (full word duration), feed a second onset; assert word
  index advances 0 → 1.
- **Advance mode** (current behavior): two onsets 50 ms apart advances
  word index twice (0 → 1 → 2). Regression check that Advance still
  works.
- **Syllable mode with `syllables` populated**: 3-syllable clip
  (manually constructed); 3 onsets in mode Syllable advance through
  all 3 syllables.
- **Syllable mode with empty `syllables`**: falls back to Latch
  behavior on words (graceful degradation).

### 8.2 `WordAligner::alignSyllables`

`tests/unit/audio/test_word_aligner.cpp` (if exists; else create):

- 2-word clip ("guitar gently"), input `"gui-tar gent-ly"` → 4
  segments. Each within its word's time range.
- Word with no hyphen ("speaks") → 1 segment matching that word.

### 8.3 Scene-loader regression

`tests/unit/scenes/test_scene_library_tts.cpp`:

- Scene JSON with `"wordSync": "syllable"` parses to the right enum
  value. Unknown value falls back to global.

### 8.4 `PluginState`

- `wordSyncMode` round-trips through JSON.

### 8.5 `OnsetDetector`

- Two consecutive transient pulses 40 ms apart: with min-interval
  80 ms, the second is suppressed.
- Same pulses 100 ms apart: both detected.

### 8.6 Manual / by-ear

- Logic: in scene 8, mode Latch — pluck the same note 8 times slowly;
  speech advances exactly one word per pluck, no clipping.
- Mode Advance — same plucking; verify it still cuts (regression).
- Mode Syllable — author-hyphenate scene 8's text, reload, pluck 11
  times; speech advances syllable-by-syllable.

---

## 9. File-touch summary

**New:**
- `src/audio/WordSyncMode.h` — enum + string conversion helpers.
- `tests/unit/audio/test_word_aligner.cpp` (if missing).

**Edited:**
- `src/audio/NoteSteppedTTSPlayer.{h,cpp}` — mode storage + branch in
  process; segmentation list selection.
- `src/audio/TTSClip.h` — add `syllables` vector.
- `src/audio/WordAligner.{h,cpp}` — add `alignSyllables`.
- `src/audio/OnsetDetector.{h,cpp}` — add `setMinIntervalMs`, default
  80 ms.
- `src/scenes/SceneLibrary.cpp` — parse `wordSync` field.
- `src/scenes/Scene.h` (or wherever the TTS config struct lives) — add
  `std::string wordSync = "global"`.
- `src/scenes/SceneEngine.{h,cpp}` — on scene activation, push mode +
  ensure `syllables` populated if needed.
- `src/audio/AudioGraph.{h,cpp}` — forward setMode / mode.
- `src/app/PluginProcessor.{h,cpp}` — forward + persist.
- `src/app/PluginState.{h,cpp}` — mode field + JSON.
- `src/app/VocoderPanel.{h,cpp}` — 3-way mode selector (new row).
- `assets/scenes/08_speaking_finale.json` — example: update text to
  hyphenated form + add `"wordSync": "syllable"`.
- `README.md` — document the mode selector + per-scene override.

---

## 10. Out of scope (this spec)

- **Automatic syllabification** of unhyphenated text. Deterministic
  hyphen authoring is the v1 contract.
- **Phoneme-level alignment** (Montreal aligner / whisper-timestamped).
  Equal subdivision within words is the v1 timing model.
- **Per-syllable stress weighting** (long stressed syllables, short
  unstressed). Equal-duration in v1.
- **Onset detector retuning beyond min-interval.** Threshold, gain,
  attack/release tuning is its own future work.
- **Conversational AI LLM integration.** The forward-looking note in §6
  documents the requirement; the LLM call site doesn't exist yet.
- **UI for `vibrato/quantize` knobs from the singing-polish spec** —
  out of this spec entirely.

---

## 11. Verification before "done"

- All new + existing tests pass (target: 293/296 baseline + ~7 new in
  this spec).
- `auval -v aumf GtSp TdBF` succeeds.
- `pluginval` strictness-10 in-process succeeds.
- Manual gate: in Logic Pro, scene 8 in Latch mode demonstrates the
  1:1 word-to-pluck mapping the demo needs.
