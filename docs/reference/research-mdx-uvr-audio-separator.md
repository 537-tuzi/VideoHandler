# MDX-Net / UVR 生态与 audio-separator 调研报告

## 元信息

- 调研日期：2026-10-13 ～ 2026-10-14
- 调研目的：为 VideoHandler 自研 MDX-Net 人声分离引擎的 parity 测试（判据 −60dB RMS，NFR-Q1）提供官方参考实现的源码级依据；并盘点 UVR 生态模型与许可证，评估 C++/ONNX Runtime 路线。
- 信源范围：仅一手信源 —— audio-separator（nomadkaraoke/python-audio-separator）源码、UVR 官方资源仓库（TRvlvr/application_data、TRvlvr/model_repo）、UVR GUI 仓库（Anjok07/ultimatevocalremovergui）、Politrees/UVR_resources 模型卡片、ZFTurbo/Music-Source-Separation-Training、facebookresearch/demucs，以及对 `UVR-MDX-NET-Voc_FT.onnx` 模型文件本体的 ONNX 图检查（onnx 1.23.2 实测）。
- 版本基线：audio-separator main 分支（2026-10 抓取），与 v0.47.0 tag（2026-08-27 发布）的 Q1 关键文件（mdx_separator.py / stft.py / common_separator.py）已做本地 diff 确认逐字节一致；行号引用按 main。
- 网络环境说明：本环境 github.com 直连不通，源码经 raw.githubusercontent.com（多次重试）、GitHub REST API、hf-mirror.com（HuggingFace 镜像）取得，内容为同一仓库的同一对象。个别文件（TRvlvr/model_repo README、旧版 tag、lucidrains 仓库 LICENSE）取不到，相关结论已标【未确认】。
- 本地验证产物：`/tmp/models/UVR-MDX-NET-Voc_FT.onnx`（hf-mirror.com/Politrees/UVR_resources 下载，66,762,490 字节，全文件 MD5 `d21dc03e4b9ef397b47231f483af6db8`）；audio-separator 源码与数据文件快照在 `/tmp/audsep/`。

> **2026-10-14 反证补强（`falsify-mdx.md`，穷尽 7 个实现的对照复核）**：
> ① plane-major 结论【站得住】：kuielab 原始仓库、UVR5 GUI（master/5.4）、audio-separator、独立 C++ 复刻 el-tahir/mdxnet_cpp、MSST mdx23c 等全部把输出原样喂 istft，未找到任何 frame-major 实现。
> ② **查表独立性打折**：UVR5 GUI 与 audio-separator 用**同一张 TRvlvr 表 + 同一末 10MiB 哈希算法**，两处命中不构成独立交叉；独立佐证 = UVR 5.4 硬编码 mdx_new 家族 7680/3072 + kuielab 原始 configs 证明 n_fft 是逐模型训练参数（vocals 6144/drums 4096/bass 16384/other 8192）。
> ③ **dim_t 并非全系 256**：TRvlvr 表存在 dim_t_set=7→128 与 9→512 的条目；n_fft∈{4096..16384}、dim_f∈{2048,2560,3072}、compensate∈[1.019,1.75] 全逐模型——引擎断言与缓冲必须用查表值；audio-separator 的 ORT 直连条件 `segment_size==dim_t` 对 512 帧模型失效（我们不设该条件则无此问题）。hop=1024 对 MDX-Net 全系恒定（kHop 硬编码决策被证实；bs_roformer hop=441 属另一架构）。
> ④ **「前 3 频点置零」是 5.5 世代行为**：UVR 5.4 与 kuielab 原始均无此处理；parity 以 audio-separator（有置零）为准。GUI 的 `*self.adjust` 恒为 1（遗迹代码）。
> ⑤ **chunk 边界两家默认不同**：audio-separator 恒 overlap 0.25 → step=195840；UVR5 GUI 默认 'Default' → `step=chunk−n_fft`=253440。**parity 参考必须钉死 audio-separator 默认参数集**（overlap 0.25 / segment 256 / batch 1 / denoise off）。
> ⑥ 「唯 MDX-Net 有官方 ONNX」需修正：**ZFTurbo/MSS_ONNX_TensorRT**（MSST 作者官方维护，MIT）提供 htdemucs/bs_roformer/mel_band_roformer/mdx23c/segm 的 ONNX 导出脚本（opset 17、STFT 外置）；但官方不发布现成 ONNX 权重，HF 上全是社区账号，Meta demucs 零 ONNX。FR-15 注记应改为「官方导出工具存在、官方不发布成品、社区成品需核验或自导」。

---

## 主要发现

### 1.【最高优先】MDX-Net ONNX 在 audio-separator 中的调用细节（源码级证据）

