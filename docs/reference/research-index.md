# 外部调研索引与结论对齐

- 建立：2026-10-14（7 路并行调研完成后收尾）
- 性质：**活文档**——后续新调研在此追加索引行与结论对齐
- 方法：7 个并行调研代理，只取一手信源（官方论文/源码/文档/许可证原文/官方 issue），逐条标注【已确认】【倾向】【未确认】；各文档自带参考链接清单
- 用途：REQUIREMENTS/spec 修订的决策依据汇总；「建议修订清单」需用户确认后才落基线

## 1. 调研文档一览

| 文档 | 主题 | 服务对象 |
|---|---|---|
| `research-gtcrn-sherpa-onnx.md` | GTCRN 论文/sherpa-onnx 源码级细节/对齐约定/许可证 | WP-1.1 parity、O1 |
| `research-mdx-uvr-audio-separator.md` | MDX 输出布局实测/官方参数表机制/处理链/UVR 模型生态 | WP-1.1/1.4、FR-15 |
| `research-deepfilternet3.md` | DFN3 架构/ONNX 状态约束/官方 golden 夹具/集成路径 | FR-4（M2） |
| `research-rvc-voice-conversion.md` | RVC 现状/ContentVec 许可证溯源/实时延迟证据/ONNX 路线 | O2、O3、FR-7（M3） |
| `research-realtime-audiostack.md` | WASAPI 延迟/miniaudio/VB-CABLE 条款/钟漂/150ms 预算 | FR-11（M4）、NFR-L* |
| `research-fft-libraries.md` | pffft/kissfft/pocketfft 精度与 twiddle 实践 | WP-1.3（fft-rewrite） |
| `research-model-mgmt-llm-infra.md` | HF 下载规范/llama.cpp 结构化输出/Qwen GGUF/whisper.cpp/silero/打包 | FR-8/FR-24/FR-A*、O4、NFR-M4 |
| `research-streaming-pipelines.md` | 7 流式框架（SoX/GStreamer/JUCE/Web Audio/miniaudio…）× IStreamEngine 对照 + v2 接口 R1–R8 草案 | 架构审视（architecture-review.md）、NFR-L2、NFR-M1 |
| `research-realtime-threading.md` | RT 铁律一手原文（Bencina/PortAudio/JUCE）/MMCSS/SPSC 队列/双线程模型/预算分解/注入法 | M4、NFR-L1/L2/L3、O3、架构审视 |
| `research-qt-audio-apps.md` | Mixxx/LMMS/Hydrogen/Audacity 四仓实践 + Qt 官方线程文档 26 条；现状惯用项确认 + 隐患 H1–H8 | 架构审视、FR-24、A11–A13 |
| `research-cpp-engine-patterns.md` | GCC 13/MinGW C++23 可用表/注册表模式（LLVM 式）/processInto 形态/CMake 现代惯例 | NFR-M1、FR-24、NFR-M4、A14 |
| `research-cpp11-26-evolution.md` | C++11→26 面向音频/DSP/实时的特性筛选 + C++26 编译器落地快照 + 采用路线（含「已该用而未用」行号审计） | C++ 标准档位决策、v2 接口、WP-1.3 二期（simd）、NFR-L2 |
| `research-audio-algorithms-frontier.md` | 音频算法前沿：FastEnhancer（FR-4 候选）/Band-SCNet（实时分离反证）/VC 延迟新锚点（O3）/INT8 量化/Signalsmith Stretch/DNS 挑战赛现状 | FR-4、FR-5、O3、NFR-C3、NFR-P5 |

> 2026-10-14 追加上 4 行（架构审视批，方法同前：一手信源 + 【已确认/倾向/未确认】标注；结论已汇入 `docs/spec/architecture-review.md` §4）。当日再追加末行（音频算法前沿批，方法同前；基线修订建议 §7.5 五条已于同日用户确认，落地 REQUIREMENTS v1.4）。

## 2. 开放问题证据状态（REQUIREMENTS §10）

