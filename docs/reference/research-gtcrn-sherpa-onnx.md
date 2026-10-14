# GTCRN 与 sherpa-onnx 一手信源调研

## 元信息

- 日期：2026-10-14（任务发起 2026-10-13）
- 目的：为 GTCRN 引擎 parity 测试（NFR-Q1，判据 −60dB RMS）与开放问题 O1（模型许可证核验）提供官方实现的一手事实基线
- 信源范围：仅一手信源——上游官方仓库 Xiaobin-Rong/gtcrn 源码与 LICENSE、k2-fsa/sherpa-onnx master 源码快照（2026-10-14 经 GitHub API tarball 获取，版本 1.13.8）、sherpa 官方文档站、PyPI 官方包索引、arXiv 官方 API、Semantic Scholar API（论文元数据聚合，已标注）
- 本地核验手段：上游与 sherpa 源码均下载到本地逐行阅读（/tmp 快照），非页面转述；关键数字均有 file:line 依据

---

## 主要发现

### 1. GTCRN 论文与模型（问题 1）

**论文信息**

- 标题：*GTCRN: A Speech Enhancement Model Requiring Ultralow Computational Resources*，ICASSP 2024
- DOI：`10.1109/ICASSP48485.2024.10448310`（IEEE 文档号 10448310）
- 作者（Semantic Scholar API 聚合，IEEE 原页未直接访问）：Xiao-Bin Rong, Tianchi Sun, Xu Zhang, Yu-Xiang Hu, Changbao Zhu, Jing Lu
- 该论文**无 arXiv 预印本**：arXiv API 检索 "GTCRN" 仅返回 4 篇引用/衍生论文（H-GTCRN 2505.19597 等），无原文
- 被引约 75 次（Semantic Scholar，2026-10 快照）
- 来源：<https://github.com/Xiaobin-Rong/gtcrn>（README 首段给出 IEEE 链接）；<https://api.semanticscholar.org/graph/v1/paper/DOI:10.1109/ICASSP48485.2024.10448310>

**网络结构**（源码级确认，`gtcrn.py`，与 `stream/gtcrn.py` 逐类一致）

```
输入谱 (B,257,T,2) → 拆 mag/real/imag 3 通道
→ ERB 降带 257→129（低 65 bin 直通 + 高 192 bin 线性组合成 64 带；滤波器组固定不可学习）
→ SFE：频轴 3 帧上下文 unfold，3ch→9ch
→ Encoder：Conv2d(9→16,k=(1,5),stride 2) + grouped Conv2d(16→16) + 3×GTConvBlock(因果，dilation 1/2/5)
→ 2×DPGRNN：intra 轴=频域双向 GRU，inter 轴=时域单向 GRU，LayerNorm+残差
→ Decoder：3×GTConvBlock(反卷积) + ConvTranspose2d×2，与 Encoder 逐层 skip 连接
→ iERB 升带 129→257 → 复数比率掩码(CRM) 乘回输入谱
```

- GTConvBlock 内部：SFE → 1×1 升维 → 因果 depthwise 卷积（时间维前置 padding `(kT−1)*dilation`）→ 1×1 降维 → TRA（时间维能量均值经 GRU→sigmoid 的通道注意力）→ channel shuffle（ShuffleNetV2 风格）
- 量化复杂度入口：`get_model_complexity_info(model, (257, 63, 2))`；`model.py` 自带因果性验证代码（331–345 行：拼接不同未来段验证输出不变）

**STFT 参数**（`infer.py:18` / `model.py:337`，`stream/gtcrn_stream.py:371` 同）

- `torch.stft(x, 512, 256, 512, torch.hann_window(512).pow(0.5))`：**n_fft=512，hop=256，win=512，窗=sqrt-Hann（periodic hann 开方）**，`center=True` 默认（帧 t 中心位于 t·hop），采样率 16 kHz 单声道

**三个状态缓存的设计**（`stream/gtcrn_stream.py`，导出即 ONNX 的状态张量）

