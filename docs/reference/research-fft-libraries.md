# FFT 库调研：许可证、精度实践与自研重写方案核验

- 日期：2026-10-14
- 背景：本项目 `src/util/fft.h` 为 124 行 header-only 自研 FFT（基-2 迭代 + Bluestein 任意长度，全 float），供 GTCRN（n_fft=512，2 的幂）与 MDX-Net（n_fft=6144=3·2^11，非 2 的幂）的 STFT 使用。已计划重写（见 `docs/spec/fft-rewrite.md`）。本调研核验该方案与业界实践是否一致。
- 红线：不得引入 GPL 依赖（FFTW、KFR 等出局）； LGPL/MIT/BSD 组合可用。
- 信源纪律：仅采信一手信源——各库官方源码 tarball / 仓库内 README/LICENSE / numpy 与 scipy 官方发行注记 / FFTW 官方 accuracy 页面 / nayuki 官方页面。检索引擎仅用于发现仓库地址（kissfft 的 owner 名、muFFT 的作者归属即由此纠正），不采信二手转述的技术结论。
- 方法与局限：源码均下载至 `/tmp/fftresearch/`（不入库）逐文件核验；`raw.githubusercontent.com` 曾瞬断、`android.googlesource.com` 被本机网络策略拦截、GitHub API 配额耗尽，个别项因此降级为【未确认】，均在文末标注。

## 1. 库盘点表

| 库 | 语言/形态 | 许可证 | 精度 | 非 2 的幂长度支持 | 维护状态（pushed_at 取自 GitHub API 元数据） |
|---|---|---|---|---|---|
| pffft（原版，Pommier 2011） | C，单 .c+.h | FFTPACK 衍生的修改版 BSD（UCAR/NCAR 条款，源码头注明 v5 条款适用于 v4 源） | float 专用（单精度 real+complex） | 混合基 radix 2/3/4/5：合法尺寸 = SIMD 最小尺寸×2^a·3^b·5^c，**6144 原生合法**，无 Bluestein | 原版 bitbucket 上游；社区 fork 延续 |
| pffft（marton78 fork） | C + C++ 包装 + CMake | 同上（LICENSE.txt：UCAR + Pommier 2013 + Ayguen 2019 + Mambro 2020） | float + **double**（`pffft_double.c` 与 float 版共用 `pffft_priv_impl.h`） | 同原版 | **活跃**：2026-09-29，378 stars；含 pffastconv、CMake FetchContent、**MinGW 交叉构建脚本** |
| pffft（hayguen fork） | C | 同上 | float + double（`pffft_double.c` 出处） | 同原版 | 2022-04-10，6 stars |
| pffft（cpuimage fork） | C，单 .c | 同上 | float | 非法尺寸（2^a·3^b·5^c 之外）走 **fork 自加的 Bluestein** | 2018-09-06 冻结 |
| kissfft（mborgerding） | 纯 C（kiss_fft.c/.h）+ C++ 模板 kissfft.hh；**无 Fortran**（Fortran 血统的是 FFTPACK 系） | BSD-3-Clause（每文件 SPDX 头；test/doit.c 单独为 Unlicense） | float（默认）/ double / 定点（i32 变体；kissfft.hh 模板化） | 混合基 radix 2/3/4/5 + 通用蝶形（>5 的素因子，O(p²) 退化），**无 Bluestein**（全库 grep 零命中）；6144 走 radix-3+radix-2/4 直接算 | 接口稳定 20 年（版权 2003–2010 + 现代 SPDX/CMake 化改造并存）；具体推送日期未取到（API 限流），【未确认】 |
| pocketfft（C 版，mreineck） | C99，pocketfft.c/.h | BSD-3（Max-Planck-Society，LICENSE.md） | **double 专用**（数据通路全 double） | 混合基 codelet 2/3/4/5/7/11（复）/2/3/4/5（实）+ **Bluestein**（大素因子时，决策规则见 §3.2）；6144 走混合基 | 活跃：2026-06-30，159 stars；**numpy 1.17 起、scipy 1.4.0 起的后端**，scipy 1.18 切至后继 ducc0.fft |
| ducc0（pocketfft 后继，Reinecke） | C++ 模板（PyPI 源码包） | 多数文件 BSD-3；`unity_roots.h` 为 "BSD-3-Clause OR GPL-2.0-or-later" 双许可（OR 任选，取 BSD-3 即可）；fft.h 的 DCT-IV 段源自 FFTW（BSD 系，Frigo & Johnson 署名） | float/double/long double 模板；**float 时 twiddle 角度强制 double**（见 §3.1） | 全尺寸（pocketfft 衣钵） | 活跃（scipy 1.18 的新后端） |
| muFFT（themaister，Hans-Kristian Arntzen） | C | MIT（COPYING，2015）；注意 bench/test 二进制链 FFTW3（GPLv2+），仅测试工具、核心库不受影响 | float 专用 | **仅 2 的幂**（README Features 明示 "Power-of-two transforms"）→ **6144 直接出局** | 最后活跃时间未取到（API 限流），【未确认】 |
| FFTS（anthonix） | C，运行时 JIT 代码生成（可 `--disable-dynamic-code`） | COPYRIGHT 为 BSD 三条款式文本 | float（`--enable-single` 构建） | README 未明示 radix 集，【未确认】 | 主仓 README 自述 "haven't been able to touch this for many years"；活跃 fork：linkotec、ValveSoftware；meta pushed 2024-07-14（零星） |
| nayuki free-small-fft | C++/多语言参考实现 | MIT（官方页面自述） | double | 含 Bluestein 任意长度 | 教学参考；pffft 原作者评语见 §5 |

