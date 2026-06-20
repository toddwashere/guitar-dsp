---
title: "mp3 import + `.gspeak` in-session persistence fix"
date: 2026-06-19
status: draft
---

# mp3 import + `.gspeak` in-session persistence fix

## 1. Goal

Two related changes to the `.gspeak` workflow:

1. **Persistence fix.** Edits made in `WaveformView` and persisted via the
   Save button must remain visible after navigating away and coming back
   within the same app session. They currently disappear (and are also
   invisible to a manual Load press) until the next rebuild + relaunch.

2. **mp3 import.** Let scene 10 hold a clip whose audio comes from an
   external recorded file (e.g. ElevenLabs mp3) instead of a Piper bake,
   sliced through the existing v1 word-stepped clip system. The vocoder
   still operates downstream of the clip player as today.

Scene 10 is the only target this round; scene 0 stays on its Apple-TTS
bake and only inherits the persistence fix.

## 2. Persistence fix

### 2.1 Current behavior (the bug)

Two file paths exist for the same `assets/clips/gspeak/*.gspeak`
relative path:

- **Source path** — `AssetLocator::resolveSourceRelativePath`. Walks up
  from the binary location to find an `assets/` dir that is a sibling
  of a `build/` dir. This is the repo's source tree.
- **Runtime path** — `AssetLocator::resolveRelativePath`. The path
  baked into the running binary (the AU bundle's `Resources/assets/`
  for plugin builds, the runtime-resolved working dir for standalone).

The Save path was changed in commit `430c39d` to use the source path so
that the build system's `POST_BUILD "Copying assets/ (clean)"` step
doesn't wipe the saved file. But the read paths were left on the
runtime path:

- `WaveformView::onLoadPressed_` uses `resolveRelativePath`
  (`src/app/WaveformView.cpp:392`).
- `PluginProcessor::tryAutoLoadGspeak_` uses `resolveRelativePath`
  (`src/app/PluginProcessor.cpp:253`).

Result in a single dev-build session: Save → source dir gets the tuned
file → navigate to a different scene → return to scene 0 → auto-load
reads the bundle copy → the bundle copy is still the pre-Save version
until the next build's POST_BUILD copy → user sees their edits gone.
Manually pressing Load behaves the same way for the same reason.

### 2.2 Fix

Symmetric path resolution. Add a small helper:

```cpp
// AssetLocator.h
static std::string resolveForRead(const std::string& relPath);
```

```cpp
// AssetLocator.cpp
std::string AssetLocator::resolveForRead(const std::string& relPath) {
    auto src = resolveSourceRelativePath(relPath);
    if (!src.empty() && std::filesystem::exists(src))
        return src;
    return resolveRelativePath(relPath);
}
```

The `exists` check is load-bearing: without it, a missing source file
would silently fall through to the bundle copy, which masks the
"source dir found but file not there" case (e.g. user deleted the
saved file). With it, the resolver explicitly chooses — source wins
only when the file is actually there.

Then change the two read sites:

- `WaveformView::onLoadPressed_` — `resolveRelativePath` →
  `resolveForRead`.
- `PluginProcessor::tryAutoLoadGspeak_` — `resolveRelativePath` →
  `resolveForRead`.

`onSavePressed_` is unchanged (already prefers source).

### 2.3 Behavior matrix

| Build type | Source dir | Source file | Read picks |
|---|---|---|---|
| Dev build, never Saved | found | absent | runtime (bundle) |
| Dev build, Saved this session | found | present | **source** |
| Dev build, file deleted by user | found | absent | runtime (bundle) |
| Installed AU / standalone .app | not found | n/a | runtime (bundle) |

The "Installed AU / standalone .app" row is what runs in Logic at a
performance. The source-tree dir simply isn't there (no `build/`
sibling); `resolveSourceRelativePath` returns empty and the runtime
path is used as today.

### 2.4 Testing

Unit (`tests/unit/app/test_asset_locator.cpp` — extend existing if
present, else new):

- `resolveForRead` returns the source path when both the source dir
  and source file exist.
- `resolveForRead` returns the runtime path when the source dir is
  found but the source file is absent.
- `resolveForRead` returns the runtime path when the source dir is
  not found.

