# Piper TTS binary + voice

This directory is populated by `scripts/fetch_piper.sh`. The contents
(`piper` binary + voice `.onnx` models) are NOT committed to git — they
are downloaded on demand because of their size (~90 MB total).

## First-time setup

```bash
./scripts/fetch_piper.sh
```

This downloads:
- `piper` — the Piper CLI binary (~30 MB)
- `voices/en_US-amy-medium.onnx` — voice model (~60 MB)
- `voices/en_US-amy-medium.onnx.json` — voice metadata (~5 KB)

After fetching, build the app normally:

```bash
cmake --build build
```

The CMake post-build asset copy step picks up the new `piper/` directory
and includes it in `Contents/Resources/assets/piper/` in the .app bundle.
The PiperTTSSource resolves these paths at runtime via AssetLocator.

If `piper/` is missing or incomplete, `PiperTTSSource::isReady()` returns
false and any scene using `tts.source = "piper"` falls back to the
declared `tts.fallback` source (typically `"apple"` or `"prebaked"`).
