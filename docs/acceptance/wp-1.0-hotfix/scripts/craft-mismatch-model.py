# craft-mismatch-model.py — 生成「元数据齐全但图输入维度错位」的 ONNX（WP-1.0 A2 错误注入用）
# 原理：模型带全 GTCRN 约定元数据（通过 AiDenoiser::setup 全部检查），但输入 x 声明为 [1,100,1,2]，
#       引擎按 n_fft=512 实发 [1,257,1,2] → 首次 sess->Run 抛 Ort::Exception（维度校验失败）。
# 用法：python3 craft-mismatch-model.py <输出.onnx>   （需 python3 + onnx 库）
import sys

import onnx
from onnx import TensorProto, helper


def main() -> None:
    out_path = sys.argv[1] if len(sys.argv) > 1 else "mismatch.onnx"
    states = [(f"s{i}", [1, 1, 2, 1]) for i in range(3)]  # conv/tra/inter 缓存张量
    inputs = [helper.make_tensor_value_info("x", TensorProto.FLOAT, [1, 100, 1, 2])]
    inputs += [helper.make_tensor_value_info(n, TensorProto.FLOAT, s) for n, s in states]
    outputs = [helper.make_tensor_value_info(f"y{i}", TensorProto.FLOAT, shape)
               for i, (_, shape) in enumerate([("x", [1, 100, 1, 2])] + states)]
    nodes = [helper.make_node("Identity", [name], [f"y{i}"]) for i, name in
             enumerate(["x"] + [n for n, _ in states])]
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
