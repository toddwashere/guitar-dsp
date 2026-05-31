#!/usr/bin/env python3
"""
prebake.py — generate TTS audio clips for the speaking-guitar app.

Usage:
    python prebake.py --text "hello cleveland" --out assets/tts/06_hello_cleveland
    python prebake.py --config clips.yaml --out assets/tts/

Output per clip:
    <out>/audio.wav   — mono 48 kHz float32 WAV
    <out>/meta.json   — { "text": ..., "voice": ..., "duration_s": ... }

Requires:
    piper-tts (pip install piper-tts)
    A Piper voice .onnx file under tools/tts_prebake/voices/.
"""
import argparse
import json
import os
import subprocess
import sys
import wave
from pathlib import Path


def run_piper(text: str, voice_onnx: Path, out_wav: Path) -> None:
    """Invoke Piper to synthesize `text` to `out_wav`."""
    out_wav.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        sys.executable, "-m", "piper",
        "--model", str(voice_onnx),
        "--output_file", str(out_wav),
    ]
    proc = subprocess.run(cmd, input=text.encode("utf-8"), check=True)
    if proc.returncode != 0:
        raise SystemExit(f"piper failed for: {text!r}")


def wav_duration_seconds(path: Path) -> float:
    with wave.open(str(path), "rb") as f:
        return f.getnframes() / f.getframerate()


def bake_one(text: str, voice_onnx: Path, out_dir: Path) -> None:
    audio = out_dir / "audio.wav"
    run_piper(text, voice_onnx, audio)

    meta = {
        "text": text,
        "voice": voice_onnx.name,
        "duration_s": wav_duration_seconds(audio),
    }
    with open(out_dir / "meta.json", "w") as f:
        json.dump(meta, f, indent=2)
    print(f"baked {out_dir.name}: {meta['duration_s']:.2f}s")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--text", help="single text string to bake")
    ap.add_argument("--out",  required=True, help="output directory for the clip")
    ap.add_argument("--voice", default=None,
                    help="path to a Piper .onnx voice (default: first found in voices/)")
    args = ap.parse_args()

    if not args.text:
        ap.error("Phase 3 supports --text only; YAML batch mode comes in Phase 5.")

    voices_dir = Path(__file__).parent / "voices"
    if args.voice:
        voice = Path(args.voice)
    else:
        candidates = sorted(voices_dir.glob("*.onnx"))
        if not candidates:
            print(f"No voice .onnx in {voices_dir}/. Download one from "
                  "https://github.com/rhasspy/piper/blob/master/VOICES.md "
                  "and place it there.", file=sys.stderr)
            return 1
        voice = candidates[0]

    bake_one(args.text, voice, Path(args.out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
