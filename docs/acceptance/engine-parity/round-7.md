# WP-1.1 round-7：FR-4 质量档 PAR-DF 族首测（2026-11-04）

FR-4（spec `docs/spec/fr4-quality-tier.md` §7.1 拍板）落地 DFN3（DeepFilterNet3）质量档引擎
`dfn3denoiser`，本 round 首测 PAR-DF parity 族并登记基线。素材三件（`gen_inputs.py dfn3_speech`
确定性合成，配方/哈希登记 fixtures.json）：dfn3noisy65（65s 合成 noisy 语音）/ dfn3bound
（480×7+377 边界）/ dfn3short（480×3+123 最短可发射 3 帧）。

## 0. 金样本主链校准（对 spec 附裁 B / plan 勘误的再勘误）

spec 附裁 B 定「cargo 构建官方 CLI 产金样本」；实现轮实测（2026-11-04）：官方 Releases
**有** deep-filter CLI 预编译产物（`deep-filter-0.5.1-x86_64-unknown-linux-musl`，23689656
字节，sha256 `ea9c8ce420bc7dfbba7deb5a3a980903f8ef31b9bde7647ea5458e8a767659bc`）——
plan.md 勘误条目里「Releases 无对应发布物」只对**模型 tar**成立，对 CLI 二进制不成立。
据此金样本主链改用 Releases 预编译（`fetch-deps.sh` sha 钉死备齐到 `third_party/tools/`）：

- 免 cargo/rustc 工具链依赖（ci 环境少一条重依赖链）；
- 二进制可 byte 级钉死出处（版本漂移由 sha 把关），优于「本机 cargo 构建」的可复现性口径；
- cargo 构建路径保留为 fetch 失败时的 runbook 备选（克隆 f1d19bff 在位可考）。

金样本生成 `gen_golden.py dfn3 <素材>`：CLI 产 wav → ffmpeg 解 f32le（48k/1ch）对齐 DUT 裸流域。
**⚠ 一文件一进程**：官方 CLI 单次调用多文件时**不重置模型状态**（libDF 滚动谱缓冲/归一化状态跨
文件携带，round-1 实锤：三文件同次调用产出的第 2/3 件金样本吃到前件尾巴，parity 假 FAIL 到
−0.0dB），gen_golden 逐文件起独立进程并在 docstring 记档。

## 1. 实测（`run_parity.sh build-linux`，PAR-DF 族全 PASS）

| 用例 | 实测 | 门 | 说明 |
|---|---|---|---|
| PAR-DF-001 | **−75.3dB** | ≤−60 | 65s 主素材整段单 Run vs 金样本，corr 0.9999+ |
| PAR-DF-002 | **−78.9dB** | ≤−60 | 480×7+377：τ<2 零初始化发射 + 非整 hop 尾帧双边界 |
| PAR-DF-003 | **rms(d)=1.65e-05**（max\|d\| 3.1e-05） | ≤2e-4 绝对 | 近零 golden 相对 dB 失真（降噪器把近静音压到 ~1e-4 量级），绝对判据量真差异（compare.py `--gate-abs`，本轮新增） |
| PAR-DF-SEAM | **−3.4dB** | ≤−2.5（回归护栏） | 生产块 30s vs 整段单 Run，见 fr4-quality-tier/round-1.md §3（素材依赖性 + K 扫描 + 注意力记忆结论；阈值归 OQ-2 主线程裁） |
| PAR-DF-FEAT | **9 输出全 0.000e+00** | ≤1e-5 | 图级夹具回放：python ort 1.31.1 单线程 Sequential vs 夹具（C++ ort 1.30.0 产）逐位一致；df_dec 未命名第二输出官方导出即有、引擎按名绑定 `coefs` 不消费，只比登记输出 |
| PAR-DF-SR-meta | PASS | — | 生产链 48k 单声道 `engineResampled=false` + outputSpec 48000/1ch |
| PAR-DF-SR-head | PASS | — | 产物容器头 48000/1ch |
| PAR-DF-SR | **max\|d\|=0** | ≤−60 | 生产链 vs 引擎直调整段单 Run bit-exact（48k 源全链免重采样，PAR-SR-001 同型；AI 引擎首个跟随源） |

判据脚本改动：`compare.py` 新增 `--gate-abs`（绝对判据）与 log10 零除护栏（rms_d=0 时 PAR-DF-SR
bit-exact 触发）；`gen_inputs.py` 新增 dfn3_speech 合成配方与三素材；`gen_golden.py` 新增 dfn3
子命令；`run_parity.sh` 新增 PAR-DF 族（依赖分级 SKIP：缺模型/CLI 缺席整族 SKIP，
`PARITY_REQUIRED=1` 升 FAIL；FEAT 子族另需 python onnxruntime）。

## 2. 结论

- 单 Run 数值 parity −75.3dB 过 −60dB 硬门禁（NFR-Q1）：前端 DSP 复刻 + 三会话 ORT 推理与官方
  CLI 数值同构，残差归 float32 舍入与 vorbis 窗/ERB 累加次序差。
- 输出长度约定实测钉死（附裁 A）：**输出长度 = 输入长度**（含不足帧尾帧零填；官方 CLI 对短尾帧
  break 不输出、引擎零填，两者在整素材上等长对齐——PAR-DF-002/003 即该约定的边界卷）。
- SEAM −3.4dB 非 bug：DFN3 注意力/GRU 记忆跨块有效，块化必然放弃跨块未来上下文；该差在真语音
  素材上放大到 −12.8dB（开发素材 /tmp/noisy65.f32 复测），属**质量档选型的固有代价**而非实现
  错误，与单 Run 门禁分立判据（详见 fr4-quality-tier/round-1.md §3）。
