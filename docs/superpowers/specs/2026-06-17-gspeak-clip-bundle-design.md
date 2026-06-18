---
title: ".gspeak clip bundle — persist hand-tuned scene clips to disk"
date: 2026-06-17
status: draft
---

# `.gspeak` clip bundle

## 1. Goal

Persist the hand-tuned audio + syllable map for scenes 0 (Intro) and 10
(Speaks-for-me) to a single file on disk so every performance starts from
the same known-good clip — eliminating the per-performance variance that
comes from re-running Piper / Apple TTS / energy-valley snapping at boot.

Two scenes have different first-impression goals:

- **Scene 0 (Intro)** is the audience's first encounter with "the guitar
  speaking". It must be perfect on first note. → Bundle is auto-loaded
  on scene activation; the rough Apple-TTS bake never plays.
- **Scene 10 (Speaks-for-me)** is the finale. The narrative arc relies
  on showing the rough Piper bake first, then revealing the tuned
  version. → Bundle is opt-in via the **Load** button.

A small extension to the existing `WaveformView` editor — two buttons,
**Load** and **Save** — drives both flows.

## 2. Demo arcs (the user-visible behavior)

### Scene 0 — silent perfection

1. User activates scene 0.
2. Scene engine sees `gspeakAutoLoad: true` and reads
   `assets/clips/gspeak/scene0.gspeak`.
3. Hand-tuned clip is installed into the v1 note-stepped player; Apple
   TTS / prebaked path is **skipped**.
4. First note triggers the perfectly tuned phrase.

If the file is missing: a subtle 5s muted-grey line in `TtsStatusBar`
("`scene0.gspeak` missing — using fallback"), then the existing
Apple→prebaked fallback runs unchanged.

### Scene 10 — rough → reveal

1. User activates scene 10.
2. Scene engine runs the existing `PhonemeAlignedClipBuilder` path:
   Piper → espeak → energy-valley boundary snap. Rough version installed.
3. Audience hears the ugly boundaries.
4. User clicks **Load** in `WaveformView`. The `.gspeak` is read,
   hand-tuned clip replaces the rough one, and P + M pills auto-engage.
5. Same waveform redraws with clean boundaries; the AI now sings the
   line over the chord backing.

### Tuning workflow (development)

1. Activate the scene; boot bake runs (or auto-load runs for scene 0).
2. Edit text in `SayPanel`, press **Say** if you want a fresh bake from
   the current text.
3. Drag / double-click / right-click boundaries in `WaveformView`
   (existing v2 affordances; this spec adds the same for v1).
4. Press **Save** — current clip (samples + boundaries + text) is
   written to `scene.gspeakPath`.
5. Next launch, **Load** (or auto-load) brings the tuned version back.

## 3. File format

`.gspeak` is a zip file containing two entries:

```
manifest.json
audio.wav
```

### 3.1 `audio.wav`

Mono PCM, **16-bit signed**, at the sample rate of the engine that
saved the file. JUCE's `WavAudioFormat` with `useBigEndian=false`,
`numChannels=1`, `bitsPerSample=16`. 16-bit is below TTS noise floor
and roughly halves on-disk size (~600 KB for scene 0, ~2 MB for scene
10) so the files can live in regular git without LFS.

### 3.2 `manifest.json`

```jsonc
{
  "version": 1,                       // schema version
  "kind": "clip",                     // reserves "scene" for future use
  "savedBy": "guitar-dsp 0.x",        // free-form, informational
  "text": "I look at the world…",     // canonical text (from SayPanel)
  "sampleRate": 44100,                // matches audio.wav, used for resample on load
  "lengthSamples": 882000,            // must equal audio.wav length; load fails if mismatch
  "clipKind": "v2",                   // "v1" or "v2" — drives install dispatch

  // Present when clipKind == "v2":
  "syllables": [
    {
      "label": "I",
      "startSample": 0,
      "endSample": 7200,
      "vowelNucleusSample": 3600,
      "attackEndSample": 1800,
      "codaStartSample": 5800,
      "nucleusIsFricative": false,
      "phonemeIndices": [0, 1]
    }
    /* … */
  ],
  "phonemes": [
    { "label": "AY", "type": "Vowel", "startSample": 0, "endSample": 7200 }
    /* … */
  ],

  // Present when clipKind == "v1":
  "wordsV1": [
    { "word": "Developers", "startSample": 0, "endSample": 28000 }
    /* … */
  ],
  "syllablesV1": [
    { "word": "De",   "startSample": 0,     "endSample": 6500 },
    { "word": "vel",  "startSample": 6500,  "endSample": 13000 }
    /* … */
  ]
}
```

