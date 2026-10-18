#!/usr/bin/env python3
"""WP-1.1 比较器：DUT 输出 vs 金样本，按 spec parity-test-plan.md §1 判据。

用法：compare.py <dut.f32> <golden.f32> --case <id> [--gate -60]
  等长逐样本对齐（不等长直接 FAIL，暴露首尾边界 bug）；
  parity_dB = 20·log10( RMS(dut−ref) / max(RMS(ref), 1e-12) )，≤ gate 为 PASS。
  输出一行结果（供 round-N.md 抄录），退出码非零即 FAIL（CI 判定）。
"""
import argparse
import sys

import numpy as np


def read_f32(path: str) -> np.ndarray:
    return np.fromfile(path, dtype='<f4').astype(np.float64)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dut')
    ap.add_argument('golden')
    ap.add_argument('--case', required=True)
    ap.add_argument('--gate', type=float, default=-60.0)
    ap.add_argument('--shift', type=int, default=0,
                    help='DUT 相对金样本的样本平移（正=DUT 滞后；GTCRN 首跳约定用）')
    a = ap.parse_args()
    dut, ref = read_f32(a.dut), read_f32(a.golden)
    if a.shift:
        dut = dut[a.shift:] if a.shift > 0 else np.concatenate([np.zeros(-a.shift), dut])
        ref = ref[:-a.shift] if a.shift > 0 else ref[:len(ref) + a.shift]
    n = min(len(dut), len(ref))
    if len(dut) != len(ref):
        print(f'{a.case}: FAIL 长度不等 dut={len(dut)} golden={len(ref)}（首尾边界 bug，不截断凑数）')
        sys.exit(1)
    d = dut[:n] - ref[:n]
    rms_ref = max(np.sqrt((ref[:n] ** 2).mean()), 1e-12)
    parity_db = 20 * np.log10(np.sqrt((d ** 2).mean()) / rms_ref)
    corr = float(np.corrcoef(dut[:n], ref[:n])[0, 1]) if n > 1 else float('nan')
    ok = parity_db <= a.gate
    print(f'{a.case}: {"PASS" if ok else "FAIL"} parity={parity_db:.1f} dB '
          f'(gate {a.gate})  max|d|={np.abs(d).max():.2e}  corr={corr:.6f}  n={n}')
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()