排除项（红线或场景不符）：FFTW（GPL）、Intel MKL / Apple vDSP / IPP（专有）、KFR（GPL）。

## 2. 各库要点与一手证据

### 2.1 pffft
- 定位（README 原话）：single precision real and complex 1D FFT，"tries to do it fast, it tries to be correct, and it tries to be small"；SIMD 覆盖 SSE1/AVX/AVX2/Altivec/NEON（含 Apple Silicon）/WASM SIMD。
- 血统：FFTPACKv4（Swarztrauber, NCAR 1985, Fortran77）的 C 移植 + SIMD 改造；`pffft.c:1-46` 源码头逐条给出 UCAR 条款。
- 尺寸规则（`pffft_priv_impl.h:78-98`）：`pffft_is_valid_size(N)` 要求 N = N_min·2^a·3^b·5^c，N_min = simdSz²（complex）或 2·simdSz²（real）。6144 = 3·2^11 在 SSE（N_min=16）下 6144/16 = 384 = 3·2^7，合法。
- radix 内核：passf2/3/4/5，另有限定条件的 passf8（仅 N=32 complex 单趟场景，fork 注释中附 A/B 实测数据）。
- 实时友好：变换函数零内存分配，显式 work 缓冲入参——与本项目 NFR-L2 方向同构。
- 采用者：AOSP `platform/external/pffft`（存在性经检索确认；googlesource 被本机网络拦截，内容未能直接核验）。原 README 与 LICENSE 中**未发现**"学术发表请引用作者"类条款（两份 README 全文 grep "cite/academic" 零命中）——此前传说的引用请求条款未获证实。

### 2.2 kissfft
- "Keep It Simple, Stupid" 混合基 FFT；核心就是 kiss_fft.c/.h 两个文件。
- 任意长度实现方式：kf_factor 分解出 2/3/4/5 用专用 codelet，其余素因子走 `kf_bfly_generic`（`kiss_fft.c:192-230`，查表 twiddle 的 O(p²) 通用蝶形）。**没有 Bluestein**——大素数长度性能退化但结果正确。
- 实数优化 kiss_fftr、多维 kiss_fftnd/ndr；kissfft.hh 提供 C++ 模板（float/double 同源）。
- 采用者（一手核验）：**Rubber Band**（breakfastquay/rubberband）在 `src/ext/kissfft/` 内嵌 kiss_fft/kiss_fftr，meson 构建的 fft 后端选项为 auto/builtin/kissfft/fftw/sleef/vdsp/ipp，自带 Makefile 默认 KissFFT + Speex——商用级音频变速库把 kissfft 作为小体积内嵌选项。

