#!/usr/bin/env python3
"""
Generate 10 placeholder WAVs for the vocal-guitar clip bank (Scene 2).

Each clip is a short tone/burst at a different pitch/duration so the user can
audibly hear the per-pick cycling work end-to-end. Replace these with real
recorded vocal-guitar samples later — same filename/folder layout.
"""
import math
import os
import struct
import wave
from pathlib import Path

CLIPS = [
    # (folder name, duration ms, pitch Hz, shape)
    ("00_wee",          300, 1200, "tone"),
    ("01_doo",          250,  600, "tone"),
    ("02_ner",          200,  400, "noise"),
    ("03_new",          250,  900, "tone"),
    ("04_yeah",         400,  500, "tone"),
    ("05_brrr",         400,  150, "noise"),
    ("06_skronk",       500,  250, "noise"),
    ("07_weeeeee",     1200, 1400, "tone"),
    ("08_ahhhh",       1200,  700, "tone"),
    ("09_ner-ner-ner",  700,  450, "noise"),
]

SR = 48000

def synth(duration_ms, hz, shape):
    n = int(SR * duration_ms / 1000)
    out = []
    for i in range(n):
        # ~10 ms attack/release envelope so there's no click.
        env = min(1.0, i / (SR * 0.01), (n - i) / (SR * 0.01))
        if shape == "tone":
            s = 0.5 * env * math.sin(2 * math.pi * hz * i / SR)
        else:
            # Pitched noise: tone * (1 + 0.4 * white).
            import random
            s = 0.5 * env * math.sin(2 * math.pi * hz * i / SR) * \
                (1.0 + 0.4 * (random.random() - 0.5))
        out.append(int(max(-1.0, min(1.0, s)) * 32767))
    return out

def write_wav(path, samples):
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", s) for s in samples))

def main():
    root = Path(__file__).resolve().parent.parent / "assets" / "clips" / "vocal-guitar"
    for name, dur, hz, shape in CLIPS:
        path = root / name / "audio.wav"
        if path.exists():
            continue
        write_wav(path, synth(dur, hz, shape))
        print(f"wrote {path}")

if __name__ == "__main__":
    main()
