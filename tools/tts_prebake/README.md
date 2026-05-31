# tts_prebake

Offline TTS pre-baker for the speaking-guitar app. Generates `audio.wav` files that the running app loads as vocoder modulator material.

## Setup

```bash
cd tools/tts_prebake
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

Download a Piper voice from https://github.com/rhasspy/piper/blob/master/VOICES.md and place both files in `voices/`:

- `<voice>.onnx`
- `<voice>.onnx.json`

Recommended for clarity at vocoder-modulator use: `en_US-amy-medium` (~63 MB total).

## Generating clips

```bash
python prebake.py --text "hello cleveland" --out ../../assets/tts/06_hello_cleveland
python prebake.py --text "I think therefore I riff" --out ../../assets/tts/07_mid_talk
python prebake.py --text "I look at you all see the love there that's sleeping" \
                  --out ../../assets/tts/08_gently_weeps
```

Commit the resulting `audio.wav` and `meta.json` files — they're the app's runtime assets.