仓库：`nomadkaraoke/python-audio-separator`（MIT）。以下文件路径均相对仓库根：
- `audio_separator/separator/architectures/mdx_separator.py`
- `audio_separator/separator/uvr_lib_v5/stft.py`
- `audio_separator/separator/separator.py`
- `audio_separator/separator/common_separator.py`
- `audio_separator/separator/uvr_lib_v5/spec_utils.py`

引用 URL 形如 `https://github.com/nomadkaraoke/python-audio-separator/blob/main/<路径>#L<行号>`。

#### 1.1 输入张量：shape 与构造（STFT → 4 平面）

`stft.py` 的 `STFT.__call__`（L22–L58）：

- L20：`self.hann_window = torch.hann_window(window_length=self.n_fft, periodic=True)` —— **周期 Hann 窗**（periodic=True，非对称窗）。
- L40：`reshaped_tensor = input_tensor.reshape([-1, time_dim])` —— 把 `[batch, 2, samples]` 摊平成 `[batch*2, samples]`，每声道独立做 STFT。
- L43：`stft_output = torch.stft(reshaped_tensor, n_fft, hop_length, window, center=True, return_complex=False)` —— **center=True**，torch 内部对信号两端做 `n_fft//2` 的反射填充（reflect padding，非零填充）；输出实部/虚部为单独维度。
- L46：`permuted_stft_output = stft_output.permute([0, 3, 1, 2])` —— 变为 `[B*, 2(re/im), F, T]`。
- L49–L51：`reshape([*batch, ch, 2, -1, T])` 再 `reshape([*batch, ch*2, -1, T])` —— 合并出 **4 个平面，顺序为 `[ch0_re, ch0_im, ch1_re, ch1_im]`**（每声道的 re/im 紧邻成对）。
- L58：`return final_output[..., : self.dim_f, :]` —— **频率维只保留前 `dim_f` 个 bin**（对 n_fft=7680：n_bins=3841，保留 0..3071，即 0–17.94kHz；对 n_fft=6144：n_bins=3073，仅砍掉 Nyquist）。

因此送入 ONNX 的张量 shape 为 `[batch, 4, dim_f, dim_t]`，**平面优先（plane-major）**：4 是通道维，3072 是频率维，256 是时间帧维。内存布局按行优先展开即 plane0 的 3072×256 连续块，接着 plane1、plane2、plane3。

对 Voc_FT（dim_t=256、segment_size 默认 256）：demix 每次送 1 个 chunk，实际 batch=1，即 `[1, 4, 3072, 256]`。

#### 1.2 ONNX 调用与输出张量的真实 shape/布局

- `mdx_separator.py` L115：`if self.segment_size == self.dim_t:` —— **只有 segment_size == dim_t 时才走 ONNX Runtime 直连**；否则用 onnx2torch 把 ONNX 转成 PyTorch 模型（L138–L146，并打日志警告"processing may be slower"）。Voc_FT：dim_t=2^8=256，默认 segment_size=256，满足条件，走 ORT。
- L136：`self.model_run = lambda spek: ort_inference_session.run(None, {"input": spek.cpu().numpy()})[0]` —— **输入名 `"input"`，取第一个输出**。
- L434–L438（`run_model`）：
  ```python
  spek = self.stft(mix.to(self.torch_device))   # L434
  spek[:, :, :3, :] *= 0                        # L438 —— 前 3 个频点（0、102、205Hz @ n_fft=7680）置零
  ```
  注意 `[:, :, :3, :]` 是对 `[B, 4, dim_f, dim_t]` 的频率维前 3 个 bin 置零（4 个平面全部置零）。
- L448–L456：`spec_pred = self.model_run(spek)`（若 enable_denoise=True 则对 ±spek 各跑一次取 `neg*-0.5 + pos*0.5`；默认 False）。
- L460：`result = self.stft.inverse(torch.tensor(spec_pred).to(self.torch_device))` —— **模型输出被原样送进 `stft.inverse`，即输出被当作与输入完全相同的 `[batch, 4, dim_f, dim_t]` 布局**。

**ONNX 图实测（本调研用 onnx 1.23.2 直接检查 Politrees 副本的模型文件）**：

| 项 | 值 |
|---|---|
| 输入 | 名 `"input"`，float32，shape `['batch_size'(动态), 4, 3072, 256]` |
| 输出 | 名 `"output"`，float32，shape `['batch_size', 4, 3072, 256]` |
| dim_t | **静态 256**（不接受任意帧数，喂不足 256 帧必须填充） |
| metadata_props | **空**（模型内不携带 sample_rate/n_fft/hop 等任何参数） |
| producer / opset | pytorch 1.9 / opset 13 |

