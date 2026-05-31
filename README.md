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

This branch implements **Phase 3: Vocoder + Prebaked TTS**. Scenes 6, 7, and 8 now activate a 24-band channel vocoder whose modulator is a pre-baked TTS audio clip — the live guitar becomes the carrier, producing a "guitar speaks the words" effect.

### Generating TTS clips

Speaking-scene clips are generated offline by `tools/tts_prebake/prebake.py` (Piper-based — see `tools/tts_prebake/README.md` for setup). The committed clips under `assets/tts/` cover scenes 6, 7, and 8:

- `06_hello_cleveland/` — "hello cleveland"
- `07_mid_talk/` — "I think therefore I riff"
- `08_gently_weeps/` — opening lyric from the title track

To change a clip's text:

```bash
cd tools/tts_prebake && source .venv/bin/activate
python prebake.py --text "your new text here" --out ../../assets/tts/06_hello_cleveland
```

then rebuild the app — the post-build asset copy picks it up.

### Live behavior

Switch to scene 7 (key `7`, or PC 6 on the FCB1010) and play guitar. The vocoder shapes the guitar's harmonics with the TTS clip's envelope, producing intelligible-but-very-clearly-guitar speech. The active scene's `dryWet` controls how much of the dry guitar bleeds through.

### Subsequent phases (see plans directory)

- **Phase 3.5**: Apple `AVSpeechSynthesizer` source — live TTS for the future audience-text encore.
- **Phase 3.6**: Piper subprocess source — alternative live engine with bundled binary.
- **Phase 4**: Instrument Carousel — real per-scene DSP for scenes 1–5.
- **Phase 5**: Full-screen visualization (spectrogram, karaoke text overlay).
- **Phase 6**: Hardening + dress rehearsal.