### 2.3 pocketfft（C 版）
- numpy 1.17 发行注记（官方原文）："NumPy's FFT implementation was changed from fftpack to pocketfft, resulting in faster, more accurate transforms and better handling of datasets of prime length"，并明确 "Both implementations have the same ancestor (Fortran77 FFTPACK …), but pocketfft contains additional modifications which improve both accuracy and performance"、"For FFT lengths containing large prime factors, pocketfft uses Bluestein's algorithm, which maintains O(N log N)"。
- scipy 1.4.0 发行注记（仓库内 rst 原文）：scipy.fft "is based on the pypocketfft library, developed by the author of pocketfft which was recently adopted by NumPy as well"；scipy.fftpack 同步切换（PR #10238、#10507）。
- scipy 1.18 发行注记：内部 FFT 后端从 pocketfft 切至后继 `ducc0.fft`（issue #23318、PR #23436），理由之一是长变换 plan 内存占用显著下降。

### 2.4 muFFT
- README 自述：单精度、2 的幂、1D/2D、快卷积（音频滤波场景）、radix-2/4/8 Stockham autosort（免位反转重排）、SSE/SSE3/AVX2 运行时检测、"usually competitive with FFTW3 and FFmpeg/libavcodec FFT"。
- 对本项目：不支持 6144，直接出局；但它是"float 专用 + 2 的幂 + 音频卷积导向"设计取向的对照样本。

## 3. 精度与实践细节

### 3.1 twiddle 因子：点名四家库均为 setup 期预计算表（「业界无递推实现」的说法已被反证限定）
逐库核验结果：

| 库 | twiddle 策略（源码定位） | 精度 |
|---|---|---|
| pffft | setup 期一次性建表，`cffti1_ps`：`float argh = 2·(float)M_PI/n`，逐项 `wa[i] = cosf(fi·argld)`（`FUNC_COS=cosf`）——每项独立调三角函数，无递推累积 | float 直接算（最宽松的一家） |
| kissfft | `kiss_fft.c:357-369`：`double phase = -2π·i/nfft`（全精度 π 字面量）→ `kf_cexp` 以 **double cos/sin** 求值后存入 float 标量（`_kiss_fft_guts.h:158-159`） | double 算、float 存 |
| pocketfft C | README 自述 "more accurate twiddle factor computation"：角度归约到 [0, π/4]，自实现 `sincosm1pi`（fma 极小化多项式，仅 [-0.25, 0.25] 定义域），每 n 点只调 2√n 次 sincos，其余用角度加法定理在 double 域传播（`pocketfft.c:34-110`） | 全 double，且刻意压角域 |
| ducc0 | `unity_roots.h:66`：`using Thigh = conditional<(sizeof(T)>sizeof(double)), T, double>` —— **float 模板的 twiddle 角度与三角求值强制 double**，配合象限对称把角压进 [0, π/4] | float 数据 + double twiddle 生成 |
| FFTW 的评述 | fftw.org/accuracy/comments.html："Inaccurate twiddle factors are the most likely reason for the inaccuracy of an FFT routine"；"most such recurrence formulas accumulate errors as O(√N), O(N), or even O(N²), much faster than the FFT itself (Tasche & Zeuner, 2002)" | —— |

对自研代码的直接含义：`fft.h:25-36` 蝶形内 `cwr/cwi` 递推复乘正是 FFTW 点名的 "trigonometric recurrence"，属最优先消除项；替换为查表后误差增长回落到 Cooley-Tukey 固有的 O(log N)（最坏）/O(√log N)（平均）。

> **2026-10-14 反证修正（`falsify-fft-dfn3.md`）**：「没有一家在蝶形内做递推」的**泛化解读被推翻**——广泛使用的递推实现确实存在：GSL `fft/real_radix2.c`（Buneman **稳定**递推，源码注释自认）与 Numerical Recipes（**double** 递推变量，FFTW 官方页点名其 "O(√N)-accuracy recurrence … with explicitly double-precision variables, which for single-precision transforms produces accurate results"）。但四家点名库源码逐行复核全部属实（另增 CMSIS-DSP、FFmpeg TX 表驱动旁证），且本项目 `fft.h` 是 **float 种子 + float 递推变量**的链式复乘，恰处 FFTW 误差分类的最差档（as117 档 "single-precision variables and correspondingly large errors"），非 NR double 档亦非 Buneman 稳定档——**重写优先级论证不被削弱，反被 FFTW 分类学加固**。表述规范：不写「业界无递推实现」，写「主流库一致选表；递推实现存在但要么 double 递推变量、要么 Buneman 稳定形式，与本项目 float 链式复乘不是一回事」。

