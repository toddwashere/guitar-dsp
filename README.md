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

## Project status

This branch implements **Phase 4: Instrument Carousel A**. Carousel scenes 1–5
now process the live guitar through a pedal-style effects chain (`audio::Carousel`)
— no MIDI notes, no pitch-shifting; the effect only reshapes timbre:

| Key | Scene | Effect |
|-----|-------|--------|
| `2` | 1 | Organ / Leslie (tanh warmth + chorus + light reverb) |
| `3` | 2 | Distorted guitar (drive + hard clip + tone filter) |
| `4` | 3 | Synth lead (LFO-swept resonant filter + chorus + reverb) |
| `5` | 4 | 8-bit chiptune (bit crusher + sample-rate reducer) |
| `6` | 5 | Auto-wah (envelope-following resonant bandpass) |

The chain is built from `juce::dsp` modules (`WaveShaper`,
`StateVariableTPTFilter`, `Chorus`, `Reverb`) plus bespoke `Crusher` and
modulation stages. Each scene's `carousel` JSON block parameterizes the chain;
absent sub-blocks bypass that stage. `AudioGraph` routes instrument scenes
through the carousel and speaking scenes through the vocoder via a wet-source
selector. Piano and choir/pad (which need pitch-shifting/harmonization) are
deferred to **Phase 4b**.

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

- **Phase 4b**: pitch/harmony instruments — pitch shifter, harmonizer, formant shifter (piano, choir/pad).
- **Phase 5**: Full-screen visualization (spectrogram, karaoke text overlay).
- **Phase 6**: Hardening + dress rehearsal (lands queued chips: data race, UAF guards).

### Listing available Apple voices

To find voice identifiers for your `tts.voice` field:

```bash
say -v ?
```

Voice identifiers shown are the ones to use (e.g. `com.apple.voice.compact.en-US.Samantha`).