| 缓存 | 形状 | 语义 |
|---|---|---|
| `conv_cache` | `(2,1,16,16,33)` | dim0=[encoder,decoder] 两份；16 通道；时间维 16 = 每侧三层因果 depthwise 卷积的 `(kT−1)·dilation` 之和（en: 2+4+10；de: 10+4+2，见 `StreamEncoder::forward` 的切片 `[:2]`/`[2:6]`/`[6:16]`）；F=33 |
| `tra_cache` | `(2,3,1,1,16)` | [en,de] × 每侧 3 个 GTConvBlock 的 TRA-GRU 隐状态（1 层，B=1，16 通道） |
| `inter_cache` | `(2,1,33,16)` | [dpgrnn1,dpgrnn2] 的 inter-GRU（时域单向）隐状态，B·F=33，hidden=16 |

- 机制（`stream/modules/convolution.py`）：`StreamConv2d.forward(x, cache) = Conv2d(cat(cache, x))`，`out_cache = inp[..., 1:]`——纯滑窗拼接，无计算；注释明确"导出 ONNX 时缓存必须是张量而非 list"
- 三缓存由模型自身输出回填（`conv_cache_out/tra_cache_out/inter_cache_out`），初始全零

**模型大小与算力**

- 论文口径：23.7K 参数 / 39.6 MMACs/s；README 修订口径（2024 年后）：**48.2K 参数 / 33.0 MMACs/s**（计入 ERB 不可学习参数；低频段由矩阵乘改为直通拼接降低 MACs）
- 性能（README 表格）：VCTK-DEMAND PESQ 2.87 / SI-SDR 18.83 / STOI 0.940；DNS3 盲测 DNSMOS 3.44 / BAK 3.90 / OVRL 2.70
- 流式 RTF 0.07（i5-12400，上游 stream demo）；sherpa CLI 亦打印 RTF
- 来源：<https://github.com/Xiaobin-Rong/gtcrn>（README"About/Performance/Streaming Inference"三节）

### 2. sherpa-onnx 的 speech-enhancement/GTCRN 支持（问题 2）

**支持时间线**：2025-3-10 经 PR #1977（Fangjun Kuang）加入（上游 README News 节）。当前 master（1.13.8 快照）已有四套实现：offline/online × gtcrn/dpdfnet；online API 为 2026 年新增（源码 Copyright 2026）。

**Python API**（包名 `sherpa_onnx`；pybind 源 `sherpa-onnx/python/csrc/online-speech-denoiser.cc`、示例 `python-api-examples/*-speech-enhancement-gtcrn.py`）

```python
import sherpa_onnx

# 离线（整段）
config = sherpa_onnx.OfflineSpeechDenoiserConfig(
    model=sherpa_onnx.OfflineSpeechDenoiserModelConfig(
        gtcrn=sherpa_onnx.OfflineSpeechDenoiserGtcrnModelConfig(model="gtcrn_simple.onnx"),
        debug=False, num_threads=1, provider="cpu"))
sd = sherpa_onnx.OfflineSpeechDenoiser(config)
denoised = sd(samples_float32_1d, sample_rate)   # → DenoisedAudio(.samples, .sample_rate)

# 流式
config = sherpa_onnx.OnlineSpeechDenoiserConfig(model=同上)
sd = sherpa_onnx.OnlineSpeechDenoiser(config)
for chunk in ...:                      # 任意大小分块
    denoised = sd(chunk, sample_rate)  # 或 sd.run(...)；返回当前可出的增量
tail = sd.flush()                      # 排空并复位（流终止）
# 只读属性：sd.sample_rate、sd.frame_shift_in_samples
```

- **状态不向 Python 暴露**：三份缓存完全封装在 C++ 对象内，调用方只管喂样本、收样本
- 输入采样率可 ≠16k（内部 `LinearResample`：lowpass 0.99·Nyquist，filter width 6；流式固定一次采样率，中途切换报错退出）

**CLI**（`sherpa-onnx/csrc/sherpa-onnx-offline-denoiser.cc` / `sherpa-onnx-online-denoiser.cc` 内嵌 usage 原文）

```
sherpa-onnx-offline-denoiser \
  --speech-denoiser-gtcrn-model=gtcrn_simple.onnx \
  --input-wav=in.wav --output-wav=out_16k.wav

sherpa-onnx-online-denoiser \
  --speech-denoiser-gtcrn-model=gtcrn_simple.onnx \
  --chunk-duration-ms=16 \
  --input-wav=in.wav --output-wav=out_16k.wav
```

**C++ / C API 形态**

