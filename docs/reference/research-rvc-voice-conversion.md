# RVC 变声生态调研（O2 ContentVec 许可证 / O3 实时延迟）

## 元信息

- **调研日期**：2026-10-13 ～ 2026-10-14
- **调研目的**：为 VideoHandler 规划 RVC 离线变声（后续实时变声）关闭两个高风险开放问题：
  - **O2/R1**：RVC 依赖的 ContentVec 特征提取器被称是 CC-BY-NC-4.0，随包分发有法律风险，社区是否有替代方案？
  - **O3/R2**：实时变声在 RTX 3060 级 GPU 上能否压进 150ms 延迟，需要社区实测数据锚点。
- **信源范围**：仅一手信源——RVC 官方仓库（源码 tarball + README + changelog + license 文件 + issues）、w-okada/voice-changer 源码、Applio 源码、so-vits-svc 4.1-Stable README、ContentVec 官方仓库及其作者在 issue 中的表态、Hugging Face 模型仓库页面。二手信源（博客/搜索摘要）一律不作为结论依据，仅用于定位一手出处。
- **核验方式**：GitHub API（仓库元数据）、codeload 源码 tarball 下载后本地逐文件阅读、raw.githubusercontent 取原始文件、webReader 抓取 HF 页面与 issue 页面。所有 URL 均实际访问过；无法访问的页面明确标注。
- **限制说明**：本环境 curl 无法直连 huggingface.co（连接超时），HF 模型卡经 webReader 抓取（正文可得，侧栏元数据部分可得）；GitHub core API 配额中途耗尽，issue 正文改用 search API 与 webReader 页面抓取交叉获取。

---

## 主要发现

### 1. RVC 项目现状（2025-2026）

**【已确认】RVC 官方仓库活跃，仍在迭代，现行版本 v2，v3 底模未发布。**

- 仓库 `RVC-Project/Retrieval-based-Voice-Conversion-WebUI`：MIT 协议，38718 stars，最后 push 2026-08-04（GitHub API 实测 2026-10-13）。
- 近期 changelog（`docs/cn/Changelog_CN.md`）：
  - 2026-07-23：UVR5 后端换 PyMSS；GPU 重采样；修复实时变声开降噪时 CUDA Graph 报错。
  - 2026-07-18：新增 FCPE 音高算法；实时变声 GUI 支持 CUDA Graph 推理加速，"算法推理延时指标最高加速4.7倍（4090D测试结果），同时显卡占用相对20~30%左右"。
- README 明确："请期待RVCv3的底模，参数更大，数据更大，效果更好，基本持平的推理速度" —— v3 已宣布但底模至今未发布。
- README 声明："底模使用接近50小时的开源高质量VCTK训练集训练，无版权方面的顾虑"。
- 官方仓库新增了 `RVCRealtimeVST/` 子工程：Windows x64 VST2/VST3 插件，C++17 + iPlug2，插件本体只做音频缓冲/无锁队列，RVC 推理由独立 Python worker 进程完成（共享内存 + Windows Event IPC）。这说明官方实时化路线是"插件壳 + Python 推理进程"，而非全原生推理。

来源：
- https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI（README、docs/cn/Changelog_CN.md）
- https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI/tree/main/RVCRealtimeVST

**生态格局**：RVC（MIT，活跃）为主干；Applio（MIT，活跃 fork，增强训练/管理）；w-okada/voice-changer（MIT，21170 stars，最后 push 2026-09-26，实时变声专用客户端，内置 RVC 支持）；so-vits-svc（AGPL-3.0，2023-11-11 归档停维护）；GPT-SoVITS（MIT，62763 stars，少样本 TTS/SVC，方向不同）。

### 2. 【O2 核心】ContentVec 许可证证据链

#### 2.1 ContentVec 是什么、RVC 怎么用

- ContentVec = auspicious3000（微软研究者）在 HuBERT-base 基础上做说话人解耦训练得到的特征提取器（Interspeech 2022 论文）。
- RVC 源码 `infer/hubert.py` 一手确认：`assets/hubert_base` 就是 ContentVec 转成 Transformers 格式后的权重（docstring "Load the local Transformers HuBERT/ContentVec model for RVC"；注释说明 v1 取第 9 层 + final_proj 得 256 维、v2 取第 12 层得 768 维）。
- so-vits-svc 4.1-Stable README 交叉验证：直接把 `lj1995/VoiceConversionWebUI/resolve/main/hubert_base.pt` 称作 "ContentVec"（"Or download the following ContentVec, which is only 199MB in size but has the same effect"）。

