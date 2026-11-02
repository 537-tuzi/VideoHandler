#!/usr/bin/env python3
# craft-mismatch-model.py — 生成「元数据与输入/输出名全合规、但图输入维度错位」的 GTCRN ONNX
# （FR-24 CMD-032 坏模型注入用；wp-1.0-hotfix 同名脚本的 v2——WP-1.1 后 AiDenoiser::setup 增加了
#  缓存张量命名契约校验（conv_cache/tra_cache/inter_cache + *_out，aidenoiser.cpp:284-299），
#  旧配方 s0/s1/s2 命名已在 setup 期被拒（E_MODEL），到不了推理期。）
# 原理：模型带全 GTCRN 约定元数据与 4入/4出 契约命名（通过 setup 全部检查），但谱输入 x 声明为
#       [1,100,1,2]，引擎按 n_fft=512 实发 [1,257,1,2] → 首次 sess->Run 抛 Ort::Exception
#       （维度校验失败）→ lastError 非空 → 命令层 E_ENGINE。
# 用法：python3 craft-mismatch-model.py <输出.onnx>   （需 python3 + onnx 库）
import sys

import onnx
from onnx import TensorProto, helper


def main() -> None:
    out_path = sys.argv[1] if len(sys.argv) > 1 else "mismatch.onnx"
    caches = ["conv_cache", "tra_cache", "inter_cache"]
    shape = [1, 1, 2, 1]
    inputs = [helper.make_tensor_value_info("x", TensorProto.FLOAT, [1, 100, 1, 2])]
    inputs += [helper.make_tensor_value_info(n, TensorProto.FLOAT, shape) for n in caches]
    outputs = [helper.make_tensor_value_info(n + "_out", TensorProto.FLOAT, shape) for n in caches]
    outputs += [helper.make_tensor_value_info("enh", TensorProto.FLOAT, [1, 100, 1, 2])]
    names = ["x"] + caches
    nodes = [helper.make_node("Identity", [n], [n + "_out" if n != "x" else "enh"]) for n in names]
    graph = helper.make_graph(nodes, "mismatch", inputs, outputs)
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)])
    model.ir_version = 8  # 兼容 ORT 1.x 常见接受范围
    for k, v in {
        "sample_rate": "16000", "n_fft": "512", "hop_length": "256",
        "window_length": "512", "window_type": "hann",
        "conv_cache_shape": "1,1,2,1", "tra_cache_shape": "1,1,2,1",
        "inter_cache_shape": "1,1,2,1",
    }.items():
        m = model.metadata_props.add()
        m.key, m.value = k, v
    onnx.save(model, out_path)
    print(f"OK: {out_path}")


if __name__ == "__main__":
    main()