#### 3.2.1 Field semantics

- `version: 1` is required. Future readers will gate on this.
- `kind: "clip"` is required. Other values reserved.
- `clipKind` drives install dispatch — `"v1"` → `installEditedV1Clip`,
  `"v2"` → `installEditedPhonemeClip` (existing).
- Sample indices are inclusive-start, exclusive-end — same convention
  as `WordSegment` and `SyllableSpan`.
- All `*Sample` indices are in the saved file's sample rate. Loader
  rescales them when the engine rate differs (§5.3).
- Unknown top-level fields are ignored by the loader (forward compat).

### 3.3 Validation

On load, the reader fails closed if any of the following don't hold:

- Zip opens; both entries present.
- `manifest.version == 1` and `manifest.kind == "clip"`.
- `audio.wav` decodes; mono; length equals `lengthSamples`.
- `clipKind` is `"v1"` or `"v2"`.
- The corresponding boundary arrays are present and non-empty.
- All boundary spans are non-empty, ordered, non-overlapping, within
  `[0, lengthSamples]`.
- For v2: `phonemeIndices` reference valid entries in `phonemes`.

On any failure: log the specific reason to stderr, surface the subtle
5s message in `TtsStatusBar`, leave the currently-playing clip
untouched (no half-applied state).

## 4. Scene JSON additions

Two new optional fields on the scene schema. No change to existing
fields.

```jsonc
// assets/scenes/00_intro.json
{
  /* existing fields unchanged */
  "gspeakPath": "assets/clips/gspeak/scene0.gspeak",
  "gspeakAutoLoad": true
}

// assets/scenes/10_speak_v2_guitar_lead.json
{
  /* existing fields */
  "tts": {
    "source": "piper",
    "fallback": "prebaked",
    "text": "I look at the world and I notice it's turning. While my guitar gently speaks. With every mistake we must surely be learning. Still my guitar gently speaks.",
    "clarity": 0.80
  },
  "gspeakPath": "assets/clips/gspeak/scene10.gspeak"
  // gspeakAutoLoad omitted → defaults to false
}
```

`SceneLibrary` gains two optional field parses:

```cpp
if (obj->hasProperty("gspeakPath"))
    s.gspeakPath = obj->getProperty("gspeakPath").toString().toStdString();
if (obj->hasProperty("gspeakAutoLoad"))
    s.gspeakAutoLoad = (bool) obj->getProperty("gspeakAutoLoad");
```

Backed by new fields on the scene struct: `std::string gspeakPath` and
`bool gspeakAutoLoad = false`.

**Path resolution** uses the same resources-directory mechanism as the
existing `assets/tts/...` lookups in `PrebakedTTSSource`. Standalone
resolves relative to the repo root; bundled (AU plugin) resolves
inside the bundle's Resources directory.

## 5. Implementation

### 5.1 New code units

- `src/audio/GspeakBundle.{h,cpp}` — read/write `.gspeak` files.
  Pure functions, no UI/processor dependencies. Returns
  `audio::TTSClipPtr` + manifest fields on read; takes a `TTSClip` +
  text on write.
- `src/audio/V1BoundaryEdits.{h,cpp}` — `addBoundaryV1`,
  `moveBoundaryV1`, `removeBoundaryV1`. Parallel to existing
  `addBoundary` / `moveBoundary` / `removeBoundary` for v2.
- New unit tests under `tests/unit/audio/` for both.

### 5.2 Changed code units

- `src/scenes/Scene.h`, `src/scenes/SceneLibrary.cpp` — new scene fields.
- `src/app/PluginProcessor.{h,cpp}` — new `installEditedV1Clip()`
  method that updates `lastV1Clip_` and calls
  `graph_.noteSteppedPlayer().setClip(clip)`. Mirrors
  `installEditedPhonemeClip()` (which installs to
  `phonemeSteppedPlayer`). Both message-thread.
  Also: scene-activation hook checks `gspeakAutoLoad` and runs the
  Load path before the existing TTS source path.
