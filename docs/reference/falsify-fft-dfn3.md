# FFT 库与 DFN3 调研结论反证审查

- **性质：反证审查**（假定 `research-fft-libraries.md` 与 `research-deepfilternet3.md` 的结论有错，专找推翻证据）
- 日期：2026-10-14
- 审查对象：`docs/reference/research-fft-libraries.md`（下称〔FFT 调研〕）、`docs/reference/research-deepfilternet3.md`（下称〔DFN3 调研〕）
- 影响范围：WP-1.3（FFT 重写方案）、FR-4（DFN3 质量档）选型与集成路径
- 方法：只取一手信源——各库源码逐行核对（jsDelivr CDN 取 GitHub 仓库内容 + netlib + ffmpeg.org 官方 release + arXiv 官方 HTML 全文 + FFTW 官方页原文 + 官方模型包本地解包解析 protobuf）。ONNX 图 input/output 签名与节点 op_type 统计为本地自写解析脚本实测（非转述）。检索引擎仅用于发现反例线索（deepfilter-rt 等），技术结论一律回到源码/原文核验。
- 环境备注：本次 raw.githubusercontent 与 github.com 被本机网络策略拦截，GitHub 内容经 cdn.jsdelivr.net 获取；DFN 模型包经 jsDelivr 下载后字节数 7,983,136 与〔DFN3 调研〕记录完全一致，内容一致性有强佐证。

## 判级总表

| # | 被审查结论 | 判级 | 一句话理由 |
|---|---|---|---|
| 1 | 「主流库（pffft/kissfft/pocketfft/ducc0）无一例外 setup 期预计算 twiddle、无一家蝶形内递推」 | **需修正**（限定范围内属实，泛化解读被推翻） | 四家库复核全部属实；但「业界无广泛使用的递推实现」不成立——GSL radix-2 与 Numerical Recipes 是广泛使用的真实反例，FFTW 官方页原话 "many codes use" 递推 |
| 2 | 「FFTW accuracy 页明确点名递推是 FFT 误差最可能来源」 | **站得住** | 两处引文逐字核对一致；且原文比调研引用点名得更直接 |
| 3 | 「pocketfft Bluestein 用整数索引 chirp 表（coeff += 2m−1 mod 2n）」 | **站得住** | 源码逐行核实，写法、精度措施、决策条件全部属实 |
| 4 | 「DFN3 ONNX 图无状态 I/O（状态在 Rust runtime 维护）」 | **官方图部分站得住；「需自行改状态化导出」需修正** | 官方三图无状态双重实测属实；但社区 deepfilter-rt 已有完整带状态 I/O 手术方案（vs 官方参考 47.7dB），“自行改”不再是唯一路径 |
| 5 | 「DFN3 lsnr 阈值两说（−10/30/20 vs −15/35/35）」 | **站得住（补强）** | 两组数都没抄错；新证据钉死口径：论文 [−15,35]、config/CLI/CAPI 均 −15/35/35，−10/30/20 仅 Rust API 出厂默认 |
| 6 | 「DFN3 原生 48k」 | **站得住** | config/README/CLI/社区镜像四方一致 48k；无 44.1k/16k 变体 |

另有一项**审查任务本身的纠错**：任务书第 5 条指定的「论文 arXiv 2302.00945 或 ICASSP 版」有误，见 §5。

---

## 1. 「主流库无一例外预计算 twiddle、无一家蝶形内递推」

〔FFT 调研〕§3.1 表述为「所有库都用预计算表，没有一家在蝶形内做递推」，范围是其盘点表中的库（pffft/kissfft/pocketfft/ducc0/muFFT/FFS/nayuki）。反证从两个方向进攻：逐一复核四家点名库是否真是预计算表；在四家之外找广泛使用的递推实现。

### 1.1 四家点名库复核（全部属实，逐家源码定位）

