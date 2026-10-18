#pragma once

#include <cmath>
#include <utility>
#include <vector>

inline constexpr double kPi = 3.14159265358979323846;// 圆周率：全项目统一

namespace fft_detail {

// 旋转因子主表（double）：e^{-j2πk/size}，按需懒建到已请求的最大 2 的幂长度，进程内缓存。
// 任意 len 级蝶形的 W_len^k = 表[k·size/len]（size 是 len 的倍数，索引精确无角乘）。
// 契约：引擎调用为单线程（MediaService busy 锁一次一任务），懒建无并发竞争。
struct Twiddle {
    int size = 0;
    std::vector<double> cosT, sinT;
    void ensure(int n) {
        if (size >= n) return;
        int s = size > 1 ? size : 1;
        while (s < n) s <<= 1;
        cosT.assign(static_cast<size_t>(s), 0.0);
        sinT.assign(static_cast<size_t>(s), 0.0);
        for (int k = 0; k < s; ++k) {
            cosT[static_cast<size_t>(k)] = cos(-2 * kPi * k / s);
            sinT[static_cast<size_t>(k)] = sin(-2 * kPi * k / s);
        }
        size = s;
    }
};
inline Twiddle& twiddle() { static Twiddle t; return t; }

}// namespace fft_detail

// 基-2 迭代 FFT（原地变换 re/im，n 须为 2 的幂，T 取 float/double）：
// 旋转因子查 double 主表，无递推累积误差（旧版链式复乘为 FFTW 误差分类最差档）
template <typename T>
inline void fft_core(T* re, T* im, int n, int sign) {
    for (int i = 1, j = 0; i < n; ++i) {// 位反转置换
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    auto& tw = fft_detail::twiddle();
    tw.ensure(n);
    const int size = tw.size;
    const double* WC = tw.cosT.data();
    const double* WS = tw.sinT.data();
    for (int len = 2; len <= n; len <<= 1) {
        const int half = len >> 1, stride = size / len;
        for (int i = 0; i < n; i += len) {
            for (int k = 0; k < half; ++k) {
                const double wr = WC[static_cast<size_t>(k) * stride];
                const double wi = sign < 0 ? WS[static_cast<size_t>(k) * stride]
                                           : -WS[static_cast<size_t>(k) * stride];
                const int a = i + k, b = a + half;
                const T vr = static_cast<T>(re[b] * wr - im[b] * wi);
                const T vi = static_cast<T>(re[b] * wi + im[b] * wr);
                re[b] = re[a] - vr;
                im[b] = im[a] - vi;
                re[a] += vr;
                im[a] += vi;
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

// 任意长度 DFT 计划：2 的幂直通基-2（零 chirp 开销），否则 Bluestein（chirp-z）。
// Bluestein 加固（WP-1.3）：chirp 表用 pocketfft 式整数索引查单位根表拼接
// （k² 以 +(2k+1) 步进 mod 2n，零角乘误差）；b 序列频谱 init 期以 double 计算一次；
// 运行期 chirp 预乘/后乘用 double 直乘；频域卷积保持 float（实时性能折中，
// parity 不达 −60dB 时再全量升 double——fft_core 已模板化，路径现成）。
struct FftPlan {
    int n = 0, m = 0;
    bool pow2 = false;
    std::vector<float> bre, bim;// b 序列的 M 点 FFT（init 期 double 计算后存 float）
    std::vector<float> cre, cim;// 工作缓冲
    std::vector<double> tcos, tsin;// chirp 表 e^{iπk²/n}

    void init(int len) {
        n = len;
        pow2 = len > 0 && (len & (len - 1)) == 0;
        if (pow2) return;
        m = 1;
        while (m < 2 * n - 1) m <<= 1;
        // 单位根表 e^{iπj/n}（j=0..2n-1，逐点直算无 fmod），k² 以整数步进取模拼接
        std::vector<double> uc(static_cast<size_t>(2 * n)), us(static_cast<size_t>(2 * n));
        for (int j = 0; j < 2 * n; ++j) {
            uc[static_cast<size_t>(j)] = cos(kPi * j / n);
            us[static_cast<size_t>(j)] = sin(kPi * j / n);
        }
        tcos.assign(static_cast<size_t>(n), 0.0);
        tsin.assign(static_cast<size_t>(n), 0.0);
        int64_t idx = 0;// k=0 → k²≡0 (mod 2n)
        for (int k = 0; k < n; ++k) {
            tcos[static_cast<size_t>(k)] = uc[static_cast<size_t>(idx)];
            tsin[static_cast<size_t>(k)] = us[static_cast<size_t>(idx)];
            idx += 2 * k + 1;// (k+1)² = k² + 2k + 1
            if (idx >= 2 * n) idx -= 2 * n;
        }
        // b[j] = e^{+iπj²/n}，j 取模嵌入长度 m（b 关于 j 偶对称）
        std::vector<double> bRe(static_cast<size_t>(m), 0.0), bIm(static_cast<size_t>(m), 0.0);
        bRe[0] = 1.0;
        for (int j = 1; j < n; ++j) {
            bRe[static_cast<size_t>(j)] = tcos[static_cast<size_t>(j)];
            bIm[static_cast<size_t>(j)] = tsin[static_cast<size_t>(j)];
            bRe[static_cast<size_t>(m - j)] = bRe[static_cast<size_t>(j)];
            bIm[static_cast<size_t>(m - j)] = bIm[static_cast<size_t>(j)];
        }
        fft_core(bRe.data(), bIm.data(), m, -1);
        bre.assign(static_cast<size_t>(m), 0.f);
        bim.assign(static_cast<size_t>(m), 0.f);
        for (int i = 0; i < m; ++i) {
            bre[static_cast<size_t>(i)] = static_cast<float>(bRe[static_cast<size_t>(i)]);
            bim[static_cast<size_t>(i)] = static_cast<float>(bIm[static_cast<size_t>(i)]);
        }
        cre.assign(static_cast<size_t>(m), 0.f);
        cim.assign(static_cast<size_t>(m), 0.f);
    }

    // sign=-1 前向 DFT，+1 逆 DFT（已归一化），原地变换 re/im 前 n 个元素
    void run(float* re, float* im, int sign) {
        if (pow2) {
            fft_core(re, im, n, sign);
            if (sign == 1) {// 与 Bluestein 路径同语义：逆变换除以 n
                const float inv = 1.f / n;
                for (int i = 0; i < n; ++i) {
                    re[i] *= inv;
                    im[i] *= inv;
                }
            }
            return;
        }
        for (int i = 0; i < n; ++i) {// a[n] = x[n]·e^{i·sign·θn}（double 角表直乘）
            const double c = tcos[static_cast<size_t>(i)], s = sign * tsin[static_cast<size_t>(i)];
            cre[static_cast<size_t>(i)] = static_cast<float>(re[i] * c - im[i] * s);
            cim[static_cast<size_t>(i)] = static_cast<float>(re[i] * s + im[i] * c);
        }
        for (int i = n; i < m; ++i) {
            cre[static_cast<size_t>(i)] = 0.f;
            cim[static_cast<size_t>(i)] = 0.f;
        }
        fft_forward(cre.data(), cim.data(), m);
        for (int i = 0; i < m; ++i) {// 频域乘 b（逆变换取共轭 b）
            const float br = bre[static_cast<size_t>(i)];
            const float bi = sign == 1 ? -bim[static_cast<size_t>(i)] : bim[static_cast<size_t>(i)];
            const float ar = cre[static_cast<size_t>(i)] * br - cim[static_cast<size_t>(i)] * bi;
            const float ai = cre[static_cast<size_t>(i)] * bi + cim[static_cast<size_t>(i)] * br;
            cre[static_cast<size_t>(i)] = ar;
            cim[static_cast<size_t>(i)] = ai;
        }
        fft_inverse(cre.data(), cim.data(), m);// 循环卷积（已按 m 归一化）
        const float inv = 1.f / n;// 逆 DFT 恒等式自带 1/n 因子
        for (int i = 0; i < n; ++i) {// X[k] = e^{i·sign·θk}·conv[k]
            const double c = tcos[static_cast<size_t>(i)], s = sign * tsin[static_cast<size_t>(i)];
            float xr = static_cast<float>(cre[static_cast<size_t>(i)] * c - cim[static_cast<size_t>(i)] * s);
            float xi = static_cast<float>(cre[static_cast<size_t>(i)] * s + cim[static_cast<size_t>(i)] * c);
            if (sign == 1) { xr *= inv; xi *= inv; }
            re[i] = xr;
            im[i] = xi;
        }
    }
};
