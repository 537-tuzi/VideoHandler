// fft_test — FFT 性质测试宿主（WP-1.3，用例组 FFT-PR，判据见 docs/spec/fft-rewrite.md §3）
// 用法：
//   fft_test self [n ...]     自检：往返恒等/Parseval/线性/冲激（缺省 512 6144 7680 8192 16384；
//                             2 的幂走直通、其余走 Bluestein，两路径都覆盖）
//   fft_test dump <n> <前缀>  写 <前缀>-in.f32c（LCG 随机复信号）与 <前缀>-fwd.f32c（前向谱），
//                             供 scripts/fft-numpy-check.py 对照 numpy（FFT-PR-005）
#include "util/fft.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

// 32 位 LCG：确定性伪随机，跨机器跨构建逐位可复现
struct Lcg {
    uint32_t s;
    float next() { s = 1664525u * s + 1013904223u; return (static_cast<float>(s >> 8) / 8388608.f) - 0.5f; }
};

void genComplex(int n, std::vector<float>& re, std::vector<float>& im, uint32_t seed)
{
    Lcg r{seed};
    re.assign(n, 0.f);
    im.assign(n, 0.f);
    for (int i = 0; i < n; ++i) {
        re[static_cast<size_t>(i)] = r.next();
        im[static_cast<size_t>(i)] = r.next();
    }
}

double rmsDb(const std::vector<float>& re, const std::vector<float>& im,
             const std::vector<float>& re2, const std::vector<float>& im2)
{// 误差 RMS 相对参考 RMS 的 dB（同量纲比较）
    double es = 0, rs = 0;
    for (size_t i = 0; i < re.size(); ++i) {
        const double dr = re[i] - re2[i], di = im[i] - im2[i];
        es += dr * dr + di * di;
        rs += static_cast<double>(re2[i]) * re2[i] + static_cast<double>(im2[i]) * im2[i];
    }
    return 10.0 * std::log10(es / rs);
}

bool writeFile(const char* path, const std::vector<float>& re, const std::vector<float>& im)
{
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    std::vector<float> inter(re.size() * 2);
    for (size_t i = 0; i < re.size(); ++i) {
        inter[i * 2] = re[i];
        inter[i * 2 + 1] = im[i];
    }
    const bool ok = fwrite(inter.data(), sizeof(float), inter.size(), f) == inter.size();
    fclose(f);
    return ok;
}

