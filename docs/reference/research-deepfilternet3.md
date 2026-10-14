# DeepFilterNet3（DFN3）调研报告

## 元信息

- **调研日期**：2026-10-14（任务下发 2026-10-13）
- **信源范围**：仅一手信源——官方 GitHub 仓库 [rikorose/DeepFilterNet](https://github.com/Rikorose/DeepFilterNet)（README、`libDF/` Rust 源码、`DeepFilterNet/df/scripts/export.py`、git tree、issues、CI workflow）、arXiv 论文（DFN1/2/3 三篇）、GitHub Releases 页、PyPI、官方发布模型包 `models/DeepFilterNet3_onnx.tar.gz`（实际下载解包，解析 config.ini 与三个 ONNX 图）、GTCRN 官方仓库 [Xiaobin-Rong/gtcrn](https://github.com/Xiaobin-Rong/gtcrn)。社区集成案例（soniqo、Intel OpenVINO、VST3、npm 包）仅作「路径先例」证据，均标注出处。
- **用途**：VideoHandler「AI 降噪质量档」选型与集成路径评估；支撑 `docs/REQUIREMENTS.md` 的 NFR-Q1（DFN3 parity 对照官方 Rust runtime）与后续实时化（48k、全链路 ≤150ms）决策。
- **方法备注**：调研期间 GitHub API / raw.githubusercontent.com 多次限流，源码与模型经 jsDelivr CDN 与 GitHub blobs API 获取，与仓库文件交叉核对一致（未做逐 blob SHA 校验）。ONNX 参数量与图结构为本地实测（自写 protobuf 解析脚本统计 initializer，非估算）。
- **标注规则**：见文末「结论分级」——【已确认】一手信源可核验；【倾向】有依据的推断；【未确认】查不到，禁止采信为事实。

---

## 主要发现

### 1. 架构与参数（问题 1）

**定位**：全频带 48kHz 实时语音增强，两阶段输出——

- **Stage 1（ERB 增益）**：对 32 个 ERB（等效矩形带宽）频带估计实数增益掩码，覆盖全频带（0–24kHz）；
- **Stage 2（Deep Filtering）**：对最低 `nb_df=96` 个频点（bin 宽 50Hz，即约 0–4.8kHz）估计 `df_order=5` 阶复数滤波系数，对滚动频谱缓冲做多帧复数滤波。

**DSP 参数**（官方 `DeepFilterNet3_onnx.tar.gz` 内 `config.ini` 实测，`[df]` 与 `[deepfilternet]` 段）：

| 参数 | 值 | 含义 |
|---|---|---|
| `sr` | **48000** | 原生采样率确为 48k（README 亦自称 "Full-Band Audio (48kHz)"） |
| `fft_size` | 960 | 20ms 窗 |
| `hop_size` | 480 | 10ms 帧移 |
| `nb_erb` | 32 | ERB 频带数 |
| `nb_df` | 96 | DF 作用频点数 |
| `df_order` | 5 | DF 阶数（时间维滤波长度） |
| `df_lookahead` | 2 | DF 前视帧数 |
| `conv_lookahead` | 2 | 卷积编码器前视帧数 |
| `norm_tau` | 1 | 特征归一化时间常数（秒） |
| `min_nb_erb_freqs` | 2 | 每 ERB 带最少频点数 |
| `lsnr_min` / `lsnr_max` | −15 / 35 | 局部 SNR 估计夹取范围 |
| `conv_ch` | 64 | 卷积通道宽度 |
| `emb_hidden_dim`×层数 | 256×3 | GRU 嵌入层 |
| `df_hidden_dim`×层数 | 256×2 | DF GRU 层 |
| `linear_groups` / `enc_linear_groups` | 16 / 32 | 分组线性层 |

**算法延迟**（两种口径，均为一手）：

- **实现代码口径**：`libDF/src/bin/enhance_wav.rs` L134–135：`delay = (fft_size − hop_size) + lookahead × hop_size = 480 + 2×480 = 1440 样本 = 30ms`（`lookahead = max(conv_lookahead, df_lookahead) = 2`，`tract.rs` L313）。`--compensate-delay` 即按此裁剪。
- **论文口径**：DFN3 论文（arXiv 2305.08227）称 "overall latency of 40 ms"（20ms 窗 + 2×10ms 前视）。
- 另有官方无前视变体 `DeepFilterNet3_ll_onnx.tar.gz`（`default-model-ll` feature，LADSPA 用），最小延迟只剩 STFT 的 20ms。

**模型大小**（对官方发布 ONNX 的实测）：

- 参数总量 ≈ **950,385**（fp32 权重约 3.8MB）：enc 88,833 + erb_dec 30,145 + df_dec 831,407（df_dec 由 4 个 `[1,768,256]` GRU 张量主导）。
- 发布包 `DeepFilterNet3_onnx.tar.gz` = 7,983,136 B（未压缩 ONNX 共 8,601,600 B）；社区部署方 soniqo 也标注 ~8MB。

**状态设计**（对集成最关键的一点）：

- **三个 ONNX 图内没有任何循环状态 I/O**（实测图输入仅特征张量；GRU 算子内化在图中）。即：每次 `Run()` 从零状态出发，沿动态时间轴 S 处理整段序列。
- **官方流式状态全部在 Rust runtime 里**，分三层：
  1. `tract-pulse` 推理状态：`PulsedModel::new(&m, s, &1.to_dim())` 把时间维转成脉冲-1，`SimpleState` 跨 `run()` 调用保持 GRU 隐状态（`libDF/src/tract.rs`）；
  2. `DFState`（`libDF/src/lib.rs` L50–226，每声道一份）：STFT `analysis_mem`/`synthesis_mem`（长 fft−hop）、**vorbis 窗** `sin(π/2·sin²(π(n+0.5)/N))`（sqrt-Hann 族，L127–132）、`wnorm`、ERB filterbank（`erb_fb`，L68–100）、`mean_norm_state` 初值 −60→−90 线性（L184–194）、`unit_norm_state` 初值 0.001→0.0001（L195–204）；
  3. `DfTract`（`tract.rs`）：`rolling_spec_buf_y`（df_order+conv_lookahead 帧）与 `rolling_spec_buf_x`（max(df_order, lookahead) 帧）滚动频谱、`skip_counter` 静音跳过、归一化系数 `alpha = calc_norm_alpha`（`exp(−dt/tau)` 后按精度取整，L989–999）。

来源：
- https://github.com/Rikorose/DeepFilterNet/blob/main/models/DeepFilterNet3_onnx.tar.gz （config.ini 与 ONNX 实测）
- https://github.com/Rikorose/DeepFilterNet/blob/main/libDF/src/bin/enhance_wav.rs
- https://github.com/Rikorose/DeepFilterNet/blob/main/libDF/src/tract.rs
- https://github.com/Rikorose/DeepFilterNet/blob/main/libDF/src/lib.rs
- https://arxiv.org/abs/2305.08227
- https://github.com/Rikorose/DeepFilterNet/blob/main/README.md

### 2. 官方运行时形态与 ONNX 导出；状态如何传递（问题 2）

**官方运行时形态**：

| 形态 | 说明 | 证据 |
|---|---|---|
| Rust `libDF` | 核心。推理用 tract + tract-pulse（非 ORT）；`crate-type = ["cdylib","rlib","staticlib"]`；`capi.rs` 暴露 C API：`df_create / df_free / df_get_frame_length`(=hop) / `df_process_frame`(进 hop 样本出 hop 样本，返回 lsnr) / `df_set_atten_lim` / `df_set_post_filter_beta` / `df_process_frame_raw`；cbindgen 生成 `deep_filter.h` + pkgconfig；CI `build_capi.yml` 构建 6 目标（含 **windows-msvc**） | `libDF/Cargo.toml`、`libDF/src/capi.rs`、`.github/workflows/build_capi.yml` |
| `deep-filter` CLI | Releases 页预编译二进制：win-msvc `.exe`、linux-musl、macOS、arm 等；**只接受 48kHz wav** | README「Usage」+ Releases |
| LADSPA 插件 | `ladspa/`，配 PipeWire filter-chain 做麦克实时降噪；用 `_ll` 无前视模型 | `ladspa/README.md` |
| Python 包 | PyPI `deepfilternet` 0.5.6（2023-08-31 上传，MIT）；libdf 的 STFT 经 maturin 混编，DNN 走 PyTorch | https://pypi.org/project/deepfilternet/ |
| WASM | `libDF/src/wasm.rs` + `build_wasm.yml` | git tree 实证 |

**官方 ONNX 导出（仓库内证据，非社区产物）**：

- `DeepFilterNet/df/scripts/export.py`：把模型拆成**三个子图**导出——`enc.onnx`（feat_erb/feat_spec → e0,e1,e2,e3,emb,c0,lsnr）、`erb_dec.onnx`（emb,e3,e2,e1,e0 → m）、`df_dec.onnx`（emb,c0 → coefs）；时间维 S 为动态轴；opset 默认 12（`--opset` 可给 14）；导出后 `onnxsim` 化简，并**用 ONNX Runtime CPUExecutionProvider 对拍** `rtol=1e-6, atol=1e-5`（export.py L113–122）——即官方自己就把 ORT 当作三个图的正确性裁判。
- 随包产出 npz 输入输出夹具（`enc_input.npz` 等）可直接当 parity 测试的 golden 数据。
- 成品 `models/DeepFilterNet3_onnx.tar.gz`（enc/erb_dec/df_dec + config.ini + version.txt）**直接提交在仓库里**。

**运行时张量形状**（issue #406 + tract.rs 输入 fact）：

- enc 输入：`feat_erb [1,1,S,32]`、`feat_spec [1,2,S,96]`（实虚拆到 channel 轴）
- erb_dec 输入：`emb [1,S,512]`（= conv_ch×nb_erb/4）、`e3/e2 [1,64,S,8]`、`e1 [1,64,S,16]`、`e0 [1,64,S,32]`
- df_dec 输入：`emb [1,S,512]`、`c0 [1,64,S,96]`；输出 `coefs [1,96,5]`（实虚交织）**〔2026-10-14 反证勘误（`falsify-fft-dfn3.md`）：实测末维为 10（=5 阶×实虚 2），且该图有第二个输出——集成时以自查图签名为准〕**

**若走 ONNX Runtime C++，状态传递的结论**（由图结构 + Rust 源码推得）：

- 图内 GRU 无状态 I/O、每次 `Run()` 从零状态出发 → **离线整段/大块一次 Run 是天然正确路径**（GRU 状态沿 S 维在图内演进）；
- **逐帧 Run（S=1）会每帧清零状态，结果错误**。流式需要二选一：(a) 改 `export.py` 把 GRU 的 initial/final state 暴露为图 I/O 后重导出（官方无现成产物，需自行改）；(b) 块式 Run，接受块边界状态重置引入的接缝误差。
- 除推理状态外，**前端 DSP 必须复刻**（全部在 lib.rs / tract.rs，有现成 Rust 参考实现）：vorbis 窗 STFT/WOLA、ERB filterbank、`feat_erb`（对数域 + 指数均值归一，alpha）、`feat_cplx`（单位归一）、lsnr 三档门控跳级（`apply_stages`）、DF 滚动缓冲复数滤波、post_filter、atten_lim 混合（`enh·(1−lim)+lim·noisy`）、静音跳过（rms<1e-7 连续 5 帧输出零）。
- **阈值口径必须对齐（2026-10-14 反证坐实）**：论文原文（"local SNR ξ ∈ [−15, 35] dB"）、config.ini、官方 CLI 默认（enhance_wav.rs）、capi.rs **四方一致 = −15/35/35**；−10/30/20 仅是 `tract.rs` `default_with_ch` 的 Rust API 出厂默认，被所有官方发布入口显式覆盖。**parity 对拍目标固定为官方 deep-filter CLI（−15/35/35）**，见 `falsify-fft-dfn3.md`。

来源：
- https://github.com/Rikorose/DeepFilterNet/blob/main/DeepFilterNet/df/scripts/export.py
- https://github.com/Rikorose/DeepFilterNet/blob/main/libDF/Cargo.toml
- https://github.com/Rikorose/DeepFilterNet/blob/main/libDF/src/capi.rs
- https://github.com/Rikorose/DeepFilterNet/issues/406
- https://github.com/Rikorose/DeepFilterNet/releases
- https://pypi.org/project/deepfilternet/

### 3. 许可证（问题 3）

- **代码**：MIT OR Apache-2.0 **双许可**。根目录与 `libDF/`、`DeepFilterNet/`、`pyDF/`、`pyDF-data/`、`ladspa/` 各自都有 `LICENSE-MIT` + `LICENSE-APACHE`（git tree 实证）。
- **模型权重**：`models/` 目录**只有** tar.gz/zip 文件，**无独立 LICENSE**；README 许可段措辞是 "All **code** in this repository is dual-licensed"。→ 权重许可正式意义上未单独声明【倾向：随仓库双许可覆盖；非法律意见】。
- PyPI 包元数据：MIT。
- 与本项目红线的关系：MIT/Apache-2.0 与现有 LGPL/MIT 组合（D1）兼容，无 GPL 引入风险；权重许可这条在发版前值得向作者要一句书面确认（对应我们 O1/O4 许可证核验闸门）。

来源：
- https://github.com/Rikorose/DeepFilterNet/blob/main/README.md （License 段）
- https://github.com/Rikorose/DeepFilterNet/tree/main/models （目录内容）
- https://pypi.org/project/deepfilternet/

### 4. 性能（问题 4）

| 数据 | 值 | 出处 | 口径 |
|---|---|---|---|
| DFN3 RTF | **0.19** | DFN3 论文（arXiv 2305.08227） | 单线程、笔记本 CPU（论文自述） |
| DFN2 RTF | 0.04 | DFN2 论文（arXiv 2205.05474） | Core-i5 笔记本，代际参考 |
| DFN3 社区 RTF | ~0.15 | soniqo.audio 部署指南 | Android/嵌入式级 CPU + ORT，自报，未标明具体 CPU |
| GTCRN RTF | 0.07 | GTCRN 官方 README | 流式、i5-12400 |
| 内存占用 | **查无数据** | — | 官方论文/仓库/Releases 均未给 RSS 【未确认】 |

- 可推断的内存下界：权重 fp32 ≈ 3.8MB + 推理运行时自身（tract 或 ORT）+ 每声道状态（KB 级）——但这是推断不是实测。
- 计算量级差：DFN3 参数 950K vs GTCRN 48.2K（约 20 倍），RTF 0.19 vs 0.07（硬件口径不同，不能直接除，仅数量级参考）→ 「DFN3=质量档、GTCRN=快速档」的分工与算力差成立。

来源：
- https://arxiv.org/abs/2305.08227
- https://arxiv.org/abs/2205.05474
- https://github.com/Xiaobin-Rong/gtcrn
- https://soniqo.audio/guides/denoise/android

### 5. 与 GTCRN 的定位差异与社区口碑（问题 5）

| 维度 | DeepFilterNet3 | GTCRN |
|---|---|---|
| 原生采样率 | 48k 全频带 | 16k |
| 参数量 | ≈950K（官方 ONNX 实测） | 48.2K（官方 README） |
| 算力 | 论文 RTF 0.19 | 33.0 MMACs/s；流式 RTF 0.07@i5-12400（官方 README） |
| 算法延迟 | 30ms（实现口径）/ 40ms（论文口径） | 本次未核对具体值【未确认】 |
| 质量（ intrusive） | PESQ 3.17 / CSIG 4.34 / CBAK 3.61 / COVL 3.77 / STOI 0.944（**VoiceBank+Demand 48k**，论文） | PESQ 2.87 / STOI 0.940（**VCTK-DEMAND 16k**，官方 README） |
| 感知指标 | 论文 LSNR-MOS（VCTK-DEMAND）：DFN2→DFN3 OVRL 2.57→2.78 | DNS3 盲测 DNSMOS 3.44/3.90/2.70（官方 README） |
| C++ 生态 | 无 sherpa-onnx 级现成前端；官方参考实现是 Rust | sherpa-onnx 内置（PR 1977，2025-03）→ 有现成 C++ 前端可抄 |
| 维护状态 | **休眠**：最后 push 2024-10，最后 release v0.5.6（2023-08-31） | **活跃**：news 至 2026-01（LADSPA、H-GTCRN、wasm demo） |

**可比性警告**：两者质量数字**数据集与采样率都不同，不可直接对比**。同数据集可横比的只有 GTCRN 论文表格（16k VCTK-DEMAND）：GTCRN 0.05M/0.03GMACs/s，PESQ 2.87/STOI 0.940 vs DeepFilterNet（2022，即 DFN1）1.80M/0.35G，PESQ 2.81/STOI 0.942——即同表内 GTCRN 以 1/36 参数打平 DFN1；DFN3 的提升要看其论文自比的 LSNR-MOS 与 VoiceBank 指标。「DFN3 感知质量优于 GTCRN」是方向性倾向，不是同基准结论。

**口碑**：DFN 在 Linux 桌面社区作为 PipeWire/LADSPA 麦克风降噪方案广泛使用；v0.5.1 release note 曾「Revert DNN inference framework … due to audible artifacts」——同一权重换推理框架会出现可听差异，这正是我们 NFR-Q1 要求 parity 的实证理由。

来源：
- https://github.com/Xiaobin-Rong/gtcrn （参数/算力/指标/news）
- https://arxiv.org/abs/2305.08227 、https://arxiv.org/abs/2110.05588
- https://github.com/Rikorose/DeepFilterNet/releases
- https://github.com/k2-fsa/sherpa-onnx/pull/1977

### 6. 集成路径先例（问题 6）

**路径 A：链接官方 Rust libDF（C API）**

- 官方原生支持跨语言消费：`staticlib`/`cdylib` + cbindgen 的 `deep_filter.h` + pkgconfig；`df_process_frame` 即 10ms 进/10ms 出的实时逐帧接口。官方自己的 LADSPA 插件、pyDF（maturin）、WASM 都是「外部链接 libDF」的先例。
- 风险：官方 CI 只构建 **windows-msvc**（build_capi.yml 六目标），我们的工具链是 MinGW GCC（MSYS2）——GNU target 编译 libDF 未被官方验证【未确认可行性】；tract/tract-pulse 依赖树较大。

**路径 B：ONNX Runtime C++ + 前端复刻**

- 官方立场背书：export.py 本身用 ORT 做导出对拍；模型包直接入库分发。
- 先例（均为第三方，非官方）：
  - **soniqo.audio**：DFN3 跑在 Android/嵌入式 Linux/Windows，后端 ONNX Runtime，麦克风降噪置于 VAD/STT 之前；模型镜像 `huggingface.co/soniqo/DeepFilterNet3-ONNX`，自报 RTF ~0.15、模型 ~8MB——即「ORT C++ 前端复刻」在移动/嵌入式上可行的公开案例（其 Android 源码在 github.com/soniqo/speech-android）。
  - **issue #472**：作者上传的两个浏览器示例，其一 WASM、其一 **onnxruntime-web**（2023-11）——官方作者本人演示过 ORT 前端跑 DFN3【倾向：细节来自搜索摘要，GitHub API 限流未核验正文】。
  - **Intel/deepfilternet-openvino**（HF）：DFN2/3 的 OpenVINO IR 导出——又一个第三方运行时移植。
  - **Shuichi346/DeepFilterNet3-VST3**：macOS VST3 插件，用官方模型。
  - **npm `deepfilternet3-noise-filter`**（LiveKit 生态）：浏览器端 DFN3。
- 盲点：**未找到公开完整源码的桌面 C++ ORT 离线引擎级复刻项目**（soniqo 最接近但绑定 Android 场景）——若走路径 B，前端复刻基本要自己对着 lib.rs/tract.rs 做，golden 数据可用 export.py 的 npz 夹具起步。

来源：
- https://soniqo.audio/guides/denoise/android 、https://huggingface.co/soniqo/DeepFilterNet3-ONNX 、https://github.com/soniqo/speech-android
- https://github.com/Rikorose/DeepFilterNet/issues/472
- https://huggingface.co/Intel/deepfilternet-openvino
- https://github.com/Shuichi346/DeepFilterNet3-VST3
- https://www.npmjs.com/package/deepfilternet3-noise-filter

---

## 对我们集成的含义（两条路径对照）

| 维度 | 路径 A：链接 Rust libDF（C API） | 路径 B：ORT C++ + 前端复刻 |
|---|---|---|
| NFR-Q1 parity | **天然达成**：与官方 runtime 是同一份代码，golden 对照近乎免费 | 需自建：STFT/窗、ERB、归一化、lsnr 门控、DF 缓冲、post_filter、atten_lim、静音跳过全部复刻 + golden 测试（−60dB RMS 准则可用 export.py 的 npz 夹具起步） |
| 依赖与构建 | 引入 Rust 工具链 + tract/tract-pulse 依赖树；官方 CI 仅 windows-msvc，**MinGW 未验证**（要么切 MSVC 目标、要么自证 GNU target 可编） | 零新增推理依赖（ORT 已在栈内），纯 C++ 与现有 aidenoiser/vocalseparator 同构 |
| 许可 | MIT/Apache，无碍 | MIT/Apache，无碍 |
| 流式（后续实时化） | `df_process_frame` 本就是逐帧实时接口，状态管理官方已做 | GRU 状态困在图内：离线可整段 Run；实时需改图暴露状态重导出，或块式 Run 接受接缝误差——官方无现成状态化导出（官方仓库 2024-10 起休眠，仍无）。**2026-10-14 反证更新（`falsify-fft-dfn3.md`）**：社区 shimondoodkin/deepfilter-rt 已验证完整带状态 I/O 手术方案（enc 拆 conv+GRU 两图、h0/h1/erb_h0/erb_h1/df_h0/df_h1 显式状态 I/O、时间切片；自报与官方 tract 一致 corr=0.999991 / 47.7dB SNR、逐帧 1.7ms、RTF 0.167，覆盖 dfn2/dfn3 全变体）——**该仓库无 LICENSE 文件，代码不可复制，只能参考思路、自行实现或索取授权**；实时化时不必从零摸索 |
| 引擎规格 | 48k（多声道支持，n_ch 参数） | 48k，同左 |
| 工作量重心 | 构建/ABI/体积；引擎封装薄 | 前端 DSP + parity 测试是大头（与 P0 债 1 同源，可复用其测试框架） |
| 主要风险 | MinGW/MSVC 二选一的工程扰动；tract 内存开销无数据 | 精度偏移（v0.5.1「audible artifacts」前车之鉴）；上游休眠，问题只能自己修 |

对项目现状的两点具体含义：

1. **采样率链**：canonical 44.1k 下视频源（48k）会被重采样两次（P0 债 4 / NFR-Q3）。DFN3 原生 48k——若 canonical 改 48k，DFN3 可直通；GTCRN（16k）仍需一次降采样。选 DFN3 做质量档会加强「canonical 改 48k」的动机。
2. **执行顺序**：无论哪条路径，DFN3 都是质量档而非首个落地物——按 REQUIREMENTS 排序（正确性兜底 → 离线闭环 → …），路径 B 的前端复刻工作应排在 parity 测试框架（P0 债 1）建成之后复用之；路径 A 则完全绕开 P0 债 1 对 DFN3 的适用范围。

---

## 结论分级

**【已确认】**（一手信源可直接核验）

1. 原生 48kHz；fft 960 / hop 480（20ms/10ms）；nb_erb=32、nb_df=96、df_order=5、lookahead=2（官方 tar.gz 内 config.ini 实测）。
2. 实现延迟 1440 样本 = 30ms（enhance_wav.rs L134–135 公式）；论文口径 40ms。
3. 参数量 ≈950K（fp32 ≈3.8MB，官方 ONNX initializer 逐张量统计）；发布包 7,983,136 B。
4. 官方 ONNX 三子图导出脚本 export.py 存在且用 ORT 对拍（rtol 1e-6 / atol 1e-5）；npz 夹具随包；模型 tar.gz 直接入库 models/。
5. 三个 ONNX 图无循环状态 I/O，GRU 内化；官方流式状态在 tract-pulse（SimpleState）+ DFState + DfTract 滚动缓冲（源码定位到函数与行）。
6. 代码 MIT OR Apache-2.0 双许可（含各子 crate LICENSE 文件）；models/ 目录无独立许可文件。
7. RTF：DFN3 0.19（论文，笔记本单线程）、DFN2 0.04（论文）；GTCRN 0.07@i5-12400（官方 README）。
8. libDF 以 staticlib/cdylib + C API（cbindgen 头）分发，CI 六目标含 windows-msvc；`df_process_frame` 为 10ms 逐帧接口。
9. GTCRN 官方数据：48.2K 参数 / 33.0 MMACs/s / PESQ 2.87（VCTK-DEMAND）；sherpa-onnx PR 1977 收录。
10. DFN 仓库休眠（最后 push 2024-10，最后 release v0.5.6 = 2023-08-31）；GTCRN 活跃至 2026-01。
11. ~~lsnr 门控阈值存在两套口径，parity 必须先钉死对照对象~~ 已钉死（2026-10-14 反证）：论文/config/CLI/capi 四方一致 −15/35/35，−10/30/20 只是 Rust API 出厂默认；parity 对拍 = 官方 deep-filter CLI。

**【倾向】**（有依据的推断，未经实验或二手转述）

1. 模型权重随仓库双许可覆盖（README 只声明 "code"，models/ 无 LICENSE——发版前建议向作者书面确认）。
2. issue #472 为作者上传的 WASM + onnxruntime-web 浏览器示例（搜索摘要转述，API 限流未核验正文）。
3. ORT C++ 流式必须自管 GRU 状态（改图暴露状态或块式 Run）——由图结构 + 官方用 tract-pulse 的事实推得，未做实验。
4. DFN3 感知质量优于 GTCRN——跨数据集不可直接比，仅方向性判断（同数据集仅有 GTCRN vs DFN1 打平的横比）。

**【未确认】**（查不到，禁止当事实用）

1. 内存占用：官方与社区均无 RSS 实测数据。
2. GTCRN 的算法延迟具体数值（本次未核对其帧结构）。
3. MinGW/GNU 工具链构建 libDF 的可行性（官方 CI 仅 windows-msvc）。
4. soniqo 的 RTF ~0.15 所对应的具体硬件型号。
5. `export.py` 的 `export_full` 单图整模型导出对 DFN3 是否可用（脚本存在但面向旧命名 `deepfilternet2.onnx`，未验证）。
6. huggingface.co/hshr/DeepFilterNet3 不存在（404）——权重仅经 GitHub 仓库 models/ 分发，无官方 HF 权重库。

---

## 参考链接清单

**官方仓库与源码**

- 仓库：https://github.com/Rikorose/DeepFilterNet
- README：https://github.com/Rikorose/DeepFilterNet/blob/main/README.md
- libDF 核心源码：https://github.com/Rikorose/DeepFilterNet/blob/main/libDF/src/lib.rs （DFState/STFT/ERB）
- tract 运行时：https://github.com/Rikorose/DeepFilterNet/blob/main/libDF/src/tract.rs （DfTract/门控/DF）
- C API：https://github.com/Rikorose/DeepFilterNet/blob/main/libDF/src/capi.rs
- libDF 构建配置：https://github.com/Rikorose/DeepFilterNet/blob/main/libDF/Cargo.toml
- CLI 延迟补偿：https://github.com/Rikorose/DeepFilterNet/blob/main/libDF/src/bin/enhance_wav.rs
- C API CI：https://github.com/Rikorose/DeepFilterNet/blob/main/.github/workflows/build_capi.yml
- ONNX 导出脚本：https://github.com/Rikorose/DeepFilterNet/blob/main/DeepFilterNet/df/scripts/export.py
- 官方模型包：https://github.com/Rikorose/DeepFilterNet/tree/main/models
- LADSPA 插件：https://github.com/Rikorose/DeepFilterNet/blob/main/ladspa/README.md
- 张量形状 issue：https://github.com/Rikorose/DeepFilterNet/issues/406
- 浏览器示例 issue：https://github.com/Rikorose/DeepFilterNet/issues/472
- Releases：https://github.com/Rikorose/DeepFilterNet/releases

**论文**

- DFN3（Interspeech 2023 demo）：https://arxiv.org/abs/2305.08227
- DFN2（IWAENC 2022）：https://arxiv.org/abs/2205.05474
- DFN1（ICASSP 2022）：https://arxiv.org/abs/2110.05588

**Python 分发**

- PyPI：https://pypi.org/project/deepfilternet/

**GTCRN 对照**

- 官方仓库：https://github.com/Xiaobin-Rong/gtcrn
- sherpa-onnx 收录 PR：https://github.com/k2-fsa/sherpa-onnx/pull/1977

**社区集成先例（第三方，仅作路径证据）**

- soniqo 部署指南：https://soniqo.audio/guides/denoise/android
- soniqo HF 模型镜像：https://huggingface.co/soniqo/DeepFilterNet3-ONNX
- soniqo Android 源码：https://github.com/soniqo/speech-android
- Intel OpenVINO 导出：https://huggingface.co/Intel/deepfilternet-openvino
- VST3 插件：https://github.com/Shuichi346/DeepFilterNet3-VST3
- npm 浏览器包：https://www.npmjs.com/package/deepfilternet3-noise-filter
