// fft_probe.cpp — util/fft.h 架构审视测试探针（2026-10-14）
// 目的：给 docs/spec/architecture-review.md 提供 FFT 精度/耗时/分配的硬数据
// 依据源码：src/util/fft.h（fft_core 基-2 递推 twiddle / FftPlan Bluestein 全 float）
// 编译：g++ -std=c++20 -O2 -Wall -Wextra -I<repo>/src fft_probe.cpp -o fft_probe
#include "util/fft.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <random>
#include <vector>

// ---- 全局分配计数（replaceable operator new，统计本进程全部堆分配）----
static long g_allocs = 0;
static long g_alloc_bytes = 0;
static bool g_count_on = false;// 只在显式开启时计数，避免污染测试框架自身分配

void* operator new(std::size_t n) {
    if (g_count_on) { g_allocs++; g_alloc_bytes += static_cast<long>(n); }
    if (void* p = std::malloc(n)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

namespace {

const long double kPiL = 3.141592653589793238462643383279502884L;// long double 圆周率

// 朴素 DFT 孤立真值（long double），n ≤ 2048 可用
void dft_naive(const std::vector<float>& re, const std::vector<float>& im,
               std::vector<long double>& R, std::vector<long double>& I) {
    const int n = static_cast<int>(re.size());
    R.assign(n, 0.0L);
    I.assign(n, 0.0L);
    for (int k = 0; k < n; ++k) {
        long double sr = 0.0L, si = 0.0L;
        for (int t = 0; t < n; ++t) {
            const long double ang = -2.0L * kPiL * k * t / n;
            const long double c = cosl(ang), s = sinl(ang);
            sr += re[t] * c - im[t] * s;
            si += re[t] * s + im[t] * c;
        }
        R[k] = sr;
        I[k] = si;
    }
}

std::vector<float> make_random(int n, std::uint32_t seed) {
    std::vector<float> v(static_cast<size_t>(n));
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-1.f, 1.f);
    for (int i = 0; i < n; ++i) v[static_cast<size_t>(i)] = dist(rng);
    return v;
}

// T1/T2：radix-2 与 FftPlan 对朴素 DFT 的最大系数误差 + 往返误差
void test_vs_oracle(const char* tag, int n, std::uint32_t seed) {
    std::vector<float> re = make_random(n, seed), im = make_random(n, seed + 1);
    std::vector<long double> R, I;
    dft_naive(re, im, R, I);
    std::vector<float> fre = re, fim = im;
    fft_forward(fre.data(), fim.data(), n);
    long double maxAbs = 0.0L, maxRef = 0.0L;
    for (int k = 0; k < n; ++k) {
        const long double er = fre[k] - R[k], ei = fim[k] - I[k];
        const long double e = fabsl(er) > fabsl(ei) ? fabsl(er) : fabsl(ei);
        if (e > maxAbs) maxAbs = e;
        const long double r = fabsl(R[k]) > fabsl(I[k]) ? fabsl(R[k]) : fabsl(I[k]);
        if (r > maxRef) maxRef = r;
    }
    // 往返（先正向再逆向；fft_inverse 含 1/n）
    std::vector<float> bre = re, bim = im;
    fft_forward(bre.data(), bim.data(), n);
    fft_inverse(bre.data(), bim.data(), n);
    long double rt = 0.0L;
    for (int k = 0; k < n; ++k) {
        const long double e = fabsl(bre[k] - re[k]) + fabsl(bim[k] - im[k]);
        if (e > rt) rt = e;
    }
    std::printf("%s | n=%5d | 最大系数绝对误差=%.3Le（相对谱峰 %.2Le）| 往返误差=%.3Le\n",
                tag, n, maxAbs, maxAbs / (maxRef > 0 ? maxRef : 1), rt);
}

// T3：FftPlan(roundtrip + Parseval)——大尺寸无朴素真值时的判据
void test_plan_roundtrip(int n) {
    FftPlan plan;
    plan.init(n);
    std::vector<float> re = make_random(n, 7), im = make_random(n, 8);
    const std::vector<float> xre = re, xim = im;
    plan.run(re.data(), im.data(), -1);
    long double power = 0.0L;
    for (int k = 0; k < n; ++k)
        power += (long double)re[k] * re[k] + (long double)im[k] * im[k];
    plan.run(re.data(), im.data(), +1);// FftPlan 逆已含 1/n
    long double rt = 0.0L;
    for (int k = 0; k < n; ++k) {
        const long double e = fabsl(re[k] - xre[k]) + fabsl(im[k] - xim[k]);
        if (e > rt) rt = e;
    }
    // Parseval：Σ|X|² = n·Σ|x|²
    long double xpower = 0.0L;
    for (int k = 0; k < n; ++k)
        xpower += (long double)xre[k] * xre[k] + (long double)xim[k] * xim[k];
    std::printf("PLAN | n=%5d | 往返误差=%.3Le | Parseval 偏差=%.3Le\n",
                n, rt, fabsl(power - (long double)n * xpower) / ((long double)n * xpower));
}

// T4：同尺寸 radix-2 直通 vs FftPlan(Bluestein) 的精度对比（n=2^k）
void test_pow2_headtohead(int n) {
    std::vector<float> re = make_random(n, 42), im = make_random(n, 43);
    std::vector<float> rre = re, rim = im, bre = re, bim = im;
    fft_forward(rre.data(), rim.data(), n);
    FftPlan plan;
    plan.init(n);
    plan.run(bre.data(), bim.data(), -1);
    long double maxDev = 0.0L, peak = 0.0L;
    for (int k = 0; k < n; ++k) {
        const long double dr = bre[k] - rre[k], di = bim[k] - rim[k];
        const long double d = fabsl(dr) > fabsl(di) ? fabsl(dr) : fabsl(di);
        if (d > maxDev) maxDev = d;
        const long double p = fabsl(rre[k]) > fabsl(rim[k]) ? fabsl(rre[k]) : fabsl(rim[k]);
        if (p > peak) peak = p;
    }
    std::printf("H2H  | n=%5d | Bluestein 相对 radix-2 最大系数偏差=%.3Le（相对谱峰 %.2Le）\n",
                n, maxDev, maxDev / peak);
}

// T5：耗时（每尺寸取 3 轮最优）
double bench(int n, bool usePlan, int iters) {
    std::vector<float> re = make_random(n, 5), im = make_random(n, 6);
    double best = 1e30;
    for (int round = 0; round < 3; ++round) {
        const auto t0 = std::chrono::steady_clock::now();
        if (usePlan) {
            FftPlan plan;
            plan.init(n);
            const auto t1 = std::chrono::steady_clock::now();
            if (round == 0)
                std::printf("     |   init 预计算 %.1f ms", std::chrono::duration<double, std::milli>(t1 - t0).count());
            for (int i = 0; i < iters; ++i) plan.run(re.data(), im.data(), -1);
        } else {
            for (int i = 0; i < iters; ++i) {
                fft_forward(re.data(), im.data(), n);
                fft_inverse(re.data(), im.data(), n);// 与 PLAN 的 run(-1)+run(+1) 对齐为完整往返
            }
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / iters;
        if (ms < best) best = ms;
    }
    return best;
}

// T5：分配行为——FftPlan 稳态测量。
// 注：原计划的「拷贝返回 vs 复用」对照被 GCC 15 -O2 的堆消除吃掉（合成循环无真实消费者，
// 编译器证明分配无副作用可整体删除）——该事实属语言级保证（vector 区间构造必分配），
// 无需实证；真实转码循环中 vector 被 memcpy 消费，不可消除。
void test_alloc_pattern() {
    g_allocs = 0;
    g_alloc_bytes = 0;
    {
        FftPlan plan;
        plan.init(6144);
        g_allocs = 0;
        g_alloc_bytes = 0;
        g_count_on = true;
        std::vector<float> re(6144, 1.f), im(6144, 0.f);
        for (int i = 0; i < 100; ++i) plan.run(re.data(), im.data(), -1);
        g_count_on = false;
        std::printf("ALLOC| FftPlan::run 稳态 100 次：%ld 次分配 / %ld 字节\n", g_allocs, g_alloc_bytes);
    }
}

}// namespace

int main() {
    std::printf("== T1 radix-2 对朴素 DFT(long double) ==\n");
    for (int n : {64, 256, 1024, 2048}) test_vs_oracle("RADIX", n, 11);

    std::printf("== T2 FftPlan 大尺寸往返 + Parseval ==\n");
    for (int n : {6144, 7680, 8192, 12288}) test_plan_roundtrip(n);

    std::printf("== T3 2 的幂尺寸 Bluestein vs radix-2 精度 ==\n");
    for (int n : {4096, 8192}) test_pow2_headtohead(n);

    std::printf("== T4 耗时（完整往返，3 轮最优均值）==\n");
    for (int n : {4096, 8192}) {
        const double r = bench(n, false, 200);
        std::printf("TIME | n=%5d | radix-2 往返 %.3f ms", n, r);
        const double p = bench(n, true, 200);
        std::printf(" | FftPlan 往返 %.3f ms | 慢 %.1fx\n", p, p / r);
    }
    for (int n : {6144, 7680, 12288}) {
        const double p = bench(n, true, 100);
        std::printf("TIME | n=%5d | FftPlan 往返 %.3f ms\n", n, p);
    }

    std::printf("== T5 分配行为 ==\n");
    test_alloc_pattern();
    return 0;
}
