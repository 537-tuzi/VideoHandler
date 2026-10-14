# 反证审查：MDX/UVR 技术结论

- 审查类型：**反证审查**（假设现有结论为错，专门寻找能证明其错误的证据；找不到反证只能写「暂未找到反证」）
- 日期：2026-10-14
- 针对文档：`docs/reference/research-mdx-uvr-audio-separator.md`（主对象）；受波及文档：`docs/spec/separator-output-validation.md`、`docs/REQUIREMENTS.md` FR-15
- 信源（均为一手，本地克隆/下载后核读）：
  - `ZFTurbo/Music-Source-Separation-Training`（main，浅克隆 2026-10-14，本地 `/tmp/falsify/Music-Source-Separation-Training`）
  - `ZFTurbo/MSS_ONNX_TensorRT`（main，本地 `/tmp/falsify/MSS_ONNX_TensorRT`）——本次审查新发现的关键仓库
  - `Anjok07/ultimatevocalremovergui`（master = v5.6.0 克隆 + v5.4.0 / v5.5.0 tag tarball，本地 `/tmp/falsify/uvr5`、`/tmp/falsify/uvr540t`、`/tmp/falsify/uvr550t`）
  - `kuielab/mdx-net`（MDX-Net 架构与训练的原始官方仓库，本地 `/tmp/falsify/mdx-net`）及 `kuielab/mdx-net-submission`
  - `facebookresearch/demucs`（本地 `/tmp/falsify/demucs`）
  - `TRvlvr/application_data`（官方参数表，codeload 当日拉取，与既有快照逐字节一致）
  - `TRvlvr/model_repo` release 资产（**github.com 直连已恢复，本次成功下载官方原件**，见 §6）
  - 独立第三方复刻：`seanghay/uvr-mdx-infer`、`el-tahir/mdxnet_cpp`（含一个纯 C++ 实现）
  - HF（hf-mirror）搜索 bs_roformer / mdx23c / demucs 的 onnx 发布物
- 本地实测：onnx 1.23.2 检查 5 个模型（含 4 个官方原件 + Politrees 副本比对）；hashlib 重算末 10MiB / 全文件 MD5。
- 网络环境变化：与原调研不同，本次 github.com 与 release 资产下载可达（raw.githubusercontent.com 仍不通，用 codeload/API 替代）。

---

## 1. 反证目标 1：输出为 plane-major `[b,4,3072,256]`

**原结论**：输出按平面优先（plane-major，元素 (p,f,t) 在 `(p·dimF+f)·dimT+t`）读取；frame-major 读法错误。

**反证过程**：逐一核查各官方/独立实现的「模型输出 → iSTFT」路径，找任何先做转置/按帧重排（frame-major）再逆变换的实现。

**核查结果（7 个实现，全部直读、零转置）**：

| # | 实现 | 证据（本地路径:行号） | 读法 |
|---|---|---|---|
| 1 | kuielab/mdx-net（架构与训练起源仓库） | `src/evaluation/separate.py:30-31`：`model.istft(torch.tensor(_ort.run(None, {'input': model.stft(mix_waves).numpy()})[0]))`；`src/models/mdxnet.py:45-47,88-93`（`istft(self(mix_spec))`，istft 内 cat freq_pad 后逐维还原） | 输出原样喂 istft |
| 2 | UVR5 GUI master(v5.6.0) | `separate.py:627-637`：`spec_pred = self.model_run(spek)` → L637 `self.stft.inverse(torch.tensor(spec_pred)...)` | 同上 |
| 3 | UVR5 GUI v5.4.0 | `inference_MDX.py:1065-1067`：`_ort.run(...)[0]` → `model.istft(torch.tensor(...))` | 同上 |
| 4 | audio-separator（原调研证据复核） | `mdx_separator.py:460`（快照行号一致） | 同上 |
| 5 | seanghay/uvr-mdx-infer（独立 CLI） | `separate.py:138-146` | 同上 |
| 6 | el-tahir/mdxnet_cpp（**独立 C++ 复刻**） | `src/utils.cpp`（stft_to_tensor/tensor_to_stft）：注释与实现均为 `index = (channel*dimF*dimT) + (freq*dimT) + time`，且填谱从 f=3 起步（等价于前 3 bin 置零） | **显式 plane-major 索引** |
| 7 | ZFTurbo MSST（MDX23C，同类谱域网络） | `models/mdx23c_tfc_tdf_v3.py:204-238`：forward 末尾 `x = self.stft.inverse(x)`；其 STFT 类（同文件 L6-43）的 reshape 链 `permute([0,3,1,2]) → reshape([b,c,2,F,T]) → reshape([b,c*2,F,T]) → [..., :dim_f, :]` 与 UVR/audio-separator 逐行同构 | 网络输出直喂 stft.inverse |

