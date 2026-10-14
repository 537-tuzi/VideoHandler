#!/usr/bin/env python3
# fft_sim.py — src/util/fft.h 的算法级 float32 仿真（2026-10-14 架构审视取证）
# 性质：逐运算复刻 fft.h 的舍入点（float32 运算、double 三角函数后转 float），
#      量化「float twiddle 递推 / Bluestein 全 float」的精度风险（P0 债 #2）。
# 限制：非 C++ 编译产物——数值行为与 C++ 同型（IEEE754 单精度逐点舍入一致），
#      但耗时不可代表 C++ 性能；耗时结论待 g++/真机编译补测。
# 对照真值：numpy 双精度 FFT（np.fft.fft 即 e^{-j2πkn/N}，与 fft.h 前向约定一致）
import numpy as np

f32 = np.float32

def fft_core(re, im, n, sign):
    """复刻 fft.h fft_core：位反转 + 逐蝶形 float twiddle 递推，原地"""
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            re[i], re[j] = re[j], re[i]
            im[i], im[j] = im[j], im[i]
    le = 2
    while le <= n:
        ang = sign * 2 * np.pi / le          # double
        wr = f32(np.cos(ang))
        wi = f32(np.sin(ang))
        half = le // 2
        for i in range(0, n, le):
            cwr = f32(1.0)
            cwi = f32(0.0)
            for k in range(half):
                a = i + k
                b = a + half
                vr = f32(f32(re[b] * cwr) - f32(im[b] * cwi))
                vi = f32(f32(re[b] * cwi) + f32(im[b] * cwr))
                re[b] = f32(re[a] - vr)
                im[b] = f32(im[a] - vi)
                re[a] = f32(re[a] + vr)
                im[a] = f32(im[a] + vi)
                nwr = f32(f32(cwr * wr) - f32(cwi * wi))
                cwi = f32(f32(cwr * wi) + f32(cwi * wr))
                cwr = nwr
        le <<= 1

def fft_forward(re, im, n):
    fft_core(re, im, n, -1)

def fft_inverse(re, im, n):
    fft_core(im, re, n, -1)                  # 复刻 fft.h：交换实虚参位
    inv = f32(1.0 / n)
    for i in range(n):
        re[i] = f32(re[i] * inv)
        im[i] = f32(im[i] * inv)

class FftPlan:
    """复刻 fft.h FftPlan：Bluestein，chirp 表 double、b 序列与卷积全 float"""
    def __init__(self, n):
        self.n = n
        m = 1
        while m < 2 * n - 1:
            m <<= 1
        self.m = m
        self.tcos = [0.0] * n
        self.tsin = [0.0] * n
        for i in range(n):
            ang = np.fmod(np.pi * (i * i) / n, 2 * np.pi)
            self.tcos[i] = np.cos(ang)
            self.tsin[i] = np.sin(ang)
        bRe = [f32(0)] * m
        bIm = [f32(0)] * m
        bRe[0] = f32(1)
        for j in range(1, n):
            bRe[j] = f32(self.tcos[j])
            bIm[j] = f32(self.tsin[j])
            bRe[m - j] = bRe[j]
            bIm[m - j] = bIm[j]
        bRe = bRe.copy(); bIm = bIm.copy()
        fft_forward(bRe, bIm, m)
        self.bre = bRe
        self.bim = bIm

    def run(self, re, im, sign):
        n, m = self.n, self.m
        cre = [f32(0)] * m
        cim = [f32(0)] * m
        for i in range(n):
            c = self.tcos[i]
            s = self.tsin[i] * sign
            fc = f32(c); fs = f32(s)
            cre[i] = f32(f32(re[i] * fc) - f32(im[i] * fs))
            cim[i] = f32(f32(re[i] * fs) + f32(im[i] * fc))
        fft_forward(cre, cim, m)
        for i in range(m):
            br = self.bre[i]
            bi = f32(-self.bim[i]) if sign == 1 else self.bim[i]
            ar = f32(f32(cre[i] * br) - f32(cim[i] * bi))
            ai = f32(f32(cre[i] * bi) + f32(cim[i] * br))
            cre[i] = ar
            cim[i] = ai
        fft_inverse(cre, cim, m)
        inv = f32(1.0 / n)
        for i in range(n):
            c = self.tcos[i]
            s = self.tsin[i] * sign
            fc = f32(c); fs = f32(s)
            xr = f32(f32(cre[i] * fc) - f32(cim[i] * fs))
            xi = f32(f32(cre[i] * fs) + f32(cim[i] * fc))
            if sign == 1:
                xr = f32(xr * inv)
                xi = f32(xi * inv)
            re[i] = xr
            im[i] = xi