- C++：`sherpa_onnx::OnlineSpeechDenoiser`（`Run(samples,n,sr)→DenoisedAudio`、`Flush()`、`Reset()`、`GetSampleRate()`、`GetFrameShiftInSamples()`，头 `sherpa-onnx/csrc/online-speech-denoiser.h`）；`OfflineSpeechDenoiser::Run` 同构；`DenoisedAudio{std::vector<float> samples; int32_t sample_rate;}`
- C API：`SherpaOnnxCreateOnlineSpeechDenoiser` / `SherpaOnnxOnlineSpeechDenoiserRun` / `...Flush` / `...Destroy` / `...GetSampleRate` / `...GetFrameShiftInSamples`（`sherpa-onnx/c-api/c-api.cc` 3056 行起），示例 `c-api-examples/online-speech-enhancement-gtcrn-c-api.c`
- 另有 Java/Kotlin/Swift/Dart/Go/Rust/JS(node-addon)/WASM 绑定

**模型侧约定**（`offline-speech-denoiser-gtcrn-model.cc:148-167`）

- 读 metadata 键：`model_type`(必须="gtcrn")、`sample_rate`、`n_fft`、`hop_length`、`window_length`、`window_type`、`version`、`conv_cache_shape`、`tra_cache_shape`、`inter_cache_shape`——**与我们实现的键集一致**（sherpa 多读 `model_type`/`version` 做校验）
- 每 hop 一次 `session.Run`，输入 4 张量（谱+3 状态），输出 4 张量（增强谱+3 新状态），状态回填
- 谱张量布局 `(1, n_fft/2+1, 1, 2)`，实虚 interleaved（`ProcessFrame`，online-impl 84–94 行）

**流式分帧/延迟约定——parity 对齐核心**（全部出自 `sherpa-onnx/csrc/online-speech-denoiser-stft-impl.h`，逐行核读）

1. **分帧**：输入先进 `pending_input_`，凑满一个 `hop_length`(256) 才 `ProcessHop`；`analysis_buffer_`(512) 每次左移一 hop、新 hop 补到**尾部**——即滑窗右对齐，等效 `torch.stft(center=True)`：帧 t 覆盖 `[t·hop−256, t·hop+256)`，首帧前置 256 个零
2. **首帧延迟的处理**：`started_` 标志。第一次 `ProcessHop` 照常做 STFT+模型推理+WOLA 累加，但**不产出输出**（309–312 行提前 return）。模型从第 0 帧起每帧都推理，跳过的只是输出
3. **对齐关系**：第 t 次 `ProcessHop`（t≥1）产出区间 `[t·hop−256, t·hop)`，与输入**样本级对齐，无时间平移**。喂入 T 个 hop 后累计输出 `(T−1)·hop` 样本——算法滞后恰 **1 hop（16 ms）**，等于中心化 STFT 的 lookahead；这 1 hop 由 flush 排空
4. **WOLA**：分析窗与综合窗都是 √hann（`window_type="hann_sqrt"`→`knf::GetWindow("hann")` 再逐点开方），窗平方和=hann，50% 重叠 COLA 恒为 1，**无归一化分母**（信赖 COLA）
5. **flush 语义**（184–223 行）：重采样器排空 → 处理剩余整 hop → 剩余不足一 hop 的样本**补零成一整个 hop** 再跑一次 → 若 `started_` 则**再补一个全零 hop** → 按总输入样本数（重采样后）**截断**输出 → `Reset()` 并将模型状态重置为零。即 flush 后流终止，不可续流
6. **数值实现**：`StreamingDft` 是 **double 精度直接 DFT**（O(N²) 乘加，余弦表 double，非 FFT）——与我们的 float 基-2 FFT 是精度差来源之一
7. **离线路径差异**（`offline-speech-denoiser-gtcrn-impl.h` + knf v1.22.3 `stft.cc/istft.cc`）：knf::Stft 默认 `center=true, pad_mode="reflect"`（online 是隐式零填充），knf::IStft 用**窗平方和逐样本归一化**（边界精确重建）且输出长度=输入长度。即 sherpa 自家 online/offline 两条路径在首尾 `n_fft/2` 内也不严格一致——**parity 基准应固定用 online 路径**（我们的引擎是流式语义）

### 3. gtcrn_simple.onnx 的出处与许可证（问题 3，对应 O1）

**出处链**（源码级，每一环有据）

