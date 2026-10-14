# 音频处理算法前沿调研（2023–2026）

## 元信息

- 快照日期：**2026-10-14**
- 性质：前沿论文快照（近三年为主，2023–2026），为路线图选型（FR-4 降噪 / FR-5、FR-15 分离 / FR-7 变声 / M4 实时化 / O3 延迟闸门）提供证据
- 方法：一手信源优先（arXiv 论文原文 HTML/PDF、Interspeech/ICASSP 论文页、官方仓库 README/LICENSE 原文、PyPI 元数据）；二手转述只作线索不作证据，标注【倾向】或【未确认】
- 衔接（不重抄）：GTCRN / DFN3 / MDX / RVC 四份既有调研见 `research-index.md` §1；本文只覆盖其**之后与之外**的前沿。已有基线数字直接引用：GTCRN 48.2K 参数 / 33.0 MMACs/s / RTF 0.07@i5-12400（`research-gtcrn-sherpa-onnx.md`）；DFN3 950K 参数 / RTF 0.19（论文，笔记本单线程）/ 40ms 延迟（20ms 窗 + 2×10ms 前视）（`research-deepfilternet3.md`）
- 范围声明：so-vits-svc / DiffSVC 生态（FR-22，v2 档）本轮未做新一轮一手核验，维持既有结论；RVC 生态现状见 `research-rvc-voice-conversion.md`

## 0. 总览（一页结论）

| 战场 | 最重要发现 | 判级 | 对路线图的直接含义 |
|---|---|---|---|
| 1 实时降噪 | **FastEnhancer**（ICASSP 2026，MIT，官方 ONNX + ORT 流式文档，全因果 6–16ms 算法延迟，ORT 单线程 RTF 0.012–0.03） | 【已确认】 | GTCRN 快速档的接棒候选 + 实时链降噪段首选；有 48kHz 全频带变体，与 canonical-48k（NFR-Q3）对齐 |
| 2 音源分离 | **Band-SCNet**（Interspeech 2025）：因果实时 MSS，92ms 延迟、2.59M 参数、**CPU 单线程 RTF 0.478**（i7-13700H） | 【已确认】 | 「实时分离算力不现实」（REQUIREMENTS §4.4 Won't）在论文级已被推翻；但无代码/权重发布 + 92ms 吃掉 150ms 预算过半，v1 维持不做，表述建议更新 |
| 3 变声 | O3 出现**新正锚点**：StreamVC 端到端 70.8ms（Pixel 7 CPU）；Zero-VC 20ms 零前瞻 + CPU RTF 0.063；MeanVC2 110ms（Apache-2.0 开源权重） | 【已确认】（数字）/【倾向】(可移植性) | 「正反锚点皆缺」可更新为「架构级正锚点已现，但均非 RVC 架构、无一可直用」；RVC 系 3060 级仍无锚点，O3 闸门保留 |
| 4 DSP/CPU | **faster-enhancer.c**（2026）：INT8 专用运行时比 ORT fp32 快 3.3×（RTF 0.069 vs 0.230，Apple M2 单核），精度损失 −0.006 PESQ | 【已确认】 | CPU 实时优化的论文级证据：INT8 可行且精度近乎无损；实践路径 = ORT QDQ INT8（S8S8 默认）+ 真机实测，fp32 兜底 |
| 4 变调 DSP | **Signalsmith Stretch**（MIT，C++11 header-only 多声部变调/变速库） | 【已确认】 | SoundTouch 之外的质量升级选项，许可干净（MIT），FR-6/FR-19 可评估替换 |

---

## 1. 实时语音增强 / 降噪（FR-4 / M4）

背景：**DNS Challenge 系列已收摊**——传统 DNS 赛事止于第 5 届（ICASSP 2023，全频带实时增强为主题）；后继者 **URGENT Challenge**（Interspeech 2025 / ICASSP 2026）**不设延迟预算**，头部系统是 ~102M 参数的离线大模型。结论：「近届 DNS 冠军方案」已不再是实时 SOTA 的有效代理，实时轻量线的证据源转为单篇论文 + 官方仓库实测。

### 1.1 FastEnhancer —— 本项目最强候选【已确认】

- **FastEnhancer: Speed-Optimized Streaming Neural Speech Enhancement** | Sunghwan Ahn, Jinmo Han, Beom Jun Woo, Nam Soo Kim | **ICASSP 2026**（接收信息来自官方仓库 README）| arXiv:2509.21867（2025-09）
- 核心思路：面向真实在线延迟优化的流式 SE。极简编码器-解码器 + 高效 RNNFormer 块；时间轴卷积核长恒为 1（**全因果、零前瞻**），流式即逐帧推理。
- 关键数字（论文 Table 1/2 + 仓库 README）：
  - 参数/MACs 五档：Tiny 22K/55M、Base 92K/262M、Small 195K/664M、Medium 492K/2.9G、Large 1105K/11G
  - **RTF（ONNX Runtime，单线程 CPU）**【已确认，测点：Intel Xeon Gold 6248R 与 Apple M1】：Tiny 0.012/0.013；Base 0.022/0.026；Large 0.313/0.632
  - 算法延迟 = 一个 hop：Tiny/Base 16ms（hop 256@16k）、Small 10ms、Medium 6.25ms——远优于 DFN3 的 40ms
  - 质量（VoiceBank-Demand，16k）：Base PESQ 3.13 / DNSMOS 3.47；Large PESQ 3.26——**Base 即以 92K 参数超过 GTCRN（PESQ 2.87，48.2K 参数）与 BSRNN（3.06）**；同表基线还有 LiSenNet 2.98（streamable）、FSPEN 3.00
  - 另有 **48kHz 全频带版本**（改 n_fft/hop，VCTK+多语料训练）——与本项目 canonical-48k 方向（NFR-Q3）直接对齐
