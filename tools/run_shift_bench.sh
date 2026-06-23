#!/usr/bin/env bash
set -euo pipefail
HERE=$(cd "$(dirname "$0")"/.. && pwd)
BIN=$HERE/build-tests/tools/shift_test
OUT=$HERE/tools/test_envelopes
mkdir -p "$OUT"

# Three source grains: m1 ah long-straight (mid anchor),
# m1 scale slice (low anchor — first slice of C scale),
# m1 scale slice (high anchor — last slice of C scale).
SRC1=$HERE/assets/vocalset/m1/long_tones/straight/m1_long_straight_a.wav
SRC2=$HERE/assets/vocalset/m1/scales/slow_piano/m1_scales_c_slow_piano_a.wav
SRC3=$HERE/assets/vocalset/m1/scales/slow_piano/m1_scales_f_slow_piano_a.wav

for ratio in 0.5 0.75 1.0 1.25 1.5 2.0; do
  echo "=== ratio $ratio ==="
  for src in "$SRC1" "$SRC2" "$SRC3"; do
    name=$(basename "$src" .wav)
    "$BIN" --input "$src" --ratio "$ratio" \
      --output "$OUT/${name}_${ratio}.wav" \
      --report "$OUT/${name}_${ratio}.txt"
    grep -E "total_ms|realtime_factor" "$OUT/${name}_${ratio}.txt"
  done
done
