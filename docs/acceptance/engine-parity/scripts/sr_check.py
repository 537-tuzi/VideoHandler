#!/usr/bin/env python3
"""WP-1.1 PAR-SR 体检器（round-5，输出跟随源）：CLI 输出 WAV 的头规格断言 +
可选逐样本 bit-exact 比对 + 可选裸流导出（供 compare.py / pitch_check.py 复用）。

用法：sr_check.py <out.wav> --case <id> --sr 48000 --ch 2 [--bitexact-vs ref.f32] [--dump out.f32]
判据：WAV 头 sr/ch 与期望一致、样本 float32；--bitexact-vs 时帧数与参考一致且逐样本相等
（直通链 bypass 成立时 swr 恒等，输出应与解码参考零差别——PAR-SR-001）。
输出一行结果，退出码非零即 FAIL（供 run_parity.sh 判定）。
"""
import argparse
import sys

import numpy as np
from scipy.io import wavfile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wav')
    ap.add_argument('--case', required=True)
    ap.add_argument('--sr', type=int, required=True)
    ap.add_argument('--ch', type=int, required=True)
    ap.add_argument('--bitexact-vs')
    ap.add_argument('--dump')
    a = ap.parse_args()
    sr, y = wavfile.read(a.wav)
    ch = 1 if y.ndim == 1 else y.shape[1]
    fails = []
    if sr != a.sr:
        fails.append(f'头采样率 {sr}≠{a.sr}')
    if ch != a.ch:
        fails.append(f'头声道 {ch}≠{a.ch}')
    if y.dtype != np.float32:
        fails.append(f'样本 dtype {y.dtype}≠float32')
    extra = ''
    if a.bitexact_vs:
        ref = np.fromfile(a.bitexact_vs, dtype='<f4').reshape(-1, a.ch)
        same = len(y) == len(ref) and bool(np.array_equal(y, ref))
        if not same:
            if len(y) != len(ref):
                fails.append(f'帧数 {len(y)}≠{len(ref)}')
            else:
                bad = int(np.count_nonzero(y != ref))
                d = float(np.abs(y.astype(np.float64) - ref.astype(np.float64)).max())
                fails.append(f'非 bit-exact（{bad}/{len(ref)} 样本不等，max|d|={d:.3e}）')
        extra = f'  bit-exact={"是" if same else "否"}({len(y)}/{len(ref)} 帧)'
    if a.dump:
        y.tofile(a.dump)
    print(f'{a.case}: {"PASS" if not fails else "FAIL"}  头={sr}/{ch}ch {y.dtype} {len(y)} 帧{extra}'
          + (f'  <- 不满足: {"; ".join(fails)}' if fails else ''))
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