结论：**输出是平面优先（plane-major）的 `[batch, 4, dimF, dimT]`，与输入同布局**。若按「每帧 dimF*4 连续」（frame-major，`[dimT][4][dimF]`）索引输出，是布局错误——除非先做显式转置，否则读到的是错位的频谱。

#### 1.3 iSTFT（逆变换）

`stft.py` 的 `STFT.inverse`（L101–L128）：

- L78 / L114 / L65–L68：`num_freq_bins = n_fft // 2 + 1`；`pad_frequency_dimension` 在**频率维末尾补零**，把 dim_f 补回 n_bins（即被砍掉的高频段/Nyquist 在逆变换时恒为零）。
- L88–L97：reshape 成 `[ch//2, 2, F, T]` → permute → `complex_tensor = re + im * 1.0j`。
- L119：`istft_result = torch.istft(complex_tensor, n_fft, hop_length, window, center=True)` —— **用同一个 periodic Hann 窗、center=True**。窗平方（WOLA）归一化由 `torch.istft` 内部完成（除以重叠窗平方和，边界按中心对齐裁掉 n_fft//2）。
- L122：`reshape([*batch, 2, -1])` 恢复 `[batch, 2, samples]`。

即官方参考的 iSTFT 归一化 = Σ(窗²) 加权归一（WOLA），与 VideoHandler 自实现的「WOLA + 窗²归一化」思路一致；差别在于 torch.istft 的具体边界处理（enframe/中心对齐）需在 parity 测试中对照。

#### 1.4 分块（chunking）与重叠相加

`mdx_separator.py` `demix`（L306–L425）与 `initialize_model_settings`（L218–L241）：

- L227–L236（对 Voc_FT 代入 n_fft=7680、hop=1024、segment_size=256）：
  - `n_bins = n_fft//2 + 1 = 3841`
  - `trim = n_fft//2 = 3840`
  - `chunk_size = hop_length * (segment_size - 1) = 1024*255 = 261120` 样本（≈5.92s）
  - `gen_size = chunk_size - 2*trim = 253440`
- L342–L344：`pad = gen_size + trim - (len % gen_size)`；`mixture = zeros(2,trim) + mix + zeros(2,pad)` —— 混合信号首尾加 `trim` 零样本（注意：这里是**零填充**；反射填充只发生在 torch.stft 的 center=True 内部，两者是不同层次）。
- L348：`step = int((1 - overlap) * chunk_size)`，overlap 默认 0.25 → step = 195840。
- L368–L372：`window = np.hanning(chunk_size_actual)` —— **NumPy 对称 hanning（symmetric）**，`np.tile(window[None,None,:], (1,2,1))` 广播到双声道。
- L399–L405：`tar_waves[..., :chunk_size_actual] *= window`；`divider[..., start:end] += window`；`result[..., start:end] += tar_waves[...]`。
- L409：`tar_waves = result / divider` —— 除以重叠窗和。
- L413：`[:, :, self.trim : -self.trim]` 裁掉两端 trim 后返回。

#### 1.5 主/副 stem 与 compensate

- `separator.py` L847–L896：**模型参数不是从 ONNX 读**，而是按"文件末尾 10MiB 的 MD5"（L526：`BYTES_TO_HASH = 10000 * 1024`；L537–L541 seek 到 `file_size - BYTES_TO_HASH`）查 UVR 官方表 `TRvlvr/application_data/mdx_model_data/model_data_new.json`，查不到直接 ValueError（L892）。
- 用该算法对本地 Voc_FT 算哈希 = `77d07b2667ddf05b9e3175941b4454a0`，**命中 UVR 官方表**，参数：
  ```json
  { "compensate": 1.021, "mdx_dim_f_set": 3072, "mdx_dim_t_set": 8, "mdx_n_fft_scale_set": 7680, "primary_stem": "Vocals" }
  ```
  即：**dim_f=3072、dim_t=2^8=256、n_fft=7680、主输出=Vocals（模型直接预测人声）、compensate=1.021**。
- `mdx_separator.py` L195：副 stem（Instrumental）= `(-primary_source * compensate) + mix.T` —— **波形域相减**；仅当 `invert_using_spec=True`（默认 False）才走 `spec_utils.invert_stem` 频谱反演（spec_utils.py L583–L588）。

#### 1.6 数值格式与批内处理

- 输入按 float32（L301 `torch.tensor(mix_waves, dtype=torch.float32)`）。
- `batch_size` 参数只在单 chunk 内再切分（L384 `mix_part.split(self.batch_size)`）；一个 chunk 本身就是 1 个元素，故默认 batch=1。

### 2. 完整预处理 / 后处理链

按 `common_separator.py` + `mdx_separator.py` + `spec_utils.py` 的执行顺序：