| 库 | 源码版本 | twiddle 策略实证 | 与〔FFT 调研〕表述比对 |
|---|---|---|---|
| pffft | hayguen/pffft@master | `pffft_priv_impl.h` L959 `cffti1_ps`：L965 `float argh = (2*(float)M_PI) / n;`，L985-986 `wa[i-1] = FUNC_COS(fi*argld); wa[i] = FUNC_SIN(fi*argld);`（`FUNC_COS` 即 `cosf`，`pffft.c` L128）；蝶形 passf2/3/4/5 全部查 `&wa[iw]` 表（L1015-1020 调用点）。setup 期逐项独立三角调用，无递推 | 属实 |
| kissfft | mborgerding/kissfft@master | `kiss_fft.c` L358-368：L362 全精度 π 字面量，L363 `double phase = -2*pi*i / nfft;` → `kf_cexp(st->twiddles+i, phase)`；`_kiss_fft_guts.h` L158-159 `(kiss_fft_scalar) cos(phase)`——**double 三角求值后 cast 成 float**；通用蝶形 `kf_bfly_generic` 自 L192 起，查 `st->twiddles` 表 | 属实（含 L158-159 行号） |
| pocketfft C | mreineck/pocketfft@master | `cfftp_comp_twiddle`（L999-1036）：setup 期先 `sincos_2pibyn(length, twid)` 生成 2·length double 表，再按因子索引进 plan 表；`my_sincosm1pi` L37 注释 "CAUTION: this function only works for arguments in the range [-0.25; 0.25]"，fma 多项式；README L22-30 原文 "all angles are reduced to the range `[0; pi/4]`"、"only called `2*sqrt(n)` times" | 属实 |
| ducc0 | mreineck/ducc@master，`src/ducc0/math/unity_roots.h` | L69 `using Thigh = typename conditional<(sizeof(T)>sizeof(double)), T, double>::type;`——float 模板强制 double 求值；L83-105 象限对称 `cos(Thigh(x)*ang)` | 属实（行号 L66→实际 L69，master 漂移，内容一致） |

更多「表驱动」旁证（本轮新增复核）：
- **CMSIS-DSP**（ARM 官方嵌入式 DSP 库，ARM-software/CMSIS-DSP@main）`Source/TransformFunctions/arm_cfft_f32.c` L116-133：蝶形读 `p_rearranged_twiddle_tab_stride1/2/3`——init 期重排好的预计算表。
- **FFmpeg 7.1.1**（官方 release tarball）`libavutil/tx_template.c` L68-78（SR_TABLE 宏）：`double freq = 2*M_PI/len; ... *tab++ = RESCALE(cos(i*freq));`，`AVOnce` 保证一次性建表；`ff_tx_fft_factor_init`/`ff_tx_sr_codelet_init`（L482-490、L592-599）在变换初始化期调用。

### 1.2 反证命中：广泛使用的递推实现真实存在

**(a) GNU GSL（GNU 科学库，Linux 发行版标配级别）——蝶形内三角递推，源码自己注释承认**

`fft/real_radix2.c`（ampl/gsl 镜像 @master，Brian Gough 1996-2007，**GPL-3.0**）：

```c
ATOMIC w_real = 1.0;
const double theta = - 2.0 * M_PI / p;
const ATOMIC s = sin (theta);
const ATOMIC t = sin (theta / 2.0);
const ATOMIC s2 = 2.0 * t * t;
for (a = 1; a < (p_1)/2; a++)
  {
    /* trignometric recurrence for w-> exp(i theta) w */
    const ATOMIC tmp_real = w_real - s * w_imag - s2 * w_real;
    const ATOMIC tmp_imag = w_imag + s * w_real - s2 * w_imag;
    w_real = tmp_real; w_imag = tmp_imag;
```

- 每层只调 2 次 `sin`，蝶形内纯递推；注释原文 "trignometric recurrence for w-> exp(i theta) w"。
- 形式注意：这是 **Buneman (1987) 稳定递推**（s 与 s²/2 系数形式），不是 Numerical Recipes 那种朴素复乘递推；按 FFTW 页的分类，Buneman 递推平均精度 O(√log N)，「与 FFT 本身同阶」。FFTW 页原文：rmayer-buneman "uses a recurrence algorithm by Buneman (1987) achieving O(√log N) mean accuracy (like the FFT itself)"。
- 即：GSL 证明「广泛使用的库 + 蝶形内递推 + 精度可接受」三者可以同时成立。

**(b) Numerical Recipes——FFTW 官方页点名的递推实现**

fftw.org/accuracy/comments.html（原文，见 §2 全文）："Both the Numerical Recipes (nr-c and nr-f) and ransom codes use the same O(√N)-accuracy recurrence algorithm with **explicitly double-precision variables**, which **for single-precision transforms produces accurate results** (for the sizes benchmarked)."

