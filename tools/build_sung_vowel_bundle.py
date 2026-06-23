#!/usr/bin/env python3
"""Build per-voice sung-vowel .gspeak bundles from assets/vocalset/.

For each singer folder (m1, m10, f2, f8):
  1. Read long_tones/straight/*.wav → 1 grain per vowel (mid-anchor; estimate F0).
  2. Read scales/slow_piano/*_a.wav etc. → slice into 3 anchor grains per vowel.
  3. Concatenate all grains into one audio.wav with 200 ms silence pads.
  4. Emit manifest.json with per-grain bankKey/anchorPitchHz/variantTag.
  5. Zip → assets/clips/gspeak/scene11_sung_<voice>.gspeak.

Run with: python3 tools/build_sung_vowel_bundle.py
"""
import json
import math
import os
import struct
import sys
import wave
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SRC_ROOT  = REPO_ROOT / "assets" / "vocalset"
OUT_ROOT  = REPO_ROOT / "assets" / "clips" / "gspeak"
OUT_ROOT.mkdir(parents=True, exist_ok=True)

VOICES = ["m1", "m10", "f2", "f8"]
VOWELS = ["a", "e", "i", "o", "u"]
VOWEL_TO_BANK_KEY = {
    "a": "sung_ah",
    "e": "sung_eh",
    "i": "sung_ee",
    "o": "sung_oh",
    "u": "sung_oo",
}
PAD_MS         = 200
TARGET_SR      = 48000
LONG_TONE_SEC  = 4.0  # central window from each long_tones/straight clip
                       # Short enough that each guitar attack triggers a
                       # crisp single vowel; the carrier/shifter pre-render
                       # cost also scales with grain length.
ANCHOR_SLICES_PER_VOWEL = 3  # from scales/slow_piano — low/mid/high


def read_wav_mono(path: Path):
    with wave.open(str(path), "rb") as w:
        sr   = w.getframerate()
        n    = w.getnframes()
        bits = w.getsampwidth()
        raw  = w.readframes(n)
    if bits == 2:
        fmt = f"<{n}h"
        samples = struct.unpack(fmt, raw)
        samples = [s / 32768.0 for s in samples]
    else:
        raise RuntimeError(f"unsupported sample width {bits} in {path}")
    if sr != TARGET_SR:
        # Simple linear resample.
        ratio = TARGET_SR / sr
        out_n = int(n * ratio)
        out = []
        for i in range(out_n):
            src = i / ratio
            i0 = int(src)
            frac = src - i0
            i1 = min(i0 + 1, n - 1)
            out.append((1 - frac) * samples[i0] + frac * samples[i1])
        samples = out
        sr = TARGET_SR
    return samples


