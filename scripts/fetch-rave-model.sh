#!/usr/bin/env bash
# fetch-rave-model.sh — Download and convert an Acids-IRCAM RAVE voice checkpoint
# to ONNX format for use with Guitar Speak scene 5 "Neural Voice".
#
# Usage:
#   bash scripts/fetch-rave-model.sh
#
# The script downloads the Acids-IRCAM 'speech' TorchScript checkpoint from
# the IRCAM forum server and converts it to ONNX using PyTorch's built-in
# ONNX exporter. If the URL changes (model hosting is not guaranteed to be
# permanent), update CKPT_URL to the new location.
#
# Prerequisites:
#   - curl (system)
#   - Python 3 with torch >= 2.0 (install via: pip install torch --index-url
#     https://download.pytorch.org/whl/cpu)
#
# The output file (assets/models/rave-voice.onnx) is .gitignored because at
# ~10–20 MB it is too large to commit. Run this script once after cloning, or
# after deleting the cached file.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/assets/models/rave-voice.onnx"

if [ -f "$OUT" ]; then
    echo "Model already at $OUT; delete it to re-fetch."
    exit 0
fi

mkdir -p "$(dirname "$OUT")"

# --------------------------------------------------------------------------
# Step 1: Download the TorchScript checkpoint.
# Primary: Acids-IRCAM RAVE-VST API (CC-licensed, non-commercial use).
# If this URL returns 401/404, try the alternative URLs commented below, or
# download manually from https://acids-ircam.github.io/rave/ and place the
# .ts file at /tmp/rave-voice.ts before re-running.
# --------------------------------------------------------------------------
CKPT_URL="https://play.forum.ircam.fr/rave-vst-api/get_model/speech"
# Alternatives (uncomment to try):
# CKPT_URL="https://play.forum.ircam.fr/rave-vst-api/get_model/vintage"

TMP_CKPT="/tmp/rave-voice.ts"

echo "Downloading RAVE checkpoint from: $CKPT_URL"
curl -L --fail --show-error --progress-bar -o "$TMP_CKPT" "$CKPT_URL"
echo "Download complete: $TMP_CKPT ($(du -h "$TMP_CKPT" | cut -f1))"

# --------------------------------------------------------------------------
# Step 2: Install conversion tooling (optional helper package).
# torch.onnx.export is part of PyTorch itself; rave-toolkit is optional.
# --------------------------------------------------------------------------
python -m pip install rave-toolkit 2>/dev/null || true

# --------------------------------------------------------------------------
# Step 3: Convert TorchScript → ONNX using PyTorch's built-in exporter.
# The RAVE model takes a 1×N float tensor (mono audio at the model's native
# sample rate, typically 44100 Hz). We export with a dynamic N axis so the
# block size can vary at runtime.
#
# Note: the model's actual input/output signature depends on the checkpoint.
# If torch.onnx.export raises "unexpected keyword argument" or dimension
# errors, inspect the model first:
#   python -c "import torch; m=torch.jit.load('/tmp/rave-voice.ts'); print(m.code)"
# and adjust the dummy tensor shape and axis dict accordingly.
# --------------------------------------------------------------------------
TMP_ONNX="/tmp/rave-voice.onnx"

python3 - <<'PY'
import sys
import torch

print(f"PyTorch version: {torch.__version__}")

ckpt_path = "/tmp/rave-voice.ts"
out_path   = "/tmp/rave-voice.onnx"

print(f"Loading TorchScript model from {ckpt_path} ...")
model = torch.jit.load(ckpt_path, map_location="cpu").eval()

# RAVE models typically expect 1×N audio; block size 2048 is a safe dummy.
dummy = torch.zeros(1, 2048)

print("Exporting to ONNX (opset 17) ...")
torch.onnx.export(
    model,
    (dummy,),
    out_path,
    input_names=["audio"],
    output_names=["voice"],
    dynamic_axes={"audio": {1: "n_samples"}, "voice": {1: "n_samples"}},
    opset_version=17,
)
print(f"Wrote {out_path}")
PY

mv "$TMP_ONNX" "$OUT"
echo "RAVE ONNX model ready at: $OUT  ($(du -h "$OUT" | cut -f1))"