**找不到 frame-major 实读的实现**。布局由两层锁定：(a) ONNX 张量形状语义本身（声明 `[b,4,3072,256]` 即行主序 plane-major，不存在另一种内存解释）；(b) 全部参考实现把输出当与输入同布局送回 istft，而输入布局的构造（`[ch0_re, ch0_im, ch1_re, ch1_im]` 平面）在 7 处代码中一致。

**附带勘误**：任务提到的 `mdx_c.py` / `mdx_processing.py` 在 UVR5 GUI 任何 tag（v5.0.0–v5.6）与 kuielab 两个官方仓库中**都不存在**；UVR5 GUI 各版本 MDX ONNX 引擎的实际文件是：v5.0–v5.1 `inference_v5.py` → v5.2–v5.4 `inference_MDX.py` → v5.5+ `separate.py::SeperateMDX`（+ `lib_v5/mdxnet.py`、`lib_v5/tfc_tdf_v3.py::STFT`）。代码里的 "mdx_c"（`is_mdx_c`）指 **MDX23C ckpt 模型族**的标志位，不是文件名。`mdx_c.py/mdx_processing.py` 这两个名字应出自第三方 fork/Colab，非官方文件。

**判级：【站得住】**（暂未找到反证；反证方向上可查的官方与独立实现已穷尽——7/7 支持 plane-major）。

---

## 2. 反证目标 2：Voc_FT n_fft=7680（末 10MiB MD5 查 UVR 官方参数表）

**原结论**：n_fft=7680、dim_f=3072、dim_t=256、compensate=1.021、primary_stem=Vocals，参数来源为哈希查表。

**反证过程**：

1. **直接核对 UVR5 GUI 官方仓库自己的参数数据**。结果：现行 UVR5 GUI **不在仓库内置参数表**，运行时从 `TRvlvr/application_data` 下载（`gui_data/constants.py:91` `MDX_MODEL_DATA_LINK = .../model_data_new.json`；`UVR.py:5339` 加载）；哈希算法与 audio-separator 完全相同（`UVR.py:737-741`：`f.seek(-10000*1024, 2)` 取末 10MiB 的 MD5，异常时退化全文件）。**即 UVR5 GUI 与 audio-separator 用的是同一张表、同一算法，两者不构成独立信源交叉**——这一点原调研文档未明示，属证据独立性上的实质折扣。
2. **官方表当日重取**：codelock 拉 TRvlvr/application_data main，`mdx_model_data/model_data_new.json` 与本地既有快照逐字节一致（86 条）；`77d07b2667ddf05b9e3175941b4454a0 → {compensate: 1.021, mdx_dim_f_set: 3072, mdx_dim_t_set: 8, mdx_n_fft_scale_set: 7680, primary_stem: Vocals}`。
3. **本地 ground-truth 重算**：`/tmp/models/UVR-MDX-NET-Voc_FT.onnx` 全文件 MD5 `d21dc03e...`、末 10MiB MD5 `77d07b26...`，与原调研一致；且本次已下载 **TRvlvr 官方原件**比对（见 §6），官方原件与该副本**逐字节相同**。
4. **`dim_t = 2**mdx_dim_t_set` 的官方代码证据**：UVR5 GUI master `separate.py:210`：`self.dim_f, self.dim_t = model_data.mdx_dim_f_set, 2**model_data.mdx_dim_t_set` —— dim_t_set=8 → 256，确认。
5. **寻找独立于 TRvlvr 表的参数佐证**：
   - UVR 5.4.0（TRvlvr 表启用前）内置**硬编码家族桶**（`lib_v5/filelist.py::provide_mdx_model_param_name`）：`mdx_new` 家族 = n_fft **7680**、dim_f **3072**、`mdx_original` = 6144/2048 —— 家族级独立佐证 7680/3072 组合确实存在于 UVR 官方另一代实现；且其噪声档命名 `MDX-NET_Noise_Profile_17_kHz` 与 7680 自洽（3072 bin × 44100/7680 ≈ 17.6 kHz 截止）。**但 Voc_FT 的全文件 MD5 不在 5.4 的三份哈希清单里**（该版本早于 Voc_FT 发布），故 5.4 只是家族级、非该模型级佐证。
   - kuielab/mdx-net 原始 configs：`configs/model/ConvTDFNet_vocals.yaml` n_fft **6144**、drums **4096**、bass **16384**、other **8192**（全系 dim_t=256、hop=1024）——证明 **n_fft 是逐模型训练超参、ONNX 图内不可见**（图只暴露 dim_f×dim_t），「必须查表」的机制性结论成立。
