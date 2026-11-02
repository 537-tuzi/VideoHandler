#!/usr/bin/env python3
"""WP-1.1 金样本生成：调参考实现产 golden f32，哈希登记 fixtures.json。

子命令：
  dsp  <素材名...>          scipy RBJ 级联（与 DspDenoiser 同参：基频及 2/3 次谐波 Q30 陷波
                            + 100Hz Q0.7071 高通；基频随素材走——hum50/60 素材取 50/60Hz，
                            其余素材对 DUT 缺省 50Hz；sr 取素材在 fixtures.json 登记的元数据，
                            44.1k/48k 素材各自成参考），双精度主参考 + float32 同构参考
                            （系数/信号均 f32，lfilter f32 累加——hum50/hum60 分级判据用，round-1 §2）
  gtcrn <素材名...>         sherpa-onnx OfflineSpeechDenoiser 同模型推理（16k 单声道裸流直入，
                            num_threads=1 与引擎一致；round-3 实锤输出 = (N//hop)·hop 裁头对齐）
  mdx  <素材名...>          audio-separator 0.47.0 官方管线（UVR-MDX-NET-Voc_FT，torch cpu），
                            拦截 write_audio 抓模型原始输出；同时旁路前置 normalize（peak>0.9
                            工作点偏移非引擎差异）并把 ×peak 增益除回引擎域，每次产
                            mdx-voc / mdx-inst 两金样本（round-3 判据）
已存在且哈希与 fixtures.json 登记一致的金样本默认复用（--force 强制重生成——换参考
实现版本后用，重生成后新哈希自动登记）。
用法：gen_golden.py <子命令> <素材名...> [--check|--force]；素材须先由 gen_inputs.py 生成。
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
GTCRN_MODEL = Path(__file__).resolve().parents[4] / 'third_party' / 'models' / 'denoiser' / 'gtcrn_simple.onnx'

FORCE = '--force' in sys.argv


def fixtures() -> dict:
    return json.loads((OUT / 'fixtures.json').read_text())


def golden_ok(name: str, stem: str) -> bool:
    """金样本在位且哈希与 fixtures 登记一致（可复用）。"""
    p = OUT / f'{name}.{stem}.golden.f32'
    return (not FORCE and p.is_file()
            and fixtures().get(name, {}).get(f'golden_{stem}') == hashlib.sha256(p.read_bytes()).hexdigest())


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
    fx = fixtures()
    for name in names:
        sr = float(fx[name]['sr'])# 素材登记采样率：48k 素材（PAR-SR）在源率上成参考，44.1k 素材同旧
        base = {'hum50': 50.0, 'hum60': 60.0}.get(name, 50.0)# 白名单分派（审查 P2-1）：前缀解析对带后缀素材名会崩；其余素材对 DUT 缺省 50Hz
        bs = [rbj_notch(base * h, 30.0, sr) for h in (1, 2, 3)]
        hp = rbj_highpass(100.0, 0.7071, sr)
        x64 = np.fromfile(OUT / f'{name}.f32', dtype='<f4').reshape(-1, 2).astype(np.float64)
        y = np.empty_like(x64)
        y32 = np.empty_like(x64)
        for ch in range(2):
            v = x64[:, ch]
            for b, a in bs:  # 三级陷波级联
                v = lfilter(b, a, v)
            y[:, ch] = lfilter(hp[0], hp[1], v)
            # float32 同构参考：系数与信号均 f32、lfilter f32 累加——DUT 引擎同精度路径
            #（round-1 实锤：hum50 陷波深度被 float32 系数量化限制在 −26.7dB 理论上限，
            #  vs double 只剩 −15.9dB 属引擎固有精度而非实现 bug，故 hum50 用此参考分级判据）
            v32 = x64[:, ch].astype(np.float32)
            for b, a in bs:
                v32 = lfilter(b.astype('<f4'), a.astype('<f4'), v32)
            y32[:, ch] = lfilter(hp[0].astype('<f4'), hp[1].astype('<f4'), v32)
        save_golden(name, 'dsp', y)
        save_golden(name, 'dspf', y32)


def do_gtcrn(names):
    import sherpa_onnx
    den = None# 惰性建：全部素材复用时免加载模型
    for name in names:
        if golden_ok(name, 'gtcrn.sherpa'):
            print(f'{name}.gtcrn.sherpa.golden.f32: 复用（哈希与登记一致）')
            continue
        if den is None:
            cfg = sherpa_onnx.OfflineSpeechDenoiserConfig(
                model=sherpa_onnx.OfflineSpeechDenoiserModelConfig(
                    gtcrn=sherpa_onnx.OfflineSpeechDenoiserGtcrnModelConfig(model=str(GTCRN_MODEL)),
                    num_threads=1))# 单线程与引擎 setup 一致（确定性）
            den = sherpa_onnx.OfflineSpeechDenoiser(cfg)
        x = np.fromfile(OUT / f'{name}.f32', dtype='<f4')
        y = np.asarray(den.run(x, 16000).samples, dtype='<f4')
        save_golden(name, 'gtcrn.sherpa', y)


def do_mdx(names):
    import logging
    logging.disable(logging.WARNING)
    from audio_separator.separator import Separator
    from audio_separator.separator.common_separator import CommonSeparator
    from audio_separator.separator.uvr_lib_v5 import spec_utils

    # 拦截写盘抓模型原始输出：免 pydub FLOAT→ffmpeg '-f float' 坑，也免 soundfile 路径的
    # normalize——DUT 侧引擎不归一化，对等比较须取未归一化值
    caps = []

    def capture(self, stem_path, stem_source):
        caps.append((stem_path, stem_source, self.compensate))

    # 旁路参考前置 normalize（peak>0.9 时 mix×0.9/peak）：非线性模型下 demix(s·x)≠s·demix(x)，
    # 工作点偏移不是引擎实现差异，对等比较须同工作点（round-3 实锤：唯一 peak<0.9 的 odd1s
    # 机器精度一致，其余素材残差与重叠区无关、段A 也差、sine1k 拟合增益恰 0.5=0.9/1.8 级）
    orig_norm = spec_utils.normalize
    spec_utils.normalize = lambda wave, max_peak, min_peak: wave

    orig = CommonSeparator.write_audio
    CommonSeparator.write_audio = capture
    try:
        sep = None# 惰性建：全部素材复用时免加载 torch/模型（秒级 vs 分钟级）
        for name in names:
            if golden_ok(name, 'mdx-voc') and golden_ok(name, 'mdx-inst'):
                print(f'{name}: 复用（mdx-voc/mdx-inst 哈希与登记一致）')
                continue
            if sep is None:
                sep = Separator(log_level=logging.ERROR, model_file_dir=str(MDX_MODEL_DIR))
                sep.load_model(model_filename=MDX_MODEL)
            caps.clear()
            sep.separate(str(OUT / f'{name}.wav'))
            assert len(caps) == 2, f'{name}: 期望双 stem，实得 {len(caps)}'
            x = np.fromfile(OUT / f'{name}.f32', dtype='<f4').reshape(-1, 2)
            peak = float(np.abs(x).max())
            # 注意写盘顺序：secondary 先写、primary 后写（mdx_separator separate 尾段），
            # 按 path 名判别数组角色，勿按捕获顺序
            comp = caps[0][2]
            byname = {'mdx-voc' if '(Vocals)' in p else 'mdx-inst': s for p, s, _ in caps}
            voc = np.asarray(byname['mdx-voc'], dtype=np.float64)
            inst = np.asarray(byname['mdx-inst'], dtype=np.float64)
            # 域重建：参考输出 primary=demix(x)·peak、secondary=x−c·demix(x)·peak（×peak 是
            # 产品级幅度策略非引擎语义）；除回 peak 得引擎域。inst 用代数式
            # secondary + c·(primary − primary/peak) = x − c·demix(x)
            voc_true = voc / peak
            inst_true = inst + comp * (voc - voc / peak)
            # 交叉校验：直接 x − c·voc_true 重算，两条代数路径应一致
            cross = x - comp * voc_true
            d = float(np.abs(inst_true - cross[:len(inst_true)]).max())
            print(f'{name}: peak={peak:.4f} compensate={comp} 域重建交叉校验 max|d|={d:.3e}')
            assert d < 1e-6, f'{name}: inst 域重建自洽失败 {d}'
            save_golden(name, 'mdx-voc', voc_true)
            save_golden(name, 'mdx-inst', inst_true)
    finally:
        CommonSeparator.write_audio = orig
        spec_utils.normalize = orig_norm


def main():
    args = [a for a in sys.argv[1:] if a not in ('--check', '--force')]
    check = '--check' in sys.argv
    if len(args) < 2:
        sys.exit(__doc__)
    cmd, names = args[0], args[1:]
    for n in names:
        assert (OUT / f'{n}.f32').is_file(), f'缺素材 {n}（先跑 gen_inputs.py）'
    if check:
        print(f'CHECK OK: {cmd} × {names}')
        return
    {'dsp': do_dsp, 'gtcrn': do_gtcrn, 'mdx': do_mdx}[cmd](names)


if __name__ == '__main__':
    main()
