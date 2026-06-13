# "Developers!" Scene (Scene 1) — Design Spec

**Status:** Approved for planning
**Date:** 2026-06-13
**Parent specs:**
- [`2026-05-29-while-my-guitar-gently-speaks-design.md`](2026-05-29-while-my-guitar-gently-speaks-design.md)
- [`2026-05-31-note-triggered-speech-design.md`](2026-05-31-note-triggered-speech-design.md) (Phase 5a — reused as-is)

## 1. Goal

Replace Scene 1 (currently "Carousel — choir / pad") with a live recreation of
Steve Ballmer's "Developers, developers, developers!" moment. Each guitar pluck
fires the next "DEVELOPERS!" in the original chronological sequence; the natural
Ballmer crescendo IS the intensity ramp. The guitar speaks each "DEVELOPERS!"
through the existing channel vocoder, with a small dry layer of Ballmer's actual
voice mixed underneath for grit.

A general visual upgrade to `WordReadout` shows the performer (and audience)
exactly where they are in the chant: a row of pips plus a font-size + color
ramp that mirrors the audio crescendo. The upgrade benefits all
note-triggered scenes (1, 7, 8) — not Scene 1 only.

**Design principle:** zero new audio code. Everything below routes through the
Phase 5a machinery already on `main`. The only net-new code is a UI tweak
(`WordReadout`) and an offline asset-prep script (Python).

## 2. Mechanism / data flow

```
Scene 1 activate (FCB pedal / MIDI / keyboard)
   ─► PluginProcessor scene-change callback (existing)
       ├─ PrebakedTTSSource loads assets/tts/01_developers/audio.wav
       ├─ WordAligner splits 14-word text on 13 silence gaps → 14 WordSegments
       ├─ NoteSteppedTTSPlayer.setClip(seg)
       │  + modulatorSource = NoteStepped
       ├─ Mixer: masterGainDb -3, dryWet 0.9, transitionMs 30
       └─ Vocoder.setClarity(0.25)   (75 % vocoded + 25 % dry Ballmer)

Each guitar pluck (existing pipeline)
   ├─ OnsetDetector fires (clean InputStage)
   ├─ NoteSteppedTTSPlayer advances wordIndex_, plays segment as modulator
   ├─ Carrier = guitar; ChannelVocoder produces "guitar speaks DEVELOPERS"
   ├─ Clarity blends 25 % dry-Ballmer-modulator into the wet bus
   └─ currentWordIndex_ (atomic) updates → WordReadout repaints
```

After segment 14, `NoteSteppedTTSPlayer` wraps to 0 (existing behavior). To
prevent a sudden drop from peak intensity back to calm, the **chop script
duplicates the two loudest bursts at the end** of the concatenated WAV — so
segments 13 and 14 are dupes of the peak. Wrap then resets to the original
opening; one full cycle is calm → crescendo → peak → peak → calm again.

## 3. Net-new files

### 3.1 `assets/tts/01_developers/audio.wav` (gitignored)
The Ballmer chant concatenated chronologically with ~100 ms silences between
each "DEVELOPERS!". 14 bursts total (12 from the original + 2 duplicated
peak bursts at the end — see §2). Mono, 22.05 kHz, ~25 s. **Not committed**:
we do not redistribute Ballmer's audio from this repo. The build script
regenerates it locally per machine.

### 3.2 `assets/tts/01_developers/meta.json` (committed)
```json
{
  "text": "DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS!",
  "voice": "ballmer",
  "duration_s": 25.0
}
```
14 word tokens (whitespace-split — `!` stays attached, one token).
Capitalized + exclamation so the `WordReadout` displays "DEVELOPERS!" rather
than lowercase. Duration is informational (existing `meta.json` files in
`assets/tts/` follow the same shape — see `06_hello_cleveland/meta.json`).

### 3.3 `scripts/build_developers_clip.py` (committed)
Offline Python script the user runs once per machine.