- `src/app/WaveformView.{h,cpp}` — Save/Load buttons (top-right of
  the panel). v1 edit handlers added: when `clip_->sylsV2` is empty
  but `clip_->syllables` (or `clip_->words`) is populated, the same
  drag/insert/delete UX operates on the v1 arrays via the new helpers
  and `installEditedV1Clip`.
- `src/app/TtsStatusBar.{h,cpp}` — accepts an optional transient
  message that displays for 5s in muted grey. Used for save/load
  feedback and the auto-load missing-file warning.
- `src/app/SayPanel.{h,cpp}` — expose a public method
  `void setText(juce::String)` so the Load path can update the input
  field with the manifest's text. No new editing UI.

### 5.3 Sample rate handling

The clip in memory always lives at the engine's current sample rate
(host rate when running as an AU, fixed rate standalone). The
`.gspeak` carries its own sample rate in the manifest.

- **Save:** write the WAV and `manifest.sampleRate` at
  `clip->sampleRate`. No resample.
- **Load:** if `manifest.sampleRate == currentEngineRate`, use the
  decoded samples verbatim. Otherwise resample using the same
  linear-interp routine as `PrebakedTTSSource.cpp:50-64`, and scale
  every `*Sample` index by `currentRate / fileRate` rounded to
  nearest. Clamp the last syllable's `endSample` to
  `samples.size()` to absorb rounding (same defensive pattern as
  `PhonemeAlignedClipBuilder.cpp:47`).

Round-trip cases:
- Save in Logic @44.1, load in Logic @44.1: bit-exact, no resample.
- Save in Logic @44.1, open standalone @48: clean resample + index
  rescale. Sub-millisecond precision loss on boundaries (below
  hand-tuning grain).
- Save standalone @48, load in Logic @44.1: same, in reverse.

### 5.4 Auto-load on scene activation

Inserted into the scene-activation path in `PluginProcessor.cpp`
**before** the existing TTS-source dispatch:

```cpp
if (!scene.gspeakPath.empty() && scene.gspeakAutoLoad) {
    if (auto result = GspeakBundle::read(resolvePath(scene.gspeakPath),
                                          currentSampleRate);
        result.has_value()) {
        if (result->clipKind == V1)
            installEditedV1Clip(result->clip);
        else
            installEditedPhonemeClip(result->clip);
        sayPanel_.setText(result->text);
        return;  // skip the normal TTS bake
    }
    ttsStatusBar_.flashMessage(
        scene.gspeakPath + " missing — using fallback", 5s);
    // fall through to normal bake
}
/* existing TTS source dispatch unchanged */
```

### 5.5 Load button

Reads `scene.gspeakPath` (same `resolvePath` logic), validates,
installs the clip via the right path, updates `SayPanel`. Additionally,
**if the loaded clip is v2** (`clipKind == "v2"`):

```cpp
processor_.setPitchSinging(true);  // P pill
processor_.setSinging(true);       // M pill
```

Both setters already exist on `PluginProcessor` (`setPitchSinging` for
the P pill, `setSinging` for the M pill — `PluginProcessor.h:204`).

For v1 clips, no toggle changes — scene 0 is speech, not singing.

The Load button is disabled when `scene.gspeakPath` is empty.

### 5.6 Save button

Captures:

- `clip_` (the in-memory clip currently shown in `WaveformView`).
- `sayPanel_.getText()` for the manifest text.

Determines `clipKind` from clip shape (sylsV2 populated → v2; else
v1). Writes to `scene.gspeakPath`. The Save button is disabled when
there is no clip or no `gspeakPath`.

Save and Load are both message-thread synchronous. Worst-case write
size is ~2 MB; well under a video frame on local disk.

### 5.7 v1 edit handlers in WaveformView

Today, `WaveformView` displays v1 boundaries (the priority-ordered
`useV2Syls`/`useV1Syls`/`useV1Words` accessor exists since commit
`91162a6`) but the mouse handlers (`hitBoundary_`, `mouseDown`, etc.)
short-circuit on `clip_->sylsV2.empty()`.

Two changes:

1. The hit-test and edit handlers learn to operate on
   `clip->syllables` for v1 clips — the same array the existing
   display priority shows first. Scene 0 uses syllable-mode word
   sync, so this matches what the player consults. The `clip->words`
   array is not edited by hand in this round (no in-scope scene
   needs it); it stays at bake-time values.
2. Edits call `installEditedV1Clip(edited)` instead of the v2 path.

The drag/insert/delete UX is identical between v1 and v2 — only the
underlying array and the install method differ.

## 6. Error handling

| Condition | Behavior |
| --- | --- |
| `gspeakPath` empty / scene has no field | Load button disabled. Save button disabled. Auto-load skipped. |
| File missing at auto-load | 5s muted message in `TtsStatusBar`; existing TTS source path runs. |
| File missing at manual Load | 5s muted message; current clip untouched. |
| Zip / manifest / WAV malformed | 5s muted message with the reason; current clip untouched. |
| Length mismatch / index out of range | 5s muted message; current clip untouched. |
| Save fails (disk full, permission) | 5s muted "Save failed: <reason>"; in-memory clip untouched. |
| Sample rate differs from engine | Silent resample + index rescale. No message. |

The current clip is **never** half-replaced on a failure path. Either
the new clip is fully validated and atomically swapped, or the old
clip stays.

## 7. Testing

Unit:

- `tests/unit/audio/test_gspeak_bundle.cpp` — round-trip a synthetic
  v2 clip: write → read → assert samples + sylsV2 + phonemes match.
- Same, for a synthetic v1 clip: assert samples + words + syllables
  match.
- Reject malformed inputs: missing manifest, wrong version, length
  mismatch, overlapping spans, out-of-range indices.
- Sample-rate cross-load: write at 44.1, load at 48; assert sample
  count and boundary indices scale correctly within ±1 sample
  rounding.
- `tests/unit/audio/test_v1_boundary_edits.cpp` — mirror the existing
  v2 boundary-edit tests for the new v1 helpers.

Integration:

- `tests/integration/test_gspeak_autoload.cpp` — fixture scene with
  `gspeakAutoLoad: true` and a tiny fixture `.gspeak`. Assert the
  loaded clip's samples appear in the v1 note-stepped player's
  output. Repeat with the file missing — assert the fallback runs
  and no clip is broken.
- `tests/integration/test_gspeak_load_button.cpp` — scene-engine
  activates scene 10, simulate clicking the Load button (via the
  same processor entry point the button calls), assert clip is
  swapped and P+M flags are flipped.

Manual (live performance dress-rehearsal):

- Boot the app, activate scene 0, play one note — perfect clip plays.
- Rename `scene0.gspeak`, restart, activate scene 0 — message
  appears, fallback plays.
- Activate scene 10, hear rough Piper, click Load, hear tuned sung
  clip with chord backing.

## 8. Files committed to git

- `assets/clips/gspeak/scene0.gspeak` — hand-tuned scene 0 clip.
  ~600 KB (mono 16-bit @ 44.1 kHz, ~7s).
- `assets/clips/gspeak/scene10.gspeak` — hand-tuned scene 10 clip.
  ~2 MB (mono 16-bit @ 44.1 kHz, ~22s).

Both are versioned in regular git. No LFS.

## 9. Out of scope (explicit)

These are intentionally not in this spec; add later if needed.

- Auto-snap-to-zero-crossings on Save.
- Normalize peak amplitude / DC-offset removal / silence trim.
- File picker UI (Save As / Load From).
- Re-bake confirmation dialog on Say.
- Disabling Say when a tuned clip is loaded.
- New waveform editor features (play-from-here, slice audition,
  zoom, undo/redo).
- Sing-mode auto-engage on scene activation (only fires on Load).
- `kind: "scene"` bundles — full scene state save. Reserved by the
  schema; not implemented.
- Bundles for any other scenes — only 0 and 10 in this round.

## 10. Open extensions (room to grow)

The format is designed to extend cleanly without breaking older
readers:

- `manifest.kind = "scene"` — a future bundle that also carries
  vocoder / mixer / pill-state overrides. Loaders gating on
  `kind == "clip"` ignore these.
- New top-level fields (lyrics, notes, hash, savedAt) — readers
  ignore unknown keys.
- A `manifest.version = 2` would gate a schema-breaking change; the
  current reader rejects unknown versions explicitly so the user
  sees a clean error instead of half-loading.