- 许可证：**MIT**【已确认，仓库页】；官方发布 PyTorch checkpoint + **ONNX 模型（GitHub Releases）+ ONNXRuntime 流式推理文档**【已确认，仓库页】
- 对本项目：**候选（高优先）**。GTCRN 快速档的接棒者与实时链降噪段首选：更小/更强/全因果/官方 ONNX/ORT 实测数字齐备。落 M2 后可与 DFN3 组成「FastEnhancer 快 + DFN3 质」双档；M4 实时链用其 6–16ms 算法延迟给 150ms 预算让出 ~25–35ms 余量。

### 1.2 aTENNuate —— 因果原始波形状态空间模型【已确认】

- **Optimized Real-time Speech Enhancement with Deep State Space Modeling（aTENNuate）** | Y. R. Pei 等 | **Interspeech 2025** | arXiv:2409.03377
- 核心思路：深度状态空间（SSM）自编码器，**端到端原始波形**（无 STFT/iSTFT），全因果（无双向 SSM）；训练用长 FFT 卷积并行化，推理等价转循环形式在线跑。
- 关键数字【已确认，论文】：0.84M 参数 / 0.33G MACs/s / 16kHz；算法延迟 46.5ms（base）/ 31.25ms / 16ms（去 PreConvs 变体）；PESQ VB-DMD **3.27 > DeepFilterNet3 3.16**，DNS1 无混响 **2.98 ≫ DFN3 2.58**（同表：FRCRN 6.9M/30ms、PercepNet 8M/40ms、Demucs 33.5M/40ms）；抗压缩输入鲁棒（8kHz&4-bit 时 PESQ 仍 2.72）。RTF 未报告（论文明确只给理论延迟）【已确认其缺】。
- 许可证：**Apache-2.0**【已确认，PyPI 元数据】；代码仓库 github.com/Brainchip-Inc/aTENNuate（Brainchip，事件芯片公司，工业背书）。ONNX 导出支持未见提及【未确认】。
- 对本项目：**候选（备选）**。DNS1 上对 DFN3 的显著优势 + 16–46ms 延迟档，适合作为质量/延迟折中档；但无 ONNX 官通路、无 RTF 实测，集成成本高于 FastEnhancer。

### 1.3 CoFi-Lite —— 超轻量效率学术参照【倾向】

- **CoFi-Lite: Pushing the Limits of Ultra-Lightweight Speech Enhancement** | Leyan Yang, Dahan Wang, Xiaobin Rong, Jing Lu | **IEEE SPL**（接收）| arXiv:2607.10142（2026-07）
- 核心思路：粗/细双路对称编解码（全频带包络 + 低频细节）+ 跨路融合（CPF）。
- 关键数字【已确认，摘要】：83.12K 参数 / 12.87M MACs/s；以 GTCRN 40.26% 的计算量超过 GTCRN；放大变体以少 19.34% 计算量匹配 AdaptCRN。RTF/延迟/采样率/硬件：摘要未给【未确认】。
- 许可证：代码未见发布【未确认】。
- 对本项目：**参照**（效率上限标尺；无代码不可落地）。

### 1.4 Stream.FM —— 流式生成式修复新趋势【倾向】

- **Real-Time Streamable Generative Speech Restoration with Flow Matching（Stream.FM）** | Simon Welker 等（Gerkmann 组） | 投 IEEE 在审 | arXiv:2512.19442（2025-12，v3 2026-04）
- 核心思路：帧因果流匹配（flow matching）生成模型，缓冲流式推理 + 学习型少步求解器 + 权重压缩，一模型覆盖降噪/去混响/编解码后处理/带宽扩展/相位恢复/Mel 声码六任务。
- 关键数字【已确认，摘要】：算法延迟 32ms / 总延迟 48ms（SE 变体 24ms）；运行于「现有消费级 GPU」（具体型号/RTF 未给）【未确认】。参数量未给。
- 许可证：代码未发布【未确认】。
- 对本项目：**趋势参照**。生成式流式修复是 2025–2026 明确新方向（URGENT 2025 结论亦显示生成式在主观分上可超判别式、但有语言依赖/幻觉风险）；v1 判别式路线不变，v2 可关注。

### 1.5 URGENT Challenge（挑战赛格局）【已确认】

- **Interspeech 2025 URGENT Challenge** | Saijo, Zhang, Cornell, Scheibler, Li 等 | arXiv:2505.23212；**ICASSP 2026 版** | arXiv:2601.13531（80+ 队注册 / 29 队有效提交，Track 2 新增质量评估）
- 要点：七档采样率（8–48k）、七类失真联合的「通用 SE」；**不设延迟/参数预算**。2025 版 32 提交：第一名 T1 为纯判别式子带 RNN（~102M 参数）；多数头部为判别+生成混合；纯生成式（ latent diffusion）在未见语言（日语）上崩塌、低 SNR 出现英语幻觉；DNSMOS 类非侵入指标测不出幻觉，P.808 主观分更可靠。数据尺度结论：60k 小时 noisy 数据不必然更好。
- 对本项目：**格局参照**——「一模型抗所有失真」是学界方向，但头部体量与本项目 CPU/流式约束无关；评测方法论（DNSMOS 测不出幻觉）值得在自建 eval 时借鉴（引入主观/转录型校验）。