#### 2.2 许可证一手证据（按证据强度排序）

1. **【决定性】作者本人确认预训练权重是 MIT**：ContentVec 仓库 issue #34 "Is the pretrain model also MIT license?"（2025-03-04 提问），**仓库所有者 auspicious3000 于 2025-03-18 回复 "Yes" 并关闭 issue**。
   - https://github.com/auspicious3000/contentvec/issues/34
2. **代码仓库 LICENSE = MIT**：仓库 LICENSE 文件全文验证（"MIT License Copyright (c) 2019"），main 与 master 分支均如此。issue #2（2022-10-16）显示仓库早期缺 LICENSE，系后来补加。
   - https://github.com/auspicious3000/contentvec/blob/main/LICENSE
3. **权重原发点无附加限制的负面证据（未找到 NC 条款）**：ContentVec README 将权重托管在 IBM Box（https://ibm.ent.box.com/s/z1wgl1stco8ffooyatzdwsqn2psd9lrr），README 全文无任何权重许可声明（本地核验）。Box 页面本环境无法核验内容，标注为盲点。
   - https://github.com/auspicious3000/contentvec
4. **RVC 官方立场**：仓库根目录"MIT协议暨相关引用库协议"文件（blob sha dbb6c6d5）将 ContentVec 列为 MIT，链接指向其代码仓库 LICENSE。
5. **权重实际分发点**：RVC 用户从 HF `lj1995/VoiceConversionWebUI` 下载 `hubert_base/`（config.json + preprocessor_config.json + pytorch_model.bin，189MB）。webReader 一手核验：**该 repo 模型卡为空**（"README.md exists but content is empty"），repo 元数据标 `License: mit`，无逐文件许可/署名声明——分发点自身合规信息缺失，但上游作者已确认 MIT。
   - https://huggingface.co/lj1995/VoiceConversionWebUI/tree/main/hubert_base

#### 2.3 CC-BY-NC-4.0 说法的溯源结果

- 两次定向 WebSearch（"ContentVec CC-BY-NC license auspicious3000 weights" / 中文变体）**均未找到任何一手来源声明 ContentVec 权重为 CC-BY-NC**。
- 唯一 CC 匹配是 VoicePrivacy 2026 Challenge 评估计划 PDF 自身的文档许可（CC BY-NC-ND 4.0），与 ContentVec 权重无关。
- 注意：`facebook/contentvec` 这个 HF 仓库**不存在**（404，一手验证）——HF 上没有官方 ContentVec 权重仓库，"HF 模型卡标 NC"的说法没有落点。
- 结论：**O2 的前提（"ContentVec 是 CC-BY-NC-4.0"）在可核验的一手信源中不成立，且作者已正面确认 MIT**。该前提疑为社区讹传（可能与 VoicePrivacy 类文档许可或 fairseq 生态其他模型混淆）。

#### 2.4 即便如此：替代特征提取器清单（一手）

so-vits-svc 4.1-Stable README 给出可互换 content encoder 全集（这是社区替代方案的直接答案）：

| 编码器 | 权重来源 | 说明 |
| --- | --- | --- |
| vec768l12 / vec256l9（ContentVec） | IBM Box 或 lj1995 hubert_base.pt | 默认推荐 |
| vec* 的 onnx 变体（vec256l9-onnx 等 4 种） | NaruseMioShirakana/MoeSS-SUBModel | ONNX 格式 ContentVec |
| hubertsoft | bshall/hubert v0.1 | Soft-VC 原版 |
| whisper-ppg / whisper-ppg-large | OpenAI 官方 medium/large-v2 | |
| cnhubertlarge | TencentGameMate/chinese-hubert-large | 中文语料 |
| dphubert | pyf98/DPHuBERT | |
| wavlmbase+ | 微软 WavLM-Base+ | |