| # | 调研前认知 | 调研后证据 | 建议 | 残余盲点 |
|---|---|---|---|---|
| O1 GTCRN 许可证 | ⚠ 待核验（印象 Apache-2.0） | 上游仓库 LICENSE = **MIT**（出处链：sherpa run.sh → 上游 gtcrn_stream.py 导出）【倾向】 | 关闭，按 MIT 登记 | sherpa release 页无逐字模型许可字样（发版前可再请官方确认一次） |
| O2 ContentVec 替代 | CC-BY-NC-4.0、高风险 R1、须找替代 | **前提被推翻**：仓库 MIT + 作者 issue #34（2025-03-18）确认预训练权重同 MIT；CC-BY-NC 说法两轮定向溯源无一手出处【已确认】 | 关闭 O2，R1 降级；替代方案降为「效果选项」（so-vits 系可换编码器 8 类） | Meta 原始 HuBERT-base 权重无显式许可文本（RVC 用 ContentVec 再训练版，不直接用原始权重；商业化前法务复核传递链） |
| O3 RVC 延迟实测 | 未知能否 ≤150ms | **RVC 官方 README** 声明端到端 170ms / ASIO 90ms（未标 GPU；⚠ 2026-10-14 反证澄清归属：非 w-okada 数字，v1.1 基线归错了）；RVC VST 报告延迟 = 2×Block（默认 260ms，理论下限 40ms）；**RTX 3060 专属数据不存在（正反皆无锚点）**；w-okada 反例 #796（i9+RX6800XT 实时不可用）；捕获链 OEM 现实 20–45ms/向 | 保留闸门，表述下调为「正反锚点皆缺、150ms 未证实；必须真机脉冲法实测」（v1.1「激进可达」表述已随 v1.2 修订落地） | 3060 级实测数字（只能自测）；VB-CABLE 环回已收窄 10–30ms（官方指南+相邻实测），仍须实测 |
| O4 Qwen 再分发许可 | 待选型核验 | Qwen3 全系 **Apache-2.0**（官方 README）；官方 GGUF 与 unsloth 量化包均 Apache-2.0 可再分发【已确认】 | 关闭，选型限定官方/unsloth 源 | 4B 级 CPU 工具调用可靠性无独立评测（R8 风险仍在，靠自家 eval 集） |

## 3. 新确认的实现缺陷（M1 输入）

| 缺陷 | 证据 | 影响 | 落点 |
|---|---|---|---|
| MDX 输出按 frame-major 读，实际 plane-major | onnx 实测图签名 + audio-separator 源码 | **Voc_FT 现产出废音** | `separator-output-validation.md`、goal.md SG5 |
| `nFft=dimF*2` 推导错误（Voc_FT 实为 7680，参数须查 MD5→UVR 官方表） | audio-separator `separator.py:526/537-541` + 官方表命中 | 同上 | 同上 |
| GTCRN parity 数值变数：sherpa online 为 double DFT | sherpa 源码 | −60dB 判据的主要数值风险，FFT 重写优先级坐实 | goal.md 重要信息 |
| 官方 MDX 前端细节差三处（前 3 频点置零/compensate 1.021/chunk 边界策略） | audio-separator 源码 | parity 用例设计 | goal.md SG5 修复清单 |

## 4. 规格回填记录（本次调研已同步的文档）

- `docs/spec/canonical-rate-48k.md` §2：补 DFN3 原生 48k 动机（research-deepfilternet3）。
- `docs/spec/fft-rewrite.md` §2：twiddle 表业界对照、chirp 整数索引查表（pocketfft 式）、6144→7680 工作点订正、实时热点逃生门（research-fft-libraries + research-mdx）。
- `docs/spec/separator-output-validation.md`：整节重写——图签名实测、模型参数表机制、plane-major 读法、前端对齐项（research-mdx）。
- `docs/acceptance/engine-parity/goal.md`：D-06 决策（golden 取 sherpa online CLI）；重要信息区登记 sherpa 1.13.8 / audio-separator v0.47.0 锁定、对齐约定、图签名、SG5 修复清单。

## 5. 修订清单（**已确认落地**，2026-10-14 用户确认 → REQUIREMENTS v1.1 + 开发计划/CLAUDE.md 同步；证据见各文件修订记录）

