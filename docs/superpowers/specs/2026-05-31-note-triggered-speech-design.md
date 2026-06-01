# Note-Triggered Word-by-Word Speech — Design Spec (Phase 5a)

**Status:** Approved for planning
**Date:** 2026-05-31
**Parent spec:** [`2026-05-29-while-my-guitar-gently-speaks-design.md`](2026-05-29-while-my-guitar-gently-speaks-design.md)
**Builds on:** Phase 3/3.5/3.6 (TTS sources + `TTSClipPlayer` + `ChannelVocoder`)

## 1. Goal

The defining feature of the project: **the guitar speaks one word per note.** In
a speaking scene, the TTS phrase is pre-split into per-word audio segments. Each
guitar-note attack (onset) plays the next word as the vocoder's modulator — so
the guitar "says" that word — and advances a word index. The performer paces the
whole sentence by how they play; nothing speaks until a note is plucked. After
the last word, the next onset auto-loops back to word 0.

This phase delivers the audio mechanism end-to-end plus a **minimal on-screen
word readout** so it is demoable. The full karaoke styling + spectrogram backdrop
are deferred to **Phase 5b**.

Design principle (the user's words): *balance stability with real-time.* Favor
the lower-risk, allocation-free, uniform-across-backends approach over fragile
engine-specific cleverness.

## 2. Mechanism / data flow

```
InputStage (clean guitar)
   ├─→ OnsetDetector ──(onset event)──► NoteSteppedTTSPlayer.advance()
   │                                        │  plays current word segment
   │                                        ▼
   └─→ ChannelVocoder.carrier            modulator = current word segment
                                            │
                                  vocoded "spoken word" → Mixer.wet
```

- Onset detection runs on the **clean InputStage output** (pre-vocoder, pre-
  carousel) so effects never confuse it.
- The note-stepped player emits the current word's segment as the vocoder
  modulator; between/after a segment it emits silence (so one pluck = one word,
  spoken once).
- The current word index is published atomically for the UI readout.

## 3. Components

Each is small, single-purpose, and (where on the audio thread) allocation-free.

### 3.1 `audio::OnsetDetector` (audio thread)
Envelope-follower with hysteresis + debounce.
- One-pole envelope follower on |x|.
- Fires an onset when the envelope rises **above** `attackThreshold` while the
  detector is "armed."
- After firing, the detector **disarms** until the envelope falls **below**
  `rearmThreshold` (hysteresis) — so a sustained/ringing note does not
  retrigger; you must re-pluck.
- A minimum inter-onset time (`debounceMs`, default ~50 ms) rejects double-fires
  from a single noisy attack.
- API: `prepare(sampleRate)`, `reset()`, `bool processSample(float x)` returns
  true on the sample where an onset fires. (Or a block form returning a count.)
- Pure, no allocation, unit-tested on synthetic pluck trains.

### 3.2 `audio::WordAligner` (message thread, pure)
Energy-gap word segmentation. Uniform across all TTS backends — operates only on
the produced float PCM + the word list, with no engine-specific timing APIs.
- Input: the clip's `samples` + the `words` (split the scene text on whitespace).
- Compute a short-window RMS/energy envelope. Find the `N-1` largest contiguous
  low-energy gaps (N = word count). Word boundaries are placed at the centers of
  those gaps; segment `i` spans `[boundary[i-1], boundary[i])`.
- Output: `std::vector<WordSegment>` (see 3.3), length == N.
- Degenerate handling: N==1 → one segment spanning the whole clip; clip empty →
  empty list; fewer detectable gaps than N-1 → distribute remaining boundaries
  evenly (so we always return exactly N segments).
- Runs once at synthesis time (in the prewarmer / source), never on the audio
  thread. Fully unit-tested on synthetic clips with known gaps.

### 3.3 `WordSegment` on `TTSClip`
Add to `src/audio/TTSClip.h`:
```cpp
struct WordSegment {
    std::string word;
    std::size_t startSample = 0;   // inclusive, into TTSClip::samples
    std::size_t endSample   = 0;   // exclusive
};
```
And a member `std::vector<WordSegment> words;` on `TTSClip` (default empty →
"not segmented"; consumers fall back to whole-clip behavior). `TTSClip` stays a
plain aggregate; `words` is filled off the audio thread before the clip is
published as a `TTSClipPtr` (shared_ptr<const>, so it's read-only on the audio
thread).

### 3.4 `audio::NoteSteppedTTSPlayer` (audio thread)
Note-triggered analogue of `TTSClipPlayer`. Same atomic clip-swap pattern
(`setClip` on message thread; pending/active + flag; picked up in `process`).
- Holds `activeClip_` + `wordIndex_` + `segmentPlayPos_`.
- `process(const float* onsetSourceIn, float* modulatorOut, size_t n)`:
  reads the clean guitar via an internal `OnsetDetector`; on an onset, set
  `wordIndex_` to play (advance with wrap to 0 after the last), reset
  `segmentPlayPos_ = words[wordIndex_].startSample`. Each sample, if a segment is
  active and not exhausted, output `samples[segmentPlayPos_++]`; else 0.
  Advancing the index happens **on** the onset; "wrap to 0 after last" means: the
  onset after the final word selects word 0.
- Allocation-free; `RealtimeSentinel`-tested.
- Exposes `std::atomic<int> currentWordIndex()` (−1 when idle) for the UI.
- If the active clip has no `words` (unsegmented), it plays the whole clip on the
  first onset (graceful fallback).

### 3.5 Minimal word readout (UI)
A small `app/WordReadout` component (or a line added to an existing panel) that
polls `processor`'s exposed current word index + the active scene's word list on
a timer and shows the current word as large text. This is the demoable payoff for
5a; Phase 5b replaces it with the full-screen karaoke + spectrogram.

## 4. Scene config — `tts.trigger` + the whole-clip → word-by-word progression

Add a `trigger` field to the scene `tts` block (`src/scenes/Scene.h` `TtsConfig`):
```cpp
std::string trigger;  // "auto"/"" (whole clip, default) | "note" (word-by-word)
```
- `"auto"` **or empty (the default)** → today's linear `TTSClipPlayer` behavior:
  the whole clip plays through on scene activation. This **preserves the original
  Phase-3 speaking behavior** unchanged and keeps existing tests + the type-and-say
  overlay working. Defaulting to whole-clip is the conservative, backward-
  compatible choice ("balance stability with real-time").
- `"note"` → route the modulator through `NoteSteppedTTSPlayer` (one word per
  pluck). Opt-in only.

**Illustrating the progression.** The two modes coexist as live, switchable
scenes, with the **whole-clip version at a lower scene number than the
word-by-word version** so that stepping up the scenes demonstrates the
evolution:

| Scene | Mode | Role |
|---|---|---|
| 6 — Speaking A | **whole clip** (`trigger: "auto"`) | the "before": the original Phase-3 speak-on-activate |
| 7 — Speaking B | **word-by-word** (`trigger: "note"`) | the "after": one word per pluck |
| 8 — Speaking finale | **word-by-word** (`trigger: "note"`) | the showpiece end goal |

`trigger` is parsed defensively in `SceneLibrary.cpp` like the other `tts`
fields. No extra scene slots are consumed — the three existing speaking slots
hold both modes. (Other effects-bank slots remain available if a future phase
wants more contrast scenes.)

## 5. AudioGraph wiring

`AudioGraph` gains a `NoteSteppedTTSPlayer` alongside the existing
`TTSClipPlayer`, and a selector for which feeds the vocoder modulator (set from
the message thread on scene change, like the Phase-4 wet-source selector):
- Speaking scene, `trigger == note`: modulator = `noteSteppedPlayer_.process(
  cleanGuitar, …)`; carrier = clean guitar; vocoder output → wet.
- Speaking scene, `trigger == auto`: modulator = `ttsClipPlayer_.process(…)`
  (unchanged).
- Instrument scenes: unchanged (carousel branch from Phase 4).
The note-stepped player needs the clean InputStage output as its onset source —
which `AudioGraph::process` already has in `postInputBuffer_`.

`PluginProcessor`'s scene-change callAsync pushes the segmented clip into
whichever player the scene selects and sets the modulator selector.

## 6. Word index exposure

`PluginProcessor` exposes `int currentSpokenWordIndex() const noexcept` (reads
the player's atomic) and `std::vector<std::string> activeSceneWords() const`
(message thread; splits the active scene text). The UI readout uses both.

## 7. Testing

- **OnsetDetector**: synthetic pluck train (decaying bursts separated by silence)
  → exactly one onset per pluck; a single sustained note → exactly one onset;
  two attacks within `debounceMs` → one onset.
- **WordAligner**: synthetic 3-"word" clip (three tone bursts separated by clear
  silence) → 3 segments whose boundaries fall in the gaps; word count matches the
  text; 1-word and empty-clip and no-gap degenerate cases return exactly N (or 0)
  segments without crashing.
- **NoteSteppedTTSPlayer**: feeding an onset advances to the next segment and
  emits its samples; after the last word, the next onset selects word 0 (wrap);
  no onset → silence; `RealtimeSentinel` → zero allocations in `process`;
  unsegmented clip → whole-clip fallback on first onset.
- **Integration** (`AudioGraph`): a pluck train through a note-triggered speaking
  scene yields vocoded output that is non-silent only after onsets, stepping
  through words in order and looping; bit-exact/again-green for `auto` scenes.
- **No regressions**: existing speaking-scene tests pass (they use `auto` or are
  updated to assert the new default explicitly).

## 8. Out of scope (this phase → Phase 5b)

- Spectrogram backdrop, full-screen `ShowView`, polished karaoke text styling
  (5a ships only a minimal current-word readout).
- Engine-true word timestamps (5a uses uniform energy-gap segmentation; the
  `WordSegment` interface allows a later swap-in).
- Tempo/transient-based effects, beat sync — out of project scope.

## 9. Risks & mitigations

- **Onset detection robustness** on real guitar (palm mutes, hammer-ons, noisy
  attacks): mitigate with hysteresis + debounce + a tunable attack threshold;
  detect on the clean pre-effect signal. Acceptable failure mode is an occasional
  missed/extra word, never a crash.
- **Energy-gap mis-segmentation** on run-together speech: acceptable for the
  effect; tunable gap threshold; baked finale clip can be voiced with clear word
  gaps.
- **Latency** from onset → spoken word must feel immediate: onset detection is
  per-sample on the audio thread and the segment starts the same block, so
  latency is ~one block (a few ms) — well within feel.
- **Modulator/carrier timing**: the spoken word plays at its natural duration
  regardless of how long the note rings; this is the intended "gently speaks"
  character, not a bug.