- Applio 侧（源码一手）：`load_embedding_manager` 支持 contentvec / spin / spin-v2 / chinese-hubert / japanese-hubert / korean-hubert。
- **data2vec：主流 RVC/SVC 生态（RVC、so-vits-svc、Applio、DDSP-SVC、w-okada）中均未见采用证据**——需求文档若以此为候选替代，需自行验证。
- ~~上游初始化链条盲点：ContentVec 从 facebook/hubert-base-ls960 初始化~~ **2026-10-14 反证修正（`falsify-licenses.md`）**：本句失实——ContentVec 公开训练配置无任何 checkpoint 加载字段，论文原文显示 HuBERT-base 是**教师标签来源**（第 7 层特征 + voice conversion + k-means 聚类标签），**非权重初始化**，故不存在「继承 HuBERT 权重许可」的链条。且 fairseq examples/hubert README（含全部 7 次历史提交）从未载有 CC-BY-NC 或任何权重许可限制文案；Meta 官方 5 个 facebook/hubert-* HF 仓库（含 hubert-base-ls960）均为 **apache-2.0**，2021-06 起逐历史修订一贯如此——原「Meta 原始 HuBERT 权重许可无法闭合」盲点已闭合。

### 3. RVC 推理管线构成（VideoHandler 集成所需）

**【已确认】管线 = ContentVec 特征 + F0 + faiss 检索 + VITS 派生生成器。**

1. **特征提取**：ContentVec，16kHz 单声道，160 样本窗（`infer/vc/pipeline.py`：`sr=16000, window=160`）。
2. **F0 提取**：现行官方支持 **pm / rmvpe / fcpe** 三种（pipeline.py 一手；VST README 同样列出 RMVPE、FCPE、PM）。crepe/harvest 已被 RMVPE 取代（changelog todolist 明确"已经被RMVPE取代"）。**"parpitch" 不存在**——官方代码、文档、changelog 均无此词。
3. **检索**：faiss 索引，k=8 加权混合，`index_rate` 控制混合比例（pipeline.py）。
4. **生成器**：`SynthesizerTrnMs768NSFsid`（VITS 派生 + NSF HiFi-GAN 声码器）。
5. **模型格式**：`.pth`（PyTorch），配套可选 `.index`（faiss）。底模与用户微调模型同格式。
6. **RMVPE 许可**：Apache-2.0（Dream-High/RMVPE 仓库 LICENSE 一手核验）；RVC 用的 rmvpe.pt 由 yxlllc 与 RVC-Boss 重训（RVC README 参考项目节）。

### 4. ONNX 可行性

**【已确认】官方 ONNX 推理路线已废弃但社区 ONNX 全链路存在。**

- RVC 官方：2023-06 加过"onnx导出选项卡"，但 ONNX **推理**实验失败废弃，原话"生成nsf还是需要pytorch"（changelog 失败实验清单）；`rmvpe.onnx` 仅保留给 DirectML 路径。
- **w-okada/voice-changer 的 onnx edition 是最强证据**：RVC 完整跑在 ONNX Runtime 上（源码一手：`OnnxContentvec`、`RMVPEOnnx`、`OnnxRVCInferencer` 及对应 exporter 类）——证明"ContentVec + RMVPE + 生成器"三段均可 ONNX 化。
- Applio：`APPLIO_EXPERIMENTAL_ONNX_DECODER` 环境变量开实验性 ONNX 解码器（源码一手）。
- 社区分发：dev6699/rvc-onnx（转换+推理）、rvc-onnx-web（npm）、TTS-with-RVC-ONNX（PyPI）、ohnoitsaninja/rvc-base-onnx（HF）。
- RVC issues #2684 / #1519 / #1592 记录官方导出 tab 的废弃状态与社区诉求。

来源：https://github.com/w-okada/voice-changer（server/voice_changer/RVC/onnxExporter 等）、https://github.com/IAHispano/Applio、https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI/issues/2684

**对 VideoHandler 的含义**：C++/ORT 技术栈做 RVC 离线变声有 w-okada onnx edition 全套导出器代码可参考（三段模型逐一导出），不需要自研转换脚本；但导出产物质量需自行验收（见证据评估）。

### 5. 【O3 核心】实时变声延迟证据

#### 5.1 官方声明值（一手）

- RVC README：**"我们已经实现端到端170ms延迟。如使用ASIO输入输出设备，已能实现端到端90ms延迟，但非常依赖硬件驱动支持。"**（未注明测试 GPU 型号/参数）
  - https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI#L40