int selfOne(int n, bool& allPass)
{
    FftPlan plan;
    plan.init(n);
    const bool pow2 = (n & (n - 1)) == 0;
    bool pass = true;

    {// FFT-PR-001 往返恒等：RMS(ifft(fft(x)) − x) 相对 ≤ −130dB
        std::vector<float> re, im, xre, xim;
        genComplex(n, xre, xim, 0x1234u + n);
        re = xre; im = xim;
        plan.run(re.data(), im.data(), -1);
        plan.run(re.data(), im.data(), +1);
        const double db = rmsDb(re, im, xre, xim);
        const bool p = db <= -130.0;
        printf("  n=%5d %-9s 往返 %8.1f dB  %s\n", n, pow2 ? "直通" : "Bluestein", db, p ? "PASS" : "FAIL");
        pass = pass && p;
    }
    {// FFT-PR-002 Parseval：Σ|X|² = n·Σ|x|²，相对误差 ≤ 1e-6
        std::vector<float> re, im, xre, xim;
        genComplex(n, xre, xim, 0x5678u + n);
        re = xre; im = xim;
        double ex = 0;
        for (int i = 0; i < n; ++i) ex += static_cast<double>(xre[i]) * xre[i] + static_cast<double>(xim[i]) * xim[i];
        plan.run(re.data(), im.data(), -1);
        double ek = 0;
        for (int i = 0; i < n; ++i) ek += static_cast<double>(re[i]) * re[i] + static_cast<double>(im[i]) * im[i];
        const double rel = (ek / n - ex) / ex;
        const bool p = std::fabs(rel) <= 1e-6;
        printf("               Parseval %+.2e     %s\n", rel, p ? "PASS" : "FAIL");
        pass = pass && p;
    }
    {// FFT-PR-003 线性：FFT(ax+by) 与 a·FFT(x)+b·FFT(y) 差 ≤ −130dB
        const float a = 0.7f, b = -1.3f;
        std::vector<float> xr, xi, yr, yi, zr, zi, fr, fi, gr, gi;
        genComplex(n, xr, xi, 0x9abcu + n);
        genComplex(n, yr, yi, 0xdef0u + n);
        zr.assign(n, 0.f); zi.assign(n, 0.f);
        for (int i = 0; i < n; ++i) {
            zr[i] = a * xr[i] + b * yr[i];
            zi[i] = a * xi[i] + b * yi[i];
        }
        plan.run(zr.data(), zi.data(), -1);
        fr = xr; fi = xi; gr = yr; gi = yi;
        plan.run(fr.data(), fi.data(), -1);
        plan.run(gr.data(), gi.data(), -1);
        std::vector<float> lr(n), li(n);
        for (int i = 0; i < n; ++i) {
            lr[i] = a * fr[i] + b * gr[i];
            li[i] = a * fi[i] + b * gi[i];
        }
        const double db = rmsDb(zr, zi, lr, li);
        const bool p = db <= -130.0;
        printf("               线性   %8.1f dB  %s\n", db, p ? "PASS" : "FAIL");
        pass = pass && p;
    }
    {// FFT-PR-004 冲激：δ 输入谱全 1（|X[k]−1| ≤ 1e-6）
        std::vector<float> re(static_cast<size_t>(n), 0.f), im(static_cast<size_t>(n), 0.f);
        re[0] = 1.f;
        plan.run(re.data(), im.data(), -1);
        double worst = 0;
        for (int i = 0; i < n; ++i)
            worst = std::max(worst, std::fabs(re[i] - 1.0) + std::fabs(static_cast<double>(im[i])));
        const bool p = worst <= 1e-6;
        printf("               冲激   %8.2e     %s\n", worst, p ? "PASS" : "FAIL");
        pass = pass && p;
    }
    allPass = allPass && pass;
    return pass ? 0 : 1;
}

}// namespace

int main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "用法：fft_test self [n ...] | fft_test dump <n> <前缀>\n");
        return 2;
    }
    if (strcmp(argv[1], "self") == 0) {
        std::vector<int> ns;
        for (int i = 2; i < argc; ++i) ns.push_back(std::atoi(argv[i]));
        if (ns.empty()) ns = {512, 6144, 7680, 8192, 16384};
        bool all = true;
        for (int n : ns) selfOne(n, all);
        printf(all ? "== fft self 全绿 ==\n" : "== fft self 存在 FAIL ==\n");
        return all ? 0 : 1;
    }
    if (strcmp(argv[1], "dump") == 0 && argc == 4) {
        const int n = std::atoi(argv[2]);
        std::vector<float> re, im;
        genComplex(n, re, im, 0xc0deu + static_cast<uint32_t>(n));
        const std::vector<float> zre = re, zim = im;// 原输入留档（run 原地变换）
        FftPlan plan;
        plan.init(n);
        plan.run(re.data(), im.data(), -1);
        char path[512];
        snprintf(path, sizeof(path), "%s-in.f32c", argv[3]);
        if (!writeFile(path, zre, zim)) { fprintf(stderr, "FAIL: 写 %s\n", path); return 1; }
        snprintf(path, sizeof(path), "%s-fwd.f32c", argv[3]);
        if (!writeFile(path, re, im)) { fprintf(stderr, "FAIL: 写 %s\n", path); return 1; }
        printf("OK: dump n=%d 前缀=%s\n", n, argv[3]);
        return 0;
    }
    fprintf(stderr, "用法：fft_test self [n ...] | fft_test dump <n> <前缀>\n");
    return 2;
}