### 1.6 效率类短讯（线索级）

- **TF-MLPNet**（Clarity 2025 Workshop，arXiv:2508.03047，UW Gollakota 组）：MLP-Mixer 式极小语音分离，GAP9 低功耗处理器上 6ms 块实时，QAT 混合精度，较先前流式模型降 3.5–4× 运行时【已确认（摘要）】。面向助听器硬件，代码未发布。
- **Redesigning LiSenNet for embedded speech enhancement**（arXiv:2609.29866，2026-09）：全 INT8 无浮点回退的嵌入式重设计【倾向，仅线索】。

---

## 2. 音源分离（FR-5 / FR-15 / 实时化空白）

格局：离线质量 SOTA 仍为 **BS-RoFormer / Mel-RoFormer**（频带切分 + RoPE Transformer）；**实时/因果分离**在 2024–2025 从无到有成为独立战线（HS-TasNet → Online SCNet → Band-SCNet），这正是本项目实时化阶段的关键空白的外部进展。

### 2.1 Band-SCNet —— 因果实时分离的当前标杆【已确认】

- **Band-SCNet: A Causal, Lightweight Model for High-Performance Real-Time Music Source Separation** | Junqi Yang, Yuhong Yang 等（武汉大学 NERCMS）+ **OPPO** | **Interspeech 2025**（DOI 10.21437/Interspeech.2025-448）
- 核心思路：把 SCNet 改造成因果版（Online SCNet：因果卷积 + cLN + 单向 LSTM），再加 Cross-band/Narrow-band 块（借 SpatialNet 全/子带交替建模）与压缩自注意力融合（CSA）。**44.1kHz 立体声、MUSDB18-HQ、单模型出 4 stem**。
- 关键数字【已确认，论文 Table 1（RTF 测点：**Intel i7-13700H 2.40GHz 单线程**）】：

| 模型 | 因果 | SDR All | SDR Vocals | 参数 | FLOPs | 算法延迟 | RTF |
|---|---|---|---|---|---|---|---|
| HS-TasNet（ICASSP 2024） | 是 | 4.65 | 5.13 | 42M | — | 23ms | — |
| Wave-U-Net（因果） | 是 | 6.47 | 6.91 | 5.7M | — | 23ms | — |
| Online SCNet（其基线） | 是 | 7.09 | 8.53 | 4.36M | 12.25G/s | 92ms | 0.428 |
| **Band-SCNet** | **是** | **7.79** | **8.74** | **2.59M** | 17.59G/s | **92ms** | **0.478** |
| SCNet（非因果） | 否 | 9.00 | 9.89 | 10.08M | — | — | — |
| BS-RoFormer（非因果） | 否 | 9.80 | 10.66 | 72.2M×3 | — | — | — |

  - 92ms 延迟 = 4096 点窗 @44.1k（hop 1024≈23ms）；论文论证 23ms 短窗抓不住音乐低频动态，是实时模型质量差的根因之一。
- 许可证：**代码未发布**【已确认其缺：论文无代码链接，多轮检索无官方仓库】。
- 对本项目：**候选（实时化阶段）+ 证据锚点（现在）**。三个直接结论：(a) 因果-非因果质量差 ~2dB SDR（Vocals 8.74 vs 10.78 口径各异），可分离但仍有价；(b) **CPU 单线程 RTF 0.478 证明消费 CPU 实时分离可行**——REQUIREMENTS §4.4「实时人声分离（算力不现实）」的**理由已被论文级反例推翻**（是否改档位是需求决策，本文只登记证据）；(c) 92ms 算法延迟将吃掉 150ms 预算的 61%，与变声链叠加必然超支——实时链若上分离只能独占链路。注意：无代码权重，短期内不可集成，只能参照架构。

### 2.2 HS-TasNet —— 23ms 低延迟实时分离【已确认（经 Band-SCNet Table 1 转引）】

- **Real-time low-latency music source separation using hybrid spectrogram-TasNet** | S. Venkatesh, A. Benilov, P. Coleman, F. Roskam（Dolby） | **ICASSP 2024**
- 频域+时域混合编码的实时 MSS；23ms 延迟档 SDR All 仅 4.65——**23ms 窗长下质量崩**的实证（Band-SCNet 论文归因：低频分辨率不足）。
- 对本项目：**参照**（延迟-质量 trade-off 的下界证据；直接证明「分离要压到 <25ms 延迟」在当前技术上不现实）。

### 2.3 SCNet / BS-RoFormer / Mel-RoFormer / Moises-Light —— 离线质量档对照【已确认】

