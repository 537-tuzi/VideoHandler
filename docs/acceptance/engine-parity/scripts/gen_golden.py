#!/usr/bin/env python3
"""WP-1.1 金样本生成：调参考实现产 golden f32，哈希登记 fixtures.json。

子命令：
  dsp  <素材名...>          scipy 双精度 RBJ 级联（与 DspDenoiser 同参：50/100/150Hz Q30 陷波
                            + 100Hz Q0.7071 高通 @44.1k），系数保持 double（DUT 侧 float 存储，
                            差异量级正是本组用例要量的）
  gtcrn <素材名...>         sherpa-onnx 同模型 speech-enhancement（装好后启用）
  mdx  <素材名...>          audio-separator 0.47.0 官方管线（UVR-MDX-NET-Voc_FT，torch cpu），
                            拦截 write_audio 抓模型原始输出（绕开 pydub/ffmpeg FLOAT 导出坑与
                            归一化），每次产 mdx-voc / mdx-inst 两金样本
用法：gen_golden.py <子命令> <素材名...> [--check]；素材须先由 gen_inputs.py 生成。
依赖：test/pydeps 定向安装（绕 diffq：py3.14 无 wheel 且 MDX 路径不用它），见 requirements.txt。
"""
import hashlib
import json
import sys
from pathlib import Path

import numpy as np

OUT = Path(__file__).resolve().parents[4] / 'test' / 'parity'
MDX_MODEL_DIR = Path(__file__).resolve().parents[4] / 'third_party' / 'models' / 'separator'
MDX_MODEL = 'UVR-MDX-NET-Voc_FT.onnx'


def fixtures() -> dict:
    return json.loads((OUT / 'fixtures.json').read_text())


def save_golden(name: str, stem: str, arr: np.ndarray):
    p = OUT / f'{name}.{stem}.golden.f32'
    arr.astype('<f4').tofile(p)
    fx = fixtures()
    fx.setdefault(name, {})[f'golden_{stem}'] = hashlib.sha256(p.read_bytes()).hexdigest()
    (OUT / 'fixtures.json').write_text(json.dumps(fx, indent=1))
    print(f'{p.name}: {arr.shape[0]} 帧')


def rbj_notch(f0: float, q: float, sr: float):
    w0 = 2 * np.pi * f0 / sr
    cw, alpha = np.cos(w0), np.sin(w0) / (2 * q)
    a0 = 1 + alpha
    return (np.array([1, -2 * cw, 1]) / a0,
            np.array([1, -2 * cw / a0, (1 - alpha) / a0]))


def rbj_highpass(f0: float, q: float, sr: float):
    w0 = 2 * np.pi * f0 / sr
    cw, alpha = np.cos(w0), np.sin(w0) / (2 * q)
    a0 = 1 + alpha
    return (np.array([(1 + cw) / 2, -(1 + cw), (1 + cw) / 2]) / a0,
            np.array([1, -2 * cw / a0, (1 - alpha) / a0]))


def do_dsp(names):
    from scipy.signal import lfilter
    sr = 44100.0
    bs, a = [], None
    for h in (1, 2, 3):
        bs.append(rbj_notch(50.0 * h, 30.0, sr))
    hp = rbj_highpass(100.0, 0.7071, sr)
    for name in names:
        x = np.fromfile(OUT / f'{name}.f32', dtype='<f4').reshape(-1, 2).astype(np.float64)
        y = np.empty_like(x)
        for ch in range(2):
            v = x[:, ch]
            for b, a in bs:  # 三级陷波级联
                v = lfilter(b, a, v)
            y[:, ch] = lfilter(hp[0], hp[1], v)
        save_golden(name, 'dsp', y)


def do_mdx(names):
    import logging
    logging.disable(logging.WARNING)
    from audio_separator.separator import Separator
    from audio_separator.separator.common_separator import CommonSeparator

    # 拦截写盘抓模型原始输出：免 pydub FLOAT→ffmpeg '-f float' 坑，也免 soundfile 路径的
    # normalize——DUT 侧引擎不归一化，对等比较须取未归一化值
    caps = []

    def capture(self, stem_path, stem_source):
        caps.append((stem_path, stem_source))

    orig = CommonSeparator.write_audio
    CommonSeparator.write_audio = capture
    try:
        sep = Separator(log_level=logging.ERROR, model_file_dir=str(MDX_MODEL_DIR))
        sep.load_model(model_filename=MDX_MODEL)
        for name in names:
            caps.clear()
            sep.separate(str(OUT / f'{name}.wav'))
            assert len(caps) == 2, f'{name}: 期望双 stem，实得 {len(caps)}'
            for path, src in caps:
                stem = 'mdx-voc' if '(Vocals)' in path else 'mdx-inst'
                save_golden(name, stem, np.asarray(src))
    finally:
        CommonSeparator.write_audio = orig


def main():
    args = [a for a in sys.argv[1:] if a != '--check']
    check = '--check' in sys.argv
    if len(args) < 2:
        sys.exit(__doc__)
    cmd, names = args[0], args[1:]
    for n in names:
        assert (OUT / f'{n}.f32').is_file(), f'缺素材 {n}（先跑 gen_inputs.py）'
    if check:
        print(f'CHECK OK: {cmd} × {names}')
        return
    {'dsp': do_dsp, 'mdx': do_mdx}[cmd](names)


if __name__ == '__main__':
    main()