- RVCRealtimeVST 参数体系（README + 源码一手）：
  - Block：20–1000 ms，**默认 130 ms**（`src/RVCRealtime.cpp:153` `InitInt("Block", 130, 20, 1000, "ms")`；`src/WorkerClient.cpp:209` 同值）
  - Crossfade：10–100 ms（实际 SOLA overlap = min(Crossfade, 40 ms)）
  - Context：500–3000 ms
  - **插件报告延迟 = 两倍 Block 采样帧数**；`config.h:20` `#define PLUG_LATENCY 12480`（= 2 × 6240 帧 @48k = **260 ms 默认**；Block=20 ms 时理论下限 40 ms）
- 官方 realtime_gui.py（一手）：block 0.25s、crossfade 0.05s、extra 2.5s 上下文。

#### 5.2 加速手段与量级（一手）

- CUDA Graph（2026-07-18 changelog）：4090D 上"算法推理延时指标最高加速4.7倍"，显存占用 +20~30%。注意口径是**算法推理延时**（不含音频 IO/缓冲），非端到端。
- w-okada 客户端架构（源码一手）：Web Audio worklet BLOCK_SIZE=128，`inputChunkNum` 默认 192 → 48kHz 下 512ms 输入缓冲；发送 48k 音频到 server。RVC 设置默认 `f0Detector="rmvpe_onnx"`、`indexRatio=0`。
- Applio realtime（源码一手）：block_frame 480–48000（10 ms–1 s）@48kHz，带 VAD 与 phase vocoder crossfade。

#### 5.3 RTX 3060 级实测锚点

- **未找到可信的 RTX 3060 定量实测数据**。w-okada issue #1206 是 GPU 选购提问（列了 "Rtx 3060 [Msi Gaming X] 342 USD" 与 4060 Ti 选项），无任何延迟测量。其他 issue 要么是病理报告（爆音/卡顿）要么无量化。
- 定量锚点只有官方的 170ms（普通设备）/90ms（ASIO）与 VST 默认 260ms / 下限 40ms（未标 GPU）。

### 6. faiss 索引现状

**【已确认】仍是标准组件，但默认权重在实时场景被弱化。**

- RVC 官方离线/实时管线均含 faiss 检索（k=8、index_rate 可调）。
- w-okada 的 RVC 默认 `indexRatio=0`（即默认禁用检索混合）——实时场景社区实践是弱化 index。
- Applio 的 extract_index.py 仍构建索引。
- RVC changelog 2023-08："解决实时变声中开启索引导致的CPU极大占用问题"——历史上 index 是实时性能瓶颈点。

### 7. 使用须知/伦理声明实践（NFR-C5 对标）

- **RVC 官方**（"MIT协议暨相关引用库协议"文件）：伦理条款原话"作者不对软件具备任何控制力，使用软件者、传播软件导出的声音者自负全责"。
- **Applio**：仓库带 `TERMS_OF_USE.md` 伦理模板（要求使用者确认对目标声音的权利等），是最接近"随软件分发的使用须知"的范例。
- **voice.ai**（商业对标）：TOS 原话 "Our software is intended solely for lawful and ethical use, including the creation of original voice identities and parody content involving public figures."（https://voice.ai/tos）
- **音频水印**：开源三强（RVC/Applio/w-okada）均无内置水印；行业侧的水印实践是研究级的 Meta AudioSeal（arXiv 2401.17264，首个针对 AI 语音的局部化水印检测）。开源 VC 生态尚无水印事实标准——NFR-C5 若要求水印，将超出社区现状，属于自设高线。

---

## 对 O2/O3 的证据评估

### O2：ContentVec 许可证

**证据足够下结论，且结论与问题前提相反。**