- **SCNet: Sparse Compression Network for Music Source Separation** | Weinan Tong 等（CUHK-Future 网络） | **ICASSP 2024** | arXiv:2401.13276。频带切分 + 稀疏压缩（低/中/高频不同下采样率），单模型 4 stem，10.08M 参数 SDR 9.00、large 42.18M 达 9.69——以 ~1/7 参数逼近 BS-RoFormer。官方代码未检索到发布【未确认】；社区非官方实现 amanteur/SCNet-PyTorch（MIT）存在。
- **BS-RoFormer（Music Source Separation with Band-Split RoPE Transformer）** | Lu, Wang, Kong, Hung（ByteDance） | **ICASSP 2024**。SDR All 9.80（MUSDB18-HQ），72.2M×3（单输出模型×3）。训练侧事实实现 = ZFTurbo 仓库（MIT，即 FR-15 注记的官方导出工具同源），见 `research-mdx-uvr-audio-separator.md`。
- **Mel-Band RoFormer for Music Source Separation** | Wang, Lu, Won | **ISMIR 2023 LBD** | arXiv:2310.01809。mel 尺度重叠子带切分 + 轴向注意力。官方数字（Table 1，LL=6）：**Vocals 11.21** / Avg 9.64，84.2M；BS-RoFormer 同表 Vocals 10.78 / Avg 9.92。非因果离线（overlap&average 分块）；STFT 2048 窗；RTF/延迟未报告。官方论文无代码链接；社区 33M 参数人声模型（ZFTurbo 训练）实测 Vocals ~8.42（口径与官方不可比）。**注意两篇论文对 BS-RoFormer Vocals 报 10.66 与 10.78 两个数**——评估口径（song-level vs chunk-level museval）不同，本项目对比时须钉死口径。
- **Moises-Light: Resource-efficient Band-split U-Net For Music Source Separation** | Hung, Pereira, Korzeniowski（Music AI/Moises） | arXiv:2510.06785（2025-10）。DTTNet 骨干 + RoPE Transformer 块 + 分组卷积带切分 + 非对称编解码。**每 stem 仅 5M 参数（4 stem 共 20M）**，chunk 级 cSDR 平均 9.96 > BS-RoFormer 9.80（Vocals 10.92）；44.1k 立体声、STFT 6144/1024（与本项目 MDX 工作点同族）。**非因果、无 RTF/延迟测量、无代码发布**（论文 CC BY-NC-SA 4.0）。
- 对本项目：**FR-15 高质量档的证据更新**——质量/参数帕累托在快速移动（Moises-Light 以 ~1/14 参数打平 BS-RoFormer），但「官方不发成品 ONNX」的硬约束（FR-15 注记）依旧：Moises-Light 连代码都没发。高质量档维持「自导或社区成品逐个核验」路线，新候选按此门槛准入。

### 2.4 Apollo —— 频带序列建模音频修复（澄清定位）【已确认】

- **Apollo: Band-sequence Modeling for High-Quality Audio Restoration** | Kai Li（JusperLee）等 | **ICASSP 2025** | arXiv:2409.08514。GitHub JusperLee/apollo，**CC BY-SA 4.0**（代码仓库声明）。
- 澄清：社区语境常把 Apollo 混入「分离 SOTA」，实际任务是**有损压缩音乐修复**（MP3≤128k → 高质量），非分离、非因果（仅分块有界内存推理）。44.1k；参数量/速度数字在 README 图中未文本化【未确认】。
- 对本项目：**暂无用**（无对应 FR；ShareAlike 许可与红线组合也别扭）。若未来加「素材修复」功能再回访。

### 2.5 实时分离战场小结

- 因果实时分离已从零到一（23ms/低质 → 92ms/7.79dB），但**头部方案无代码权重**、92ms 延迟与 150ms 全链路预算冲突。
- 对 M4 的含义：实时链默认**不含**分离（与现行规划一致）；若 v2 做「实时人声分离」，架构参考 Band-SCNet（92ms 档）或等社区权重释放，且需独立链路预算。

---

## 3. 语音转换 / 变声（FR-7 / O3 / FR-22）

既有基线（`research-rvc-voice-conversion.md`）：RVC 官方 README 端到端 170ms / ASIO 90ms（未标 GPU）；w-okada#796 反例（i9+RX6800XT 实时不可用）；3060 级正反锚点皆缺；O3 定级「未证实」。本轮新增五个一手锚点。

### 3.1 StreamVC —— 流式 VC 延迟的标杆数字【已确认】

- **StreamVC: Real-Time Low-Latency Voice Conversion** | Y. Yang 等（Google Research） | 2024 | arXiv:2401.03078
- 核心思路：SoundStream 式全因果编解码（内容编码器 + 逐帧说话人编码器可学习池化 + FiLM 条件化解码器），HuBERT 伪标签（k-means 100 类@50Hz）解耦内容与音色，GAN 训练。
- 关键数字【已确认，论文】：16kHz；帧 20ms（320 样本）；2 帧前瞻 → **架构延迟 60ms**；**端到端 70.8ms（Pixel 7）**，其中计算 10.8ms/20ms 块（XNNPACK 单核，iPhone 8 相当）；F0 用 3 帧环缓冲窗。质量：DNSMOS OVRL 4.07 与离线基线持平，WER 6.22% 最佳，说话人相似度 77.81%（微调后 80.34%，低于 Diff-VCTK 84.23%）——相似度是弱项。参数量论文未报（检索摘要称 ~20M）【未确认】。
- 许可证：**无代码无权重**（仅 demo 页）【已确认其缺】。
- 对本项目：**参照（O3 关键正锚点）**。证明全因果流式 VC 在**手机 CPU 级算力**上 70.8ms 端到端可行——150ms 目标在架构层面不再虚无；但闭源闭权重，不能集成，只能作为「RVC 之外存在低延迟架构」的证据与自研参考。

### 3.2 Zero-VC —— 零前瞻流式 VC【已确认（数字）】

