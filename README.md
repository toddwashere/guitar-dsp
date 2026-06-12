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

## Audio Unit (Logic Pro)

The same app builds as an AUv2 plugin alongside the standalone:

    cmake --build build   # builds Standalone + AU; COPY_PLUGIN_AFTER_BUILD installs the .component

The `.component` is auto-installed to `~/Library/Audio/Plug-Ins/Components/`. It is
a MIDI-capable **Music Effect** (AU type `aumf`), so in Logic it appears under the
Audio Units for **GuitarDSP > Guitar DSP** — insert it on a guitar track; it accepts
host MIDI for scene control.

**Recording the processed/spoken output:** Bounce in Place the track, or route the
track to a bus and record that bus onto another track.

**Dev workflow (iterate fast, stay out of Logic):**
- Do most work in the **Standalone** (rebuild + relaunch in seconds) and the test suite.
- Validate the AU without Logic:
  - `auval -v aumf GtAp GtDs`
  - `pluginval --strictness-level 10 --validate-in-process "<path-to>/Guitar DSP.component"`
    (install via `brew install --cask pluginval`). Note: pluginval's embedded `auval`
    sub-test can time out on headless Apple-TTS synthesis — its own in-process tests
    are the stability signal.
- Logic caches the AU in-process — a rebuilt binary needs a **Logic restart** (or
  remove + re-insert) to load. Only open Logic at milestones.
- AUv2 runs in-process, so a plugin crash can take Logic down. Always pass pluginval's
  stress tests before trusting it in a live session.

**MIDI scene control in the plugin:** route the FCB1010 (Program Change) to the
plugin's track in Logic; Program Change *n* selects scene *n*.

**Apple TTS in a plugin host:** `AVSpeechSynthesizer` needs a pumped main run loop to
deliver its audio callbacks. Logic provides one during playback, so Apple TTS works;
fully headless hosts (e.g. `auval`) do not, and those scenes fall back to prebaked
clips. Piper and prebaked clips work regardless.

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