- **Invocation:** `python scripts/build_developers_clip.py <path-to-ballmer-source.wav>`
- **Behavior:** reads the source file; for each of 14 hand-tuned
  `(start_s, end_s)` segments baked into a constant list at the top of the
  script, slices the source; pads each slice with a 100 ms tail silence;
  concatenates in order; writes mono 22.05 kHz PCM to
  `assets/tts/01_developers/audio.wav`.
- **Hand-tuned timestamps:** the 14 `(start, end)` pairs are committed in
  the script. They are derived once against the canonical source clip
  (the iconic Microsoft conference moment) and yield clean, gap-separated
  bursts so the `WordAligner` finds 13 obvious silence gaps.
- **Peak duplication:** the last two entries in the timestamp list are the
  two loudest bursts repeated — implemented purely as repeated entries, no
  special-case logic.
- **Validation:** the script asserts the source is mono or stereo PCM at
  ≥ 22.05 kHz and emits a clear error if a slice extends past the source
  duration (helps the user notice if they downloaded the wrong cut).
- **No git operations, no network access.** Pure file in → file out.

### 3.4 `assets/scenes/01_developers.json` (committed; replaces `01_carousel_choir.json`)
```json
{
  "id": 1,
  "name": "Developers!",
  "color": "#0078D4",
  "mixer": { "masterGainDb": -3.0, "dryWet": 0.9, "transitionMs": 30 },
  "tts": {
    "source": "prebaked",
    "clip": "01_developers",
    "text": "DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS! DEVELOPERS!",
    "trigger": "note",
    "clarity": 0.25
  }
}
```
`01_carousel_choir.json` is **deleted** in the same change. (The carousel-choir
sound is preserved in git history if it's ever wanted back.)

### 3.5 README acquisition note
A short section in the project `README.md` documenting how to obtain the
source clip (e.g. `yt-dlp <known-url> -o ballmer_source.wav`) and run the
build script. No URL is committed — the user is expected to source the clip
themselves; the script just slices whatever you feed it.

### 3.6 `.gitignore` entry
Add `assets/tts/01_developers/audio.wav` to `.gitignore`. The directory
itself (with `meta.json`) is committed; only the WAV is excluded.

## 4. Visual indicator — `WordReadout` upgrade (general, not Scene-1-only)

Pure UI work in `src/app/WordReadout.{h,cpp}`. Driven entirely by the
existing `PluginProcessor::currentSpokenWordIndex()` (atomic) and
`PluginProcessor::activeSceneWords()` (vector). The 30 Hz `Timer` that
already polls and triggers `repaint()` on index change stays.

### 4.1 Existing layout (kept)

`WordReadout::paint` today fills a dark background and draws three text
slots horizontally:
- left third → previous word (gray, 18 pt)
- middle third → current word (gold `#F0E6B4`, 34 pt bold)
- right third → next word (gray, 18 pt)

The `prev | CURRENT | next` shape is preserved. The intensity ramp
(below) replaces the static gold + 34 pt center slot. The prev/next gray
slots are unchanged.

### 4.2 New: pip row at the bottom

A new horizontal strip below the three-slot row, ~14 px tall, spanning
the full readout width. Drawn last (on top of the dark fill, below the
text rows — paint order: fill → text rows → pips).

- One small filled circle per word — `N` circles evenly distributed
  across the readout width (no scrolling: Scene 1 has 14, Scene 7/8 have
  ~5, all fit; circle diameter = `min(10, stripHeight * 0.7)`).
- Pip at `currentIndex` → solid-bright at the scene color (full alpha).
- Pips with index `< currentIndex` (completed) → scene color at 0.45 alpha.
- Pips with index `> currentIndex` (upcoming) → scene color at 0.15 alpha.
- When `currentIndex == -1` (idle / no scene words / no clip) → all
  pips at 0.15 alpha; the "(pluck a note to speak)" idle message in the
  existing paint code still draws above.

### 4.3 New: intensity ramp on the center (current) word

Replaces the static gold + 34 pt center draw. Drives a font-size and
color ramp from `progress = currentIndex / max(1, N - 1)`:

- **Font size:** `34.0f * (1.0f + 0.6f * progress)` — so 34 pt at the
  first pluck, ~54 pt at the last. (Capped at center-slot height so it
  never overflows.)
- **Color:** lerp from the scene color at `progress = 0` to `#FF3030`
  (hot red) at `progress = 1`. Implementation: `sceneColor.interpolatedWith
  (juce::Colour::fromRGB(0xFF, 0x30, 0x30), progress)` — JUCE handles
  channel math.
- Idle (`-1`) → no center draw, idle hint shown (existing behavior).

### 4.4 Scene-color source

The scene color is exposed via a new
`PluginProcessor::activeSceneColorRgb()` getter (uint32 RGB; reads
`sceneEngine_.activeScene().colorRgb`). `WordReadout::paint` reads it
each repaint. Idle / no scene → falls back to a neutral mid-gray.

### 4.5 Hard-coded constants and intentional non-customization

The ramp coefficients (`0.6f` size multiplier, `0.45f / 0.15f` pip
alphas, `#FF3030` peak color, pip-strip height) are hard-coded constants
near the top of `WordReadout.cpp` — easy to tune without changing the
data flow. **No new scene-config fields and no per-scene customization**:
keeping this a uniform global upgrade is intentional. A future spec can
add per-scene tuning if the ramp needs to differ for the speaking scenes
vs. the chant.

Audio thread is untouched.

## 5. Scope of net-new code

| Area | Files | Lines (estimate) |
| --- | --- | --- |
| Asset prep (Python) | `scripts/build_developers_clip.py` | ~80 |
| Asset metadata | `assets/tts/01_developers/meta.json` | ~5 |
| Scene config | `assets/scenes/01_developers.json` (+ delete old) | ~14 |
| UI tweak | `src/app/WordReadout.{h,cpp}` + tiny `PluginProcessor` getter | ~80 |
| Tests | scene fixture + WordReadout unit test | ~80 |
| Docs | README section, `.gitignore` line | ~15 |
| **Total** | | **~250 LoC, no audio-thread changes** |

## 6. Error handling

- **Missing `01_developers/audio.wav`** → existing `PrebakedTTSSource` logs
  `[PrebakedTTSSource] missing: ...` and returns nullptr → scene activates
  but produces silence on the wet path. Acceptable failure mode: user
  re-runs the build script. README documents this.
- **Source clip wrong length / wrong cut** → build script slice goes past
  the source end → script aborts with a clear error pointing at the
  offending segment. README mentions this as the most likely setup issue.
- **`WordAligner` fails to find 13 clean gaps** → falls back to even
  boundary distribution (existing degenerate handling in Phase 5a). The
  100 ms silences engineered into the concatenated WAV make this scenario
  unlikely; tests assert it.
- **`currentWordIndex_ == -1` (no clip / idle)** → `WordReadout` shows the
  word at base size + scene color, all pips dim. No crashes on Scene 0
  ("Clean intro"), no division by zero on `N == 0` or `N == 1`.

## 7. Testing

- **Scene JSON parse** — add `tests/fixtures/scenes/with_developers.json`
  (a copy of `assets/scenes/01_developers.json`). Test asserts:
  - `name == "Developers!"`, `id == 1`, `color == 0x0078D4`
  - `tts.source == "prebaked"`, `tts.clip == "01_developers"`,
    `tts.trigger == "note"`, `tts.clarity == 0.25f`
  - `tts.text` splits to exactly 14 word tokens
  Follows the pattern of `tests/fixtures/scenes/with_tts_trigger.json` and
  the corresponding `SceneLibraryTests`.
- **WordAligner on synthetic 14-burst clip** — generate a synthetic mono
  clip of 14 tone bursts separated by 100 ms silences (mirroring the
  shape of the produced WAV); feed to `WordAligner::align` with 14 word
  tokens; assert exactly 14 segments returned with boundaries that fall
  inside the gaps and in monotonically increasing order.
- **Build script** — Python `unittest` that writes a synthetic source WAV
  containing 14 marker tones at known offsets, calls
  `build_developers_clip.main(<that path>)` with a stub timestamp list
  pointing at those offsets, then verifies the output WAV has 14 segments
  with the expected per-segment durations (within one frame) and the
  correct total length. Also one negative test: a timestamp past the
  source end → script aborts with the documented error.
- **`WordReadout` paint** — unit test using `juce::Image` + a software
  renderer (the project's existing UI test pattern). Cases:
  - `index == 7, N == 14, sceneColor = #0078D4` → pip row has 7 alpha-
    0.45 pips + 1 full-alpha + 6 alpha-0.15 pips; center word bounding
    box ~1.32× base height (44.9 pt); sampled center text color ~halfway
    between `#0078D4` and `#FF3030`.
  - `index == -1, N == 0` → no pips drawn, idle hint shown, no crash.
  - `index == 0, N == 1` → one full-alpha pip, base size (34 pt), scene
    color (`progress = 0`, no ramp).
  - `index == 0, N == 14` → first pip full-alpha + 13 dim, base size,
    scene color (sanity: opening pluck looks calm).
- **No regression** — existing `Scene 7/8` integration tests still pass
  and now exercise the upgraded `WordReadout` paint path. Any visual
  assertions that pinned the old non-ramped behavior get loosened to
  accept the new pips + ramp.

## 8. Out of scope

- **No new audio mechanisms.** No sample-bank player. No per-pluck
  parameter automation. No crowd-layer sample. The chronological-chop
  ramp + clarity blend are sufficient.
- **No `WordReadout` spectrogram backdrop** — that remains Phase 5b.
- **No engine-true word timestamps** (`WordSegment.timeSrc`) — energy-gap
  segmentation is sufficient given the hand-tuned silences in the WAV.
- **No per-scene customization of the visual ramp.** The 1.0× → 1.6×
  size, the alpha steps, and the peak color are global constants. Future
  spec can add per-scene tuning if needed.
- **No automated source-clip download.** The build script does not call
  `yt-dlp` or any network tool. README documents the manual step.
- **No second "Developers!" scene** at another slot. One Scene 1, that's it.

## 9. Risks & mitigations

- **Audio acquisition.** The iconic Ballmer clip is a copyrighted Microsoft
  conference recording. Mitigation: do not ship the WAV in the repo
  (gitignored); the user obtains the source clip themselves; the build
  script transforms only what's on disk. Live demo / parody / fair-use
  posture, not redistribution.
- **`WordAligner` mis-segmentation on real Ballmer audio.** Even
  hand-tuned, audience clapping / crowd noise can fill gaps. Mitigation:
  100 ms zero-padding between bursts is engineered into the WAV by the
  build script — silence floor is true digital zero, gap detection
  becomes trivial. Tests cover the synthetic case; one manual review
  pass on the real WAV after first generation.
- **Wrap-around feel.** After segment 14, the player wraps to segment 0
  (calm opening). Mitigation: peak duplication at segments 13/14 means
  the wrap moment goes "peak → peak → calm" — the calm becomes a
  deliberate cooldown, not a dropped climax. For a conference demo, also
  acceptable: the performer pedal-switches scenes before wrap if they
  want to stay at peak.
- **`WordReadout` ramp on short-word scenes (7, 8).** Scene 7's text is
  five words; the size ramp peaks immediately. Mitigation: the ramp is
  driven by `progress`, which still goes 0 → 1 across however-many words
  exist; this looks dramatic on short phrases, which is fine. Tests
  pin both N=14 and N=5 behaviors.
- **Scene-color → red lerp on dark scene colors.** Scene 1 is Microsoft
  blue (`#0078D4`); the lerp to `#FF3030` reads cleanly. For other
  note-triggered scenes whose color happens to be near red, the ramp
  effect will be subtle; not a bug, accept as-is.