6. **未找到任何把 Voc_FT 标为 6144（或其他值）的独立实现**；seanghay/uvr-mdx-infer 把 n_fft 做成 CLI 参数（默认 6144，由调用方逐模型传入），不构成反证。

**判级：【站得住】**（数值与机制均未被推翻；但「多家独立实现交叉」应降格表述为「同一官方数据源的两个消费方 + 5.4 家族桶 + 架构机制佐证」）。

---

## 3. 反证目标 3：`spek[:, :, :3, :] *= 0` 与伴奏 = mix − 1.021×vocals

**原结论**：官方推理前前 3 频点置零；副 stem = 波形域 `mix − compensate×primary`。

**反证过程与结果**：

1. **UVR5 GUI 官方实现确实存在两处理**（此前只有 audio-separator 侧证据，现已补齐 GUI 侧实锤）：
   - 置零：master `separate.py:629-630`：
     ```python
     spek = self.stft(mix.to(self.device))*self.adjust
     spek[:, :, :3, :] *= 0
     ```
   - compensate：`separate.py:615` `source = source*self.compensate`（primary 先乘系数）；`separate.py:514` `self.secondary_source = ... mix.T-source.T`（默认波形相减；`is_invert_spec` 默认 False，`gui_data/constants.py:616`）。对 Voc_FT 即 Instrumental = mix − 1.021×vocals，与 audio-separator `mdx_separator.py:195` 一致。
   - `*self.adjust` 恒为 1（唯一赋值点 `separate.py:216` `self.adjust = 1`），是遗迹代码，无数值影响，但 parity 读码时会撞见，特此记录。
2. **版本边界（新发现）**：UVR **5.4.0** 的 `inference_MDX.py` **没有** `:3` 置零、没有 hanning OLA（纯 kuielab 式不重叠分块 + margin）；kuielab 原始仓库同样没有。即置零与重叠窗是 UVR 在 5.5 世代随 mdx_new 家族引入的后处理，**并非架构固有**；现行 audio-separator / UVR5.6 对**所有** MDX 模型（含老家族）统一施加。
3. **两家 chunk 边界策略差异（实锤，原调研未写透）**：
   - audio-separator：无 "Default" 档，overlap 恒为数值（默认 0.25）→ `mdx_separator.py:348` `step = int((1 - overlap) * chunk_size)` = **195840**。
   - UVR5 GUI：overlap 下拉默认 **'Default'**（`gui_data/constants.py:324` `DEF_OPT='Default'`、L578 默认设置、L374 `MDX_OVERLAP = (DEF_OPT, 0.25, 0.50, 0.75, 0.99)`）→ `separate.py:563`：
     ```python
     step = self.chunk_size - self.n_fft if overlap == DEFAULT else int((1 - overlap) * chunk_size)
     ```
     即 GUI 默认 step = 261120 − 7680 = **253440**（= gen_size，相邻 chunk 恰共享 n_fft 样本上下文）；只有用户手选 0.25 时才与 audio-separator 相同。其余（两端 trim=n_fft//2 零填充、对称 np.hanning、result÷divider、两端裁 trim）两家一致（GUI `separate.py:560-607` vs audio-separator `mdx_separator.py:342-409`）。

**判级**：
- 「`:3` 置零」与「伴奏 = mix − 1.021×vocals」：【站得住】（两家现行官方实现均有，行级实锤如上；附 5.4/原始仓库无此处理的版本边界注记）。
- 原调研 §1.4 把 step=0.75×chunk 表述为唯一的「官方」分块策略：**【需修正】**——UVR5 GUI 默认档 step=chunk−n_fft 与 audio-separator 默认 overlap=0.25 是两个不同的「官方默认」。对 parity 方案的直接影响：**必须钉死以 audio-separator 及其默认参数集（overlap 0.25 → step=195840）为唯一参考**，不能笼统写「对齐官方」，否则 −60dB 判据在 chunk 边界上不可能收敛。