Integration (`tests/integration/test_gspeak_save_then_load.cpp` — new):

- Set up a fixture scene with `gspeakPath` and `gspeakAutoLoad: true`.
- Bake an initial clip, install it, call the Save entry point.
- Switch active scene to a different fixture, then back to the gspeak
  scene.
- Observe that the auto-loaded clip's samples and boundaries match
  what was Saved (not the older bundle copy).

## 3. mp3 import

### 3.1 UX flow

Two new buttons in `WaveformView`, placed at the head of the existing
button row:

```
[Import] [Auto-slice] [Save] [Load]
```

**Import** — file picker for `*.mp3;*.wav;*.aif;*.aiff;*.flac`. On a
successful decode, installs the file as a single-span v1 clip; the
WaveformView immediately renders the waveform, and the vocoder can be
exercised against it via note triggers.

**Auto-slice** — reads SayPanel's current text, parses words and
syllables, runs `WordAligner::alignSyllables`, installs the resulting
boundary set onto the existing in-memory clip. Re-runnable after
editing the text. Disabled when SayPanel is empty or the clip has no
audio.

Workflow for the user picking between brit-1 and brit-2:

1. Click **Import**, pick `11labs-gently-speaks-brit-1.mp3`. The clip
   plays from end to end on a note trigger; vocoder applies.
2. Type the hyphenated transcript in SayPanel
   (`"I look at the world and I no-tice it's turn-ing…"`) — same
   hyphenation convention scene 0 already uses.
3. Click **Auto-slice**. Per-syllable boundaries appear.
4. Drag boundaries in WaveformView to clean up. Click **Save**.
   Persistence fix (§2) means the saved version is what auto-load
   surfaces on re-entry, no rebuild required.
5. To compare with brit-2: Import the second mp3, repeat Auto-slice +
   tune, Save. The previous tune is overwritten in `scene10.gspeak`.
   (Save As / multi-slot are out of scope this round, see §6.)

### 3.2 New code

`src/audio/AudioFileDecoder.{h,cpp}`:

```cpp
namespace guitar_dsp::audio {

class AudioFileDecoder {
public:
    struct Result {
        std::vector<float> samples;  // mono, at requestedSampleRate
        double sampleRate = 0.0;
        std::string formatName;       // "WAV", "MP3", etc. (informational)
    };

    // Decodes the file to mono PCM at requestedSampleRate. Returns
    // nullopt on unsupported format, decode error, or empty audio.
    static std::optional<Result> decodeMono(const juce::File& file,
                                            double requestedSampleRate);
};

} // namespace guitar_dsp::audio
```

Implementation reuses the JUCE `AudioFormatManager::registerBasicFormats`
path already used by `PrebakedTTSSource.cpp:29` and
`GspeakBundle.cpp:236`. Downmix to mono by averaging channels.
Resample with the same linear-interp loop in
`PrebakedTTSSource.cpp:50-64`.

### 3.3 Changed code

- `src/app/WaveformView.{h,cpp}` — add the two buttons; add
  `onImportPressed_` and `onAutoSlicePressed_` handlers; lay out the
  full button row.
- `src/app/PluginProcessor.{h,cpp}` — expose
  `void installImportedClip(audio::TTSClipPtr clip)` that mirrors the
  state setup `tryAutoLoadGspeak_` already does for v1 clips
  (PluginProcessor.cpp:297-305): clear `clipBankPlayer().setBank({})`,
  `setModulatorSource(NoteStepped)`,
  `setActiveSpeechPlayer(NoteStepped)`, `noteSteppedPlayer().setLoop(true)`,
  then `installEditedV1Clip(clip)`. Used by Import and Auto-slice so
  the wet path is correct after each.

The reason for a dedicated `installImportedClip` (instead of having
the view assemble the same calls) is that `tryAutoLoadGspeak_` has the
canonical wiring; pulling it into a small named method keeps the two
sites in sync and the responsibility on the processor where it
belongs.

### 3.4 Import handler

