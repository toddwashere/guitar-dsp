#!/usr/bin/env bash
# fetch-rave-model.sh — Download and convert a RAVE voice checkpoint to ONNX
# for use with Guitar Speak scene 5 "Neural Voice".
#
# Usage:
#   bash scripts/fetch-rave-model.sh              # downloads voice_vocalset (singing, 48kHz)
#   MODEL=voice_vctk bash scripts/fetch-rave-model.sh   # alt: VCTK speech, 44.1kHz
#
# Available MODEL values (Intelligent Instruments Lab on HuggingFace, CC-BY-NC):
#   voice_vocalset   — pure singing voice, 48000 Hz, latent z=16  (DEFAULT)
#   voice_vctk       — multi-speaker English speech, 44100 Hz, z=22
#   voice_multivoice — singing + speech mix, 48000 Hz, z=11
#   voice_hifitts    — audiobook speech, 48000 Hz, z=16
#   voice_jvs        — single Japanese speaker, 44100 Hz, z=16
#
# The bundled assets/models/rave-voice.onnx is overwritten by this script.
#
# Prerequisites:
#   - curl
#   - Python 3 with torch + onnx (the script will hint at install if missing).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/assets/models/rave-voice.onnx"
TMP_DIR="${TMPDIR:-/tmp}"
mkdir -p "$(dirname "$OUT")"

MODEL="${MODEL:-voice_vocalset}"
case "$MODEL" in
    voice_vocalset)   FILE="voice_vocalset_b2048_r48000_z16.ts" ;;
    voice_vctk)       FILE="voice_vctk_b2048_r44100_z22.ts" ;;
    voice_multivoice) FILE="voice-multi-b2048-r48000-z11.ts" ;;
    voice_hifitts)    FILE="voice_hifitts_b2048_r48000_z16.ts" ;;
    voice_jvs)        FILE="voice_jvs_b2048_r44100_z16.ts" ;;
    *)
        echo "Unknown MODEL='$MODEL'. See header comment for options." >&2
        exit 1
        ;;
esac
URL="https://huggingface.co/Intelligent-Instruments-Lab/rave-models/resolve/main/$FILE"
TMP_TS="$TMP_DIR/$FILE"

if [ ! -f "$TMP_TS" ]; then
    echo "Downloading $MODEL ($FILE) ..."
    curl -L --fail --progress-bar -o "$TMP_TS" "$URL"
else
    echo "Cached: $TMP_TS ($(du -h "$TMP_TS" | cut -f1))"
fi

PYTHON="${PYTHON:-python3}"
if ! "$PYTHON" -c "import torch, onnx" 2>/dev/null; then
    echo "ERROR: $PYTHON cannot import torch + onnx." >&2
    echo "Install with: $PYTHON -m pip install torch onnx" >&2
    echo "If pip refuses, try: $PYTHON -m pip install --break-system-packages torch onnx" >&2
    echo "Or try a different interpreter: PYTHON=/opt/homebrew/bin/python3.12 bash scripts/fetch-rave-model.sh" >&2
    exit 1
fi

echo "Converting $TMP_TS -> $OUT ..."
"$PYTHON" "$ROOT/scripts/convert-rave-ts-to-onnx.py" "$TMP_TS" "$OUT"
echo "RAVE ONNX model ready at: $OUT ($(du -h "$OUT" | cut -f1))"
echo
echo "Restart the host (Logic or the Standalone) so it reloads the model."