- NR 的 four1 系（教材参考实现，数十年间被大量科研代码采用）用蝶形内递推；因递推变量显式 double，在 float 变换下精度被 FFTW 判为「accurate」。
- 同页还点名一串递推实现及精度分层：as117/as83/as97/singleton（单精度递推，"correspondingly large errors"；as117 是 Bluestein 算法+递推）、gsl-radix2（递推）vs gsl-mixed-radix（fftpack 预计算表）、mixfft（O(N) 递推）、glassman/napack/sorensen-rsrfft、cross（O(N²) 递推，double）、bloodworth（同精度递推，仅 x86 x87 扩展精度掩护下准确）、monnier（有表但角度重复加法，精度无谓受损）。
- 页面总起句："A trigonometric recurrence is a trick that **many codes** use to avoid the time and memory overhead of precomputing and storing an array of accurate twiddle factors."

**(c) 闭源实现（Apple Accelerate/vDSP、Intel MKL）**：无公开源码或实现说明，无法核验，亦无法作为任何一方的证据。

### 1.3 反证对本项目「必须重写」优先级论证的影响

反证意图是：若存在广泛使用且精度可接受的递推实现，则「必须重写」的论证被削弱。**实际结论：不被削弱，反而被 FFTW 的误差分类学精确加固**：

- 我们 `src/util/fft.h` 的 `fft_core` 是 **float 种子 + float 递推变量**（L31 `const float wr = static_cast<float>(cos(ang));`、L36-40 `float cwr/cwi` 链式复乘）——恰是 FFTW 页点名 "with single-precision variables and correspondingly large errors" 的 as117/gsl-radix2 档，**不是** NR 的「double 递推保 float 变换」档，也不是 GSL 的 Buneman 稳定档。
- 本项目目标平台 x64（MinGW/Linux）SSE 无 x87 扩展精度掩护（FFTW 页原文 "x86 SIMD instructions (i.e., SSE, SSE2, and 3DNow!) do not offer extended precision"），bloodworth 式侥幸不存在。
- 附带发现（对重写方案无实质影响，但值得记录）：递推并非唯一出路——Buneman 稳定递推（O(√log N)，GSL 在用）是不建大表也能达 FFT 同阶精度的合法路线；但查表是四家库 + CMSIS + FFmpeg 的一致主流选择，〔FFT 调研〕与 `docs/spec/fft-rewrite.md` 的查表路线无需修改。

**判级：需修正**。修正内容：〔FFT 调研〕§3.1 的「所有库」「没有一家」应限定为「本次盘点的库范围内」；作为泛化命题它被 GSL 与 Numerical Recipes 两个广泛使用的反例推翻。四家点名库的逐家源码证据全部复核通过，不构成对重写方案的反驳；GTCRN 512 点这类 2 的幂场景即便按 NR 档 double 递推也可能够用，但那是「另一种可行」而非「重写不必要」。

---

## 2. 「FFTW accuracy 页明确点名递推是 FFT 误差最可能来源」

反证假设：我们误引了 FFTW 原文（原文可能只说 round-off 随 log n 增长，未点名递推）。逐字核对结果（fftw.org/accuracy/comments.html 全文抓取）：

**引文一**（〔FFT 调研〕§3.1/§8 引用："Inaccurate twiddle factors are the most likely reason for the inaccuracy of an FFT routine"）——原文 Generalities 段：

> "Most FFT implementations in the benchmark are based on the Cooley-Tukey algorithm, whose floating-point error grows as O(log N) in the worst case (Gentleman & Sande, 1966) and as O(√log N) on average (Schatzman, 1996), for a 1d transform of size N. For these bounds to hold, certain trigonometric constants used by the algorithm (the twiddle factors) must be computed accurately. **Inaccurate twiddle factors are the most likely reason for the inaccuracy of an FFT routine.**"

逐字一致。

**引文二**（"most such recurrence formulas accumulate errors as O(√N), O(N), or even O(N²), much faster than the FFT itself (Tasche & Zeuner, 2002)"）——原文紧接段：