### 3.2 Bluestein 路径的精度措施
- **pocketfft C（标杆做法）**（`pocketfft.c:1880-1936`）：
  - b 序列与它的 n2 点 FFT 全部 **double**（`double *bk, *bkf`）；
  - chirp 不做逐点 `cos(πi²/n)`：先以 `sincos_2pibyn(2n)` 生成整张高精度单位根表，再用**整数索引递推** `coeff += 2m-1 (mod 2n)` 查表拼出 e^{-iπm²/n}——角乘法误差为零，仅有表本身的 double 舍入；
  - 归一化 1/n2 预乘进 bkf，运行期少一次除法；
  - 卷积长度 n2 = `good_size(2n-1)`：取 2/3/5/7/11 的最小复合数 ≥ 2n-1（**不强制 2 的幂**，通常比 2 的幂更小）；
  - 是否走 Bluestein 的决策（`pocketfft.c:2066-2091`）：`length<50 || 最大素因子 ≤ √length` → 直接混合基；否则比较两条路径的估计代价择优。
- **cpuimage/pffft fork（反例）**：Bluestein 数据通路全 float，chirp 逐点 `SinCos(M_PI·i·i/n)`（角在 double 域算，值即转 float），b 序列 FFT、频域卷积全 float——fork 作者只为补任意长度功能，未做精度加固。
- **nayuki（教学实现）**：含 Bluestein，double；pffft 原作者评其 "everything else than fast"。
- **本项目现状**（`fft.h:64-89`）：chirp 角表 double（`fmod(πi²/n, 2π)` 逐点 double 三角）但 b 序列存 float、b 的 m 点 FFT 与频域卷积全 float——介于两者之间，正是重写方案要收紧的位置。

### 3.3 float FFT 在音频 STFT 场景的误差量级
- 公开数值分析：FFTW accuracy 页给出的是方法论与定性结论（误差增长律、twiddle 是主因），未在文本中给出逐尺寸数值（数据在其 per-machine 图表中）。**float 实现相对误差约 1e-6~1e-7（−120~−140dB）量级**这一说法属按 O(√log N)·ε_float 的推算，标注为【推算】而非【已确认】。
- 工程旁证：pffft 以纯 float twiddle 表（cosf 直算）服务实时音频多年；kissfft/ducc0 均以"double 生成 twiddle、float 跑蝶形"为默认形态——说明业界共识是 float 数据通路对音频足够，twiddle 生成精度才是要花钱的地方。
- 平台注意（FFTW 页明确）：x87 扩展精度会在 32 位 x86 上"免费"掩盖递推误差，但 **x86-64 的 SSE 标量/SIMD 无扩展精度**——本项目 MinGW x64 与 Linux x64 两个构建都不享受该掩护，递推误差全额暴露。这解释了为何 `fft.h` 的递推缺陷必须在本项目里当真。

## 4. 6144 点（=3·2^11）场景的通行做法

各库对 3·2^k 这类长度的处理，**全部走混合基（radix-3 内核），无一走 Bluestein**：

| 库 | 6144 的处理 | 依据 |
|---|---|---|
| pocketfft C | 混合基：最大素因子 3 ≤ √6144 ≈ 78，命中"直接混合基"分支 | `pocketfft.c:2073` 决策条件 |
| pffft | 混合基：radix-3（passf3）+ radix-2/4，6144 为合法尺寸 | `pffft_is_valid_size` + passf3 内核 |
| kissfft | 混合基：kf_bfly3 + kf_bfly2/4，不触及通用蝶形 | `kiss_fft.c:263-269` 分派 |
| muFFT | 不支持（仅 2 的幂） | README |
| cpuimage fork | 6144 合法（2^a·3^b·5^c），Bluestein 只兜其他素因子 | `pffft.c:2246-2251` 分派 |

原因：Bluestein 把 N 点 DFT 化为约 2N 点卷积 + 3 次复乘，开销约 2.5~2.7× 直接混合基；有 radix-3 内核就没有理由对 3·2^k 用 Bluestein。Bluestein 在各库中的角色都是"大素因子兜底"（pocketfft README/发行注记口径一致：large prime factors 才用）。
本项目只有 radix-2 内核、靠 Bluestein 补任意长度，是"内核库存"不同导致的另一条合法路线（Bluestein 内层 m=16384 是 2 的幂，恰是基-2 强项）；代价量化见 §6。