1. §6 红线表 GTCRN 行：⚠ 待核验 → MIT（倾向），对策改为发版前向 sherpa 官方要一句书面确认。
2. §6 红线表 ContentVec 行 + §8 R1：CC-BY-NC-4.0/高×高 → 推翻为 MIT；R1 降级为低概率×中影响，对策改「商业化前法务复核权重传递链」。
3. §10：O1/O2/O4 按上表关闭；O3 表述更新（保留闸门）。
4. §5.1 延迟预算表：输入捕获缓冲注 WASAPI 官方数字（共享小周期 ~10ms，128 采样=2.66ms@48k 可行）；miniaudio Win10+ 自动 IAudioClient3 为选型依据。
5. FR-11 相关：虚拟声卡策略落定——VB-CABLE **可随包分发**（官方条款：标注 vb-cable.com 来源 + donationware 提示 + 保留付费通道；A+B/C+D 组合禁捆；企业场景按量授权），或按官方推荐引导用户自装；开源替代无干净许可方案（OBS-Virtual-Mic-Plugin 为 GPL，红线禁用）。
6. FR-15 注记：唯 MDX-Net 有官方 ONNX；BS/Mel-Roformer/MDX23C/Demucs 为 PyTorch 权重（社区 ONNX 导出存在、需逐个核验）——高质量档路线受此硬约束。
7. CLAUDE.md P0 债 #3 更新为已实锤的双 bug（本次已顺手完成，见该文件）。

## 6. 未收敛盲点汇总（跨文档，供后续关闭）

- 发版前法务类：TRvlvr 官方模型源条款原文；sherpa release 模型许可书面确认；deepfilter-rt 状态化手术代码无 LICENSE（思路可参考、代码不可复制）。
- 待实测类：RTX 3060 级 RVC chunk 延迟（O3 本体，正反锚点皆缺）；VB-CABLE 环回延迟（已收窄 10–30ms，仍须实测）；DFN3 MinGW 构建；6144/7680 点 FFT 重写后耗时对比；OEM 声卡上 IAudioClient3 小周期可达性（逐机型）。
- 证据缺口类：Qwen 4B CPU 工具调用独立评测；「本地 LLM 计划 + 确定性执行」桌面同构先例（确认不存在，属自研组合）；GSL complex radix-2 递推路径未定位（real_radix2 已足）；NR 源码本体未镜像（以 FFTW 官方点名描述为准）；hf-mirror 海外出口 vantage（308 回源，国内视角待测）。

## 7. 反证审查（2026-10-14，6 路代理，假设结论为错专找反例）

方法：每条载荷性结论指定专门攻击方向（含许可证原文逐字核对、驱动包实下载解包、7 实现穷尽对照、当前 main 源码精读、官方数字溯源），找不到反证只记「暂未找到反证」。报告：`falsify-{licenses,mdx,gtcrn-golden,realtime,hf-llm,fft-dfn3}.md`。

### 7.1 判级总表（18 项目标）