```
onImportPressed_:
  Open juce::FileChooser, filter: "*.mp3;*.wav;*.aif;*.aiff;*.flac"
  On cancel → return.
  Disable [Import][Auto-slice] (state flag).
  Launch a juce::Thread:
    auto result = AudioFileDecoder::decodeMono(picked, engineSR);
    Post to message thread:
      Re-enable [Import][Auto-slice].
      If !result → flashStatusMessage("Import failed: <reason>", 3000); return.
      Build v1 clip:
        clip.samples     = result.samples
        clip.sampleRate  = engineSR
        clip.text        = processor_.currentSayText()   // informational
        clip.words       = { { "imported", 0, samples.size() } }
        clip.syllables   = clip.words   // single full-length span
      processor_.installImportedClip(clip)
      flashStatusMessage("Imported " + filename, 1500)
```

Threading rationale: JUCE's basic-format decoders are fast on small
files (a 10-second mp3 decodes in ~30 ms on M-series), but the
juce::FileChooser already implies an async boundary (modal on macOS),
and the off-thread decode is cheap insurance against pathological
inputs locking the message thread.

Concurrency invariant: a second Import click while one is in flight is
ignored (buttons disabled). No queuing.

### 3.5 Auto-slice handler

```
onAutoSlicePressed_:
  If !clip_ or clip_->samples.empty() → return (button should be disabled).
  text = processor_.currentSayText()
  If text.empty() → flashStatusMessage("Auto-slice failed: no text", 3000); return.

  (words, hyphenatedForms) = parseTranscript(text)
  // parseTranscript: split on whitespace; for each token strip leading/
  // trailing punctuation; hyphenatedForms[i] = raw token (hyphens intact);
  // words[i] = hyphens removed.

  syls = WordAligner::alignSyllables(clip_->samples, words, hyphenatedForms, clip_->sampleRate)
  If syls.empty() → flashStatusMessage("Auto-slice failed: aligner returned empty", 3000); return.

  Build v1 clip:
    new.samples     = clip_->samples            // unchanged
    new.sampleRate  = clip_->sampleRate
    new.text        = text
    new.words       = groupByOriginalWord(syls, words.size())
    new.syllables   = syls

  processor_.installImportedClip(new)
  flashStatusMessage("Auto-sliced " + |words| + " words / " + |syls| + " syls", 1500)
```

`groupByOriginalWord` walks the syllable list, consuming N consecutive
syllables per original word where N = (count of hyphens in
`hyphenatedForms[i]`) + 1 — i.e. the number of hyphen-bounded
fragments. This matches `WordAligner::alignSyllables`'s own emission
order: "within each word's range, splitting into N equal-duration
sub-segments where N is the count of hyphen-bounded fragments". For
each word, `words[i]` = first syl's `startSample` to last syl's
`endSample`.

### 3.6 Button enablement

- **Import** — enabled whenever the scene is active and the view has a
  processor. Disabled only during an in-flight import.
- **Auto-slice** — enabled iff `clip_ != nullptr && !clip_->samples.empty()`
  and the SayPanel text is non-empty. Re-evaluated on clip change and
  on SayPanel text change.
- **Save** — unchanged (existing rules: clip present, `gspeakPath`
  non-empty).
- **Load** — same enablement (existing rules: `gspeakPath` non-empty).
  Internal path resolution changes from `resolveRelativePath` to
  `resolveForRead` per §2.2 — no user-visible enablement difference.

### 3.7 SayPanel text source

The "current SayPanel text" is the same string fetched by Save today
via `processor_.currentSayText()`. We do not modify SayPanel on Import
— if the user wants the SayPanel to reflect the imported clip's
transcript, they type it there before Auto-slice. This matches the
clean separation already present: SayPanel is the canonical text
source, Auto-slice and Save both consume it.

## 4. Error handling

| Condition | Behavior |
|---|---|
| Import file picker cancelled | No-op. |
| Import unsupported format | `Import failed: unsupported audio format` (3s). |
| Import decode error | `Import failed: decode error` (3s). |
| Import empty audio | `Import failed: empty file` (3s). |
| Import during decode in flight | Buttons disabled; click ignored. |
| Auto-slice no clip | Button disabled. |
| Auto-slice no text | `Auto-slice failed: no text` (3s). |
| Auto-slice aligner returned empty | `Auto-slice failed: aligner returned empty` (3s). |
| Save (existing) | Unchanged. Persistence fix means the saved file is what subsequent Loads see. |
| Load (existing) | Unchanged contract. Now reads from source dir first when present. |

