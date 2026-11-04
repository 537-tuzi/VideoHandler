#!/usr/bin/env python3
"""FR-4（PAR-DF-FEAT）：DFN3 图级夹具回放——官方图 × ORT 的输入输出对（设计轮生成，sha256
登记于 docs/acceptance/fr4-quality-tier/plan.md）在位回放，锁「模型图行为」不锁特征管线。

- 夹具：third_party/models/denoiser/fixtures…（不入库，fetch-deps 备齐模型；夹具随
  third_party/models/candidates/dfn3/fixtures-graphlevel/ 由设计轮就位，重生成 runbook 见
  fr4-quality-tier/plan.md 研究档勘误节）。
- 判据：python onnxruntime（单线程 Sequential，与引擎同配）Run 夹具输入，逐输出比期望，
  max|d| ≤ gate（默认 1e-5；round-1 实测 ort 1.31.1 vs 夹具（C++ 1.30.0 产）逐位一致 = 0）。
- df_dec 第二输出（未命名数值名）官方导出即有、引擎不消费（按名绑定 'coefs'）——只比夹具
  登记过的输出。
用法：dfn3_feat_check.py [--gate 1e-5]；退出码非零即 FAIL。
"""
import argparse
import sys
from pathlib import Path

import numpy as np
import onnxruntime as ort

REPO = Path(__file__).resolve().parents[4]
MODEL_DIR = REPO / 'third_party' / 'models' / 'candidates' / 'dfn3'
FIXTURES = MODEL_DIR / 'fixtures-graphlevel'
GRAPHS = ('enc', 'erb_dec', 'df_dec')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--gate', type=float, default=1e-5)
    a = ap.parse_args()

    if not FIXTURES.is_dir():
        print('PAR-DF-FEAT: FAIL 缺图级夹具目录', FIXTURES)
        sys.exit(1)
    so = ort.SessionOptions()
    so.intra_op_num_threads = 1
    so.inter_op_num_threads = 1
    so.execution_mode = ort.ExecutionMode.ORT_SEQUENTIAL
    fail = 0
    for g in GRAPHS:
        sess = ort.InferenceSession(str(MODEL_DIR / f'{g}.onnx'), so, providers=['CPUExecutionProvider'])
        inp = dict(np.load(FIXTURES / f'{g}_input.npz'))
        feed = {i.name: inp[i.name] for i in sess.get_inputs()}
        out = sess.run(None, feed)
        exp = dict(np.load(FIXTURES / f'{g}_output.npz'))
        for meta, o in zip(sess.get_outputs(), out):
            if meta.name not in exp:
                continue  # 未登记输出（df_dec 第二输出，引擎不消费）
            d = float(np.abs(o.astype(np.float64) - exp[meta.name].astype(np.float64)).max())
            ok = d <= a.gate
            fail |= not ok
            print(f'PAR-DF-FEAT-{g}.{meta.name}: {"PASS" if ok else "FAIL"} max|d|={d:.3e} (gate {a.gate})')
    sys.exit(fail)


if __name__ == '__main__':
    main()