1. 上游仓库自带导出产物：`stream/onnx_models/gtcrn_simple.onnx`（352,084 B 的是 gtcrn.onnx，535,190 B 的是 gtcrn_simple.onnx，git tree API 快照）
2. 导出脚本：`stream/gtcrn_stream.py` 401–428 行——`torch.onnx.export(stream_model, (mix, conv_cache, tra_cache, inter_cache), opset 11)`，`gtcrn_simple.onnx` = `onnxsim.simplify(gtcrn.onnx)` 的简化版（上游 `stream/README.md` 注明简化 bug 由 Linghao Yu 修复，issue-3）
3. sherpa 侧打包脚本 `scripts/gtcrn/run.sh` 第 5–7 行原文：`wget https://github.com/Xiaobin-Rong/gtcrn/raw/refs/heads/main/stream/onnx_models/gtcrn_simple.onnx` 然后 `python3 ./add_meta_data.py`——**release 里的模型就是上游文件 + 注入 metadata，无任何权重改动**
4. `scripts/gtcrn/add_meta_data.py` 写入的 metadata：`model_type=gtcrn`、`sample_rate=16000`、`n_fft=512`、`hop_length=256`、`window_length=512`、`window_type=hann_sqrt`、`conv_cache_shape=2,1,16,16,33`、`tra_cache_shape=2,3,1,1,16`、`inter_cache_shape=2,1,33,16`、`model_url=https://github.com/Xiaobin-Rong/gtcrn/blob/main/stream/onnx_models/gtcrn_simple.onnx`、`maintainer=k2-fsa`、`version=1`
5. 分发：GitHub release tag `speech-enhancement-models`（官方文档页 <https://k2-fsa.github.io/sherpa/onnx/speech-enhancement/models.html> 明确"Pre-trained models can be found at …/releases/tag/speech-enhancement-models"，并注明模型来自 Xiaobin-Rong/gtcrn、~523 KB）

**许可证证据**

- 上游仓库 LICENSE 原文（已全文捕获）：
  > "MIT License / Copyright (c) 2024 Rong Xiaobin / Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction…"（标准 MIT 全文）
  来源：<https://raw.githubusercontent.com/Xiaobin-Rong/gtcrn/main/LICENSE>（1069 B）
- sherpa-onnx 代码库本体：Apache-2.0（tarball 根 LICENSE）
- ONNX metadata 中**无独立 license 字段**；sherpa 文档页未对模型另作许可声明

**结论（O1）**：【倾向】gtcrn_simple.onnx 权重随上游仓库以 **MIT** 许可发布，可商用、可再分发，与本项目 D1（LGPL/MIT 组合）兼容。依据是完整出处链 + 上游 MIT 原文；"倾向"而非"已确认"仅因 sherpa release 页面本身未逐字标注模型许可（release 资产清单因 GitHub API 限流未拉取全量）。

### 4. sherpa-onnx 版本与包形态（问题 4）

**最新版本 1.13.8**——三个独立一手信源交叉确认（2026-10-14）：

- master 源码 `CMakeLists.txt:31`：`set(SHERPA_ONNX_VERSION "1.13.8")`
- `CHANGELOG.md` 首节：`## 1.13.8`（含 "Update onnxruntime to v1.28.2 (#3935)"——sherpa 已跟进 ORT 1.28.x，我们锁 1.30 无冲突）
- PyPI 官方索引 `info.version == 1.13.8`

**包形态**

- **pip**：PyPI 包 `sherpa-onnx`（1.13.8 共 66 个文件）。Windows：`win32`/`win_amd64`/`win_arm64` wheel（cp310–cp313）；Linux：`manylinux2014_x86_64`、`manylinux_2_17_aarch64`、`linux_armv7l` wheel；来源 <https://pypi.org/pypi/sherpa-onnx/json>
- **预编译二进制**：GitHub releases 按版本提供各平台静态链接 tar 包（Linux/macOS/Windows），另有专门 speech-enhancement WASM 包 `sherpa-onnx-wasm-simd-v1.13.8-speech-enhancement-gtcrn`（版本号内嵌 1.13.8，与上互证）。具体资产清单未逐项核验（API 限流），标【倾向】
- **源码构建**：CMake（`cmake -DSHERPA_ONNX_ENABLE_BINARY=ON`），依赖经 cmake FetchContent 拉取（如 kaldi-native-fbank v1.22.3）
- **C++ API 形态**：见第 2 节——头文件 + 共享库 `libsherpa-onnx-c-api.so`/`sherpa-onnx-c-api.dll`（C API），或直接链 C++ 静态/动态库

