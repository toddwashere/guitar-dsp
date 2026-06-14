"""Tests for build_developers_clip.py — synthesises a source WAV with
marker tones at known offsets, runs the chopper with a stub timestamp
list pointing at those offsets, and verifies the output WAV shape."""

import math
import os
import struct
import sys
import tempfile
import unittest
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))

import build_developers_clip as bdc  # noqa: E402


SR = 22050


def write_marker_wav(path, marker_offsets_s, total_s):
    """Write a mono 22.05 kHz PCM WAV. Each `marker_offsets_s[i]` is the
    start of a 0.6 s 440 Hz tone burst. Everything else is silence."""
    nframes = int(total_s * SR)
    samples = [0] * nframes
    for off in marker_offsets_s:
        start = int(off * SR)
        end = min(nframes, start + int(0.6 * SR))
        for i in range(start, end):
            samples[i] = int(0.5 * 32767 *
                             math.sin(2 * math.pi * 440 * (i - start) / SR))
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(b"".join(struct.pack("<h", s) for s in samples))


def read_wav_frames(path):
    with wave.open(path, "rb") as w:
        assert w.getnchannels() == 1, "expected mono"
        assert w.getsampwidth() == 2, "expected 16-bit PCM"
        assert w.getframerate() == SR, f"expected {SR} Hz"
        raw = w.readframes(w.getnframes())
        return [struct.unpack("<h", raw[i:i+2])[0] for i in range(0, len(raw), 2)]


class ChopperTests(unittest.TestCase):

    def test_chops_marker_segments_in_order(self):
        # 5 markers at known offsets. Stub timestamp list = those offsets.
        markers = [(1.0, 1.6), (3.0, 3.6), (5.0, 5.6), (7.0, 7.6), (9.0, 9.6)]
        with tempfile.TemporaryDirectory() as td:
            src = os.path.join(td, "src.wav")
            dst = os.path.join(td, "out.wav")
            write_marker_wav(src, [m[0] for m in markers], total_s=11.0)

            bdc.chop(src, dst, segments=markers,
                     tail_silence_s=0.1, target_sr=SR)

            self.assertTrue(os.path.exists(dst))
            frames = read_wav_frames(dst)

            # Verify output structure: each segment contributes its slice
            # plus tail silence. Calculate expected frames precisely.
            tail_frames = int(0.1 * SR)
            expected_total = 0
            for start_s, end_s in markers:
                start_n = int(start_s * SR)
                end_n = int(end_s * SR)
                expected_total += (end_n - start_n) + tail_frames
            self.assertEqual(len(frames), expected_total)

            # Verify segments are non-empty by checking for non-zero samples.
            # Calculate actual segment boundaries in the output.
            seg_offsets = []
            offset = 0
            for start_s, end_s in markers:
                start_n = int(start_s * SR)
                end_n = int(end_s * SR)
                seg_len = (end_n - start_n) + tail_frames
                seg_offsets.append(offset)
                offset += seg_len

            for i in range(len(markers)):
                # Check a sample after the beginning of each segment
                idx = seg_offsets[i] + 1
                self.assertNotEqual(frames[idx], 0,
                                    f"segment {i} appears empty")

    def test_aborts_if_segment_runs_past_source(self):
        with tempfile.TemporaryDirectory() as td:
            src = os.path.join(td, "src.wav")
            dst = os.path.join(td, "out.wav")
            write_marker_wav(src, [], total_s=2.0)  # 2-second source

            with self.assertRaises(ValueError) as ctx:
                bdc.chop(src, dst, segments=[(0.0, 5.0)],   # past end!
                         tail_silence_s=0.1, target_sr=SR)
            self.assertIn("past source duration", str(ctx.exception))

    def test_stereo_source_is_downmixed_to_mono(self):
        # 2-channel source: chop should average channels and produce mono.
        with tempfile.TemporaryDirectory() as td:
            src = os.path.join(td, "src.wav")
            dst = os.path.join(td, "out.wav")
            nframes = int(3.0 * SR)
            with wave.open(src, "wb") as w:
                w.setnchannels(2)
                w.setsampwidth(2)
                w.setframerate(SR)
                # Left = 0.5 amplitude sine, right = -0.5 amplitude sine.
                # Mono mix = 0.
                interleaved = []
                for i in range(nframes):
                    s = int(0.5 * 32767 * math.sin(2 * math.pi * 440 * i / SR))
                    interleaved.append(struct.pack("<h", s))
                    interleaved.append(struct.pack("<h", -s))
                w.writeframes(b"".join(interleaved))

            bdc.chop(src, dst, segments=[(0.5, 1.0)],
                     tail_silence_s=0.0, target_sr=SR)

            frames = read_wav_frames(dst)
            # All samples ~0 (L + R cancellation), well within rounding.
            for s in frames:
                self.assertLess(abs(s), 4, f"unexpected non-zero sample {s}")


if __name__ == "__main__":
    unittest.main()