## 5. 性能参考（谨慎引用，口径与年代须随行）

- pffft 原版 README 内置两张表（MFlops，复数变换按 5N·log2N 计）：
  - 2011 年 Mac i7-2600（gcc 4.2, fftw 3.3, veclib）：N=16384 时 real FFTW 10454 vs pffft 12822；cplx FFTW 12370 vs pffft 13881；vDSP cplx 16874 仍最快；
  - 2011 年 Core2（gcc 4.5, fftw 3.2, -msse）：N=4096 real FFTW 6827 vs pffft 8777；cplx FFTW 9452 vs pffft 6827；
  - 结论口径：pffft 与 fftw3f 同量级、小尺寸常反超，与作者自述目标（"at least 50% as fast as the fastest FFT"）一致；**这是 2011-2012 年数据，仅供数量级参考**。
- muFFT README 自述 "usually competitive with FFTW3 and FFmpeg/libavcodec FFT"（无内嵌数据表）。
- kissfft：仓库内无发布的对比数据（test/benchkiss.c 是自测工具），与 pffft 的相对性能【未确认】；定性预期为标量 FFTPack 级（无 SIMD），非其设计目标。
- nayuki："everything else than fast"（pffft 原作者原话），161 行 C++ 含 Bluestein——极简参考实现。
- 现代横向对比数据在 marton78 fork 指向的外部仓库 hayguen/pffft_benchmarks（Green/kiss/pocket/pffft 对照，含 GNUPlot 图），本次未核验其内容，【未确认】。
- 本项目工作点参考：GTCRN 512 点（2 的幂）与 MDX 6144 点均为小尺寸、离线批处理，任何一家库或自研基-2 在此量级都不是瓶颈；性能矛盾要到实时化（NFR-P2）才出现。

## 6. 对本项目重写方案（docs/spec/fft-rewrite.md）的核验意见

逐条对照，结论先行：**方案三个主攻方向与业界实践全部吻合，无返工项；有两处可白捡的增强、一处需要前置量化的代价。**

1. **double 主 twiddle 表 + 懒建缓存** —— 吻合，且必要。kissfft（double 角度 + double 三角→float 存储）、ducc0（Thigh 强制 double）与此完全同构；pffft 更宽松（cosf 直算 float）也证明 float 表对音频够用，但本项目要过 −60dB parity 且在 x64 上无 x87 掩护（§3.3），取 double 生成是零成本的正确选择。FFTW 评述直接支持"递推是首要误差源"的判断。维持原设计。
2. **FftPlan 2 的幂直通** —— 吻合。pocketfft 有 radix 内核根本不会对 2^k 走 Bluestein；cpuimage fork 的分派也只在非法尺寸走 Bluestein。直通分支是共识做法，不是权宜。
3. **Bluestein 的 b 序列升 double（仅 init 期）+ 运行期 chirp 用 double 角表** —— 方向正确，建议再进一步：pocketfft 的整数索引查表技巧（`coeff += 2m-1 mod 2n`）可以零成本替换现在的 `fmod(πi²/n, 2π)` 逐点三角——先建 2n 长度单位根 double 表再索引，彻底消除角乘法舍入，代码量不增。属可选增强，不是缺陷。
4. **频域卷积保持 float** —— 与 pffft（float 通路）同档，可接受；但要认识到 pocketfft C 是全 double 通路，−100dB 的 FFT-PR-005 判据若因 float 卷积卡在 −130dB 一带，属预期内（方案已留模板化升级路径，维持既定分支）。
5. **不做混合基** —— 成立，附量化：6144 经 Bluestein 需 m=16384（2.67× 蝶形量）+ 3 次复乘 + 两次 m 点 FFT 的常数，离线无碍；实时化后 MDX 前端（44.1k 立体声、hop=1024 级别）这可能成为热点。届时三选一：补 radix-3 codelet / vendor pocketfft（BSD-3、C99、6144 直跑、全 double）/ 维持并实测。建议仅在 NFR-P2 实测不达标时触发，现阶段不动。
6. **卷积长度 m 取 ≥2n-1 的最小 2 的幂** —— 可行（基-2 内核自洽），pocketfft 的 good_size（2/3/5/7/11 复合数）能取到更小的 m（如 6144→12600 < 16384），属性能微调，不必跟进。
7. **API 不变、模板化 `fft_core<T>` 留升级路径** —— 与 kissfft.hh/ducc0 的模板化形态一致，无异议。