In every failure path, the in-memory clip is left untouched. Atomic
install-or-skip — same invariant as `tryAutoLoadGspeak_`.

## 5. Testing

### 5.1 Unit

`tests/unit/audio/test_audio_file_decoder.cpp` (new):

- Decode a fixture stereo `.wav` at engine SR → returns mono samples
  of the expected length.
- Decode at a different target SR (44.1 vs 48 kHz) → length scales by
  `targetSR / fileSR` within ±1 sample.
- Decode a malformed file → returns nullopt without throwing.
- Decode a missing file → returns nullopt.
- Decode an empty (0-sample) file → returns nullopt.

`tests/unit/app/test_asset_locator.cpp` (extend or new):

- See §2.4.

### 5.2 Integration

`tests/integration/test_gspeak_save_then_load.cpp` (new):

- See §2.4.

`tests/integration/test_mp3_import_flow.cpp` (new):

- Decode a fixture `.wav` (we don't ship an mp3 fixture; basic-format
  decoders cover wav identically and the format-detection path is the
  same code).
- Simulate Auto-slice against a fixture audio + transcript pair →
  assert non-empty word and syllable arrays, monotonic boundaries,
  last syllable end equals sample count, and word count equals the
  transcript word count.
- Save → Load round-trip on the imported+sliced clip preserves
  samples and boundaries (variant of the existing v1 round-trip
  test, but starting from a decoder-produced clip instead of a
  synthesized one).

### 5.3 Manual

Live performance dress rehearsal:

1. Open scene 10. Click **Import**, pick
   `11labs-gently-speaks-brit-1.mp3`. Single-span clip plays through
   the vocoder when a note triggers.
2. Type the brit-1 transcript in SayPanel with hyphens. Click
   **Auto-slice**. Boundaries appear.
3. Drag boundaries in WaveformView to clean up. Click **Save**.
4. Switch to a different scene and back to scene 10. Auto-load
   surfaces the saved clip (validates the persistence fix).
5. Restart the app. Auto-load surfaces the saved clip on activation.

## 6. Out of scope (explicit)

- Multi-slot gspeak per scene; in-UI selector between bundles.
- Save As / Load From file pickers.
- Force-alignment (Whisper, MFA) for accurate cross-recording phoneme
  timing.
- Pitch-singing (v2) support for imported clips. v1 only this round.
- Auto-hyphenation of the SayPanel text. User-supplied hyphens.
- Auto-slice on Import (the two are intentionally separate so each
  has crisp success / failure semantics).
- Stereo or multi-channel preservation. Downmix to mono.
- Storing the source mp3 filename in the manifest. Manifest carries
  `text` and `clipKind` only; the audio is opaque PCM after decode.
- Generalized mp3 import on scenes other than 10. Scene 10's existing
  `gspeakPath` slot is the only target this round.
- The two `*.mp3` files at the repo root. They are inputs the user
  may keep, move to `assets/raw/`, or delete; nothing at runtime
  references them.

## 7. Files touched

New:

- `docs/superpowers/specs/2026-06-19-mp3-import-and-gspeak-persistence-design.md` (this).
- `src/audio/AudioFileDecoder.{h,cpp}`
- `tests/unit/audio/test_audio_file_decoder.cpp`
- `tests/integration/test_gspeak_save_then_load.cpp`
- `tests/integration/test_mp3_import_flow.cpp`

Modified:

- `src/app/AssetLocator.{h,cpp}` — add `resolveForRead`.
- `src/app/WaveformView.{h,cpp}` — Import + Auto-slice buttons,
  handlers, layout, enablement.
- `src/app/PluginProcessor.{h,cpp}` — `installImportedClip` helper.
- `src/app/PluginProcessor.cpp` — switch `tryAutoLoadGspeak_` to
  `resolveForRead`.
- `src/app/WaveformView.cpp` — switch `onLoadPressed_` to
  `resolveForRead`.
- `tests/unit/app/test_asset_locator.cpp` — add `resolveForRead`
  cases (or create file if not present).
- `CMakeLists.txt` — add the new source and test files.

No scene-JSON schema changes. No `.gspeak` format changes.