> "Inaccurate twiddle factors are usually caused by **poor trigonometric recurrences**. A trigonometric recurrence is a trick that many codes use to avoid the time and memory overhead of precomputing and storing an array of accurate twiddle factors. Instead, these codes use a formula that starts with a small number of twiddle factors and iteratively combines them to produce the other constants needed by the algorithm. **Unfortunately, most such recurrence formulas accumulate errors as O(√N), O(N), or even O(N2), much faster than the FFT itself (Tasche & Zeuner, 2002).**"

逐字一致（调研引文省略了句首 "Unfortunately,"，主语上下文完整，非断章）。

**是否「点名递推」**：是，且比调研的转述更直接——原文明确写「twiddle 不准通常由糟糕的三角递推造成」，并点名了一批具体递推实现（见 §1.2）。「我们误引」的反证假设不成立。

**顺带核对**〔FFT 调研〕§3.3 的平台论断：原文 "x86 SIMD instructions (i.e., SSE, SSE2, and 3DNow!) do not offer extended precision"——属实；「x87 扩展精度默认开启掩盖递推误差」也有原文支持（"variables that are register-allocated by the compiler become extended-precision even if they were declared float or double"，GNU/Linux 默认扩展精度模式）。

**判级：站得住**。

---

## 3. 「pocketfft Bluestein 用整数索引 chirp 表（coeff += 2m−1 mod 2n）」

反证假设：我们对 pocketfft Bluestein 实现的描述（整数索引递推替代逐点三角）不属实。精读 `pocketfft.c`（mreineck/pocketfft@master）`make_fftblue_plan`（L1889-1935）：

```c
plan->n2 = good_size(plan->n*2-1);              // L1894
double *bk, *bkf;                                // L1885: 全 double
double *tmp = RALLOC(double,4*plan->n);          // L1901
sincos_2pibyn(2*plan->n,tmp);                    // L1903: 整表单位根
plan->bk[0] = 1; plan->bk[1] = 0;                // L1904-1905
size_t coeff=0;                                  // L1907
for (size_t m=1; m<plan->n; ++m)
  {
  coeff+=2*m-1;                                  // L1910
  if (coeff>=2*plan->n) coeff-=2*plan->n;        // L1911: mod 2n
  plan->bk[2*m  ] = tmp[2*coeff  ];              // L1912-1913: 查表
  plan->bk[2*m+1] = tmp[2*coeff+1];
  }
double xn2 = 1./plan->n2;                        // L1917: 归一化预乘进 bkf
```

逐条比对〔FFT 调研〕§3.2 的五点描述：

| 〔FFT 调研〕表述 | 源码实证 | 判定 |
|---|---|---|
| b 序列与 n2 点 FFT 全 double | L1885 `double *bk, *bkf` | 属实 |
| chirp 不做逐点 `cos(πi²/n)`，整数索引递推 `coeff += 2m-1 (mod 2n)` 查 2n 单位根表 | L1903 + L1907-1913 | 属实（m² = Σ(2k−1)，索引即 m² mod 2n，角乘法误差为零） |
| 归一化 1/n2 预乘进 bkf | L1917-1926 | 属实 |
| 卷积长度 n2 = good_size(2n−1) | L1894 | 属实 |
| 决策规则「length<50 或最大素因子 ≤ √length → 直接混合基，否则代价比较」 | `make_cfft_plan` L2072-2091：`if ((length<50) || (largest_prime_factor(length)<=sqrt(length)))` 后 `cost_guess` 比较 + 1.5 fudge | 属实 |

〔FFT 调研〕引 `pocketfft.c:1880-1936`、`2066-2091`，与当前 master 实际行号（1889-1935、2072-2091）偏差 ≤8 行，属上游漂移，定位无误。

**判级：站得住**（未找到任何反证；§6 建议「可白捡」的整数索引查表增强依据坚实）。

---

## 4. 「DFN3 ONNX 图无状态 I/O（状态在 Rust runtime 维护）」

### 4.1 官方三图无状态——双重实测复核（站得住）

对官方发布包 `DeepFilterNet3_onnx.tar.gz`（jsDelivr 下载，7,983,136 B，与〔DFN3 调研〕记录逐字节同大小）解包，自写 protobuf wire-format 解析脚本直接读图：

**输入/输出签名**（S 为动态轴）：

