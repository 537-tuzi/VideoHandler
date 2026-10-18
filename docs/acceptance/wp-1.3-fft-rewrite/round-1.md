# WP-1.3 FFT 重写 — round-1（Linux，2026-10-18）

- 范围：`src/util/fft.h` 单文件重写（spec `docs/spec/fft-rewrite.md` §2 设计全项落地），引擎零改动
- 环境：Linux / g++ (Debian) / `-O2 -DNDEBUG`（linux-test Release preset）；DUT `build-linux/fft_test`

## 改动清单（对照 spec §2）

1. **twiddle double 主表**：`fft_detail::Twiddle` 按需懒建到已请求最大 2 的幂，进程内缓存；蝶形 `W_len^k = 表[k·size/len]` 整数索引精确抽取，消灭旧版链式复乘递推累积（FFTW 误差分类最差档）。
2. **2 的幂直通**：`FftPlan::init` 判 2 的幂即直通 `fft_core`，零 chirp 开销；`run(+1)` 与 Bluestein 路径同语义（逆变换除以 n）。
3. **Bluestein 加固**：chirp 表 pocketfft 式整数索引（k² 以 +(2k+1) 步进 mod 2n 查 2n 单位根表，零角乘）；b 序列 init 期 double FFT 一次存 float；运行期 chirp 预乘/后乘 double 角表直乘；频域卷积保持 float。
4. `fft_core<T>` 模板化（double 升级路径现成，本次未触发）。
5. 注释修正：b 序列为 e^{+iπj²/n}（原注释符号写反，代码与标准 Bluestein 推导一致，冲激用例实证）。

## FFT-PR 证据（判据 spec §3；命令 `build-linux/fft_test self`）

| n | 路径 | 往返 dB（≤−130） | Parseval（≤1e-6） | 线性 dB（≤−130） | 冲激（≤1e-6） |
|---|---|---|---|---|---|
| 512 | 直通 | −138.4 | +4.1e-08 | −136.4 | 0 |
| 6144 | Bluestein | −133.8 | −8.8e-09 | −132.3 | 4.3e-07 |
| 7680 | Bluestein | −132.3 | +1.8e-09 | −131.5 | 4.4e-07 |
| 8192 | 直通 | −137.3 | +2.8e-08 | −135.6 | 0 |
| 16384 | 直通 | −136.9 | +2.6e-08 | −135.1 | 0 |

FFT-PR-005（`scripts/fft-numpy-check.py`，numpy 2.5.4 double 基准）：512 −142.5dB / 6144 −136.8dB / 8192 −141.3dB（判据 ≤−100dB）。

**spec §4 的 double 升级分支未触发**：全项余量充足（最差 7680 往返 −132.3dB vs −130dB 门槛）。

## 性能（同机 `-O2` 对照，信息项；`/tmp/fft-bench/bench.cpp`）

| 用例 | 旧 µs/call | 新 µs/call | 倍率 |
|---|---|---|---|
| FftPlan n=512 | 26.9 | 4.1 | 6.5×（旧走 Bluestein，新直通） |
| FftPlan n=6144 | 700.0 | 534.6 | 1.31× |
| FftPlan n=7680 | 710.5 | 586.4 | 1.21× |
| FftPlan n=16384 | 1721.1 | 250.5 | 6.9×（同 512） |
| fwd+inv n=512（GTCRN 路径） | 11.2 | 8.9 | 1.26× |

## 旧↔新漂移量化（引擎输出跨构建数值漂移的机制上界）

旧 fft.h（git HEAD@重写前）编 `fft_test` 同种子 dump 对照：

- n=512：−108.9dB；n=6144：−87.6dB；n=8192：−86.7dB

即旧实现自身误差 ~−87dB 量级（float 链式递推 + 全 float Bluestein），新实现 −137dB 起。引擎输出随 FFT 漂移 ≪ −60dB parity 预算；WP-1.1 金样本将以新 FFT 为基线生成，不存在跨基线比较。

## 引擎回归（回归网 = WP-1.2 ci-smoke）

`bash docs/acceptance/wp-1.2-linux-build/scripts/ci-smoke.sh` 全绿：dsp/pitch/ai/sep-voc/sep-inst 五引擎分块不变性（一次性 vs 4096 vs 977 逐字节一致）+ 7 CLI 动词 + ffprobe 体检（pcm_f32le/44100/2/5.000000）。ci-smoke 已并入 FFT 检查段（FFT-PR-001..004 + PR-005），工作流 apt 增 `python3-numpy`。

## 盲点声明

- Windows MinGW 编译未在本机验证（fft.h header-only、无平台 API，engine 库在 Windows 分支编译时自然覆盖）——随 wp-1.0 round-4 复验。
- Actions 真机跑（含 FFT 段）待 push 回填 `ci-workflow-run`。
- twiddle 主表单线程契约（引擎经 MediaService busy 锁一次一任务）已在头文件注释声明；若未来多线程引擎并发首调 `fft_forward`，需加锁或预热——实时化（M4）前不构成问题。
