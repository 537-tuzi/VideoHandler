#!/usr/bin/env python3
"""WP-1.1 PAR-PT 谱判据（round-1 §5 方法固化）：SoundTouch +n∘−n 往返恒等性；
round-5 起兼 PAR-SR 单程变调判据（--expect-cents 传期望移位，如 +2 半音 = 200）。

用法：pitch_check.py <in.f32> <out.f32> --case <id> [--sr 44100] [--expect-cents 0]
判据（round-1 决策：WSOLA 往返相位漂移使逐样本残差只到 −12.9dB，属算法固有非缺陷，
  故用谱判据）：
  1. 长度 0 差；2. 峰频误差 <10 音分（左声道 rfft 峰 bin，--expect-cents 判单程变调的
  期望移位，round-5 起）；3. 峰外（|Δf|>20Hz）杂散抑制 ≥40dB；4. RMS 守恒 ±0.1dB。
  --sr：素材采样率（rfftfreq 与峰频换算用；48k 素材须传 48000，round-5 起）。
输出一行结果，退出码非零即 FAIL（供 run_parity.sh 判定）。
"""
import argparse
import sys

import numpy as np


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('inp')
    ap.add_argument('rt')
    ap.add_argument('--case', required=True)
    ap.add_argument('--sr', type=int, default=44100)
    ap.add_argument('--expect-cents', type=float, default=0.0)
    ap.add_argument('--gate-cents', type=float, default=10.0)
    ap.add_argument('--gate-spur', type=float, default=40.0)
    ap.add_argument('--gate-rms', type=float, default=0.1)
    a = ap.parse_args()
    x = np.fromfile(a.inp, dtype='<f4').astype(np.float64).reshape(-1, 2)
    y = np.fromfile(a.rt, dtype='<f4').astype(np.float64).reshape(-1, 2)
    fails = []
    if len(x) != len(y):
        fails.append(f'长度差 {len(y) - len(x)}')
    n = min(len(x), len(y))
    d_rms = 20 * np.log10(np.sqrt((y[:n] ** 2).mean()) / np.sqrt((x[:n] ** 2).mean()))
    if abs(d_rms) > a.gate_rms:
        fails.append(f'RMS {d_rms:+.2f}dB')
    # 谱判据取左声道（440Hz@44.1k 落在整数 bin，峰 bin 即峰频）
    X = np.abs(np.fft.rfft(x[:n, 0]))
    Y = np.abs(np.fft.rfft(y[:n, 0]))
    kx, ky = int(np.argmax(X)), int(np.argmax(Y))
    cents = 1200 * np.log2(ky / kx) if kx > 0 and ky > 0 else float('inf')
    if abs(cents - a.expect_cents) > a.gate_cents:
        fails.append(f'峰频 {cents:+.2f} 音分（期望 {a.expect_cents:+g}±{a.gate_cents:g}）')
    f = np.fft.rfftfreq(n, 1.0 / a.sr)
    others = Y[np.abs(f - f[ky]) > 20.0]
    spur = 20 * np.log10(Y[ky] / max(float(others.max()), 1e-12)) if others.size else float('inf')
    if spur < a.gate_spur:
        fails.append(f'杂散抑制 {spur:.1f}dB')
    print(f'{a.case}: {"PASS" if not fails else "FAIL"}  '
          f'长度差={len(y) - len(x)}  峰频={cents:+.2f} 音分(期望{a.expect_cents:+g}±{a.gate_cents:g})  '
          f'杂散抑制={spur:.1f}dB(≥{a.gate_spur:g})  RMS={d_rms:+.2f}dB(±{a.gate_rms:g})'
          + (f'  <- 不满足: {"; ".join(fails)}' if fails else ''))
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
