#!/usr/bin/env python3
# fft-numpy-check.py — FFT-PR-005：FftPlan 前向输出对照 numpy.fft（double 基准）
# 用法：fft-numpy-check.py [fft_test 路径=build-linux/fft_test]（仓库根执行）
# 判据（docs/spec/fft-rewrite.md §3）：n∈{512,6144,8192} 幅/相复合误差 RMS ≤ −100dB
import os
import subprocess
import sys
import tempfile

import numpy as np

NS = (512, 6144, 8192)
THRESH_DB = -100.0


def read_f32c(path):
    """读 fft_test dump 的交织复数裸流（re,im 交替 float32），升 double 返回"""
    a = np.fromfile(path, dtype="<f4")
    return a[0::2].astype(np.float64) + 1j * a[1::2].astype(np.float64)


def main():
    exe = sys.argv[1] if len(sys.argv) > 1 else "build-linux/fft_test"
    ok = True
    with tempfile.TemporaryDirectory() as d:
        for n in NS:
            pfx = os.path.join(d, "n%d" % n)
            subprocess.run([exe, "dump", str(n), pfx], check=True,
                           stdout=subprocess.DEVNULL)
            got = read_f32c(pfx + "-fwd.f32c")
            ref = np.fft.fft(read_f32c(pfx + "-in.f32c"))
            err = got - ref
            rel_db = 10 * np.log10(np.sum(np.abs(err) ** 2) / np.sum(np.abs(ref) ** 2))
            mag = np.max(np.abs(np.abs(got) - np.abs(ref))) / np.sqrt(  # 幅值最差偏差（按谱 RMS 归一，信息项）
                np.mean(np.abs(ref) ** 2))
            p = rel_db <= THRESH_DB
            ok = ok and p
            print("  n=%5d 幅/相复合 RMS %7.1f dB  幅值最差(谱RMS归一) %.2e  %s"
                  % (n, rel_db, mag, "PASS" if p else "FAIL"))
    print("== numpy 对照全绿 ==" if ok else "== numpy 对照存在 FAIL ==")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
