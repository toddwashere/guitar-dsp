# guitar-dsp

Standalone macOS audio app used as the live instrument for the tech-conference talk *While My Guitar Gently Speaks*. Live guitar audio is processed into transformed timbres and into "speech" via a vocoder driven by text-to-speech, with scene switching via a Behringer FCB1010 MIDI foot controller.

See [`docs/superpowers/specs/2026-05-29-while-my-guitar-gently-speaks-design.md`](docs/superpowers/specs/2026-05-29-while-my-guitar-gently-speaks-design.md) for the full design.

## Build

Requirements: macOS 13+, Xcode 15+, CMake 3.22+, Ninja.

```bash
git clone --recurse-submodules <repo-url>
cd guitar-dsp
cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target guitar_dsp_app_Standalone
open "build/src/app/guitar_dsp_app_artefacts/Release/Standalone/Guitar DSP.app"
```

## Run tests

```bash
cmake --build build --target guitar_dsp_tests
ctest --test-dir build --output-on-failure
```

To regenerate golden-file fixtures after an intentional DSP change:

```bash
GUITAR_DSP_REGENERATE_GOLDENS=1 ctest --test-dir build --output-on-failure -R golden
```

## Conversational AI (new)

Press a foot pedal, speak, the guitar replies. Works in both the standalone app and inside Logic Pro (AU plugin, sidechain mic). Local Whisper STT, dual LLM backend (Anthropic cloud + Ollama local — pick from a dropdown), six persona presets with editable system prompts.

Bundle size grows by ~150 MB for the included Whisper model.

See [`docs/au-logic-setup.md`](docs/au-logic-setup.md) for the one-time per-project sidechain routing in Logic Pro and global setup (API key, Ollama).

## Audio Unit (Logic Pro)

The same app builds as an AUv2 plugin alongside the standalone:

    cmake --build build   # builds Standalone + AU; COPY_PLUGIN_AFTER_BUILD installs the .component

The `.component` is auto-installed to `~/Library/Audio/Plug-Ins/Components/`. It is
a MIDI-capable **Music Effect** (AU type `aumf`), so in Logic it appears under the
Audio Units for **Todd B Fisher > Guitar Speak** — insert it on a guitar track; it
accepts host MIDI for scene control.

**Recording the processed/spoken output:** Bounce in Place the track, or route the
track to a bus and record that bus onto another track.

**Dev workflow (iterate fast, stay out of Logic):**
- Do most work in the **Standalone** (rebuild + relaunch in seconds) and the test suite.
- Validate the AU without Logic:
  - `auval -v aumf GtSp TdBF`
  - `pluginval --strictness-level 10 --validate-in-process "<path-to>/Guitar Speak.component"`
    (install via `brew install --cask pluginval`). Note: pluginval's embedded `auval`
    sub-test can time out on headless Apple-TTS synthesis — its own in-process tests
    are the stability signal.
- Logic caches the AU in-process — a rebuilt binary needs a **Logic restart** (or
  remove + re-insert) to load. Only open Logic at milestones.
- AUv2 runs in-process, so a plugin crash can take Logic down. Always pass pluginval's
  stress tests before trusting it in a live session.

**MIDI scene control in the plugin:** route the FCB1010 (Program Change) to the
plugin's track in Logic; Program Change *n* selects scene *n*.

### Pitch-tracked singing carrier (toggle)

The vocoder can switch its carrier-floor source from broadband noise to a
pitched sawtooth that tracks the guitar's note ("singing" voice). The toggle
is bound to **MIDI CC#80**, value >= 64. The FCB1010 doesn't send CC#80 in
its stock programming - reprogram one switch (any free pedal) to send
`Controller 80, value 127` on press. The toggle persists in the plugin state.

You can also click the **P  Pitch sing** pill in the diagnostic toggle bar.
The pitch readout below the vocoder sliders shows the detected note + cents
+ Hz whether the toggle is on or off, so you can see what the algorithm has
locked to.

To pick a different CC, set `pitchSingingToggleCc` in your FCB mapping JSON.

**Apple TTS in a plugin host:** `AVSpeechSynthesizer` needs a pumped main run loop to
deliver its audio callbacks. Logic provides one during playback, so Apple TTS works;
fully headless hosts (e.g. `auval`) do not, and those scenes fall back to prebaked
clips. Piper and prebaked clips work regardless.

### Sing mode