- 决定性一手证据：作者在 issue #34 的 "Yes"（2025-03-18）+ 代码仓库 MIT LICENSE + 权重分发链（IBM Box / lj1995 HF）中不存在任何 NC 条款文本。
- 反向排查：两轮定向搜索找不到 CC-BY-NC 声明的任何一手出处；`facebook/contentvec` HF 仓库不存在。
- 残余盲点（不改变结论，但须记录）：(a) IBM Box 页面本身未能核验；(b) ~~ContentVec 初始化自 Meta HuBERT-base，Meta 原始权重许可未闭合~~ **2026-10-14 反证修正：该盲点已闭合**——HuBERT-base 是教师标签来源非权重初始化（训练配置无 checkpoint 加载），且 Meta 官方 facebook/hubert-* HF 仓库全为 apache-2.0（2021-06 起一贯），见 `falsify-licenses.md`。商业化前的传递链法务复核降为纯流程性动作。
- **替代方案问题随之降级**：既然 ContentVec 权重 MIT，"必须换掉 ContentVec"不再成立；so-vits-svc 的 8 类可换编码器清单仍可作为效果调优选项保留。

### O3：RTX 3060 级 150ms 实时延迟

**证据不足以对"RTX 3060 能否 <150ms"下结论；但官方数据足以界定工程可行区间。**

- 有的：官方端到端 170ms（常规）/90ms（ASIO，强烈依赖驱动）；VST 报告延迟模型 2×Block（默认 260ms，参数下限 40ms）；CUDA Graph 在 4090D 上算法段 4.7× 加速。
- 没有的：3060 级 GPU 上的任何端到端实测数；w-okada/Applio 的默认参数（512ms / 480 起）都是保守值，不代表下限。
- 可行的推断（明确标注为推断而非证据）：官方 170ms/90ms 未标 GPU，若在 3060 级复现同量级，150ms 目标在"ASIO/WASAPI 独占 + 小 Block + CUDA 图 + 不开 index"配置下处于可达边界内，但**必须用真机实测关闭 O3**——VideoHandler 的 150ms 目标应保留一个"实测验证"闸门（与 REQUIREMENTS O3 的表述一致，本次调研将其具体化为：官方声明值 90–170ms 区间覆盖了 150ms，无 3060 专属锚点）。**2026-10-14 反证注记（`falsify-realtime.md`）**：① 这两个数字的出处是 **RVC README**（本节上文引用），w-okada 官方材料中不存在——任何把它们归到 w-okada 名下的表述（含 research-index/REQUIREMENTS v1.1 的旧写法）均需改归属；② w-okada 侧存在反例 #796（i9-10900K + RX6800XT 实时不可用、作者只回「换更强硬件」）——区间覆盖 ≠ 普遍可达，3060 级 benchmark 仍双向缺失。

---

## 结论分级

### 【已确认】（一手信源直接证明）

1. ContentVec **代码** MIT（LICENSE 文件）。
2. ContentVec **预训练权重** MIT——作者 2025-03-18 在 issue #34 明确回复 "Yes"。
3. CC-BY-NC-4.0 的说法找不到一手出处；`facebook/contentvec` HF 仓库不存在（404）。
4. RVC 的 `assets/hubert_base` 就是 ContentVec（源码 docstring + so-vits-svc README 交叉验证）。
5. RVC 管线：ContentVec(16k) → F0(pm/rmvpe/fcpe) → faiss(k=8, index_rate) → SynthesizerTrnMs768NSFsid；F0 无 "parpitch"，crepe/harvest 已弃。
6. RVC 官方端到端延迟声明 170ms / 90ms(ASIO)；VST 报告延迟=2×Block，默认 Block 130ms → 260ms，参数下限 40ms。
7. w-okada onnx edition 证明 RVC 三段模型（ContentVec/RMVPE/生成器）全部可 ONNX 化且有现成导出器。
8. faiss 仍是 RVC/Applio 标准组件；w-okada 实时默认 indexRatio=0。
9. RMVPE 代码 Apache-2.0；Applio 有 TERMS_OF_USE.md 伦理模板；voice.ai TOS 有限 lawful/ethical 条款；开源 VC 无内置水印实践。
10. RVC 官方仓库活跃（2026-08 仍有提交），v2 现行，v3 底模未发布；官方实时化路线是 VST 壳 + Python worker 子进程。

### 【倾向】（一手证据支持但含推断成分）