| 目标 | 判级 | 一句话 |
|---|---|---|
| ContentVec=MIT | **站得住（强化）** | 四路反证未命中；HuBERT 系教师标签非权重初始化（无继承链）；Meta hubert-* 全 apache-2.0（2021 起） |
| GTCRN=MIT | 站得住 | 仓库身份经 arXiv 作者名单绑定；LICENSE 非事后补加；维持【倾向】+发版前书面确认 |
| Qwen3 全系 Apache-2.0 | **需修正** | Omni 例外（license:other 无 LICENSE 文件）；2026 已有 Qwen3.8-Max/Flash-Next 转自定义许可、同代混用——「逐仓库核对」升为硬规则 |
| VB-CABLE 可随包分发 | **需修正** | 官方驱动包 EULA 原文禁「未经同意集成进其他软件安装流程」——原样拷贝/引导自装 OK，集成进安装器需书面同意 |
| MDX plane-major | 站得住 | 7 实现穷尽（含独立 C++ 复刻），零 frame-major 反例 |
| Voc_FT n_fft=7680 | 站得住（独立性打折） | UVR5 GUI 与 audio-separator 共用同一张表；另有 UVR 5.4 硬编码 + kuielab configs 佐证 |
| :3 置零 / compensate | 站得住 | UVR5 GUI master 行级实锤；但属 5.5 世代行为（5.4/kuielab 无） |
| chunk 边界 | **需修正** | 两家默认不同（step=195840 vs 253440）——parity 钉死 audio-separator 默认参数集 |
| 唯 MDX 有官方 ONNX | **需修正** | ZFTurbo MSS_ONNX_TensorRT（MIT）官方导出脚本存在；但官方不发成品权重 |
| MDX onnx 无 metadata | 站得住 | 5 个官方原件实测 metadata_props 全空 |
| sherpa online 对齐约定 | 站得住 | 「滞后 1 hop 发射」非「吞帧」，源码判据闭环；我们 aidenoiser 同构，SG4 首查项闭环 |
| sherpa 1.13.8 存在 | 站得住 | PyPI+GitHub+master 三源互证（2026-09-10，当前最新） |
| online double DFT | 站得住 | 精确化：端点 float，参考侧非全 double；余量仍 4 个数量级 |
| offline 禁作 golden（D-06） | 需修正（收紧） | 差异经因果状态递推传播到全部帧，无互替段——禁令不可放宽 |
| WASAPI ~10ms/向 | **需修正** | 小周期 OEM 决定、默认 10ms；实测钳制 480 帧、共享实测 ~45ms/向——预算按 20–45ms 计 |
| miniaudio 自动 IAudioClient3 | 站得住 | 源码核实；但「自动启用 ≠ 拿到小周期」 |
| RVC 150ms 区间证据 | **原证据不足+归属修正** | 170/90 出自 RVC README（此前误归 w-okada）；w-okada#796 反例；3060 锚点双向缺失 |
| HF「照抄」下载集 | **部分被推翻** | HF 现行实现无跨运行续传（Xet 不可续）；自研 resolve+Range+sha256 路线实测站得住；镜像 CAS 绕过 HF_ENDPOINT |
| llama.cpp 三通路 | 需修正 | server 两通路属实；进程内头文件级复用已失实（实现移入 .cpp），移植成本上修 |
| whisper -ml 1 词级 | 需修正 | 启发式实验特性，已知错例；高精度需 -dtw/外部对齐 |
| FFTW 点名递推 | 站得住 | 逐字核实，原文比转述更直接；本项目 float 链式复乘=最差档，重写论证加固 |
| 「业界无递推实现」 | 需修正（限定） | GSL（Buneman 稳定）/NR（double）存在；四家点名库属实——措辞改「主流库一致选表」 |
| DFN3 ONNX 无状态 | 站得住+需修正 | 官方图无状态（仓库休眠仍无）；但社区 deepfilter-rt 已验证状态化手术（47.7dB，无 LICENSE 不可复制） |
| DFN3 lsnr/48k | 站得住（补强） | −15/35/35 四方一致（−10/30/20 仅 API 出厂默认）；48k 全仓零例外 |

### 7.2 本次反证落盘的文档修正

- `research-rvc-voice-conversion.md`：HuBERT 关系 3 处修正（教师标签非初始化、Meta apache-2.0 闭合）、O3 归属澄清 + w-okada#796 反例。
- `research-mdx-uvr-audio-separator.md`：反证补强块（6 条）+ Politrees 副本等价性闭环。
- `research-gtcrn-sherpa-onnx.md` 对应结论经 `falsify-gtcrn-golden.md` 复核未改；`goal.md` 回填（首查项闭环、D-06 收紧、golden 执行 5 注意）。
- `research-fft-libraries.md`/`docs/spec/fft-rewrite.md`：递推存在性限定 + 本项目最差档论证加固。
- `research-deepfilternet3.md`：lsnr 四方坐实、df_dec shape 勘误、deepfilter-rt 状态化路径。
- `research-realtime-audiostack.md`：WASAPI OEM 修正、150ms 预算重算、VB-CABLE 10–30ms、Qt 证据力注记。
- `research-model-mgmt-llm-infra.md`：HF 续传推翻 + 自研路线定位、llama.cpp 移植成本、-ml 1 精度注记。
- `docs/spec/separator-output-validation.md`：查表值断言（dim_t 非全系 256）+ parity 参数集钉死。

### 7.3 REQUIREMENTS v1.2 修订建议（**已确认落地**，2026-10-14 用户确认 → REQUIREMENTS v1.2 + 修订记录；逐条对应见该文件 §5.1/§6/§8/§10/FR-8/FR-15/FR-A6/NFR-C4）