---

## 4. 反证目标 4：「唯 MDX-Net 有官方 ONNX；BS-Roformer/MDX23C/Demucs 无」

**原结论**（已落入 FR-15 注记）：BS-Roformer/MDX23C/Demucs 均为 PyTorch 权重，无官方 ONNX，社区导出需逐个核验。

**反证过程与结果**：

1. **反证实锤：ZFTurbo 官方维护的 ONNX/TensorRT 导出仓库存在**——`ZFTurbo/MSS_ONNX_TensorRT`（MIT），且 MSST 主仓库 README L76 官方链接："Convert models to ONNX and TensorRT formats here"。`export_to_onnx.py` 支持 **htdemucs / bs_roformer / mel_band_roformer / mdx23c / segm** 五类（opset 17）；`models_without_stft/` 目录是剥离 STFT 的网络变体（bs_roformer_no_stft.py、demucs4ht_no_stft.py、mdx23c_tfc_tdf_v3_no_stft.py、mel_band_roformer_no_stft.py、segm_models_no_stft.py），推理侧（`utils.py:436,443-445`）由外部 Python 做 STFT/iSTFT，ONNX 只含谱域网络。**「无官方导出通路」这一否定性结论被推翻。**
2. **但官方确实不发布现成 ONNX 权重**：该仓库内无任何 .onnx/.engine 文件；HF 搜索 bs_roformer/mdx23c/demucs onnx 全部为社区账号（xycld/BS-RoFormer-ONNX、elicwhite、safescribeai、rysertio/Demucs-onnx、MrCitron/demucs-v4-onnx 等，含原调研已记的 musetric），无 UVR/ZFTurbo/TRvlvr/Meta 官方发布物；mdx23c onnx 连社区成品都未检得。
3. **facebookresearch/demucs 仓库零 ONNX 内容**：全仓 grep "onnx" 无命中；`tools/export.py` 是 checkpoint→.th（PyTorch half/DiffQ）导出，与 ONNX 无关。Meta 官方无导出脚本——但 ZFTurbo 工具覆盖 htdemucs，故「Demucs(HTDemucs) 无任何官方系导出工具」也不成立。
4. **对 FR-15 的影响**：注记应修正为——「官方**导出工具**存在（ZFTurbo/MSS_ONNX_TensorRT，MIT，导出的网络不含 STFT，STFT/iSTFT 须在引擎侧自实现，参数来自各模型 YAML config）；官方**不发布** ONNX 权重；社区成品需核验，或用官方工具从官方 ckpt 自行导出（许可证链路干净）」。「唯 MDX-Net 有官方 ONNX **发布物**」这一半仍然成立（TRvlvr 公共模型库全系 .onnx，本次已实测下载）。

**判级：【需修正】**（原结论的「无官方导出」部分被推翻；「无官方 ONNX 权重发布物、社区成品需核验」部分站得住）。

---

## 5. 反证目标 5：「MDX 模型 ONNX 无 metadata，参数必须查表」

**原结论**：metadata_props 为空，参数唯一通路是哈希查表。

**反证过程与结果**：抽查 5 个跨年代/跨尺寸的 MDX 模型——为排除镜像因素，**其中 4 个 + Voc_FT 均从 TRvlvr 官方 release 重新下载原件**（与 Politrees 副本逐字节相同，见 §6），用 onnx 1.23.2 实测：

| 模型 | 输入 shape | metadata_props | 表条目（末 10MiB MD5 查 TRvlvr 表） |
|---|---|---|---|
| UVR-MDX-NET-Voc_FT（官方原件） | [batch,4,3072,256] | **空** | n_fft 7680 / dim_f 3072 / dim_t 8 / comp 1.021 / Vocals |
| UVR-MDX-NET-Inst_HQ_3（官方原件） | [batch,4,3072,256] | **空** | n_fft **6144** / 3072 / 8 / 1.022 / Instrumental |
| UVR-MDX-NET-Inst_HQ_4（官方原件） | [batch,4,**2560**,256] | **空** | n_fft **5120** / 2560 / 8 / 1.019 / Instrumental |
| UVR-MDX-NET-Inst_Main（官方原件） | [batch,4,2048,256] | **空** | n_fft 5120 / 2048 / 8 / 1.025 / Instrumental |
| UVR_MDXNET_1_9703（官方原件） | [batch,4,2048,256] | **空** | n_fft 6144 / 2048 / 8 / 1.03 / Vocals |

