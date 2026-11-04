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
SR_48 = 48000   # 视频源常见率（PAR-SR 输出跟随源用例）
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


def dfn3_speech(seed: int, n: int, sr: int) -> np.ndarray:
    """FR-4（PAR-DF）合成 noisy 语音：150Hz 基频带颤音 + 1/h 谐波堆 + 音节包络 + 10dB 白噪。

    n 样本（48k 单声道）；起音相位 +π 使 t=0 即有声（近零 golden 会让相对 dB 判据失真）。
    平稳周期结构会让注意力记忆锚定——SEAM 误差较真语音偏大（OQ-2 数据，fr4 round-1.md）。
    """
    rng = np.random.default_rng(seed)
    t = np.arange(n) / sr
    f0 = 150 + 6 * np.sin(2 * np.pi * 0.7 * t)
    ph = 2 * np.pi * np.cumsum(f0) / sr
    v = np.zeros(n)
    for h in range(1, 12):
        v += (0.28 / h) * np.sin(ph * h + 0.3 * h)
    v *= 1.0 + 0.5 * np.sin(2 * np.pi * 3.1 * t)
    syll = 0.5 * (1 + np.sin(2 * np.pi * 1.7 * t + np.pi - 1.2))
    v *= np.maximum(0.0, syll - 0.15)
    v += 0.003 * rng.standard_normal(n)
    noise = rng.standard_normal(n) * 0.03
    sp = np.sqrt(np.mean(v ** 2)); sn = np.sqrt(np.mean(noise ** 2))
    y = v + noise * (sp / sn / 10 ** 0.5)
    y = (y / np.abs(y).max() * 0.7).astype(np.float32)
    return y[:, None]  # (n,1) 单声道，与其余素材同形


# (名字, 采样率, 声道数, 生成器)——边界素材盯 hop 边界：MDX kHop=1024、GTCRN hop=256
CASES = [
    ('sine1k',    sine(1000, 5.0, SR_ST, 2),   SR_ST, 'sanity'),
    ('sine440',   sine(440, 5.0, SR_ST, 2),    SR_ST, 'PAR-PT 变调往返靶（round-1 判据口径）'),
    ('white',     white(5.0, SR_ST, 2),        SR_ST, '全频带'),
    ('hum50',     hum(50, 5.0, SR_ST),         SR_ST, 'DSP 50Hz 靶'),
    ('hum60',     hum(60, 5.0, SR_ST),         SR_ST, 'DSP 60Hz 靶（PAR-DS-005）'),
    ('white16mo', white(5.0, SR_MO, 1),        SR_MO, 'GTCRN 全频带'),
    ('sine16mo',  sine(1000, 5.0, SR_MO, 1),   SR_MO, 'GTCRN sanity'),
    ('odd',       white(1024 * 7 + 123, 0, 0, 0) if False else None, SR_ST, '非整 hop 尾（1024*7+123 样本）'),
    ('odd1s',     None,                             SR_ST, '非整 hop 尾·1s（1024*43+123，MDX 用：引擎契约最短 ~0.17s，odd 太短被拒）'),
    ('short',     white(300 / SR_ST, SR_ST, 2), SR_ST, '短于 1 hop（300 样本）'),
    ('multi12s',  white(12.0, SR_ST, 2),       SR_ST, '多 batch（>dimT 帧，MDX 批间衔接）'),
    ('white48k',  white(5.0, SR_48, 2),        SR_48, 'PAR-SR 48k 全频带（直通 bit-exact + DSP@48k parity）'),
    ('sine48k',   sine(440, 5.0, SR_48, 2),    SR_48, 'PAR-SR 48k 变调靶（+2 半音谱判据）'),
    # GTCRN 边界组（round-3）：white16mo 前 N 样本前缀（同 seed 同生成器 → 逐字节同前缀）
    ('bound100',  None, SR_MO, 'GTCRN 边界：短于 hop（零输出约定）'),
    ('bound255',  None, SR_MO, 'GTCRN 边界：hop−1（零输出约定）'),
    ('bound256',  None, SR_MO, 'GTCRN 边界：恰一 hop（反射深折叠）'),
    ('bound1025', None, SR_MO, 'GTCRN 边界：odd 尾（1 hop+1）'),
    ('bound4097', None, SR_MO, 'GTCRN 边界：odd 尾（16 hop+1）'),
    # FR-4（PAR-DF）：DFN3 引擎规格 48k/1ch，hop=480；dfn3noisy65 主 parity 素材（65s 跨两块，
    # SEAM 同素材），dfn3bound/dfn3short 边界组（τ<2 零初始化发射 + 非整 hop 尾）
    ('dfn3noisy65', None, SR_48, 'PAR-DF 主素材：65s 合成 noisy 语音（seed 42）'),
    ('dfn3bound',   None, SR_48, 'PAR-DF 边界：480*7+377（seed 43，立即起音）'),
    ('dfn3short',   None, SR_48, 'PAR-DF 边界：480*3+123（最短可发射 3 帧）'),
]


def main():
    check = '--check' in sys.argv
    if check:
        import scipy  # noqa: F401  自检依赖在位
        print(f'CHECK OK: 将生成 {len(CASES)} 组素材 → {OUT}')
        return
    OUT.mkdir(parents=True, exist_ok=True)
    # 增量合并：重生成素材不得抹掉 gen_golden 登记的 golden_* 哈希
    old = json.loads((OUT / 'fixtures.json').read_text()) if (OUT / 'fixtures.json').is_file() else {}
    fixtures = {}
    for name, arr, sr, _ in CASES:
        if name in ('odd', 'odd1s'):  # 非整 hop：取 white 的前 N 样本
            n = 1024 * 7 + 123 if name == 'odd' else 1024 * 43 + 123
            arr = white(5.0, SR_ST, 2)[:n]
        elif name.startswith('bound'):  # GTCRN 边界：white16mo（16k 单声道）前 N 样本前缀
            arr = white(5.0, SR_MO, 1)[:int(name[5:])]
        elif name.startswith('dfn3'):  # PAR-DF 素材：合成 noisy 语音（48k 单声道）
            n = {'dfn3noisy65': 48000 * 65, 'dfn3bound': 480 * 7 + 377, 'dfn3short': 480 * 3 + 123}[name]
            arr = dfn3_speech(42 if name == 'dfn3noisy65' else 43, n, SR_48)
        f32 = OUT / f'{name}.f32'
        wav = OUT / f'{name}.wav'
        arr.tofile(f32)
        wavfile.write(wav, sr, arr)
        entry = {'f32': sha256(f32), 'wav': sha256(wav),
                 'frames': int(arr.shape[0]), 'sr': sr, 'ch': int(arr.shape[1])}
        entry.update({k: v for k, v in old.get(name, {}).items() if k.startswith('golden_')})
        fixtures[name] = entry
        print(f'{name}: {arr.shape[0]} 帧 @{sr}/{arr.shape[1]}ch')
    (OUT / 'fixtures.json').write_text(json.dumps(fixtures, indent=1))
    print(f'fixtures.json 已写 {OUT}')


if __name__ == '__main__':
    main()