def estimate_f0_autocorr(samples, sr, search_range=(60, 1000)):
    """Crude autocorrelation pitch estimate over the center 1 s window."""
    n = len(samples)
    if n < sr // 2:
        return 0.0
    mid = n // 2
    win = samples[mid - sr // 2 : mid + sr // 2]
    # Energy normalisation.
    energy = sum(x * x for x in win)
    if energy < 1e-6:
        return 0.0
    best_lag, best_corr = 0, 0.0
    min_lag = sr // search_range[1]
    max_lag = sr // search_range[0]
    for lag in range(min_lag, max_lag):
        c = 0.0
        for i in range(len(win) - lag):
            c += win[i] * win[i + lag]
        if c > best_corr:
            best_corr = c
            best_lag = lag
    return sr / best_lag if best_lag else 0.0


def trim_silence(samples, sr, threshold=0.01):
    n = len(samples)
    start = 0
    while start < n and abs(samples[start]) < threshold:
        start += 1
    end = n
    while end > start and abs(samples[end - 1]) < threshold:
        end -= 1
    return samples[start:end]


def center_window(samples, sr, sec):
    n = len(samples)
    want = int(sec * sr)
    if n <= want:
        return samples
    start = (n - want) // 2
    return samples[start : start + want]


def normalize_peak(samples, peak_dbfs=-3.0):
    max_abs = max((abs(s) for s in samples), default=0.0)
    if max_abs < 1e-6:
        return samples
    target = 10 ** (peak_dbfs / 20.0)
    g = target / max_abs
    return [s * g for s in samples]


def slice_scale_into_anchors(samples, sr, num_slices):
    """Detect note attacks by simple energy-rise; return num_slices grain ranges."""
    n = len(samples)
    win = sr // 50  # 20 ms RMS window
    rms = []
    for i in range(0, n - win, win):
        e = sum(samples[i+j] * samples[i+j] for j in range(win)) / win
        rms.append(math.sqrt(e))
    if not rms:
        return []
    rms_max = max(rms)
    if rms_max < 1e-6:
        return []
    threshold = rms_max * 0.25
    onsets = []
    in_note = False
    for k, r in enumerate(rms):
        if not in_note and r > threshold:
            onsets.append(k * win)
            in_note = True
        elif in_note and r < threshold * 0.5:
            in_note = False
    # Pick evenly spaced onsets across the detected list. If the singer
    # is sung legato (common with slow_piano scales — RMS never drops
    # below the silence threshold between notes), the detector finds
    # only one or two onsets and we fall back to even N-way split.
    if len(onsets) < num_slices:
        step = n // num_slices
        grains = [(i * step, (i + 1) * step) for i in range(num_slices)]
    else:
        pick_idx = [int(i * (len(onsets) - 1) / (num_slices - 1))
                    for i in range(num_slices)]
        grains = []
        for k in pick_idx:
            start = onsets[k]
            end = onsets[k + 1] if k + 1 < len(onsets) else n
            grains.append((start, end))
    # Universal cap: every grain ≤ ~4.0 s, regardless of which slicer
    # path produced it. Short enough for crisp per-strike playback;
    # also keeps the WORLD pre-render time bounded (scene 12 activation).
    max_len = int(sr * 4.0)
    grains = [(s, min(e, s + max_len)) for (s, e) in grains]
    return grains


def write_wav_mono16(samples, sr):
    raw = b"".join(
        struct.pack("<h", max(-32768, min(32767, int(s * 32767))))
        for s in samples)
    import io
    buf = io.BytesIO()
    with wave.open(buf, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(raw)
    return buf.getvalue()


def build_for_voice(voice: str) -> Path:
    voice_root = SRC_ROOT / voice
    if not voice_root.is_dir():
        raise RuntimeError(f"missing {voice_root}")

    audio_out   = []
    phonemes    = []
    syllables   = []
    sample_idx  = 0
    pad_samples = int(PAD_MS / 1000.0 * TARGET_SR)
    silence     = [0.0] * pad_samples

    grain_index = 0
    for vowel in VOWELS:
        bank_key = VOWEL_TO_BANK_KEY[vowel]

        # 1. Mid-anchor grain from long_tones/straight.
        prefix = voice  # filenames use the same prefix as the dir name
        straight = (voice_root / "long_tones" / "straight" /
                    f"{prefix}_long_straight_{vowel}.wav")
        if straight.exists():
            samples = read_wav_mono(straight)
            samples = trim_silence(samples, TARGET_SR)
            samples = center_window(samples, TARGET_SR, LONG_TONE_SEC)
            samples = normalize_peak(samples)
            f0 = estimate_f0_autocorr(samples, TARGET_SR)
            start = sample_idx
            audio_out.extend(samples)
            sample_idx += len(samples)
            audio_out.extend(silence)
            sample_idx += len(silence)
            end = start + len(samples)
            phonemes.append({
                "label": vowel,
                "type": "Vowel",
                "startSample": start,
                "endSample":   end,
                "bankKey":     bank_key,
                "anchorPitchHz": round(f0, 2),
                "variantTag":  "straight",
            })
            syllables.append({
                "startSample": start, "endSample": end,
                "vowelNucleusSample": (start + end) // 2,
                "attackEndSample": start, "codaStartSample": end,
                "nucleusIsFricative": False,
                "phonemeIndices": [grain_index],
            })
            grain_index += 1

        # 2. Three anchor grains from scales/slow_piano (C scale).
        scale = (voice_root / "scales" / "slow_piano" /
                 f"{prefix}_scales_c_slow_piano_{vowel}.wav")
        if scale.exists():
            samples = read_wav_mono(scale)
            for (a, b) in slice_scale_into_anchors(samples, TARGET_SR,
                                                   ANCHOR_SLICES_PER_VOWEL):
                grain = samples[a:b]
                grain = trim_silence(grain, TARGET_SR)
                if len(grain) < TARGET_SR // 4:  # < 250 ms — skip
                    continue
                grain = normalize_peak(grain)
                f0 = estimate_f0_autocorr(grain, TARGET_SR)
                start = sample_idx
                audio_out.extend(grain); sample_idx += len(grain)
                audio_out.extend(silence); sample_idx += len(silence)
                end = start + len(grain)
                phonemes.append({
                    "label": vowel, "type": "Vowel",
                    "startSample": start, "endSample": end,
                    "bankKey": bank_key,
                    "anchorPitchHz": round(f0, 2),
                    "variantTag": "scale-slice",
                })
                syllables.append({
                    "startSample": start, "endSample": end,
                    "vowelNucleusSample": (start + end) // 2,
                    "attackEndSample": start, "codaStartSample": end,
                    "nucleusIsFricative": False,
                    "phonemeIndices": [grain_index],
                })
                grain_index += 1

    wav_bytes = write_wav_mono16(audio_out, TARGET_SR)
    manifest = {
        "version": 1, "kind": "clip",
        "savedBy": "build_sung_vowel_bundle.py",
        "text": f"sung-vowels-{voice}",
        "sampleRate": TARGET_SR,
        "lengthSamples": len(audio_out),
        "clipKind": "v2",
        "syllables": syllables, "phonemes": phonemes,
    }
    out_path = OUT_ROOT / f"scene11_sung_{voice}.gspeak"
    with zipfile.ZipFile(out_path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("manifest.json", json.dumps(manifest, indent=2))
        z.writestr("audio.wav", wav_bytes)
    print(f"  -> {out_path.name}: {len(phonemes)} grains, "
          f"{len(audio_out)} samples")
    return out_path


def main():
    print("Building sung-vowel bundles…")
    for v in VOICES:
        print(f"Voice {v}…")
        build_for_voice(v)
    print("Done.")

if __name__ == "__main__":
    sys.exit(main() or 0)