### 5. sherpa-onnx 之外的 GTCRN ONNX 导出/运行时（问题 5）

| 途径 | 说明 | 来源 |
|---|---|---|
| 上游仓库本体 | 就是 ONNX 导出的源头：`gtcrn.onnx`/`gtcrn_simple.onnx` + onnxruntime CPU 逐帧推理 demo（`gtcrn_stream.py` 431–461 行，含 RTF 打印） | Xiaobin-Rong/gtcrn |
| 上游 LADSPA 插件 | `ladspa/`（Rust + 最小化 ORT，2026-1-18 加入，社区 Bruno Gonçalves 维护），Linux PipeWire 实时滤波 | 上游 README News + git tree |
| sherpa-onnx WASM | HF Space `k2-fsa/wasm-speech-enhancement-gtcrn` 在线 demo（2025-3-13） | 上游 README News |
| H-GTCRN | 双通道低信噪比衍生模型（Max1Wz，2025-5-27），非同一模型 | 上游 README News |
| AX650 NPU 移植 | HF `inoryQwQ/sherpa-onnx-gtcrn-ax650`，基于 sherpa 管线转 AXMODEL | web 检索结果 |
| TRT-SE | 作者的通用"SE 模型→流式→ONNX/TensorRT"模板仓库，上游 stream 转换方法即源于此 | 上游 `stream/README.md` |

即：**ONNX 权重只有一个源头（上游仓库），sherpa-onnx 是最完备的运行时**；其余均为其衍生或专用部署。

---

## 与我们实现的对照（差异点逐条）

我们现状（任务描述）：metadata 读 `sample_rate/n_fft/hop_length/window_length/window_type/conv|tra|inter_cache_shape`；4 输入（谱+3 状态）/4 输出；每 hop 推理一次、状态回填；首帧吞一个 hop 补偿算法延迟；flush 补零 hop 后按总输入长度截断。

| # | 项目 | sherpa-onnx（源码事实） | 我们 | 对照结论 |
|---|---|---|---|---|
| 1 | metadata 键集 | 上列 10 键 + `model_type` 校验 + `version` | 一致（少 model_type/version） | 兼容；可补 model_type 校验防误用 |
| 2 | 模型 I/O | 4 入 4 出、逐 hop 推理、状态回填、初始零 | 一致 | 一致 |
| 3 | 谱张量布局 | `(1,257,1,2)`，[bin][frame][re,im] interleaved；帧 t 中心 = t·hop（center=True 等效） | 需按此核对 | parity 时逐字节核对排布与帧中心定义 |
| 4 | 首帧延迟 | 模型**每帧都推理**（含第 0 帧，其 STFT 前置补零）；仅 WOLA 输出首 hop 不发射；输出样本级对齐、滞后恰 1 hop | "首帧吞一个 hop" | **语义若为'输出延迟一 hop'则一致；若为'少推理一帧'则错位一帧——parity 首查项** |
| 5 | flush | 余量补零成整 hop 跑一次 + 再补一个全零 hop（若已 started）→ 按输入总长截断 → 状态清零、流终止 | "补零 hop 后按总输入长度截断" | 表述一致；需核对'不足一 hop 的余量'与'截断基准是重采样后的样本数'两细节 |
| 6 | 窗 | `hann_sqrt` = **periodic** hann 开方（knf GetWindow("hann") 即 torch 周期形式） | 从 metadata 读 window_type | 确认我们实现的是 periodic 而非 symmetric hann |
| 7 | WOLA 归一化 | 信赖 COLA（√hann·√hann=hann，50% 重叠和恒 1），无分母 | 自实现 WOLA | 一致的前提下边界才对齐 |
| 8 | DFT 数值精度 | online 用 **double** 直接 DFT（O(N²) 表 double） | float 基-2 FFT | −60dB 判据的主要数值风险点；必要时我们升级 double 或 FFT 后按误差预算验证 |
| 9 | 采样率换算 | 非 16k 输入走 LinearResample(0.99·Nyquist, width 6)，输出 16k | FFTranscoder 双 SwrContext(44.1k canonical) | 两套重采样器特性不同；**parity 测试直接喂 16k WAV 绕开**，重采样一致性另测 |
| 10 | 基准路径选择 | sherpa online 与 offline 在首尾 n_fft/2 内亦不一致（零填充 vs reflect；COLA vs 窗²归一化） | — | golden 取 `sherpa-onnx-online-denoiser` 输出，勿用 offline |

