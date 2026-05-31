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

This branch implements **Phase 3.5: Apple AVSpeechSynthesizer live TTS source**. The app now ships with **two** TTS sources via the same `ITTSSource` interface:

- **PrebakedTTSSource** (Phase 3) — loads .wav files from `assets/tts/<key>/audio.wav`. Used by scenes 7 and 8.
- **AppleTTSSource** (this phase) — synthesizes text at runtime via macOS `AVSpeechSynthesizer`. Used by scene 6.

Scene JSON picks the source per scene:

```json
{
  "tts": {
    "source": "apple",
    "text": "what the guitar should say",
    "voice": "com.apple.voice.compact.en-US.Samantha"
  }
}
```

A `TTSPrewarmer` pre-synthesizes every live-scene's text in a background thread at app startup, so scene activation is instant even though each synthesis takes ~300 ms-1 s.

### Subsequent phases (see plans directory)

- **Phase 3.6**: Piper subprocess source — third source with bundled binary, completes the three-source fallback chain.
- **Phase 4**: Instrument Carousel — real per-scene DSP for scenes 1-5.
- **Phase 5**: Full-screen visualization (spectrogram, karaoke text overlay).
- **Phase 6**: Hardening + dress rehearsal.

### Listing available Apple voices

To find voice identifiers for your `tts.voice` field:

```bash
say -v ?
```

Voice identifiers shown are the ones to use (e.g. `com.apple.voice.compact.en-US.Samantha`).