5/5 全部 producer=pytorch 1.9、opset 13、metadata 空；导出器源头 kuielab `src/callbacks/onnx_callback.py` 用裸 `torch.onnx.export`（不写任何 metadata）——「UVR MDX 系无 metadata」对**该训练管线产出的全系**成立。

边界注记（不构成反证）：「查表是唯一通路」仅对 UVR/TRvlbo 官方 MDX 系成立；若未来接入第三方工具导出的 onnx（含用 MSS_ONNX_TensorRT 自导的 MDX23C/Roformer——其参数来自 YAML 而非图内），metadata 有无取决于导出工具，不能假设恒空。现行结论无需改，引擎按「查表 + 允许用户覆盖」设计即可。

**判级：【站得住】**（官方原件实测 5/5 支持；未找到带 metadata 的 UVR MDX onnx）。

---

## 6. 顺带关闭的旧【未确认】项与新风险

### 6.1 关闭原调研【未确认 #2】（副本一致性）与强化【倾向 #1】

github.com release 资产本次可达，已从 `TRvlvr/model_repo` release `all_public_uvr_models` 下载 **5 个官方原件**，全部与 Politrees/UVR_resources 副本**全文件 MD5 逐字节一致**（Voc_FT `d21dc03e...`、Inst_HQ_3 `81452e38...`、Inst_HQ_4 `16658813...`、Inst_Main `c55c837c...`、9703 `1bbcb39d...`）。parity 基准可直接用官方原件，「Politrees 副本等价性」从【倾向】升级为【已确认】。

### 6.2 新风险与新发现

1. **dim_t 并非全系 256（击穿「dim_t 静态=256」的潜在误读）**：TRvlvr 表 44 条 MDX 条目中存在 `mdx_dim_t_set=7`（→128，1 个）与 `=9`（→512，4 个）的模型；本次实测 5 个模型 dim_t 恰好都是 256，但表内确有例外。修复方案的「查表取 dim_t」设计正确且必要；引擎若在任何地方写死 256（或默认 segment_size=256）会在 dim_t=512 模型上出错（audio-separator 的 ORT 直连条件 `segment_size == dim_t`，`mdx_separator.py:115`，对 dim_t=512 模型默认参数不满足、会退化 onnx2torch——我们引擎应直接按表内 dim_t 构造静态 shape）。
2. **参数离散度**：n_fft ∈ {4096, 5120, 6144, 7680, 8192, 16384}、dim_f ∈ {2048, 2560, 3072}、compensate ∈ [1.019, 1.75] 全部逐模型——查表机制必要性的又一实据；`nFft = dimF*2` 的旧推导在 5120/2560（=2×2560=5120 恰好成立！）等组合上真假随机，必须废除（已在修复方案中废除，正确）。
3. **hop=1024 对 MDX-Net 全系恒定**：kuielab 4 个 config 均 hop 1024；TRvlvr 表无 hop 字段（消费方一律用包级默认 1024）。`vocalseparator.cpp` 的 kHop=1024 硬编码维持现状的决策被多方证实（MDX23C 亦 1024；注意 bs_roformer 是 441，属另一架构，若未来接 roformer 不能沿用）。
4. **parity 参考必须钉死到实现+参数集**：UVR5 GUI 默认（Default 档，step=253440）与 audio-separator 默认（overlap 0.25，step=195840）产出不同；`separator-output-validation.md` 的 parity 计划应显式写明「参考 = audio-separator @ 默认参数（overlap 0.25 / segment 256 / batch 1 / denoise off）」。
5. **`:3` 置零的适用范围**：是 UVR 5.5+ 世代引入的对全部 MDX 模型统一施加的后处理（5.4 与 kuielab 原始均无）；单模型（Voc_FT）parity 跟随参考实现即可，多模型支持时也统一施加，无需按家族区分。
6. **FR-15 注记需按 §4 修正**（官方导出工具存在）。

---

## 7. 判级汇总