- **Zero-VC: Zero-Lookahead Streaming Voice Conversion via Speaker Anonymization** | Yudong Li, Zhizheng Wu 等（Amphion 团队） | **Interspeech 2026**（接收） | arXiv:2606.20218
- 核心思路：用「说话人匿名化扰动（SA）」替代信息瓶颈来压制音色泄漏且保韵律，使表征不依赖未来帧——**严格因果、零前瞻、一帧进一帧出**（因果卷积状态缓存，逐帧 O(1)）。
- 关键数字【已确认，论文】：**算法延迟 20ms**（对照：DualVC3 40ms / RT-VC 47ms / StreamVC 60ms）；16kHz；**RTF 0.063（Intel Xeon Platinum 8468V CPU）**——同表 Seed-VC-Small 0.508、CosyVoice 2.441；音色泄漏 SS-S 0.171 / 目标相似度 SS-R 0.521 双最佳；但 **DNSMOS OVRL 3.044 为四者最低**（质量有代价）；加 80ms 前瞻仅再涨 <3%（SA 消融）。
- 许可证：**无代码无权重**【已确认其缺】；SA 组件来自第三方 DigitalPhonetics/speaker-anonymization。
- 对本项目：**参照（O3 正锚点 + 反偏置提示）**。20ms 零前瞻 + CPU RTF 0.063 把「流式 VC 延迟下限」拉到极低，但注意两点：(a) 测点是服务器 Xeon，非消费 CPU【测点条件】；(b) 音质最低——零延迟与质量当前是明码交换。RTF 数字不可直接外推到 MinGW/ORT 栈【未确认】。

### 3.3 MeanVC 2 —— 开源低延迟流式零样本 VC【倾向】

- **MeanVC 2: Robust Low-Latency Streaming Zero-Shot Voice Conversion** | Guobin Ma, Yuxuan Xia, Lei Xie 等（西北工业大学 ASLP） | **Interspeech 2026**（接收） | arXiv:2606.09050
- 核心思路：未来感受野分块（FRC）调度扩散 Transformer 解码器各层的有界未来上下文 + 通用音色 token 编码器（全局说话人嵌入 + 交叉注意力细粒度检索）。
- 关键数字【已确认（摘要）】：chunk **40ms**；延迟 211ms→**110ms**（vs 前代 MeanVC）；摘要未说明 110ms 是算法延迟还是端到端、测量方式未定义【未确认】；基线仅 MeanVC，无 Seed-VC/StreamVC 横比。
- 许可证：**Apache-2.0**【已确认，GitHub 仓库 README/HF 页】；代码「承诺后续发布」、**HF 权重已在**（huggingface.co/ASLP-lab/MeanVC2）。
- 对本项目：**候选（观察名单）**。当前唯一「低延迟流式 + 干净许可 + 权重可得」组合；110ms 若为算法延迟则占 150ms 预算 73%，实时链仍紧。等代码落地后做 ONNX 导出与真机 RTF 实测再定。

### 3.4 Seed-VC —— 反证锚点（GPL + 延迟超标）【已确认】

- **seed-vc**（零样本 VC/歌声 VC，含实时模式） | Plachtaa | GitHub Plachtaa/seed-vc | **GPL-3.0**【已确认，仓库页】
- 关键数字【已确认，README】：实时模式**算法延迟 ~300ms + 设备侧延迟 ~100ms**；基准表：tiny 模型在 **RTX 3060 Laptop GPU 上 ~430ms**；22.05k（实时/离线/V2）/44.1k（歌声）；无 ONNX/TensorRT 导出（仅 triton-windows JIT ~×6）；README 自述实时强推 GPU。
- 对本项目：**红线禁用（GPL-3.0 代码不可引入）+ O3 反证强化**。同代流式零样本 VC 在 3060 级移动 GPU 上 ~430ms 的一手数字，进一步支持 v1.2 对 O3 的「未证实」定级与 R2 概率上调。其架构（AR LM/codec 系）可作对照阅读。
- 关联：同作者 **Stream-Voice-Anon**（arXiv:2601.13948，2026-02）全因果流式匿名化 VC，定位隐私保护而非音质；许可证未核【未确认】，仅线索。

### 3.5 kNN-VC / OpenVoice —— 生态位补充【已确认】

- **kNN-VC: Voice Conversion With Just Nearest Neighbors** | Matthew Baas, Benjamin van Niekerk, Herman Kamper（Stellenbosch） | **Interspeech 2023** | arXiv:2305.18975 | github.com/bshall/knn-vc，**MIT**【已确认，raw LICENSE 文件】。WavLM-Large 第 6 层特征 kNN 检索（k=4）替换 + HiFi-GAN 声码，转换免训练；16kHz；离线（未涉及流式）；参考音频 ~5 分钟收益饱和。WER 6.29%（LibriSpeech dev-clean，prematched）。
  - 对本项目：**参照**。与 RVC 的 faiss 索引检索同族思想（SSL 特征 + 近邻替换），其「检索式 VC 的上限与限制」可直接迁移到 FR-7 设计（如检索粒度、参考时长收益曲线）。
- **OpenVoice: Versatile Instant Voice Cloning** | MyShell AI & MIT | arXiv:2312.01479（2023-12）| **V1/V2 自 2024-04 起 MIT**【已确认，仓库+HF 页】。音色克隆 + 情感/口音/节奏控制，离线。
  - 对本项目：**参照**。许可干净但任务形态偏 TTS 侧克隆，不是任意输入变声；v1 FR-7 仍以 RVC 管线为主，OpenVoice 不改变该决策。

