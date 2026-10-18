#!/usr/bin/env python3
"""WP-1.1 素材生成：确定性合成（固定 seed numpy），同内容产 .f32 裸流（喂 engine_test）
与 .wav pcm_f32le（喂参考实现，免 16bit 量化垫噪）。金样本哈希登记 fixtures.json。

素材落 /test/parity/（gitignore），入库只留本配方。用法：gen_inputs.py [--check]
"""
import hashlib
import json
import sys
from pathlib import Path

import numpy as np
from scipy.io import wavfile

SR_ST = 44100   # MDX/DSP 引擎规格
SR_MO = 16000   # GTCRN 引擎规格
SEED = 20261018
OUT = Path(__file__).resolve().parents[4] / 'test' / 'parity'


def sha256(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 16), b''):
            h.update(b)
    return h.hexdigest()


def hum(f0: float, dur: float, sr: int) -> np.ndarray:
    """f0 基波 + 2/3 次谐波 + 低电平噪声（DSP 陷波靶子），双声道同源。"""
    rng = np.random.default_rng(SEED)
    t = np.arange(int(sr * dur)) / sr
    x = sum((0.6 / (k + 1)) * np.sin(2 * np.pi * f0 * (k + 1) * t + 0.3 * k) for k in range(3))
    x = x / np.abs(x).max() * 0.5 + 0.01 * rng.standard_normal(len(t))
    return np.stack([x, x], axis=1).astype(np.float32)


def white(dur: float, sr: int, ch: int) -> np.ndarray:
    rng = np.random.default_rng(SEED)
    return (0.2 * rng.standard_normal((int(sr * dur), ch))).astype(np.float32)


def sine(freq: float, dur: float, sr: int, ch: int) -> np.ndarray:
    t = np.arange(int(sr * dur)) / sr
    x = 0.5 * np.sin(2 * np.pi * freq * t)
    return (np.stack([x] * ch, axis=1)).astype(np.float32)


# (名字, 采样率, 声道数, 生成器)——边界素材盯 hop 边界：MDX kHop=1024、GTCRN hop=256
CASES = [
    ('sine1k',    sine(1000, 5.0, SR_ST, 2),   SR_ST, 'sanity'),
    ('white',     white(5.0, SR_ST, 2),        SR_ST, '全频带'),
    ('hum50',     hum(50, 5.0, SR_ST),         SR_ST, 'DSP 50Hz 靶'),
    ('hum60',     hum(60, 5.0, SR_ST),         SR_ST, 'DSP 60Hz 靶（WP-1.6 后）'),
    ('white16mo', white(5.0, SR_MO, 1),        SR_MO, 'GTCRN 全频带'),
    ('sine16mo',  sine(1000, 5.0, SR_MO, 1),   SR_MO, 'GTCRN sanity'),
    ('odd',       white(1024 * 7 + 123, 0, 0, 0) if False else None, SR_ST, '非整 hop 尾（1024*7+123 样本）'),
    ('short',     white(300 / SR_ST, SR_ST, 2), SR_ST, '短于 1 hop（300 样本）'),
    ('multi12s',  white(12.0, SR_ST, 2),       SR_ST, '多 batch（>dimT 帧，MDX 批间衔接）'),
]


def main():
    check = '--check' in sys.argv
    if check:
        import scipy  # noqa: F401  自检依赖在位
        print(f'CHECK OK: 将生成 {len(CASES)} 组素材 → {OUT}')
        return
    OUT.mkdir(parents=True, exist_ok=True)
    fixtures = {}
    for name, arr, sr, _ in CASES:
        if name == 'odd':  # 非整 hop：取 white 的前 1024*7+123 样本
            arr = white(5.0, SR_ST, 2)[:1024 * 7 + 123]
        f32 = OUT / f'{name}.f32'
        wav = OUT / f'{name}.wav'
        arr.tofile(f32)
        wavfile.write(wav, sr, arr)
        fixtures[name] = {'f32': sha256(f32), 'wav': sha256(wav),
                          'frames': int(arr.shape[0]), 'sr': sr, 'ch': int(arr.shape[1])}
        print(f'{name}: {arr.shape[0]} 帧 @{sr}/{arr.shape[1]}ch')
    (OUT / 'fixtures.json').write_text(json.dumps(fixtures, indent=1))
    print(f'fixtures.json 已写 {OUT}')


if __name__ == '__main__':
    main()