| # | 反证目标 | 判级 | 一句话 |
|---|---|---|---|
| 1 | 输出 plane-major | **【站得住】** | 7/7 实现（含 kuielab 原始、UVR5 GUI 两代、独立 C++ 复刻）直读输出进 istft，未找到任何 frame-major 实现 |
| 2 | Voc_FT n_fft=7680 查表 | **【站得住】** | 官方表+官方原件+本地哈希三方一致；但「多家独立交叉」应降格为「同源两消费方 + 家族桶佐证」 |
| 3a | `:3` 置零 + mix−1.021×vocals | **【站得住】** | UVR5 GUI master L630/L615/L514 实锤补齐；注意 5.4/原始仓库无此处理 |
| 3b | 两家 chunk 边界「相同」 | **【需修正】** | GUI 默认 step=chunk−n_fft=253440，audio-separator 默认 step=0.75×chunk=195840，默认参数下不同 |
| 4 | 唯 MDX-Net 有官方 ONNX | **【需修正】** | ZFTurbo/MSS_ONNX_TensorRT 官方导出工具存在（含 htdemucs/bs_roformer/mel_band/mdx23c）；官方无现成 ONNX 权重发布物（这半句成立） |
| 5 | MDX onnx 无 metadata | **【站得住】** | 官方原件 5/5 metadata 空；导出器不写 metadata；边界：第三方导出未必 |

---

## 8. 证据索引（上游 permalink 形式）

- kuielab/mdx-net：`src/models/mdxnet.py`（stft/istft L81-93）、`src/evaluation/separate.py`（L30-31）、`src/callbacks/onnx_callback.py`、`configs/model/ConvTDFNet_*.yaml`
  https://github.com/kuielab/mdx-net
- UVR5 GUI master(v5.6.0)：`separate.py`（L210 dim_t=2**set、L489 ORT 条件、L514 副.stem、L563 step 分支、L615 compensate、L629-630 adjust 与 :3 置零、L637 inverse）、`lib_v5/tfc_tdf_v3.py`（STFT 类）、`gui_data/constants.py`（L91 表来源、L324 DEF_OPT、L374 MDX_OVERLAP、L578 默认 overlap、L616 invert_spec 默认）、`UVR.py`（L737-741 末 10MiB 哈希、L5339 表加载）
  https://github.com/Anjok07/ultimatevocalremovergui
- UVR5 GUI v5.4.0：`inference_MDX.py`（L1065-1067 无置零直读）、`lib_v5/filelist.py`（provide_mdx_model_param_name 硬编码桶）、`lib_v5/filelists/hashes/mdx_{original,new,new_inst}_hashes.txt`（全文件 MD5；Voc_FT 不在其中）
  https://github.com/Anjok07/ultimatevocalremovergui/tree/v5.4.0
- ZFTurbo/Music-Source-Separation-Training：`README.md` L76（导出仓库链接）、`utils/model_utils.py::demix`（L116-276 波形域）、`models/mdx23c_tfc_tdf_v3.py`（L6-43 STFT、L204-238 forward→inverse）
  https://github.com/ZFTurbo/Music-Source-Separation-Training
- ZFTurbo/MSS_ONNX_TensorRT：`README.md`、`export_to_onnx.py`、`models_without_stft/`、`utils.py` L436/443-445
  https://github.com/ZFTurbo/MSS_ONNX_TensorRT
- facebookresearch/demucs：全仓无 onnx；`tools/export.py`（.th 导出）https://github.com/facebookresearch/demucs
- TRvlvr/application_data：`mdx_model_data/model_data_new.json`（86 条；Voc_FT 条目见 §2）https://github.com/TRvlvr/application_data
- TRvlvr/model_repo release：https://github.com/TRvlvr/model_repo/releases/tag/all_public_uvr_models（本次实测下载 5 个官方原件）
- audio-separator（复核既有快照 `/tmp/audsep/`）：`mdx_separator.py` L115/L136/L348/L434-460、`separator.py` 参数与哈希
- 独立复刻：https://github.com/seanghay/uvr-mdx-infer 、https://github.com/el-tahir/mdxnet_cpp （`src/utils.cpp` 平面索引 + f=3 起填）
- 本地实测产物：`/tmp/falsify/models/`（5 个官方原件 + 5 个 Politrees 副本，md5sum 输出见 §5/§6.1；onnx 1.23.2 图检查脚本随审记录）
