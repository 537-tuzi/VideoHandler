# FFT 重写：twiddle 预计算 + 2 的幂直通 + Bluestein 精度加固

- 日期：2026-10-13
- 关联：P0 债 #2、NFR-Q1（parity 前提）；影响 `src/util/fft.h`（GTCRN 与 MDX 共用）

## 1. 现状与证据

`util/fft.h`（124 行，全 header-only）：

1. **基-2 twiddle 无表**：蝶形内层用递推复乘更新旋转因子（`fft.h:25-36`），每步一次复乘，误差逐步累积；n=8192（MDX Bluestein 内层 m 点）时漂移显著。
2. **2 的幂也走 Bluestein**：`FftPlan::init` 无直通分支（`fft.h:64`），任何长度都做 chirp-z。GTCRN n_fft=512（2 的幂）经 `fft_forward` 直连基-2 没问题，但任何经 `FftPlan` 的 2 的幂帧（换模型即遇）会平白多两次 m 点 FFT + 三次复乘。
3. **Bluestein 全 float**：chirp 角表按 double 算但存 float（`fft.h:70-83`），b 序列、频域卷积全程 float。MDX nFft=6144（dimF=3072×2）非 2 的幂必走此路，精度上限被 float 卷积锁死。

## 2. 设计

- **API 不变**：`fft_forward/fft_inverse/FftPlan` 签名与语义保持，引擎零改动。
- **twiddle 主表（double）**：按最大长度一次性预计算 `cos/sin(2πk/n)` 的 double 主表；基-2 蝶形按 len 抽取步进使用 float 拷贝，消灭递推累积误差。表按需懒建、进程内缓存（工作点固定 512/6144/16384 几个尺寸，内存可忽略）。
- **FftPlan 直通分支**：`init(len)` 若 len 为 2 的幂 → 走基-2 路径（零 chirp 开销）；否则 Bluestein。
- **Bluestein 加固**：b 序列的 m 点前向 FFT 升 double（仅 init 期一次，成本无关紧要）；运行期 chirp 预乘/后乘用 double 角表直接算（`fft.h:93-99/114-121` 处升 double）；频域卷积保持 float（实时化前的性能折中）。若 parity 实测不达 −60dB，再全量升 double——升级路径已留好（模板化 `fft_core<T>`）。
- **不做**：混合基（6144=3×2^11 的 radix-3 分解）——复杂度远超收益，Bluestein+表足够；real-FFT 专用优化——同理由推迟。

## 3. 验证（FFT-PR 用例组，入 engine-parity matrix）

对照 numpy.fft（double）：

| 用例 | 判据 |
|---|---|
| FFT-PR-001 往返恒等 | ifft(fft(x)) − x 的 RMS ≤ −130dB |
| FFT-PR-002 Parseval | 能量守恒相对误差 ≤ 1e-6 |
| FFT-PR-003 线性 | FFT(a·x+b·y) 与 a·FFT(x)+b·FFT(y) 差 ≤ −130dB |
| FFT-PR-004 冲激响应 | δ 输入谱全 1（幅 ≤1e-6 偏差） |
| FFT-PR-005 随机信号 | n∈{512, 6144, 8192} 随机复信号，幅/相 vs numpy 误差 ≤ −100dB（float 实现的合理上限） |

性能顺带记录：512 与 6144 两尺寸重写前后耗时对比（非判据，供 NFR-P2 观测）。

## 4. 实施与回归

- 单文件重写 `util/fft.h`，引擎不改；GTCRN/MDX parity 全绿 + FFT-PR 全绿为退出条件。
- 风险：MDX parity 若重写后仍不达线，说明误差大头在 float 卷积 → 触发 §2 的 double 升级路径，属既定分支不是返工。