## 7. 自研重写 vs 引库（利弊与结论倾向）

**自研重写（方案现状）的论据**
- 规模可控：124→约 200 行 header-only，只服务两个固定尺寸；无构建系统改动（MinGW+Linux 双构建零负担）；license 零负担；不受上游 API/维护变动牵连。
- 本调研支持其正确性：核心措施（表 twiddle、直通、Bluestein 加固）均有业界对齐物。
- 风险自担面：无第三方规模化验证背书，正确性完全靠自建 parity 用例（NFR-Q1 本来就要建）。

**引入 kissfft（BSD-3）**
- 优点：任意长度原生（6144 混合基直跑，无 Bluestein 间接开销）；BSD-3 干净；Rubber Band 内嵌背书；核心两文件可 amalgamate；kissfft.hh 有模板形态。
- 缺点：C 库非 header-only（需并入源码树或加子目录目标）；无 SIMD（标量级性能）；float/double 需编译期二选一；引入后 fft.h 的 FftPlan/引擎适配层要重写一遍——与重写自研工作量相当，却多了外部代码审计面。

**引入 pffft / marton78 fork（BSD 式）**
- 优点：SIMD 性感、实时零分配设计与本项目 NFR 同构；fork 活跃（2026-09）且自带 CMake FetchContent 与 MinGW 交叉脚本，双构建成本最低；AOSP 采用。
- 缺点：float 为主（double 走 fork 的第二实现）；合法尺寸受 SIMD 倍数约束（512/6144 均合法，但换模型可能踩界）；unordered 输出语义需适配；非 header-only。
- 注意：其 CMake/MinGW 脚本即便不引库也值得在双构建配置时参考（`cross_build_mingw64.sh`、`mingw-w64-x86_64.cmake`）。

**引入 pocketfft C（BSD-3）**
- 优点：numpy/scipy 同源血统、精度实践最讲究（§3.1/§3.2）；任意长度 + 6144 直跑；C99 单对文件。
- 缺点：double 专用（GTCRN/MDX 前端是 float，需两处转换或接受 double 开销）；plan/malloc API 是 C 库工程形态；非 header-only。

**结论倾向**：当前阶段（离线闭环 + parity 兜底）**维持自研重写**——证据充分、成本最低、风险由 parity 用例封住；把 pocketfft C 与 numpy.fft 一并作为 parity 测试的**对照参考实现**而非运行时依赖。实时化阶段（NFR-P2）若 MDX 6144 Bluestein 实测成热点，优先评估 vendor pocketfft（精度与尺寸最匹配）或 pffft fork（性能与实时设计最匹配），届时单独决策。

## 8. 结论分级

**【已确认】**（一手源码/文档直接核验）
- pffft：FFTPACK 衍生 BSD 式许可；float 专用（原版）；合法尺寸 2^a·3^b·5^c×SIMD 倍数；twiddle setup 期独立三角逐项建表（float 直算）无递推；6144 原生混合基；marton78 fork 活跃（2026-09-29）且有 double 支持与 MinGW 交叉脚本；README/LICENSE 无引用请求条款。
- kissfft：BSD-3（SPDX 逐文件）；纯 C + C++ 模板；radix 2/3/4/5 + 通用蝶形、无 Bluestein；twiddle 为 double 角度 + double 三角后存 float；Rubber Band 内嵌采用。
- pocketfft C：BSD-3；C99；double 专用；codelet 2/3/4/5/7/11；Bluestein 仅用于大素因子（`length<50 或 lpf≤√n` 走混合基，6144 走混合基）；Bluestein 全 double + 整数索引查表 chirp；good_size 取 2/3/5/7/11 复合数；twiddle 采用压角域 + 2√n 次三角的 double 方案。
- numpy 1.17 官方注记确认 pocketfft 替换 fftpack（更快更准、大素因子走 Bluestein）；scipy 1.4.0 注记确认 scipy.fft 基于 pypocketfft；scipy 1.18 注记确认切至 ducc0.fft。
- muFFT：MIT；单精度；仅 2 的幂（6144 出局）；bench/test 链 GPLv2+ FFTW 仅限工具。
- FFTS：BSD 三条款式；主仓自述多年未维护；运行时 JIT。
- ducc0：float 模板的 twiddle 强制 double 生成（Thigh）；unity_roots.h 为 BSD-3 OR GPL-2 双许可（可取 BSD-3）。
- FFTW 官方精度评述：twiddle 不精确是 FFT 误差最可能来源；递推式累积 O(√N)/O(N)/O(N²)；double 递推可救 float 例程；x86-64 SSE 无扩展精度掩护。
- nayuki free-small-fft：MIT；含 Bluestein；被 pffft 作者评"极简但不快"。
- 本项目重写方案三主攻点（double twiddle 表 / 2 的幂直通 / Bluestein b 序列升 double）与上述实践逐条吻合。