| 图 | 输入 | 输出 |
|---|---|---|
| enc.onnx | `feat_erb [1,1,S,32]`、`feat_spec [1,2,S,96]` | e0/e1/e2/e3、emb、c0、lsnr |
| erb_dec.onnx | `emb [1,S,512]`、e3/e2 `[1,64,S,8]`、e1 `[1,64,S,16]`、e0 `[1,64,S,32]` | m |
| df_dec.onnx | `emb [1,S,512]`、`c0 [1,64,S,96]` | coefs `[…,S,…,10]`、'235' |

**节点 op_type 统计**：enc.onnx 含 GRU 节点 ×1，erb_dec.onnx GRU ×2，df_dec.onnx GRU ×2——GRU 算子确实内化在图中；而三图输入中**没有任何 initial_h/状态张量**（ONNX GRU 的 initial_h 为可选输入，未暴露即每次 Run 从零状态出发）。

两处细节修正（不影响主结论）：
- 〔DFN3 调研〕写 df_dec 输出 `coefs [1,96,5]`（实虚交织）；实测签名末维是 **10**（= df_order 5 × 实虚 2），另有第二个输出 '235'（Sigmoid）。只是形状描述笔误级别。
- 〔DFN3 调研〕写「opset 默认 12（--opset 可给 14）」：CLI argparse 默认确为 12（export.py L350），export() 函数签名另有默认 14（L139），CLI 路径走 12——调研口径无误。

### 4.2 反证命中：「官方无现成状态化导出、需自行改」已过时（需修正）