### 3.6 O3 证据状态更新建议（供主会话裁定）

- 新正锚点：StreamVC 70.8ms（手机 CPU，闭源）/ Zero-VC 20ms + CPU RTF 0.063（闭源）/ MeanVC2 110ms（开源 Apache-2.0，测量口径未定义）。
- 反锚点更新：Seed-VC tiny ~430ms@RTX 3060 Laptop（README 一手）；RVC 系自身仍无 3060 级数字。
- 建议表述：「正反锚点皆缺」→「**架构级正锚点已现（非 RVC 系、无一可直用），RVC 系 3060 级实测仍缺、同代开源流式 VC 在 3060 笔记本上 ~430ms**；O3 闸门与真机脉冲法实测要求不变」。150ms 对「RVC 架构 + 3060」仍是未证实的激进目标；若放宽为「任意 VC 架构 + CPU」，论文级证据已到 70–110ms 区间。

---

## 4. DSP / 经典算法前沿 + CPU 实时推理优化

### 4.1 变调 / 变速（phase vocoder 与神经法）

- **Phase Vocoder Done Right** | Průša & Holighaus | 2022 | arXiv:2202.07382。以「phase-mass」（相位一致性等价类）统一相位锁定与相位交换，解决垂直相位相干与瞬态涂抹的老问题——**近年经典 PV 最实质的改进**（检索计量 ~50 引用【倾向】）。对本项目：**参照**（FR-6 若自研 PV 或评估 SoundTouch 质量边界时的理论标尺；超出 2023 窗口一年，作为背景保留）。
- **SELEBI: 打击乐感知的选择性编辑变速** | arXiv:2602.16421（2026-02）。PV-TSM 上对打击成分分支处理，缓解瞬态涂抹【倾向：仅检索摘要级，未全文核验】。
- 神经路线：**CLPCNet**（arXiv:2110.02360，2021，可控变调/变速声码器，质量可比 DSP 法）；**极端拉伸神经合成**（Fierro & Välimäki，arXiv:2211.16992，2022，超 2× 拉伸）。均超窗口或边缘，仅背景。**结论：变调 DSP 的学术前沿 2023 后基本沉寂，实用进展在工程库侧**。
- **Signalsmith Stretch** | Geraint Luff（Signalsmith Audio）| github.com/Signalsmith-Audio/signalsmith-stretch，**MIT**【已确认，仓库页】。C++11 单头文件多声部变调/变速（ADC22 讲座最终方案）：共振峰补偿、自定义频率映射、tonality limit、split 计算模式、seek/flush 定长 API；FFT 后端可插 PFFFT/IPP/Accelerate；最佳质量区间 0.75–1.5×。
  - 对本项目：**候选（FR-6 升级 / FR-19 实时效果器）**。许可比 SoundTouch（LGPL）更宽松，seek/flush 与 split 计算对实时线程友好；与 NFR-C3（SoundTouch 静态链接合规问题）联动——若切换可顺带消掉该项合规债。需要一次听感对比 + 延迟/开销实测后决策。

### 4.2 回声消除 / 去混响与降噪联合

- **DeepVQE: Real-Time Deep Voice Quality Enhancement（AEC+NS+DR 联合）** | Indenbom, Ristea, Cutler 等（Microsoft） | 2023 | arXiv:2306.03177。跨注意力单一模型同时做 AEC/NS/DR，ICASSP 2023 AEC Challenge 与 DNS Challenge 非个性化赛道 SOTA，已入 Teams 验证。论文 CC BY-NC-ND、无代码【已确认】。具体参数/RTF 摘要未给【未确认】。
- **EchoFree: Towards Ultra Lightweight and Efficient Neural Acoustic Echo Cancellation** | Xingchen Li, ..., Lei Xie（西工大 ASLP） | 2025-08 | arXiv:2508.06271。线性滤波 + Bark 频谱神经后滤波混合框架；**278K 参数 / 30 MMACs**，ICASSP 2023 AEC Challenge 盲测集上匹配 DeepVQE-S 并超过既有低复杂度 AEC；RTF/延迟/硬件未给【未确认】；无代码【已确认其缺】。
- 对本项目：**暂无用（登记）**。FR-11 实时链是「麦→降噪→(变声)→输出」单向链，无远端参考信号，AEC 不在 v1 需求内；若未来做「外放场景全双工」或与通信软件同机混用时回声成痛点，这两个是 2023/2025 的对应锚点。

### 4.3 CPU 实时推理优化（ORT CPU 论文级证据）

- **faster-enhancer.c: A Dependency-Free int8 Runtime for Streaming Speech Enhancement on Commodity CPUs** | Gyeongmin Kim | 2026-07 | arXiv:2607.25350 | 代码 github.com/kdrkdrkdr/faster-enhancer.c，**MIT**【已确认，仓库页】。
  - 做法：FastEnhancer-Medium（48kHz）完整移植为无依赖纯 C/SIMD INT8 运行时（6 档 int8 GEMM 启动期自选），架构与权重不动；逐帧激活范围重算（**免校准集**）、Winograd F(2,3)、fp16 跨级状态、GRU+反量化融合尾。
  - 关键数字【已确认，摘要】：**RTF 0.069（Apple M2 单核）vs 同机 ORT fp32 图 0.230——3.3×**；Galaxy S23+（骁龙 8 Gen 2）RTF 0.096；INT8 精度损失 **−0.006 PESQ / −0.08dB SNR**（824 句 VB-DMD）；启动后零分配。
  - 对本项目：**论文级 INT8/CPU 证据 + 工程范式参照**。(a) 证明 ORT fp32 路径上还有 ~3× 的专用优化空间，但代价是绑定单一模型的运行时——与本项目「多引擎注册表 + ORT 统一后端」架构相悖，**不宜效仿**；(b) 其「逐帧激活范围重算、免校准」思路可用于我们的 INT8 量化管线；(c) 3.3× 加速是 M2/SIMD 专用实现的上界参考，MinGW+ORT 1.30 上不可直接引用【未确认】。
