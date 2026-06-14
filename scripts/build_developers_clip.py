#!/usr/bin/env python3
"""Build assets/tts/01_developers/audio.wav from a user-supplied source clip.

Usage:
    python3 scripts/build_developers_clip.py <path-to-ballmer-source.wav>

Reads the source clip, chops 14 hand-tuned segments in chronological order
(the natural Ballmer crescendo), pads each with a tail silence, concatenates
in order, and writes the result. The last two segments duplicate the loudest
two bursts so wrap-around in NoteSteppedTTSPlayer still hits a loud one
before returning to the calm opening.

Pure standard library (wave + struct + math). No external dependencies.
"""

from __future__ import annotations

import math
import os
import struct
import sys
import wave

# 14 (start_s, end_s) "DEVELOPERS!" segments calibrated against the
# ElevenLabs "Harry" voice conversion of the iconic Ballmer chant
# (~10.9 s source). Entries 1-12 are unique bursts in chronological order;
# entries 13-14 duplicate the two loudest bursts (originally bursts 11 and
# 12 — both ~97-100% peak RMS) so wrap-around stays loud rather than
# dropping to the quieter final bursts of the natural take.
#
# Re-calibrate against your own source if you swap the voice/clip.
SEGMENTS_S: list[tuple[float, float]] = [
    (0.020, 0.510),
    (0.580, 1.290),
    (1.360, 2.030),
    (2.340, 2.760),
    (2.830, 3.460),
    (3.540, 4.170),
    (4.760, 5.080),
    (5.150, 5.800),
    (5.870, 6.460),
    (6.520, 7.180),
    (7.540, 7.930),
    (8.210, 8.640),
    # Last two = peak dupes (loudest bursts, repeated)
    (7.540, 7.930),
    (8.210, 8.640),
]

TAIL_SILENCE_S = 0.1
TARGET_SR = 22050
OUTPUT_REL_PATH = "assets/tts/01_developers/audio.wav"


def chop(src_path: str,
         dst_path: str,
         *,
         segments: list[tuple[float, float]] = None,
         tail_silence_s: float = TAIL_SILENCE_S,
         target_sr: int = TARGET_SR) -> None:
    """Read src_path, slice per segments, pad each with tail_silence_s, write to dst_path.

    Raises ValueError if any segment runs past the source duration."""
    if segments is None:
        segments = SEGMENTS_S

    with wave.open(src_path, "rb") as w:
        n_in_channels = w.getnchannels()
        in_width = w.getsampwidth()
        in_sr = w.getframerate()
        n_frames = w.getnframes()
        raw = w.readframes(n_frames)

    if in_width != 2:
        raise ValueError(f"source must be 16-bit PCM (got {in_width * 8} bits)")
    if in_sr < target_sr:
        raise ValueError(f"source rate {in_sr} Hz < target {target_sr} Hz")

    # Decode source to mono float-ish (int kept as int16 range) at target_sr.
    src_mono = _decode_mono(raw, n_in_channels)
    src_at_target = _resample(src_mono, in_sr, target_sr) if in_sr != target_sr else src_mono
    src_duration = len(src_at_target) / target_sr

    out: list[int] = []
    tail = [0] * int(tail_silence_s * target_sr)
    for i, (start_s, end_s) in enumerate(segments):
        if end_s > src_duration:
            raise ValueError(
                f"segment {i} ({start_s}-{end_s} s) extends past source "
                f"duration ({src_duration:.3f} s)")
        start_n = int(start_s * target_sr)
        end_n = int(end_s * target_sr)
        out.extend(src_at_target[start_n:end_n])
        out.extend(tail)

    os.makedirs(os.path.dirname(dst_path) or ".", exist_ok=True)
    with wave.open(dst_path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(target_sr)
        w.writeframes(b"".join(struct.pack("<h", _clip16(s)) for s in out))


def _decode_mono(raw: bytes, n_channels: int) -> list[int]:
    """int16 little-endian -> list[int], downmix to mono if needed."""
    samples = [struct.unpack("<h", raw[i:i+2])[0] for i in range(0, len(raw), 2)]
    if n_channels == 1:
        return samples
    if n_channels == 2:
        return [(samples[i] + samples[i+1]) // 2
                for i in range(0, len(samples), 2)]
    raise ValueError(f"unsupported channel count: {n_channels}")


def _resample(samples: list[int], src_sr: int, dst_sr: int) -> list[int]:
    """Linear-interpolation downsample. Good enough for an offline asset."""
    ratio = src_sr / dst_sr
    out_len = int(len(samples) / ratio)
    out = [0] * out_len
    for i in range(out_len):
        src_idx = i * ratio
        i0 = int(src_idx)
        frac = src_idx - i0
        i1 = min(i0 + 1, len(samples) - 1)
        out[i] = int((1.0 - frac) * samples[i0] + frac * samples[i1])
    return out


def _clip16(s: int) -> int:
    return max(-32768, min(32767, int(s)))


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 2
    src = argv[1]
    if not os.path.exists(src):
        print(f"error: source file not found: {src}", file=sys.stderr)
        return 1

    # Output path is relative to the repo root (the cwd if run from there).
    dst = os.path.abspath(OUTPUT_REL_PATH)
    print(f"chopping {src} -> {dst}")
    print(f"segments: {len(SEGMENTS_S)} (incl. 2 peak dupes at the end)")
    try:
        chop(src, dst)
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    print(f"wrote {dst}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