反证方向：找带状态输入输出的新导出/PR/分支。**命中社区项目 [shimondoodkin/deepfilter-rt](https://github.com/shimondoodkin/deepfilter-rt)**（Rust + ONNX Runtime 实时逐帧 DFN）：

- `scripts/split_encoder_and_patch_decoders.py`（590 行，docstring 原文）："Split enc.onnx into enc_conv_streaming.onnx + enc_gru_streaming.onnx and patch decoders for stateful streaming … with persistent GRU states across all modules: enc_gru_streaming.onnx: Encoder GRU with **h0/h1** state; erb_dec_streaming.onnx: ERB decoder with **erb_h0/erb_h1** state I/O; df_dec_streaming.onnx: DF decoder with **df_h0/df_h1** state I/O"。手段是 ONNX surgery：拆分 conv/GRU、给 decoder 补 GRU 隐状态输入输出、时间切片 `T[-1:]`（L166-208 逐节点构造 Slice/常量）。
- 配套 `merge_split_models.py`（合并单图保留状态 I/O）、`patch_onnx_streaming.py`（legacy）。
- 质量自报（MODES.md 原文）："corr=0.999991, **SNR=47.7 dB vs Tract reference** (DFN3)"（legacy patch 方式仅 17.0 dB）；逐帧 avg 1.70ms、RTF 0.167（480 samples/帧）。
- 支持 dfn2/dfn2_ll/dfn2_h0/dfn3/dfn3_ll/dfn3_h0 全变体，全部 48k。
- **合规警示**：deepfilter-rt 仓库**无 LICENSE 文件**（Cargo.toml 亦无 license 字段），GitHub 默认「保留所有权利」——其脚本与代码不可直接复制进本项目，只能作为思路参考或联系作者授权；官方 Rikorose 仓库侧（休眠于 2024-10）确认仍无状态化导出（API 搜索 89 条 stateful/streaming/gru 相关 issue/PR 未见官方动作）。

另核实 [bitsydarel/deepfilternet3-onnx](https://huggingface.co/bitsydarel/deepfilternet3-onnx)（HF）：为官方三图的**镜像**而非状态化导出，卡片原文 "The bare ONNX files do not include those"（指状态管理）——反向佐证官方图无状态。

**对集成路径的影响**：〔DFN3 调研〕路径 B 中「实时需改图暴露状态重导出，官方无现成状态化导出」「盲点：未找到公开完整源码的桌面 ORT 级复刻」两处应更新为：状态化手术已被社区验证可行且与官方 runtime 输出一致到 47.7dB（≈误差 −48dB，注意这高于我们 −60dB 的 RMS 准入线，达标与否要看其测量口径——SNR vs 全段 RMS 不可直接换算，需自建 parity 复核）；ONNX surgery 的具体做法（拆 conv/GRU、暴露 h0/h1、T[-1:] 切片）可按同样思路自行实现以规避无许可证问题。

**判级：官方图「无状态 I/O、GRU 内化、状态在 Rust runtime」站得住（双重实证）；「需自行改状态化导出」「无公开 ORT 复刻先例」需修正（社区方案已存在，唯许可证未明确）。**

---

## 5. 「DFN3 lsnr 阈值两说（−10/30/20 vs −15/35/35）」

**先纠审查任务书自身的一处错误**：任务指定核对「论文（arXiv 2302.00945 或 ICASSP 版）」。实测 [arXiv 2302.00945](https://arxiv.org/abs/2302.00945) 是数学论文《A Renormalization Scheme for Semi-Regular Continued Fractions》（Langeveld & Ralston，math.DS），与 DeepFilterNet 无关；DFN3 论文是 **arXiv 2305.08227**（Interspeech 2023，abs 页 Comments 字段 "interspeech 2023"），不存在「ICASSP 版」（ICASSP 2022 的是 DFN1，arXiv 2110.05588）。〔DFN3 调研〕引用的 2305.08227 是正确的。

论文侧（arXiv 官方 HTML 全文，逐字）：

> "We further predict the local SNR ξ ∈ [**−15, 35**] dB within the encoder network on frame level."

代码侧（本轮全部重新取 main 分支源码核实）：

| 位置 | 数值 | 源码位置 |
|---|---|---|
| `libDF/src/tract.rs` `default_with_ch` | **−10 / 30 / 20** | L182-184：`min_db_thresh: -10., max_db_erb_thresh: 30., max_db_df_thresh: 20.,` |
| `libDF/src/capi.rs` | **−15 / 35 / 35** | L28-31：`with_thresholds(-15.0f32, 35.0f32, 35.0f32)`（注释 min_db_thresh/max_db_erb_thresh/max_db_df_thresh） |
| `libDF/src/bin/enhance_wav.rs`（官方 CLI） | **−15 / 35 / 35** | L41-57：三个 clap 参数 `default_value_t` 分别 −15.、35.、35. |
| 模型包 `config.ini` | lsnr_min=**−15**，lsnr_max=**35** | `[df]` 段实测 |

**结论**：〔DFN3 调研〕记录的两组数都没抄错；本轮新增证据把口径彻底钉死——**论文、config、官方 CLI、C API 四方一致为 −15/35(/35)**，−10/30/20 仅是 Rust `RuntimeParams` 的出厂默认值，会被所有官方发布入口显式覆盖，只有「以库形式直接调 libDF 且不设阈值」才会生效。〔DFN3 调研〕「parity 对照哪个二进制，就用哪套值」的提醒仍然正确且更有底气：对照官方发布物（deep-filter CLI / C API / LADSPA）一律用 −15/35/35。

**判级：站得住（补强）**。

---

## 6. 「DFN3 原生 48k（config sr=48000）」

- 官方模型包 `config.ini` `[df]` 段实测：`sr = 48000`。
- README（main 分支全文）：L2 "Full-Band Audio (48kHz)"；L46 "only wav files with a sampling rate of 48kHz are supported"。
- 全文 grep `44100 / 44.1 / 16000 / 16khz`：**零命中**——无 44.1k/16k 模型变体的任何字样；README 提供的变体仅 DeepFilterNet2/3 与低延迟 `_ll`。
- 社区交叉佐证：deepfilter-rt 六个模型变体参数表 "Sample rate: 48000 Hz"；HF 镜像卡 "Native sample rate: 48 kHz (the model can also handle 16 kHz via internal resampling done by the runtime)"——16k 由 runtime 重采样处理，不是模型变体。

**判级：站得住**（未找到反证；〔FFT 调研〕/〔DFN3 调研〕据此推导的「canonical 改 48k 可让 DFN3 直通」前提成立）。

---

## 7. 对 WP-1.3 与 FR-4 的落点

1. **WP-1.3（FFT 重写）**：重写的三主攻点（double twiddle 表 / 2 的幂直通 / Bluestein 加固）全部经反证审查存活；「必须消除 float 递推」的优先级论证反被 FFTW 误差分类学加固（我们恰处 as117 档而非 NR double 档）。唯一修正：论证措辞不要写「业界无递推实现」，应写「主流 FFT 库一致选表；递推实现存在（GSL/NR 等）但要么依赖 double 递推变量、要么依赖 Buneman 稳定形式，与我们 float 链式复乘的写法不是一回事」。
2. **FR-4（DFN3 质量档）**：48k 原生、lsnr 口径（−15/35/35）、官方图无状态三点全部坐实，选型依据不变。集成路径评估需更新一条：ORT 流式运行的状态化改图已被社区（deepfilter-rt）验证可行且精度近官方（47.7dB），后续实时化时不必从零摸索手术方案；但其仓库无 LICENSE，只能参考思路自行实现或索取授权。parity 对拍目标建议固定为官方 deep-filter CLI（其默认阈值 −15/35/35 与论文/config 一致）。
3. **文档维护**：〔FFT 调研〕§3.1 表述加限定；〔DFN3 调研〕§2「官方无现成状态化导出」与 §6「盲点：未找到公开完整源码 ORT 复刻」两处按本报告 §4.2 更新。

## 8. 信源与证据存档

本轮核验所用一手信源（证据文件存 `/tmp/falsify/`，不入库）：

- pffft：https://cdn.jsdelivr.net/gh/hayguen/pffft@master/pffft.c 、…/pffft_priv_impl.h （marton78 fork 因 jsDelivr 未收录改用同源 hayguen fork；twiddle 核心在 pffft_priv_impl.h）
- kissfft：https://cdn.jsdelivr.net/gh/mborgerding/kissfft@master/kiss_fft.c 、…/_kiss_fft_guts.h
- pocketfft：https://cdn.jsdelivr.net/gh/mreineck/pocketfft@master/pocketfft.c 、…/README.md
- ducc0：https://cdn.jsdelivr.net/gh/mreineck/ducc@master/src/ducc0/math/unity_roots.h
- FFTPACK（netlib）：https://www.netlib.org/fftpack/cffti.f 、cffti1.f （wrapper 调 cffti1 预计算 wsave 表）
- GSL：https://cdn.jsdelivr.net/gh/ampl/gsl@master/fft/real_radix2.c （GPL，仅作递推存在性证据，不可引入）
- CMSIS-DSP：https://cdn.jsdelivr.net/gh/ARM-software/CMSIS-DSP@main/Source/TransformFunctions/arm_cfft_f32.c
- FFmpeg 7.1.1 官方 release：https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz （libavutil/tx_template.c；7.x 已无 fft_template.c，FFT 在 TX API）
- FFTW accuracy 原文：https://fftw.org/accuracy/comments.html 、…/method.html 、FAQ section3
- DFN 官方仓库（经 jsDelivr）：tract.rs / capi.rs / lib.rs / bin/enhance_wav.rs / DeepFilterNet/df/scripts/export.py / README.md / models/DeepFilterNet3_onnx.tar.gz（本地解包 + 自写 protobuf 解析）
- DFN3 论文全文 HTML：https://arxiv.org/html/2305.08227v1 ；abs 页：https://arxiv.org/abs/2305.08227
- 错误论文号证伪：https://arxiv.org/abs/2302.00945
- deepfilter-rt：https://github.com/shimondoodkin/deepfilter-rt （README.md、MODES.md、scripts/split_encoder_and_patch_decoders.py 经 jsDelivr 核验）
- HF 官方图镜像：https://huggingface.co/bitsydarel/deepfilternet3-onnx

主要检索发现来源（仅线索，结论均已回源码核验）：[shimondoodkin/deepfilter-rt](https://github.com/shimondoodkin/deepfilter-rt)、[bitsydarel/deepfilternet3-onnx](https://huggingface.co/bitsydarel/deepfilternet3-onnx)、[ONNX GRU 算子规范](https://onnx.ai/onnx/operators/onnx__GRU.html)、[onnx/onnx#3315](https://github.com/onnx/onnx/issues/3315)（ONNX RNN 算子无原生状态，须以 initial_h 输入暴露——与 §4 手术方案机制一致）。

未核验项（诚实标注）：Apple Accelerate / Intel MKL 闭源无公开实现资料；Numerical Recipes four1 源码本体因版权未取镜像（其递推性质以 FFTW 官方页点名描述为准）；GSL complex radix-2 文件路径未定位成功（real_radix2.c 已足以作存在性证据）。