- **ORT 官方量化文档**（一手）：CPU 支持 U8U8/U8S8/S8S8，**S8S8+QDQ 为默认**（性能/精度均衡）；AVX-512 VNNI 硬件上有显著加速（「up to 6×」说法源自社区/厂商博客，量级参考【倾向】）。
- 交叉证据（其他论文的 ORT CPU 实测）：FastEnhancer 全系（§1.1，Xeon/M1 单线程）；Band-SCNet RTF 0.478（i7-13700H 单线程，非 ORT、框架未注明）；aTENNuate（无 RTF）；DFN3 0.19（论文，笔记本单线程，既有调研）。
- 对本项目落点（M4 前置实验，非现在动手）：ORT 1.30 CPU EP 上对 GTCRN/DFN3/FastEnhancer ONNX 做 QDQ INT8 量化前后 RTF/精度对比（Windows 真机、单线程绑定），VNNI 可达性按机型登记；结论回填 NFR-L* 实测表。

---

## 5. 对路线图的汇总建议（供主会话裁定，不构成直接修改）

1. **FR-4（M2）**：DFN3 集成照旧（质量档）；将 FastEnhancer（MIT+ONNX+48k 变体）列入快速档接棒候选与 M4 实时链降噪选型清单，准入条件 = Windows/ORT 冒烟 + VB-DMD 复测 PESQ。
2. **FR-15**：高质量档参数更新——Moises-Light（5M/stem，cSDR 9.96）证明轻量化质量档存在，但无代码；维持「官方导出工具自导 / 社区成品逐个核验」路线。
3. **§4.4 Won't「实时人声分离（算力不现实）」**：论文级反例已现（Band-SCNet CPU 单线程 RTF 0.478）；建议改表述为「暂缓：头部方案无代码权重 + 92ms 延迟与 150ms 预算冲突」，是否改档位由用户决策。
4. **O3（§10）**：见 §3.6 建议表述；R2、变声链最坏算术结论不动。
5. **FR-6/NFR-C3**：评估 Signalsmith Stretch（MIT）替换 SoundTouch（LGPL 静态链接合规债顺带消除），需听感+延迟实测支撑。
6. **M4 前置**：ORT CPU INT8（QDQ S8S8）量化实验排入实时化准备项；faster-enhancer.c 的免校准逐帧范围重算思路可借鉴。

## 6. 反证与未收敛盲点（诚实登记）

- **反证执行情况**：对每条「实时/RTF」声明核了测点条件——Band-SCNet（i7-13700H 单线程，真 CPU 测）✅；FastEnhancer（Xeon/M1，ORT 单线程）✅；Zero-VC RTF 0.063 是**服务器 Xeon Platinum**，外推消费 CPU 不可靠 ⚠；StreamVC 是手机 CPU 但闭源不可复现 ⚠；Stream.FM 只说「消费级 GPU」无型号 ⚠；Seed-VC「real-time」= 300+100ms，对 150ms 目标实为**伪实时** ⚠；URGENT 冠军「SE」无延迟约束，不能当实时证据 ⚠；Moises-Light 无任何速度测量，纯离线 ⚠。
- **「DNS 冠军=实时 SOTA」的隐性假设被推翻**：DNS 系列止于 2023，后继 URGENT 不设延迟预算——引用挑战赛结果作为实时证据链的做法此后失效。
- **BS-RoFormer 数字口径分歧**（10.66 vs 10.78 Vocals）：对比实验必须钉死 song-level vs chunk-level museval 口径。
- 未核验项：StreamVC 参数量（~20M 出处为检索摘要）；SELEBI 全文数字；MeanVC2 110ms 的测量定义；Stream-Voice-Anon 许可证；CoFi-Lite 代码；SCNet 官方代码是否存在；Apollo 参数量（README 图内文字）；aTENNuate ONNX 可导出性；TF-MLPNet 的 3.6ms/帧数字（另一检索口径，摘要为 6ms 块@GAP9）。
- 本文档未覆盖（超范围）：歌声转换（so-vits 系）新一轮核验、水印/深伪检测（NFR-C5 相关，既有调研已覆盖伦理声明实践）。

## 7. 结论分级汇总

