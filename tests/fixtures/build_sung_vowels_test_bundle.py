#!/usr/bin/env python3
"""Build a 5-grain synthetic sung-vowels test bundle.

Outputs tests/fixtures/sung_vowels_test.gspeak. Designed for unit tests —
the audio content is a flat 440 Hz sine in every grain; what matters is
the manifest metadata.
"""
import json
import math
import os
import struct
import sys
import wave
import zipfile

HERE       = os.path.dirname(os.path.abspath(__file__))
OUT_PATH   = os.path.join(HERE, "sung_vowels_test.gspeak")
SAMPLE_RATE = 48000
GRAIN_SEC   = 1.0
GRAIN_SAMPLES = int(SAMPLE_RATE * GRAIN_SEC)

GRAINS = [
    # (bankKey, anchorPitchHz, label)
    ("sung_ah", 110.0, "ah"),
    ("sung_ah", 440.0, "ah"),
    ("sung_ah", 880.0, "ah"),
    ("sung_eh", 220.0, "eh"),
    ("sung_eh", 440.0, "eh"),
]

def sine_grain(seconds, freq, sr):
    n = int(seconds * sr)
    return [math.sin(2 * math.pi * freq * i / sr) * 0.3 for i in range(n)]

def write_wav_mono16(samples, sr):
    raw = b"".join(struct.pack("<h", max(-32768, min(32767, int(s * 32767))))
                    for s in samples)
    import io
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(raw)
    return buf.getvalue()

def main():
    total_samples = GRAIN_SAMPLES * len(GRAINS)
    audio = []
    phonemes = []
    syllables = []
    for i, (key, anchor, label) in enumerate(GRAINS):
        start = i * GRAIN_SAMPLES
        end   = start + GRAIN_SAMPLES
        audio.extend(sine_grain(GRAIN_SEC, 440.0, SAMPLE_RATE))
        phonemes.append({
            "label": label,
            "type": "Vowel",
            "startSample": start,
            "endSample": end,
            "bankKey": key,
            "anchorPitchHz": anchor,
            "variantTag": "test",
        })
        syllables.append({
            "startSample": start,
            "endSample": end,
            "vowelNucleusSample": (start + end) // 2,
            "attackEndSample": start,
            "codaStartSample": end,
            "nucleusIsFricative": False,
            "phonemeIndices": [i],
        })
    wav_bytes = write_wav_mono16(audio, SAMPLE_RATE)
    manifest = {
        "version": 1,
        "kind": "clip",
        "savedBy": "build_sung_vowels_test_bundle.py",
        "text": "test",
        "sampleRate": SAMPLE_RATE,
        "lengthSamples": total_samples,
        "clipKind": "v2",
        "syllables": syllables,
        "phonemes": phonemes,
    }
    with zipfile.ZipFile(OUT_PATH, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("manifest.json", json.dumps(manifest, indent=2))
        z.writestr("audio.wav", wav_bytes)
    print(f"Wrote {OUT_PATH} ({len(wav_bytes)} bytes audio, "
          f"{len(GRAINS)} grains)")

if __name__ == "__main__":
    sys.exit(main() or 0)
