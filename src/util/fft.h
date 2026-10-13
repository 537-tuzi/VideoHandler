#pragma once

#include <cmath>
#include <utility>
#include <vector>

inline constexpr double kPi = 3.14159265358979323846;// 圆周率：全项目统一

// 基-2 迭代 FFT：原地变换 re/im 数组，n 须为 2 的幂
inline void fft_core(float* re, float* im, int n, int sign) {
    for (int i = 1, j = 0; i < n; ++i) {// 位反转置换
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang = sign * 2 * kPi / len;
        const float wr = static_cast<float>(cos(ang));
        const float wi = static_cast<float>(sin(ang));
        for (int i = 0; i < n; i += len) {
            float cwr = 1.f, cwi = 0.f;
            for (int k = 0; k < len / 2; ++k) {
                const int a = i + k, b = i + k + len / 2;
                const float vr = re[b] * cwr - im[b] * cwi;
                const float vi = re[b] * cwi + im[b] * cwr;
                re[b] = re[a] - vr;
                im[b] = im[a] - vi;
                re[a] += vr;
                im[a] += vi;
                const float nwr = cwr * wr - cwi * wi;
                cwi = cwr * wi + cwi * wr;
                cwr = nwr;
            }
        }
    }
}

// 前向 FFT（约定 e^{-j2πkn/N}）
inline void fft_forward(float* re, float* im, int n) {
    fft_core(re, im, n, -1);
}

// 逆 FFT：共轭技巧实现，结果已除以 n
inline void fft_inverse(float* re, float* im, int n) {
    fft_core(im, re, n, -1);// 交换实虚等价于取共轭，结果落在原数组对应位置
    const float inv = 1.f / n;
    for (int i = 0; i < n; ++i) {
        re[i] *= inv;
        im[i] *= inv;
    }
}

// 任意长度 DFT 计划：Bluestein 算法（chirp-z），复用基-2 FFT，构造时预计算表
struct FftPlan {
    int n = 0, m = 0;
    std::vector<float> bre, bim;// b 序列的 M 点 FFT（预计算）
    std::vector<float> cre, cim;// 工作缓冲
    std::vector<double> tcos, tsin;// chirp 角表 e^{i·s·πn²/n}

    void init(int len) {
        n = len;
        m = 1;
        while (m < 2 * n - 1) m <<= 1;
        tcos.resize(static_cast<size_t>(n));
        tsin.resize(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            const double ang = fmod(kPi * static_cast<double>(i) * i / n, 2 * kPi);
            tcos[static_cast<size_t>(i)] = cos(ang);
            tsin[static_cast<size_t>(i)] = sin(ang);
        }
        // b[j] = e^{-iπj²/n}，j 取模嵌入长度 m（b 关于 j 偶对称）
        std::vector<float> bRe(static_cast<size_t>(m), 0.f), bIm(static_cast<size_t>(m), 0.f);
        bRe[0] = 1.f;
        for (int j = 1; j < n; ++j) {
            bRe[static_cast<size_t>(j)] = static_cast<float>(tcos[static_cast<size_t>(j)]);
            bIm[static_cast<size_t>(j)] = static_cast<float>(tsin[static_cast<size_t>(j)]);
            bRe[static_cast<size_t>(m - j)] = bRe[static_cast<size_t>(j)];
            bIm[static_cast<size_t>(m - j)] = bIm[static_cast<size_t>(j)];
        }
        fft_forward(bRe.data(), bIm.data(), m);
        bre = std::move(bRe);
        bim = std::move(bIm);
        cre.assign(static_cast<size_t>(m), 0.f);
        cim.assign(static_cast<size_t>(m), 0.f);
    }

    // sign=-1 前向 DFT，+1 逆 DFT（未归一化），原地变换 re/im 前 n 个元素
    void run(float* re, float* im, int sign) {
        for (int i = 0; i < n; ++i) {// a[n] = x[n]·e^{i·sign·θn}
            const double c = tcos[static_cast<size_t>(i)], s = tsin[static_cast<size_t>(i)];
            const float ar = re[i] * static_cast<float>(c) - im[i] * static_cast<float>(sign * s);
            const float ai = re[i] * static_cast<float>(sign * s) + im[i] * static_cast<float>(c);
            cre[static_cast<size_t>(i)] = ar;
            cim[static_cast<size_t>(i)] = ai;
        }
        for (int i = n; i < m; ++i) {
            cre[static_cast<size_t>(i)] = 0.f;
            cim[static_cast<size_t>(i)] = 0.f;
        }
        fft_forward(cre.data(), cim.data(), m);
        for (int i = 0; i < m; ++i) {// 频域乘 b（逆变换取共轭 b）
            const float br = bre[i], bi = sign == 1 ? -bim[i] : bim[i];
            const float ar = cre[i] * br - cim[i] * bi;
            const float ai = cre[i] * bi + cim[i] * br;
            cre[i] = ar;
            cim[i] = ai;
        }
        fft_inverse(cre.data(), cim.data(), m);// 循环卷积（已按 m 归一化）
        const float inv = 1.f / n;// 逆 DFT 恒等式自带 1/n 因子
        for (int i = 0; i < n; ++i) {// X[k] = e^{i·sign·θk}·conv[k]
            const double c = tcos[static_cast<size_t>(i)], s = tsin[static_cast<size_t>(i)];
            float xr = cre[i] * static_cast<float>(c) - cim[i] * static_cast<float>(sign * s);
            float xi = cre[i] * static_cast<float>(sign * s) + cim[i] * static_cast<float>(c);
            if (sign == 1) { xr *= inv; xi *= inv; }
            re[i] = xr;
            im[i] = xi;
        }
    }
};
