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

This branch implements **Phase 2: Scene system + MIDI/FCB1010**. The app loads 10 scenes from `assets/scenes/*.json` at startup and exposes them via:

- **Keyboard**: number keys `1`–`9` activate scenes 0–8; `0` activates scene 9.
- **MIDI**: any device whose name contains "FCB1010" is auto-connected. Program Change 0–9 activates the corresponding scene; CC 27 and CC 7 are recognized as expression-pedal channels (no-op in Phase 2; wired in Phase 3).

Each scene currently varies only the Mixer's master gain (so switching is audible). Per-scene DSP differences (carousel, vocoder) arrive in Phases 3 and 4.

### Hot reload (development)

Set `GUITAR_DSP_HOT_RELOAD=1` to enable a 2-second polling reload of `assets/scenes/`. Edits propagate without restarting the app.

### Asset path override

Set `GUITAR_DSP_ASSETS_DIR=<path>` to load assets from a directory other than the app bundle (useful for editing source `assets/` while running the binary directly).

### Subsequent phases (see plans directory)

- **Phase 3**: Vocoder + 3 TTS sources (prebaked / Apple / Piper).
- **Phase 4**: Instrument Carousel — real per-scene DSP.
- **Phase 5**: Full-screen visualization (spectrogram, karaoke text).
- **Phase 6**: Hardening + dress rehearsal.