1. **加载与重采样**：`common_separator.py` L284 `librosa.load(mix, mono=False, sr=self.sample_rate)`，`sample_rate` 默认 44100（`separator.py` L134）→ 统一重采样到 44.1kHz，保留多声道。
2. **多声道 → 立体声**：L309–L311，单声道复制为 `[x, x]`；超过 2 声道无特殊处理（librosa 已按 (ch, n) 返回，MDX 要求恰 2 声道，`initialize_mix` L254 会校验）。
3. **预归一化**：`mdx_separator.py` L167–L169：`peak = np.abs(mix).max()`；`normalize(wave, max_peak=0.9, min_peak=0.0)`（`spec_utils.py` L100–L123：峰值 >0.9 则整体降到 0.9；0<峰值<min_peak 才放大，默认 min_peak=0.0 即不放大）。
4. **demix**（见 1.4）→ 得到 primary stem；L172 `source = self.demix(mix) * peak` **乘回原峰值**。
5. **副 stem**：`(-primary * compensate) + mix`（L195，波形域；compensate=1.021）。
6. **写盘再归一化**：`common_separator.py` L344（pydub 路径）或 L431（soundfile 路径）写盘前再次 `normalize(max_peak=0.9)` —— **输出不 clip，而是把峰值压到 0.9**；之后 pydub 路径 `(stem_source * 32767).astype(np.int16)`（L364）经 ffmpeg 导出（默认路径，`use_soundfile=False`），目标位深跟随输入（L357–L359）；soundfile 路径 `sf.write(..., subtype=输入subtype)`（L480）。默认输出格式 WAV。
7. **segment_size**：`separator.py` L139 `mdx_params={"hop_length": 1024, "segment_size": 256, "overlap": 0.25, "batch_size": 1, "enable_denoise": False}`。作用：决定每 chunk 时长（hop×(segment_size−1)≈5.9s）与显存/速度；**必须等于 dim_t 才走 ORT 直连**，否则转 onnx2torch。
8. **margin**：**当前版本已移除该参数**。`mdx_separator.py` L88–L89 注释原文：`"margin" maps to sample rate and is set from the GUI in UVR (default: 44100). We have a "sample_rate" parameter instead.`（UVR GUI 中 margin 默认 44100 样本 ≈ 1s，用于 chunk 边界上下文；audio-separator 用固定 trim=n_fft//2 零填充替代，不再暴露 margin）。
9. **denoise 选项**：默认关闭；开启时对 ±谱各推理一次取 `neg*-0.5 + pos*0.5`（L448–L453）。

### 3. UVR-MDX-NET-Voc_FT.onnx 的来源与许可证

- **官方分发源**：audio-separator 代码里的公共模型下载前缀是 `https://github.com/TRvlvr/model_repo/releases/download/all_public_uvr_models`（`separator.py` L765），`UVR-MDX-NET-Voc_FT.onnx` 在官方下载清单 `TRvlvr/application_data/filelists/download_checks.json` 的 `mdx_download_list` 中（friendly name "MDX-Net Model: UVR-MDX-NET Voc FT"，非 VIP 模型）。
- **Politrees/UVR_resources 副本核验**：本调研从 hf-mirror 下载的 Politrees 副本，按 audio-separator 的哈希算法（末尾 10MiB MD5）算得 `77d07b26...`，**在 UVR 官方参数表中命中 Voc_FT 条目**（见 1.5）——即该副本与官方资产在决定推理行为的部分一致，可作为 parity 基准的模型文件。（未能下载 TRvlvr 官方原件做全字节比对，github.com release 资产下载被网络阻断；全文件 MD5 `d21dc03e4b9ef397b47231f483af6db8` 供本地核对。另注：早期把"全文件 MD5 不在表内"误判为副本不一致，根因是官方哈希只取末 10MiB，已纠正。**2026-10-14 反证闭环**：github 恢复可达后已实测 5 个 TRvlvr 官方原件（Voc_FT/Inst_HQ_3/Inst_HQ_4/Inst_Main/9703）与 Politrees 副本全文件 MD5 逐字节一致，副本等价性升级为【已确认】，见 `falsify-mdx.md`。）
- **许可证 / 再分发政策**：
  - UVR GUI 仓库（Anjok07/ultimatevocalremovergui）README 明确：GUI 代码 MIT 许可；"UVR's core developers trained all of the models provided in this package (except for the Demucs v3 and v4 4-stem models)"；对第三方开发者："please honor the MIT license by providing credit to UVR and its developers"（即官方口径：模型按 MIT 提供、要求署名）。
  - Politrees/UVR_resources 的 HF 模型卡 cardData `license: mit`。
  - **VIP 模型除外**：`Anjok0109/ai_magic` v5 release 的 VIP 模型是 Patreon 付费订阅专属（`separator.py` L766、L753–L755 有明确提醒），不可再分发。Voc_FT 属公共模型，不在其列。
  - TRvlvr/model_repo 自身的 README/条款原文未能取到（网络），若要严格合规建议发版前补核（对应开放问题 O1）。