def make_random(n, seed):
    rng = np.random.default_rng(seed)
    return [f32(x) for x in rng.uniform(-1, 1, n)], [f32(x) for x in rng.uniform(-1, 1, n)]

def test_radix_vs_oracle(n, seed):
    re, im = make_random(n, seed)
    ref = np.fft.fft(np.array(re) + 1j * np.array(im))       # 双精度真值
    fre, fim = re.copy(), im.copy()
    fft_forward(fre, fim, n)
    err = max(max(abs(fre[k] - ref[k].real), abs(fim[k] - ref[k].imag)) for k in range(n))
    peak = max(max(abs(ref[k].real), abs(ref[k].imag)) for k in range(n))
    bre, bim = re.copy(), im.copy()
    fft_forward(bre, bim, n)
    fft_inverse(bre, bim, n)
    rt = max(abs(bre[k] - re[k]) + abs(bim[k] - im[k]) for k in range(n))
    print(f"RADIX | n={n:5d} | vs 双精度最大系数误差={err:.3e}（相对谱峰 {err/peak:.2e}）| 往返误差={rt:.3e}")

def test_plan_roundtrip(n):
    plan = FftPlan(n)
    re, im = make_random(n, 7)
    xre, xim = re.copy(), im.copy()
    plan.run(re, im, -1)
    power = sum(f32(re[k]) * f32(re[k]) + f32(im[k]) * f32(im[k]) for k in range(n))
    plan.run(re, im, +1)
    rt = max(abs(re[k] - xre[k]) + abs(im[k] - xim[k]) for k in range(n))
    xpower = sum(xre[k] * xre[k] + xim[k] * xim[k] for k in range(n))
    parseval = abs(power - n * xpower) / (n * xpower)
    print(f"PLAN  | n={n:5d} | 往返误差={rt:.3e} | Parseval 相对偏差={parseval:.3e}")

def test_headtohead(n):
    re, im = make_random(n, 42)
    ref = np.fft.fft(np.array(re) + 1j * np.array(im))
    rre, rim = re.copy(), im.copy()
    fft_forward(rre, rim, n)
    err_radix = max(max(abs(rre[k] - ref[k].real), abs(rim[k] - ref[k].imag)) for k in range(n))
    bre, bim = re.copy(), im.copy()
    plan = FftPlan(n)
    plan.run(bre, bim, -1)
    err_plan = max(max(abs(bre[k] - ref[k].real), abs(bim[k] - ref[k].imag)) for k in range(n))
    peak = max(abs(ref[k]) for k in range(n))
    print(f"H2H   | n={n:5d} | radix-2 误差={err_radix:.3e} | Bluestein 误差={err_plan:.3e}"
          f" | 同输入互差={max(max(abs(bre[k]-rre[k]), abs(bim[k]-rim[k])) for k in range(n)):.3e}"
          f"（相对谱峰 {max(err_radix, err_plan)/peak:.2e}）")

if __name__ == "__main__":
    print("== T1 基-2 直通 vs 双精度真值 ==")
    for n in (64, 256, 1024, 2048, 8192):
        test_radix_vs_oracle(n, 11)
    print("== T2 FftPlan(Bluestein) 往返 + Parseval ==")
    for n in (6144, 7680, 8192, 12288):
        test_plan_roundtrip(n)
    print("== T3 2 的幂尺寸 Bluestein vs 基-2 vs 真值 三方对撞 ==")
    for n in (4096, 8192):
        test_headtohead(n)
