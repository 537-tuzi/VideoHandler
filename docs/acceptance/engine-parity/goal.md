# engine-parity 总目标与进展

- 建立：2026-10-13
- 上位计划：`docs/spec/v1-development-plan.md`（M1）；技术方案：`docs/spec/parity-test-plan.md`

## 总目标

NFR-Q1 落地：GTCRN/MDX/DSP 三引擎对照官方参考实现 parity ≤ −60dB RMS，全用例绿并进 CI 门禁；顺带覆盖 NFR-Q2（DSP 通带波纹）、分块不变性（流式正确性照妖镜）与 FFT 性质测试。**未全绿前不动 M2 及以后的引擎行为。**

## sub goal matrix（唯一进展事实源）

| SG | 目标 | 验收 | 状态 | 备注 |
|---|---|---|---|---|
| SG1 | 测试基建：gen_inputs / gen_golden / compare 三脚本 + /test/ 布局 + fixtures.json | 脚本可重跑产全部素材与金样本，哈希一致 | 未开始 | 依赖：无 |
| SG2 | Linux DUT 构建（WP-1.2a） | linux-test preset 构建 libengine+engine_test，跑通一个合成用例 | 未开始 | 依赖：无；spec ci-build-strategy §2/§6 |
| SG3 | FFT 重写（WP-1.3） | FFT-PR-001..005 全绿 | 未开始 | 依赖：SG1（脚本宿主）；spec fft-rewrite |
| SG4 | GTCRN parity（WP-1.1） | PAR-GT 组全绿 | 未开始 | 依赖：SG1+SG2；对齐约定是最大风险（spec §5） |
| SG5 | MDX parity + 输出契约（WP-1.1+1.4） | PAR-MD 组全绿（含负例 009） | 未开始 | 依赖：SG1+SG2+SG3 |
| SG6 | DSP/变调/采样率组（WP-1.5+1.6） | PAR-DS/PT/SR 组全绿 | 未开始 | 依赖：SG1+SG2 |
| SG7 | CI 接线（WP-1.2b） | linux-engine job 必绿；PR 门禁生效 | 未开始 | 依赖：SG4/SG5/SG6 |

## 重大决策记录

| # | 日期 | 决策 |
|---|---|---|
| D-01 | 2026-10-13 | 指标 = 20·log10(RMS(dut−ref)/RMS(ref)) ≤ −60dB，样本级对齐，长度不等即 FAIL |
| D-02 | 2026-10-13 | 分块不变性目标 bit-exact，确证浮点顺序差异时放宽 −120dB 并记录 |
| D-03 | 2026-10-13 | 跨采样率用例双级判据：参考侧先用同参数 swr 预降采样再推理，只比引擎段 |
| D-04 | 2026-10-13 | 素材/金样本一律不入库（/test/ 已 ignore），SHA256 进 fixtures.json 锁定 |
| D-05 | 2026-10-13 | 主线走 Linux engine_test 全自动；Windows 手跑仅作 SG2 前的过渡 |
| D-06 | 2026-10-14 | GTCRN golden 一律取 sherpa-onnx **online** CLI 产出；offline 路径（knf/反射填充/窗²归一化）边界行为不同，禁作 golden |

## 重要信息

