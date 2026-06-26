#!/usr/bin/env python3
"""Convert an Acids-IRCAM RAVE TorchScript checkpoint (.ts) to ONNX.

Usage:
    python3 convert-rave-ts-to-onnx.py <input.ts> <output.onnx>

The RAVE TorchScript export wraps a forward(audio) -> audio function with
shape [batch, channels=1, samples]. We export it as an ONNX graph that
accepts dynamic batch and sample dimensions.

If conversion fails on a particular checkpoint, common workarounds:
  - Try opset 14 instead of 17 if newer ops aren't supported.
  - Make the sample dimension static (set dynamic_axes={} and pass a
    fixed 2048-sample dummy) — slower at inference but more compatible.
  - Verify model.encode / model.decode exist; some RAVE exports expose
    only those methods and not a unified forward().
"""
from pathlib import Path
import sys
import torch


def export(in_ts: Path, out_onnx: Path, opset: int = 17) -> None:
    print(f"Loading {in_ts} ...")
    model = torch.jit.load(str(in_ts), map_location="cpu").eval()

    # RAVE nn~-exported models take audio as [batch, channels=1, samples].
    # 2048 samples is the standard block size used by these checkpoints.
    dummy = torch.zeros(1, 1, 2048)

    # Try a forward pass first to confirm the model handles this shape.
    try:
        with torch.no_grad():
            out = model(dummy)
        print(f"Forward pass OK. Output shape: {out.shape}")
    except Exception as e:
        print(f"Forward pass on [1, 1, 2048] failed: {e}")
        print("Trying [1, 2048] (some checkpoints use 2D)...")
        dummy = torch.zeros(1, 2048)
        with torch.no_grad():
            out = model(dummy)
        print(f"Forward pass OK on 2D. Output shape: {out.shape}")

    # Dynamic axes: batch varies; sample count is FIXED at 2048 (RAVE's
    # internal block size). Letting the sample dim be dynamic chokes the
    # legacy ONNX exporter on RAVE's internal prim::Loop -> cat operations
    # because the loop's accumulator can't unify ranks symbolically. The
    # RaveSynthesizer caller already feeds 2048-sample windows, so this is
    # not a real restriction.
    dynamic_axes = {"audio": {0: "batch"}, "voice": {0: "batch"}}

    print(f"Exporting to {out_onnx} (opset {opset}, dynamo) ...")
    # The legacy JIT exporter chokes on RAVE's prim::Loop + cat rank-unify
    # logic. torch 2.12's dynamo exporter handles loops via torch.export and
    # is the recommended path. Use static shape — RaveSynthesizer always
    # feeds 2048-sample windows so dynamic dims aren't needed.
    torch.onnx.export(
        model,
        (dummy,),
        str(out_onnx),
        input_names=["audio"],
        output_names=["voice"],
        opset_version=opset,
        dynamo=True,
    )
    print(f"Done: {out_onnx} ({out_onnx.stat().st_size / 1e6:.1f} MB)")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    export(Path(sys.argv[1]), Path(sys.argv[2]))