1. §5.1 延迟预算表重写：拆软件内链（降噪/变声两列）+ 用户侧链（虚拟声卡 10–30ms、消费端）；捕获预算按 OEM 现实 20–45ms/向（v1.1 的「IAudioClient3 官方 ~10ms」注记改为「特定驱动可达上限，非承诺」）；变声链显式最坏算术；NFR-L1 注明口径（软件内 vs 端到端）。
2. §10 O3：170/90ms 归属改 RVC README + 补 w-okada#796 反例；「激进可达」下调为「未证实（正反锚点皆缺），必须真机脉冲法实测」。
3. §8 R2 概率 中→高（O3 证据状态恶化 + 捕获链 45ms 现实 + 虚拟声卡环回非零头）。
4. §6 Qwen 行 + O4 注 Omni 例外与 2026 自定义许可先例；选型硬规则「逐仓库核对 license 文件，排除例外」。
5. §6 VB-CABLE 段收窄：原样拷贝/引导自装 OK；集成进安装器需 VB-Audio 书面同意（EULA 原文）。
6. §6 ContentVec 行残余更新：HuBERT=apache-2.0 已闭合、无权重继承链（R1 对应微调）。
7. FR-15 注改：官方导出工具存在（ZFTurbo MSS_ONNX_TensorRT，MIT）、官方不发布成品、社区成品核验或自导。
8. FR-A6 注：whisper.cpp `-ml 1` 词级时间戳为启发式实验特性，高精度需 `-dtw` 或外部对齐。
9. NFR-C4/FR-8 注：Xet 仓库 CAS 流量绕过 HF_ENDPOINT，镜像兼容按 resolve 契约实测；HF 官方已不做跨运行续传，FR-8 为自研纯 HTTP 下载器（已实测支持）。

### 7.4 REQUIREMENTS v1.3 修订（**已确认落地**，2026-10-14 用户确认；源头 = `docs/spec/architecture-review.md` 架构审视，其外部证据即上 4 行调研文档）

1. §8 新增 R12（转码内存无界破 NFR-P4，A1 已热修待 Windows ≥2h 素材 RSS 复验）、R13（引擎异常屏障缺失可致 std::terminate，A2 未修随 WP-1.0 收尾）；§6 组件选型补 AGPL 同禁（JUCE AbstractFifo 禁用；SPSC 自研或 rigtorp/readerwriterqueue）。
2. 同批热修 A11（关窗假死）/A12（流复制不可取消）/A14（override 卫生项）已进源码，待 Windows CLI 冒烟；开发计划增 WP-1.0 热修包，CLAUDE.md P0 债清单增 #8/#9。

### 7.5 REQUIREMENTS v1.4 修订（**已确认落地**，2026-10-14 用户确认 5 条全收 → REQUIREMENTS v1.4 + 开发计划 M2/M4 段同步；源头 = `research-audio-algorithms-frontier.md`）

1. §4「不做」清单：`实时人声分离（算力不现实）` → `实时人声分离（暂缓——Band-SCNet 已证 CPU 实时可行方向，RTF 0.478，见调研文档）`。反证性质：原表述把「当前不排期」写成了「不可能」，与 2026 证据冲突。
2. §10 O3 / §8 R2 锚点补充：新增 StreamVC 70.8ms【已确认】、MeanVC2 110ms Apache-2.0【已确认】、Zero-VC 20ms（闭源，仅论文）正锚点；Seed-VC 为 GPL-3.0 **禁用**。闸门本身（真机脉冲法实测）不变，仅改「正反锚点皆缺」的证据描述。
3. FR-4（M2 AI 降噪档）候选增补：FastEnhancer（ICASSP 2026，MIT，流式 ONNX，1-hop 延迟 6.25–16ms，ORT CPU RTF 0.012–0.03）与 DFN3 并列评估——兼供实时链参考。
4. NFR-C3（SoundTouch LGPL 静链合规）新增解法候选：Signalsmith Stretch（MIT）替换评估，许可问题随之消失。
5. M4 增 INT8 量化前置实验（调研证据：GTCRN 级模型 CPU 加速 3.3×），为实时链预算兜底。

> 落地traceability：① → REQUIREMENTS §4.4（注：当日用户随后明确否决该功能，终态「不做——用户决策」，「暂缓」措辞作废）；② → §8 R2 + §10 O3；③ → FR-4 行 + 开发计划 M2/§3；④ → NFR-C3 + §6 表；⑤ → 开发计划 §4 M4 条目 + R2 对策。逐字对应见 REQUIREMENTS 修订记录 v1.4 行。