**【倾向】**（一手证据充分但含推演成分）
- 自研重写优先于引库（当前阶段），引库候选排序 pocketfft ≈ pffft-fork > kissfft（实时化触发时再决策）。
- float FFT 相对误差约 1e-6~1e-7（−120~−140dB），对 −60dB parity 判据宽裕——按误差增长律推算，非直接文献数值。
- 6144 的 Bluestein 开销约 2.5~2.7× 混合基（按 m=16384 与复乘次数推算），离线无碍、实时需实测。
- numpy.fft 至今仍以 pocketfft 系为基础（1.17 采纳已确认，"至今"未逐版本核验）。

**【未确认】**（信源不可达或未核验，禁止当事实引用）
- pffft 的其他采用者（Chrome/WebRTC/商业产品等传闻）；AOSP platform/external/pffft 仅确认存在（googlesource 被本机网络拦截，未读到内容，其许可证传言不可采信）。
- PyTorch 是否 vendor pocketfft（jsdelivr 403，未核验）。
- kissfft、muFFT 的最近提交/发布日期（GitHub API 配额耗尽）。
- kissfft vs pffft 的具体性能倍数；hayguen/pffft_benchmarks 仓库的图表内容。
- FFTS 的 radix/尺寸支持细节。
- numpy 对 float32 输入是否一律升 double 计算（未抓取相应文档页）。

## 9. 参考链接

一手源码与文档（本次核验所用）：
- pffft 原版（Pommier，bitbucket）：https://bitbucket.org/jpommier/pffft/
- pffft marton78 fork：https://github.com/marton78/pffft
- pffft hayguen fork（pffft_double）：https://github.com/hayguen/pffft
- pffft cpuimage fork（Bluestein 扩展）：https://github.com/cpuimage/pffft
- kissfft（官方）：https://github.com/mborgerding/kissfft
- pocketfft C 版：https://github.com/mreineck/pocketfft
- ducc0（pocketfft 后继，PyPI 源码包）：https://pypi.org/project/ducc0/
- muFFT：https://github.com/themaister/muFFT
- FFTS：https://github.com/anthonix/ffts （活跃 fork：https://github.com/linkotec/ffts 、https://github.com/ValveSoftware/ffts ）
- Rubber Band（kissfft 内嵌证据）：https://github.com/breakfastquay/rubberband
- nayuki free-small-fft：https://www.nayuki.io/page/free-small-fft-in-multiple-languages
- numpy 1.17.0 Release Notes（pocketfft 采纳）：https://numpy.org/doc/stable/release/1.17.0-notes.html
- scipy 源码树内发行注记 1.4.0 / 1.18.0（pypocketfft 采纳、ducc0 切换）：https://github.com/scipy/scipy
- FFTW accuracy 评述（误差方法论）：https://fftw.org/accuracy/comments.html （方法页：https://fftw.org/accuracy/method.html ）
- pffft 基准结果仓库（未核验内容，仅指引）：https://github.com/hayguen/pffft_benchmarks
- AOSP pffft 镜像（存在性确认，内容未读取）：https://android.googlesource.com/platform/external/pffft/

关联内部文档：
- `docs/spec/fft-rewrite.md`（被核验的重写方案）
- `src/util/fft.h`（现状实现）
- `docs/REQUIREMENTS.md` NFR-Q1（parity 准入）、NFR-P2（实时性能）、NFR-C3（许可合规）