- **GTCRN 对齐约定已源码级确认**（详见 `docs/reference/research-gtcrn-sherpa-onnx.md`；2026-10-14 反证复核 `docs/reference/falsify-gtcrn-golden.md`）：sherpa online 路径逐帧推理（含前置补零的第 0 帧）、输出与输入样本级对齐、滞后恰 1 hop=16ms；flush=余量补零成整 hop+再补一个全零 hop+按总输入长截断。源码级判据：`started_` 只拦输出发射，STFT/推理/WOLA 无条件执行——是「滞后 1 hop 发射」不是「吞掉/少推理一帧」。我们 `aidenoiser.cpp:90-124` 同构（runFrame 与 WOLA 均在 `!started` 判断之前），**SG4 首查项已闭环：语义与 sherpa 一致**。
- 参考实现锁定：sherpa-onnx 1.13.8（online CLI `sherpa-onnx-online-denoiser --speech-denoiser-gtcrn-model=…`）；wheel SHA256 待 SG1 安装时登记并同步依赖矩阵。其自带 ORT 1.28.2，与我们运行时 ORT 1.30 无冲突（两侧独立环境）。版本存在性经反证复核（PyPI+GitHub release+master 三源互证，2026-09-10 发布、即当前最新）；其后 online 两核心文件零改动，锁版本安全。
- 数值风险提示：sherpa online 的谱变换为 double 直算 O(N²) DFT（端点是 float：窗乘在 float 域、出口 cast），参考侧非全 double；我们 float FFT 仍是 −60dB 判据的主要数值变数（误差 ~1e-7 vs 判据 1e-3，余量约 4 个数量级；FFT-PR-005 先行卡位）。
- **D-06 差异范围收紧（2026-10-14 反证）**：offline≠online 不止首尾边界——offline 帧 0/末帧谱用 reflect 填充 vs online 零填充，谱差经因果状态递推（conv/tra/inter cache + GRU）传播到**全部后续帧**；叠加数值路径全线不同（kissfft float+窗²分母 vs double DFT+COLA）。**不存在可互替段，D-06 禁令维持且不可放宽**。
- **golden 执行注意事项（反证审查新增，gen_inputs/gen_golden 落实时逐条对照）**：① sherpa golden WAV 为 int16 量化（量化噪声 ~−81dB，占 −60dB 判据小份额，round 记录）；② sherpa ReadWave 不支持 24-bit 与 WAVE_FORMAT_EXTENSIBLE，测试输入一律 16-bit PCM；③ golden 命令显式固定 `--chunk-duration-ms`（CLI 默认 10 非 16；16k 无重采样时分块不影响输出，仍固定以绝后患）；④ speech-enhancement-models 为 rolling tag，模型 SHA256 随 D-04 一并登记；⑤ 勿把上游 demo 输出当参考（重建端 np.hanning=symmetric 与分析端 torch.hann_window=periodic 不一致）。
- **O1 证据（GTCRN 许可证）**：gtcrn_simple.onnx 出处链已闭环到上游仓库 LICENSE = **MIT**（Copyright (c) 2024 Rong Xiaobin）——结论【倾向】可用、与 D1 兼容；缺口仅 sherpa release 页无逐字模型许可字样。已于 2026-10-14 经用户确认关闭（REQUIREMENTS v1.1）。
- 真实语音/音乐素材的来源与许可：待选定后登记（避开 ContentVec 类 NC 陷阱）。
- **UVR-MDX-NET-Voc_FT 图签名已实测**（`research-mdx-uvr-audio-separator.md`，onnx 直查模型文件 + audio-separator 源码佐证）：输入/输出名 `input`/`output`，shape `[b,4,3072,256]`，**plane-major**。官方参数表（末尾 10MiB MD5 查表）：**n_fft=7680**、dim_t 静态 256、primary_stem=Vocals、compensate=1.021。
- **SG5 修复清单（当前实现对 Voc_FT 产出废音，两处确认 bug；plane-major 已被反证穷尽 7 实现坐实）**：① 输出按 frame-major 读（`vocalseparator.cpp:149`）须改 plane-major；② `nFft=dimF*2=6144` 须改查表 7680。附带：分析谱前 3 频点置零、伴奏 compensate 系数、边界策略差异（我们整流反射填充 vs 官方 chunk+零 trim，见 parity 计划 §5）——golden 侧用编程接口取 demix 浮点输出（跳过 0.9 写盘归一化与 int16 量化）。**2026-10-14 反证补强（`falsify-mdx.md`）**：parity 参考参数集钉死 audio-separator 默认（overlap 0.25/segment 256/batch 1/denoise off；UVR5 GUI 默认 step 不同不采）；dim_t 非全系 256（表含 128/512），输出断言与缓冲用查表值；两家共用同一参数表（命中非独立交叉，另有 UVR 5.4 硬编码佐证）。
- 参考实现锁定：audio-separator v0.47.0（pip `audio-separator`，torch 硬依赖、onnxruntime 为可选 extra——golden 侧装 `[cpu]`）；模型哈希与版本随 SG1 登记进依赖矩阵。
