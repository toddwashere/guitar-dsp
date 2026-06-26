#!/usr/bin/env python3
"""Build a tiny passthrough ONNX model for RAVE-pipeline integration tests.

Input: float32 [1, 1, N]. Output: float32 [1, 1, N]. Identity.

The 3D shape [batch, channels=1, samples] matches the convention used by all
real RAVE checkpoints (Acids-IRCAM, Scyclone, IIL) — see RaveInference::process.
"""
from pathlib import Path
import onnx
from onnx import helper, TensorProto

def build(out_path: Path) -> None:
    inp = helper.make_tensor_value_info("audio", TensorProto.FLOAT, ["batch", 1, "n"])
    out = helper.make_tensor_value_info("voice", TensorProto.FLOAT, ["batch", 1, "n"])
    node = helper.make_node("Identity", inputs=["audio"], outputs=["voice"])
    graph = helper.make_graph([node], "rave_stub", [inp], [out])
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)])
    model.ir_version = 8
    onnx.checker.check_model(model)
    onnx.save(model, str(out_path))

if __name__ == "__main__":
    import sys
    build(Path(sys.argv[1]))