1. O2 应按"无许可证障碍"关闭：作者声明 MIT + 全链路找不到 NC 文本；残余风险仅剩"上游 HuBERT 初始化权重的许可文本缺失"这一流程性盲点，建议发版前法务复核而非现在阻塞。
2. 150ms 实时目标在工程上处于可达边界：官方 90–170ms 区间覆盖 150ms，且加速手段（CUDA Graph、小 Block、关 index、ASIO）方向明确；但不能外推到具体 GPU 型号。
3. VideoHandler 走 ONNX 路线做 RVC 离线推理技术可行（w-okada 全套导出器可复用），但官方从未支持 ONNX 推理、社区导出物无质量背书，**效果验收必须建 parity 测试**（与 NFR-Q1 一致）。
4. 实时场景社区实践是弱化/关闭 faiss 检索（性能瓶颈在 CPU 侧 index 查询）。

### 【未确认】（一手信源查不到，禁止编造）

1. RTX 3060 级 GPU 上的 RVC 端到端实测延迟（无定量锚点；官方数据未标 GPU）。
2. ~~Meta 原始 HuBERT-base 权重的显式许可声明文本~~ **已闭合（2026-10-14 反证，`falsify-licenses.md`）**：Meta 官方 facebook/hubert-* HF 仓库（含 hubert-base-ls960）均为 apache-2.0，逐历史修订（2021-06 起）一贯；fairseq examples/hubert README 从未载有权重限制文案。
3. IBM Box 上 ContentVec 权重页面是否载有许可信息（页面不可达）。
4. data2vec 在主流 VC 生态中的采用情况（未在任何一手清单中出现，仅能说"未见采用证据"）。
5. RVC v3 底模的发布时间与参数（官方仅"请期待"）。
6. ElevenLabs 等商业厂商的具体水印实现细节（公开信息只有 Meta AudioSeal 研究成果）。

---

## 参考链接清单

### RVC 官方
- 仓库：https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI
- README（延迟声明/参考项目/模型路径）：同上仓库 README.md（本地 tarball 核验）
- Changelog：https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI/blob/main/docs/cn/Changelog_CN.md
- VST 工程：https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI/tree/main/RVCRealtimeVST（README.md、config.h、src/RVCRealtime.cpp、src/WorkerClient.cpp 本地核验）
- 模型分发：https://huggingface.co/lj1995/VoiceConversionWebUI/tree/main/hubert_base

### ContentVec（O2）
- 代码仓库：https://github.com/auspicious3000/contentvec
- **权重许可确认 issue**：https://github.com/auspicious3000/contentvec/issues/34
- 早期许可补加 issue：https://github.com/auspicious3000/contentvec/issues/2
- LICENSE：https://github.com/auspicious3000/contentvec/blob/main/LICENSE
- 权重原发点（IBM Box，未核验页面内容）：https://ibm.ent.box.com/s/z1wgl1stco8ffooyatzdwsqn2psd9lrr

### 实时变声（O3）
- w-okada/voice-changer：https://github.com/w-okada/voice-changer（onnx edition 源码核验）
- GPU 选购 issue（无实测，排除用）：https://github.com/w-okada/voice-changer/issues/1206
- Applio：https://github.com/IAHispano/Applio（realtime 与 ONNX decoder 源码核验）

### 替代编码器（O2 降级后仍有效）
- so-vits-svc 4.1-Stable README 编码器清单：https://github.com/svc-develop-team/so-vits-svc/blob/4.1-Stable/README.md
- bshall/hubert：https://github.com/bshall/hubert
- TencentGameMate/chinese-hubert-large：https://huggingface.co/TencentGameMate/chinese-hubert-large

### ONNX 生态
- dev6699/rvc-onnx：https://github.com/dev6699/rvc-onnx
- rvc-onnx-web（npm）：https://www.npmjs.com/package/rvc-onnx-web
- ohnoitsaninja/rvc-base-onnx：https://huggingface.co/ohnoitsaninja/rvc-base-onnx
- 官方 ONNX 讨论：https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI/issues/2684

### F0 / 声码器
- RMVPE（Apache-2.0）：https://github.com/Dream-High/RMVPE
- RMVPE 重训版：https://github.com/yxlllc/RMVPE
- NSF HiFiGAN：https://github.com/openvpi/DiffSinger/tree/refactor/modules/nsf_hifigan

### 伦理 / 水印（NFR-C5）
- Applio 使用条款模板：https://github.com/IAHispano/Applio/blob/main/TERMS_OF_USE.md
- voice.ai TOS：https://voice.ai/tos
- Meta AudioSeal（arXiv 2401.17264）：https://arxiv.org/html/2401.17264v2