### 4. UVR 生态流行模型盘点（质量 / 资源 / 许可证 / ONNX 可用性）

数据来源：audio-separator 仓库内 `audio_separator/models-scores.json`（其官方基准的中位数 SDR，40 曲测试集）与 README 模型表、`download_checks.json`（文件格式）、Politrees/UVR_resources 文件清单与 YAML 配置、各上游仓库 LICENSE 文件。

| 模型 | 架构/格式 | 人声 SDR（中位数） | 备注 |
|---|---|---|---|
| UVR-MDX-NET-Voc_FT | MDX / **.onnx** | 10.15（inst 15.43） | 67MB；ORT 原生；CPU 可行 |
| model_bs_roformer_ep_317（Viperx 1297） | MDXC / .ckpt+.yaml（PyTorch） | 11.77（README 表记 12.9，来自文件名 SDR；inst 16.45/17.0） | ckpt 639MB；STFT n_fft 2048/hop 441/dim_f 1024，模型 dim 512×depth 12、dim_t 801、num_overlap 4 |
| model_bs_roformer_ep_368（Viperx 1296） | 同上 | 12.10（inst 16.31） | 同上 |
| vocals_mel_band_roformer（Kimberley Jensen） | MDXC / .ckpt+.yaml | 12.60 | MelBand Roformer |
| mel_band_roformer_kim_ft_unwa | MDXC / .ckpt+.yaml | 12.44 | unwa 微调 |
| melband_roformer_big_beta4（unwa） | MDXC / .ckpt+.yaml | 12.52 | |
| MDX23C-8KFFT-InstVoc_HQ | MDXC / .ckpt+.yaml | 10.56（inst 15.83） | ckpt 448MB；8K FFT 全频带；ZFTurbo 训练 |
| htdemucs_ft（Demucs v4） | Demucs / .th+.yaml（PyTorch） | 10.79（drums 10.02/bass 12.02，4 stem） | Meta 官方 |

各维度结论：