- 【已确认】（一手信源直接证明）：FastEnhancer 全部数字与 MIT/ONNX/ICASSP 2026；aTENNuate 数字与 Apache-2.0；Band-SCNet 全部数字（含 CPU RTF 0.478@i7-13700H 单线程）；Mel-RoFormer 官方 Table 1；Moises-Light 数字；Apollo 定位与 CC BY-SA；StreamVC 延迟链（60ms 架构/70.8ms 端到端 Pixel 7）；Zero-VC 数字（20ms/RTF 0.063@Xeon 8468V）；MeanVC2 40ms chunk/110ms 与 Apache-2.0；Seed-VC GPL-3.0 与 ~300+~100ms/430ms@3060 Laptop；kNN-VC MIT 与方法定位；OpenVoice MIT（2024-04 起）；faster-enhancer.c 全部数字与 MIT；Signalsmith Stretch MIT 与能力集；URGENT 两届要点；DNS 系列终止于第 5 届。
- 【倾向】：CoFi-Lite 效率结论（无 RTF 佐证）；Stream.FM 生成式流式趋势；TF-MLPNet/嵌入式 LiSenNet INT8 线索；Phase Vocoder Done Right 影响力计量；VNNI「up to 6×」量级。
- 【未确认】：见 §6 未核验项清单；所有跨硬件 RTF 外推（Xeon→消费 CPU、M2→x86 等）一律不作为结论使用。

## 参考链接清单

### 战场 1：实时语音增强
- FastEnhancer 论文：https://arxiv.org/abs/2509.21867 ｜代码：https://github.com/aask1357/fastenhancer（MIT，含 ONNX Releases 与 ORT 流式文档）
- aTENNuate 论文：https://arxiv.org/abs/2409.03377 ｜ISCA 页：https://www.isca-archive.org/interspeech_2025/pei25_interspeech.html ｜PyPI：https://pypi.org/project/attenuate/（Apache-2.0）｜代码：https://github.com/Brainchip-Inc/aTENNuate
- CoFi-Lite：https://arxiv.org/abs/2607.10142
- Stream.FM：https://arxiv.org/abs/2512.19442
- URGENT 2025：https://arxiv.org/abs/2505.23212 ｜ICASSP 2026 版：https://arxiv.org/abs/2601.13531 ｜官网：https://urgent-challenge.github.io/urgent2025
- DNS Challenge（第 5 届，ICASSP 2023，系列终点）：https://www.microsoft.com/en-us/research/academic-program/deep-noise-suppression-challenge-icassp-2023
- TF-MLPNet：https://arxiv.org/abs/2508.03047 ｜嵌入式 LiSenNet 重设计（线索）：https://arxiv.org/abs/2609.29866

### 战场 2：音源分离
- Band-SCNet（ISCA 全文 PDF）：https://www.isca-archive.org/interspeech_2025/yang25d_interspeech.pdf（DOI 10.21437/Interspeech.2025-448）
- HS-TasNet（经 Band-SCNet Table 1 转引；原始为 ICASSP 2024，Venkatesh 等）
- SCNet：https://arxiv.org/abs/2401.13276 ｜社区实现（非官方，MIT）：https://github.com/amanteur/SCNet-PyTorch
- BS-RoFormer / Mel-RoFormer：https://arxiv.org/abs/2310.01809（Mel-RoFormer，ISMIR 2023 LBD，含两者对照表）
- Moises-Light：https://arxiv.org/abs/2510.06785
- Apollo：https://arxiv.org/abs/2409.08514 ｜代码：https://github.com/jusperlee/apollo（CC BY-SA 4.0）
- （衔接既有调研）MDX/UVR/audio-separator：`research-mdx-uvr-audio-separator.md`；ZFTurbo 导出工具（MIT）：https://github.com/ZFTurbo/Music-Source-Separation-Training

### 战场 3：语音转换
- StreamVC：https://arxiv.org/abs/2401.03078 ｜demo：https://google-research.github.io/seanet/stream_vc/
- Zero-VC：https://arxiv.org/abs/2606.20218 ｜demo：https://amphionteam.github.io/Zero-VC-demo/
- MeanVC 2：https://arxiv.org/abs/2606.09050 ｜代码：https://github.com/ASLP-lab/MeanVC2（Apache-2.0）｜权重：https://huggingface.co/ASLP-lab/MeanVC2
- Seed-VC：https://github.com/Plachtaa/seed-vc（GPL-3.0；README 延迟数字一手）
- kNN-VC：https://arxiv.org/abs/2305.18975 ｜代码：https://github.com/bshall/knn-vc（MIT）
- OpenVoice：https://arxiv.org/abs/2312.01479 ｜代码：https://github.com/myshell-ai/OpenVoice（MIT，2024-04 起）
- Stream-Voice-Anon（线索）：https://arxiv.org/abs/2601.13948 ｜https://github.com/Plachtaa/StreamVoiceAnon
- （衔接既有调研）RVC/ContentVec/延迟证据：`research-rvc-voice-conversion.md`、`falsify-licenses.md`

### 战场 4：DSP 与 CPU 优化
- Phase Vocoder Done Right：https://arxiv.org/abs/2202.07382
- SELEBI（线索）：https://arxiv.org/abs/2602.16421
- CLPCNet：https://arxiv.org/abs/2110.02360 ｜极端拉伸神经合成：https://arxiv.org/abs/2211.16992
- Signalsmith Stretch：https://github.com/Signalsmith-Audio/signalsmith-stretch（MIT）
- DeepVQE：https://arxiv.org/abs/2306.03177 ｜EchoFree：https://arxiv.org/abs/2508.06271
- faster-enhancer.c：https://arxiv.org/abs/2607.25350 ｜代码：https://github.com/kdrkdrkdr/faster-enhancer.c（MIT）
- ONNX Runtime 量化官方文档：https://onnxruntime.ai/docs/performance/model-optimizations/quantization.html