A complementary toggle layered on top of pitch-singing that nudges the
output from "spoken-at-a-pitch" toward "sung":

- **Vibrato**: 5 Hz sine LFO, +/- 20 cents — adds expressive wobble.
- **Pitch quantize**: snaps the detected F0 to the nearest chromatic
  semitone, so slightly sharp/flat plucks still sing in tune.

Click the **M  Sing** pill in the diagnostic toggle bar to enable.
Persisted in plugin state. Works whether pitch-singing is on or off
(but only audible while pitch-singing is on, since that's what routes
the pitched saw into the carrier).

### Word-sync modes for note-triggered speech

When a TTS scene uses `"trigger": "note"`, you can choose how guitar
onsets drive the speech. The selector below the vocoder sliders has
three options:

- **Latch (default)** — one word per onset, hard latch. The current
  word plays to completion before the next onset advances. Most
  reliable 1:1 mapping.
- **Advance** — every onset advances and restarts the next word.
  Responsive but can cut multi-syllable words.
- **Syllable** — requires the scene's `text` to include hyphens
  (e.g. `"gui-tar gent-ly"`). Each hyphen-bounded fragment becomes
  one syllable segment; onsets step through syllables.

A scene can override the global UI selection via its TTS config:

`"wordSync": "latch" | "advance" | "syllable" | "global"` (default
`"global"`).

If a scene requests Syllable mode but its text has no hyphens, it
falls back to Latch on words.

**When the conversational AI flow lands (future spec):** when Syllable
mode is active for the response output, the LLM's system prompt must
include guidance to hyphenate multi-syllable words. Without that, the
AI response will use the Latch fallback and lose the syllable-level
sync.

#### Rewind

If the spoken sequence drifts out of sync with what you're playing —
WordAligner produced bad boundaries, you missed a pluck, you switched
modes mid-phrase — click the small **Rewind** pill in the top-right
corner of the word-readout strip. The next onset plays segment 0
again. Real-time-safe (atomic pending flag, drained by the audio
thread on the next block).

#### Hand-authored syllable timings for prebaked clips

The default `WordAligner` is an energy-gap heuristic that needs clear
silences between words. Prebaked TTS clips with continuous prosody
often lack them, so syllable mode can drift (especially at the start
of a phrase).

When this matters for a demo phrase, override the heuristic by adding
`syllableTimingsMs` to the clip's `meta.json`:

```json
{
  "text": "And now my gui-tar gent-ly speaks for me.",
  "voice": "...",
  "duration_s": 2.155854,
  "syllableTimingsMs": [
    [   0,  150],
    [ 150,  310],
    [ 310,  460],
    [ 460,  710],
    [ 710,  960],
    [ 960, 1180],
    [1180, 1340],
    [1340, 1720],
    [1720, 1900],
    [1900, 2156]
  ]
}
```

Array length must match the syllable count (hyphen-split tokens in
`text`). When present, `PrebakedTTSSource` populates
`clip->syllables` directly and the aligner is skipped. Refine values
by ear: load `audio.wav` in Audacity, mark boundaries, edit the
numbers. No rebuild — re-activate the scene to pick up changes.

## Scenes

### Scene 1 — "Developers!" asset

Scene 1 plays Steve Ballmer's "DEVELOPERS!" chant from the 2000 Microsoft
developer conference, one burst per guitar pluck. The audio file is not
distributed with the repo. Generate it locally:

1. Obtain a clean recording of the iconic ~25 s chant as `ballmer_source.wav`
   (mono or stereo, 16-bit PCM, ≥ 22.05 kHz). The source is on YouTube;
   `yt-dlp <url> -x --audio-format wav -o ballmer_source.wav` works.
2. Run the chopper:
   ```bash
   python3 scripts/build_developers_clip.py path/to/ballmer_source.wav
   ```
3. The script writes `assets/tts/01_developers/audio.wav`. This file is
   gitignored — re-run after fresh clone or asset edit.

If your source's timing differs from the default chops, edit `SEGMENTS_S`
at the top of `scripts/build_developers_clip.py` (14 `(start_s, end_s)`
tuples; the last two are intentional peak duplicates).

## Project status

This branch implements **Phase 5a: note-triggered word-by-word speech** — the
core "While My Guitar Gently Speaks" effect, presented as a live progression of
how the speaking guitar evolved:

- **Scene 6 — whole clip (the "before"):** activating the scene plays the entire
  TTS phrase through the vocoder, as in Phase 3. The original speak-on-activate.
- **Scenes 7 & 8 — word-by-word (the "after"/finale):** the phrase is pre-split
  into per-word audio segments and **each plucked note speaks the next word**,
  auto-looping after the last word. Nothing speaks until you play, so the
  performer paces the whole sentence note by note.

The whole-clip scene sits at a lower number than the word-by-word scenes, so
stepping up the FCB1010 walks the audience through the evolution. Pieces:
`audio::OnsetDetector` (note-attack detection on the clean guitar),
`audio::WordAligner` (uniform energy-gap word segmentation — same for all three
TTS backends), and `audio::NoteSteppedTTSPlayer` (one word per onset, feeding the
vocoder modulator). A scene's `tts.trigger` selects `"auto"` (whole clip — the
default, preserving Phase-3 behavior) vs `"note"` (word-by-word). A minimal
on-screen word readout shows the current word.

### Instrument Carousel (Phases 4 + 4b)

Carousel scenes 1–5 process the live guitar through a pedal-style effects chain
(`audio::Carousel`) — no MIDI notes; each scene reshapes timbre via a
parameterized chain:

| Key | Scene | Effect |
|-----|-------|--------|
| `2` | 1 | Choir / pad (harmonizer + vowel formant + big reverb) |
| `3` | 2 | Distorted guitar (drive + hard clip + tone filter) |
| `4` | 3 | Piano-ish (octave pitch + comb resonance + reverb) |
| `5` | 4 | 8-bit chiptune (bit crusher + sample-rate reducer) |
| `6` | 5 | Auto-wah (envelope-following resonant bandpass) |

The chain is built from `juce::dsp` modules (`WaveShaper`,
`StateVariableTPTFilter`, `Chorus`, `Reverb`) plus bespoke `Crusher` and
modulation stages. Each scene's `carousel` JSON block parameterizes the chain;
absent sub-blocks bypass that stage. `AudioGraph` routes instrument scenes
through the carousel and speaking scenes through the vocoder via a wet-source
selector. Piano (scene 3) and choir/pad (scene 1) use the new pitch/harmonizer stages added in Phase 4b.

### TTS (Phases 3–3.6)

The app supports **three** TTS sources via the same `ITTSSource` interface, with automatic fallback declared per-scene:

- **PrebakedTTSSource** — loads `.wav` files baked offline. Used by scenes 6, 8.
- **AppleTTSSource** — `AVSpeechSynthesizer`. Used as Apple's fallback when chosen.
- **PiperTTSSource** — bundled Piper CLI subprocess. Used by scene 7 (with prebaked fallback).

### One-time Piper setup

The Piper binary (~30 MB) and voice (~63 MB) are NOT committed. Fetch them once:

```bash
./scripts/fetch_piper.sh
```

This downloads:
- `assets/piper/piper` — the Piper CLI binary
- `assets/piper/voices/en_US-amy-medium.onnx` — voice model + `.json` metadata

The CMake post-build asset copy picks these up automatically and includes them in the .app bundle's `Contents/Resources/assets/piper/`. If you skip this step, scenes that declare `"source": "piper"` will fall back to their declared `tts.fallback` (typically prebaked or apple) — the app works, just without the Piper voice.

### Fallback chain

Scene JSON:

```json
{
  "tts": {
    "source": "piper",
    "text": "phrase to speak",
    "voice": "en_US-amy-medium",
    "clip": "fallback_clip_name",
    "fallback": "prebaked"
  }
}
```

At scene activation, the app calls the primary source's `synthesize()`. If it returns nullptr (binary missing, network down, voice not installed), the chain walks once into `tts.fallback`. Chain depth is capped at 1 hop (Phase 3.6 scope); deeper chains require spec changes.

A `TTSPrewarmer` per live source pre-synthesizes every scene's text in a background thread at app startup, so scene activation is instant even though each synthesis takes ~300 ms–1 s.

### Subsequent phases (see plans directory)

- **Phase 5**: Full-screen visualization (spectrogram, karaoke text overlay).
- **Phase 6**: Hardening + dress rehearsal (lands queued chips: data race, UAF guards).

### Listing available Apple voices

To find voice identifiers for your `tts.voice` field:

```bash
say -v ?
```

Voice identifiers shown are the ones to use (e.g. `com.apple.voice.compact.en-US.Samantha`).