---

## 结论分级

**【已确认】**（源码逐行或官方页直接证据）

- GTCRN 为 ICASSP 2024 论文（DOI 10.1109/ICASSP48485.2024.10448310），官方仓库 Xiaobin-Rong/gtcrn；无 arXiv 原文
- STFT：16 kHz、n_fft=512、hop=256、win=512、sqrt-Hann(periodic)、center=True 等效
- 三缓存形状 (2,1,16,16,33)/(2,3,1,1,16)/(2,1,33,16) 与滑窗回填机制；ONNX opset 11、4 入 4 出命名
- 模型规模：论文 23.7K/39.6 MMACs，README 修订 48.2K/33.0 MMACs；VCTK-DEMAND PESQ 2.87；流式 RTF 0.07@i5-12400
- sherpa-onnx 支持 GTCRN（2025-3-10，PR #1977）；Python/CLI/C++/C API 形态与函数签名（本文第 2 节全部来自源码）
- 流式对齐语义：输出与输入样本级对齐、模型逐帧推理、首 hop 输出延迟、flush 双补零 hop + 截断 + 状态复位；online 用 double 直接 DFT；offline 走 knf（reflect 填充 + 窗²归一化）
- gtcrn_simple.onnx 出处链：上游仓库导出（onnxsim 简化）→ sherpa `scripts/gtcrn/run.sh` 原样 wget → `add_meta_data.py` 注入 10 项 metadata → release `speech-enhancement-models` 分发；上游 LICENSE 为 MIT（原文全文捕获）
- sherpa-onnx 最新版 1.13.8（CMakeLists + CHANGELOG + PyPI 三源互证）；PyPI 提供 Windows(win32/amd64/arm64) 与 Linux(x86_64/aarch64/armv7l) wheel

**【倾向】**

- O1 结论：gtcrn_simple.onnx 权重许可为 MIT、可用于本项目（出处链完整 + 上游 MIT 原文；唯一缺口是 sherpa release 页无逐字模型许可声明）
- GitHub releases 的预编译二进制资产清单（版本号方向已由 WASM 包名与文档互证，未逐项拉取列表）
- 论文作者名单（来自 Semantic Scholar 聚合，IEEE 原页未能直接访问核对）

**【未确认】**

- release `speech-enhancement-models` 页面上是否存在页面级许可字样（GitHub API 限流，资产清单未拉取；对 O1 结论无实质影响但不为零）
- IEEE 论文正文中的逐字指标表（本文性能数字取自官方 README 表格，与论文排印版一致性未核对）
- 论文对三个缓存设计的原文表述（本文化用的是源码事实；论文 PDF 未获取）

---

## 参考链接清单

- 上游官方仓库：<https://github.com/Xiaobin-Rong/gtcrn>
- 上游 LICENSE（MIT 原文）：<https://raw.githubusercontent.com/Xiaobin-Rong/gtcrn/main/LICENSE>
- 论文 IEEE 页：<https://ieeexplore.ieee.org/document/10448310>（DOI: 10.1109/ICASSP48485.2024.10448310）
- sherpa-onnx 仓库：<https://github.com/k2-fsa/sherpa-onnx>（本次核读 master 快照，SHERPA_ONNX_VERSION 1.13.8）
- sherpa-onnx 语音增强模型文档：<https://k2-fsa.github.io/sherpa/onnx/speech-enhancement/models.html>
- 模型分发 release：<https://github.com/k2-fsa/sherpa-onnx/releases/tag/speech-enhancement-models>（gtcrn_simple.onnx 直链：`…/download/speech-enhancement-models/gtcrn_simple.onnx`）
- GTCRN 支持引入 PR：<https://github.com/k2-fsa/sherpa-onnx/pull/1977>
- PyPI 包索引：<https://pypi.org/pypi/sherpa-onnx/json>
- WASM 在线演示：<https://huggingface.co/spaces/k2-fsa/wasm-speech-enhancement-gtcrn>
- 作者流式转换模板仓库 TRT-SE：<https://github.com/Xiaobin-Rong/TRT-SE>
- Semantic Scholar 论文条目：<https://api.semanticscholar.org/graph/v1/paper/DOI:10.1109/ICASSP48485.2024.10448310>