- **质量口碑**：社区共识 BS-Roformer (1297/1296) 与 Mel-Band Roformer 是当前人声分离第一梯队（SDR 12+），MDX23C、htdemucs_ft、MDX-Net 属第二梯队（10–11）。audio-separator 自身基准（models-scores.json 中位数）也支持该排序。
- **ONNX 可用性**：
  - **MDX-Net 是唯一有官方 ONNX 的一等公民**（TRvlvr 公共模型库全部 .onnx），可在 C++ ONNX Runtime 直接跑——VideoHandler 现路线成立。
  - BS-Roformer / Mel-Roformer / MDX23C 在 audio-separator 中全部走 **PyTorch .ckpt + YAML 配置**（`separator.py` L977 的架构分发表 + `download_checks.json` 的 `roformer_download_list`/`mdx23c_download_list` 均为 .ckpt/.yaml 条目），**没有官方 ONNX 导出**。
  - 社区 ONNX 导出存在：如 [musetric/vocal-separation-roformer-onnx](https://huggingface.co/musetric/vocal-separation-roformer-onnx)（模型卡 license: mit，面向 onnxruntime-web/WebGPU 的 Mel-Band Roformer "core" 导出，文件 `syhft_core_t1100.onnx` + 外置 `.onnx.data`），以及 GGUF 量化路线（chenmozhijin/BSRoformer-GGUF、BSRoformer.cpp 项目，服务 CPU/低显存场景）。这些属第三方转换，权重与 UVR 生态发行版并非同一批，接 C++ ORT 前需自行核验质量与来源。
- **资源需求**：Roformer 系权重 600MB+、attention 结构重；audio-separator README（L193）明确 MDXC/Roformer"Runs correctly on CPU automatically"但 DirectML 因显存分配器问题不可用——侧面说明其显存压力大；社区普遍口径 BS-Roformer 显存 6–10GB+（随 segment 增大）、CPU 推理极慢。**权威 GB 数字未找到，标【未确认】**。对比之下 MDX-Net（67MB、CNN 结构）CPU 完全可行，Demucs CPU 可行但慢。
- **许可证**（均已取 LICENSE 原文或模型卡）：
  - audio-separator：MIT（pyproject.toml L58）。
  - ZFTurbo/Music-Source-Separation-Training（Roformer/MDX23C 训练框架）：MIT，Copyright (c) 2024 Roman Solovyev (ZFTurbo)。
  - facebookresearch/demucs：MIT，Copyright (c) Meta Platforms, Inc. and affiliates。
  - UVR GUI 代码：MIT；UVR 模型官方口径"按 MIT 提供、请署名"（见第 3 节）。
  - lucidrains/BS-RoFormer（BS-Roformer 参考实现）LICENSE 未能取到（网络），标【未确认】。
  - **LGPL/MIT 兼容性结论**：以上组合（MIT 模型 + MIT/LGPL 代码 + ORT MIT）与 VideoHandler 的 D1 免费公开发布路线兼容；Roformer 系若进 C++，要么自行把 .ckpt 导出 ONNX（保持 MIT 链路），要么采用社区导出（需逐个核验），不存在许可证硬阻碍，主要风险是导出质量与工程量。

### 5. audio-separator 包信息

来源：仓库根 `pyproject.toml`（v0.47.0）。

- 当前版本：**v0.47.0**（2026-08-27 发布；pyproject L55 `version = "0.47.0"`）。
- pip 包名：**`audio-separator`**（L8）。仓库已从 beveradb 迁到 **nomadkaraoke/python-audio-separator**。
- Python 要求：**`>=3.10,!=3.14.1`**（L9）。
- **torch 是硬依赖**（主依赖表 L32–L33、L87–L90：Linux/mac-arm 要求 torch>=2.13，其余 >=2.3）——即使只用 MDX ONNX 路线也必须装 torch。
- **onnxruntime 仅是可选 extra**：`cpu = ["onnxruntime>=1.17"]`、`gpu = ["onnxruntime-gpu>=1.17"]`、`dml = [...]`（L49–L51）。
- 其他核心依赖：librosa、pydub、soundfile、numpy>=2、onnx2torch-py313>=1.6、einops、julius、ml_collections、rotary-embedding-torch、resampy、samplerate==0.1.0、scipy、requests、tqdm 等；**系统依赖 FFmpeg 必装**（pydub 导出用，`check_ffmpeg_installed`）。

---

## 与我们实现的对照（差异点逐条）

以下按 VideoHandler 引擎现状描述（44.1k 立体声、n_fft=6144、hop=1024、周期 Hann、反射填充、WOLA+窗²归一化、输入 [1,4,dimF,dimT]、输出按"每帧 dimF*4 连续"读取）与 audio-separator 参考逐条对照。标注 ★ 的为预计影响 parity 判据（−60dB RMS）的关键项。

1. ★ **n_fft 不匹配（最大差异）**：Voc_FT 官方参数是 **n_fft=7680**（哈希查表命中，见 1.5），不是我们用的 6144。6144 时 n_bins=3073、dim_f=3072 只丢 Nyquist（覆盖到 ~22.03kHz）；7680 时 n_bins=3841、dim_f=3072 会把 3072–3840 号 bin（约 17.94–22.05kHz）整段丢弃。两者频率分辨率（7.18Hz vs 5.86Hz/bin）和输入谱完全不同，**parity 不可能通过，必须按模型参数改为 7680**。注意 UVR 生态确有 n_fft=6144+dim_f=3072 的模型（多为 Instrumental 主 stem），若引擎按那类模型设计则前端思路对、但对 Voc_FT 用错了参数——**参数必须逐模型查表，不能写死**。
2. ★ **输出读取布局**：ONNX 实测输出 shape `[batch,4,3072,256]`，**平面优先（plane-major）**——连续内存是 4 个平面（ch0_re/ch0_im/ch1_re/ch1_im）各 3072×256 连续块。我们按"每帧 dimF*4 连续"（frame-major）读取是**布局错误**，需改为按平面索引（或显式转置）。这正是任务指出的"最大疑点"，现已用 ONNX 图 + 源码双重证实。
3. ★ **输入同样 plane-major** 且 **dim_t 是静态 256**：不足 256 帧的 chunk 必须补齐（官方是按 chunk_size=261120 定长切分天然凑满 256 帧），不能送任意帧数。
4. **前 3 个频点置零**：官方在推理前 `spek[:, :, :3, :] *= 0`（mdx_separator.py L438）。我们的引擎是否做了同样处理需对照——这是幅度可能不小的差异源。
5. **STFT 窗与填充**：周期 Hann + center=True 反射填充 —— 与我们声明一致，✔。但注意官方 chunk 边界是先加 `trim=n_fft//2` 的**零填充**再进 STFT（两层填充并存），见 1.4。
6. **iSTFT 归一化**：官方是频率维末尾补零回 n_bins 后 `torch.istft`（内部窗² WOLA、center 裁剪）。我们的自研 WOLA+窗²归一化思路一致，边界行为（首尾 n_fft//2 的处理、ENF 端点）需在 parity 中专门对齐。
7. **分块与 OLA**：官方 chunk=hop×(segment_size−1)=261120、两端 trim 零填充、对称 np.hanning 交叉窗、step=0.75×chunk、result/divider 归一。我们的分段策略若不同（chunk 长度/窗型/步进任一不同），重叠区会有差异。
8. **归一化链**：官方链路 = 峰值 0.9 预归一化 → 推理 → 乘回 peak → 写盘前再归一化 0.9（不 clip）。我们输出 44.1k float WAV、无 0.9 缩放——**parity 对比时需在 Python 参考侧跳过写盘归一化、或在我方侧补同样缩放**，否则幅度对不上。副 stem 还有 compensate=1.021 的波形相减系数。
9. **主 stem 语义**：Voc_FT 的模型直接输出**人声**（primary_stem=Vocals）；Instrumental 是 `mix − 1.021×vocals` 反推出来的。我们的 `--sep-vocals`/`--sep-instrumental` 语义需与之对齐（哪个是模型直出、哪个是减法）。
10. **参数获取方式**：该 ONNX **无 metadata_props**，官方靠"末尾 10MiB MD5 查 UVR 哈希表"取参数（hop=1024/segment=256/overlap=0.25 是包级默认，dim_f/dim_t/n_fft/compensate/primary_stem 来自查表）。我们"从 ONNX metadata 读"的策略对这类模型不成立，需要等价的查表机制（可内置常用模型表 + 允许用户覆盖）。
11. **重采样与声道**：官方 librosa 重采样到 44.1k、mono 复制立体声——与我们的 44.1k/2ch 规格一致 ✔（但注意 P0 债 4：视频源 48k 会两次重采样的问题依然存在于参考实现）。

---

## 结论分级

### 【已确认】（源码级或文件级证据）

1. MDX-Net 输入张量 `[batch, 4, dim_f, dim_t]` 的构造全链路：周期 Hann、center=True 反射填充、`[ch0_re, ch0_im, ch1_re, ch1_im]` 平面顺序、频率维取前 dim_f 个 bin（stft.py L20/L40/L43/L46/L49–51/L58）。
2. ORT 调用：输入名 `"input"`、取第一个输出；仅 segment_size==dim_t 时走 ORT，否则 onnx2torch（mdx_separator.py L115/L136/L138–146）。
3. **输出张量 shape `[batch, 4, 3072, 256]`、与输入同布局（plane-major）、dim_t 静态、模型无 metadata_props**（onnx 1.23.2 对模型文件本体的检查 + mdx_separator.py L460 把输出原样送 stft.inverse 的代码证据）。
4. iSTFT：频率维末尾补零回 n_bins → 复数化 → torch.istft(同窗, center=True) 窗² WOLA（stft.py L60–70/L88–97/L119/L122）。
5. 分块 OLA 参数（chunk/trim/step/对称 np.hanning/result÷divider/两端裁 trim）与"前 3 频点置零"（mdx_separator.py L227–236/L348/L368–372/L399–409/L413/L438）。
6. 预处理链：44.1k 重采样、mono→立体声、峰值 0.9 预归一化+乘回、副 stem=波形相减（compensate）、写盘前再归一化 0.9 不 clip、位深跟随输入、margin 参数已移除（common_separator.py L284/L309–311/L344/L364；mdx_separator.py L88–89/L167–172/L195）。
7. **Voc_FT 官方参数：n_fft=7680、dim_f=3072、dim_t=256、primary_stem=Vocals、compensate=1.021**（末 10MiB MD5 `77d07b26...` 命中 UVR 官方 model_data_new.json；哈希算法见 separator.py L520–545）。
8. 模型参数来源是哈希查表而非 ONNX metadata（separator.py L847–896）。
9. audio-separator v0.47.0、pip 名 `audio-separator`、Python `>=3.10,!=3.14.1`、torch 硬依赖、ORT 可选 extra、FFmpeg 系统必装（pyproject.toml）。
10. 许可证：audio-separator MIT；ZFTurbo MSST MIT；Demucs MIT（Meta）；UVR GUI 代码 MIT、模型官方口径按 MIT 提供并要求署名；VIP 模型付费专属不可再分发。
11. Q4 生态事实：Roformer/MDX23C/Demucs 在 audio-separator 中均为 PyTorch .ckpt/.th + YAML，无官方 ONNX；SDR 数据见第 4 节表。

### 【倾向】（有间接证据，未做到逐字节/逐条官方确认）

1. ~~Politrees/UVR_resources 的 Voc_FT 副本与 TRvlvr 官方原件等价（未做全字节比对）~~ **已闭合（2026-10-14 反证，`falsify-mdx.md`）**：5 个官方原件与副本全文件 MD5 逐字节一致。
2. UVR 公共模型整体可按"MIT + 署名"再分发（UVR GUI README 语句 + Politrees 模型卡 license:mit 两处佐证；TRvlvr/model_repo 自身条款原文未取到）。
3. BS-Roformer 显存占用大（权重 639MB + attention，社区口径 6–10GB+）、CPU 推理"正确但极慢"（README 确认 CPU 可跑 + 社区口碑；无权威 GB 数字）。
4. musetric 等 Mel-Band Roformer 社区 ONNX 导出可在 ORT 系运行时推理（模型卡面向 onnxruntime-web/WebGPU；未实测，且权重非 UVR 生态发行的那批 checkpoint）。

### 【未确认】

1. TRvlvr/model_repo 的 README / 再分发政策原文（网络取不到；发版前建议补核，对应开放问题 O1）。
2. 官方 Voc_FT 与 Politrees 副本的全文件字节一致性（同上网络原因）。
3. BS-Roformer / Mel-Roformer / MDX23C 官方 ONNX 导出是否存在（源码证据表明 audio-separator 只走 PyTorch，未发现官方 ONNX；"官方没有"这一否定性结论无法穷尽证明）。
4. BS-Roformer 系列的权威显存/速度基准数字。
5. lucidrains/BS-RoFormer 仓库的 LICENSE 原文（社区普遍认为是 MIT，未核验）。
6. audio-separator 旧版本（v0.16–0.30 时代）margin 参数的源码级历史（旧 tag 拉取失败；UVR GUI 侧 margin 默认 44100 由 mdx_separator.py L88–L89 注释佐证）。
7. README 模型表中 BS-Roformer "12.9" 与 models-scores.json 中位数 11.77 的口径差异（前者疑为 ZFTurbo 榜单/文件名 SDR，未在源码中找到生成口径说明）。

---

## 参考链接清单

一手源码与数据（行号引用基准，main 分支，2026-10 快照）：

- audio-separator 仓库：https://github.com/nomadkaraoke/python-audio-separator
  - MDX 核心：https://github.com/nomadkaraoke/python-audio-separator/blob/main/audio_separator/separator/architectures/mdx_separator.py
  - STFT：https://github.com/nomadkaraoke/python-audio-separator/blob/main/audio_separator/separator/uvr_lib_v5/stft.py
  - 调度与参数：https://github.com/nomadkaraoke/python-audio-separator/blob/main/audio_separator/separator/separator.py
  - 公共前后处理：https://github.com/nomadkaraoke/python-audio-separator/blob/main/audio_separator/separator/common_separator.py
  - 工具函数：https://github.com/nomadkaraoke/python-audio-separator/blob/main/audio_separator/separator/uvr_lib_v5/spec_utils.py
  - 包定义：https://github.com/nomadkaraoke/python-audio-separator/blob/main/pyproject.toml
  - 基准分数：https://github.com/nomadkaraoke/python-audio-separator/blob/main/audio_separator/models-scores.json
  - v0.47.0 发布：https://github.com/nomadkaraoke/python-audio-separator/releases/tag/v0.47.0

UVR 官方资源：

- UVR GUI（模型/许可证口径）：https://github.com/Anjok07/ultimatevocalremovergui
- 官方模型参数表：https://github.com/TRvlvr/application_data/blob/main/mdx_model_data/model_data_new.json
- 官方下载清单：https://github.com/TRvlvr/application_data/blob/main/filelists/download_checks.json
- 官方公共模型库（release 资产）：https://github.com/TRvlvr/model_repo/releases/tag/all_public_uvr_models

模型镜像与配置：

- Politrees/UVR_resources（HF，license: mit；本文用 hf-mirror.com 镜像下载）：https://huggingface.co/Politrees/UVR_resources
- BS-Roformer 1297 配置：https://huggingface.co/Politrees/UVR_resources/blob/main/models/Roformer/BandSplit/config_bs_roformer_ep_317_sdr_12.9755.yaml
- ZFTurbo 训练框架（MIT）：https://github.com/ZFTurbo/Music-Source-Separation-Training
- Demucs（MIT）：https://github.com/facebookresearch/demucs
- BS-RoFormer 参考实现：https://github.com/lucidrains/BS-RoFormer
- Mel-Band Roformer 论文：https://arxiv.org/abs/2310.01809

社区 ONNX/GGUF 导出（非官方，采用前需核验）：

- musetric/vocal-separation-roformer-onnx（MIT，onnxruntime-web/WebGPU）：https://huggingface.co/musetric/vocal-separation-roformer-onnx
- silverdaw/mel-band-roformer-vocals-onnx（musetric 转存）：https://huggingface.co/silverdaw/mel-band-roformer-vocals-onnx
- chenmozhijin/BSRoformer-GGUF（GGUF 量化/CPU 路线）：https://huggingface.co/chenmozhijin/BSRoformer-GGUF
