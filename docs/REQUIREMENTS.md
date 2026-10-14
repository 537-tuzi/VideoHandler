# VideoHandler 需求文档

- 版本：v1.4（需求基线）
- 日期：2026-10-14
- 状态：已评审通过（决策 D1–D7 全部锁定）
- 说明：本文件是后续所有迭代的对照基准。需求 ID（FR-x / NFR-x / R-x）一经发布不得复用，变更需新增修订记录。

---

## 0. 决策记录

| # | 决策 | 内容 | 影响范围 |
|---|------|------|---------|
| D1 | 产品定位 | 免费公开发布（倾向开源） | 许可证为硬约束；需安装包、模型下载管理、双语、崩溃日志 |
| D2 | 实时标准 | 按最严预算设计：全链路 ≤150ms | 架构以实时链路为主导；离线功能复用同一引擎接口 |
| D3 | 硬件底线 | NVIDIA GPU 8GB（参考机 RTX 3060） | 单 CUDA 版本；CPU 仅保底非 AI 功能 |
| D4 | 训练边界 | 外挂 Python 训练器（方案B） | 主程序零训练代码；模型以 ONNX + 索引文件交接 |
| D5 | Agent 版本 | v1.x 落地 | v1 只做铺垫（工作区版本化 + 无头命令层），Agent 本体后置 |
| D6 | LLM 后端 | 本地优先 + 可选云 | 默认内嵌本地小模型（Qwen 系量化，CPU 推理）；设置中可填 OpenAI 兼容 key 显式开启云增强 |
| D7 | Agent 执行安全 | 每步确认 | 计划整体生成，执行逐步设闸；基础设施步骤自动不设闸；确认为策略层设计，未来可配置化 |

---

## 1. 产品定位与用户画像

**一句话**：本地运行的一站式音频处理软件——离线批处理（降噪/分离/变声/视频音轨）+ 实时处理（降噪/变声）+ 音色训练工作流 + 对话式 Agent 自动编排，全程不上云（云 LLM 为用户显式选配）。

**差异化**（针对「GitHub 上无此类成熟软件」的空档）：现有工具均为单点（UVR 只分离、w-okada 只变声、Audacity 插件不成体系）且全部 Python 技术栈（部署即劝退）。本产品 = **一站式 + 原生单二进制分发 + 管线串联 + 对话式编排**。

**用户分层**：

| 优先级 | 用户 | 核心诉求 | 主用功能 |
|---|---|---|---|
| P1 | 视频创作者 | 视频人声处理后放回 | 视频抽轨、离线降噪/变声、（v1.x）合回 |
| P2 | 直播/游戏玩家 | 实时变声给 OBS/Discord | 实时链路、虚拟声卡路由、模型管理 |
| P3 | 翻唱/配音爱好者 | 提干声、训自己音色、翻唱 | 分离、训练器、离线变声 |
| P4 | 播客制作者 | 批量清理与内容级剪辑 | 批处理、AI 降噪、（v1.x）Agent 剪辑 |

---

## 2. 用户故事

- **US1**：视频创作者把 vlog 中人声变成另一音色，导出后与原视频时间轴对齐。
- **US2**：直播玩家选中音色模型，对麦克风说话，Discord/OBS 里听到变声结果，延迟无感。
- **US3**：翻唱爱好者从一首歌提取干声，用训练好的自己的音色生成翻唱。
- **US4**：播客主拖入 20 个录音文件排队降噪，之后统一取结果。
- **US5**：用户提供 10 分钟目标音色素材，训练器产出可被主程序加载的模型。
- **US6**：用户从电影提取对白（人声分离）做配音素材。
- **US7**：用户对 Agent 说「去掉这段音频里叙述故事的部分」，Agent 转写→展示标记出的叙述区段（可核验）→用户逐步确认→输出剪辑版，原件不动。
- **US8**：用户对 Agent 说「把这批文件都降噪然后提伴奏」，Agent 生成批处理计划逐步执行。

---

## 3. 架构基线

```
   ┌────────────┐   ┌────────────┐
   │  GUI 客户端 │   │ Agent 客户端 │（v1.x：聊天面板/意图路由/计划生成/逐步闸门）
   └─────┬──────┘   └─────┬──────┘
         └───────┬────────┘
         ┌───────▼────────┐
         │  无头命令层      │ 每个引擎/管线可 JSON 进 JSON 出（同时是 CI 测试面）
         │ （引擎注册表）    │ 新增引擎注册即枚举，不改 GUI/服务层
         └───────┬────────┘
         ┌───────▼────────┐
         │ MediaService    │ 任务队列/线程调度/取消
         └───────┬────────┘
            IStreamEngine（音频流引擎） + ASR/VAD/EDL 等新引擎
```

核心原则：

1. **GUI 与 Agent 是同一命令层的两个客户端**，能力完全对等，Agent 无特权。
2. **LLM 只做计划，工具确定性执行**：同命令 → 同计划 → 同管线，可复现可测试。
3. **不可破坏性靠机制不靠自觉**：原件只读 + 工作区版本化 + 计划三道校验闸。
4. **实时与离线共用引擎接口**（`IStreamEngine` 的 `process/flush` 延伸为实时友好的零分配变体）。

---

## 4. 功能需求

### 4.1 v1.0（Must —— 出货线）

| # | 需求 | 说明 | 现状 |
|---|------|------|------|
| FR-1 | 媒体载入与探测 | 含多音轨视频的轨道选择（现仅取 best stream） | 需改 |
| FR-2 | 视频/音频分离 | 抽音轨（流复制）+ 抽无声视频 | ✅ 已有 |
| FR-3 | 离线 DSP 降噪 | 50/60Hz hum 参数化 + 高通 | 需改（60Hz） |
| FR-4 | 离线 AI 降噪 | 快速档 GTCRN + 质量档 DeepFilterNet3；注（2026-10-14 调研，v1.4）：FastEnhancer（ICASSP 2026，MIT，流式 ONNX，1-hop 延迟 6.25–16ms、ORT CPU RTF 0.012–0.03）列为质量档**并列候选**，M2 与 DFN3 一起评估选型（证据 `docs/reference/research-audio-algorithms-frontier.md`）；其流式低延迟特性兼为 FR-11 实时链参考 | 半有（缺质量档） |
| FR-5 | 离线人声分离/伴奏 | MDX 快速档 | ✅ 已有（需 parity 验证） |
| FR-6 | 离线变调（不变速） | SoundTouch | ✅ 已有 |
| FR-7 | 离线 AI 变声 | RVC：ContentVec 特征 + F0 + 索引检索 + 音高偏移，导出 | **无，最大新增** |
| FR-8 | 模型管理器 | 首次使用下载（HF/镜像可配）、本地列表、从文件导入；注（2026-10-14 反证）：HF 官方现行实现无跨运行断点续传（Xet 临时文件进程唯一）——本功能为基于 resolve 契约的自研纯 HTTP 下载器（Range 续传 + sha256 校验，已实测支持）；⚠ Xet 仓库 CAS 流量绕过 HF_ENDPOINT，镜像兼容性按 resolve 契约逐镜像实测为准 | 无 |
| FR-9 | 处理管线串联 | 降噪→分离→变声链式一键执行（核心差异化） | 无 |
| FR-10 | 批处理队列 | 多文件排队、逐个执行、失败不中断后续 | 无 |
| FR-11 | 实时引擎 | 麦克风→降噪→(变声)→输出设备（含虚拟声卡），延迟实测显示 | 无 |
| FR-12 | 进度与取消 | 全任务可取消、不留半成品 | ✅ 已有 |
| FR-13 | 外挂训练器 | Python CLI：素材→自动提人声→切片→训练→导出 ONNX+索引 | 无（Python 侧） |
| FR-A4 | 工作区版本化与任务记录 | 原件只读；手动+Agent 任务统一走此机制；输入哈希+参数+版本化输出；可回退 | 无（Agent 的地基，v1 先行落地） |
| FR-24 | 无头命令层 | 每个引擎/管线 JSON 进 JSON 出；GUI 全部操作改走此层；同时是 CI 测试面 | 无（Agent 的工具面，v1 先行落地） |

### 4.2 v1.x（Should）

| # | 需求 | 说明 |
|---|------|------|
| FR-14 | 视频合回 | 处理后音频 mux 回视频（AAC/原编码复制） |
| FR-15 | 高质量分离档 | BS-Roformer / Demucs；注（2026-10-14 反证修正）：官方 ONNX **导出工具**存在（ZFTurbo/MSS_ONNX_TensorRT，MIT，opset 17、STFT 外置，覆盖 htdemucs/bs_roformer/mel_band_roformer/mdx23c/segm），但**官方不发布成品 ONNX 权重**（HF 现存均为社区账号上传）——社区成品须逐个核验，或用官方工具自导 |
| FR-16 | 输出格式选择 | WAV/FLAC/MP3/原格式 |
| FR-17 | 预设系统 | 常用参数组合保存/复用 |
| FR-18 | 中英双语 UI | |
| FR-19 | 实时附加效果 | EQ/混响等 DSP 效果器 |
| FR-A1 | 应用内对话面板 | 中文为主，展示计划、逐步闸门与执行进度 |
| FR-A2 | 意图路由 + 计划生成 | 两级路由：Tier-1 常用命令快速匹配（不碰 LLM），复杂命令走 LLM 结构化输出（JSON schema 约束解码） |
| FR-A3 | 计划校验与逐步确认 | 计划三道闸（schema/参数范围/能力预检）；执行时**每步设闸**：人话卡片（做什么/参数/预期输出）+ 可核验中间产物（转写文本、标记区段、波形），用户可继续/中止/改参；基础设施步骤（备份、版本登记）自动执行不设闸（D7） |
| FR-A5 | LLM 后端抽象 | 内嵌 llama.cpp + Qwen 量化模型（默认 CPU 推理）；可选 OpenAI 兼容云 key，显式开关 |
| FR-A6 | ASR 引擎 | 词级时间戳，whisper.cpp / sherpa-onnx，模型走 FR-8 管理器；注（2026-10-14 反证）：whisper.cpp `-ml 1` 词级时间戳为启发式实验特性（官方文档自述、已知错例），高精度对齐需 `-dtw` 或外部对齐器——时间戳精度需求按此取设计前提 |
| FR-A7 | VAD 引擎 | silero-vad ONNX（「去掉静音段」类命令） |
| FR-A8 | EDL 区段剪辑引擎 | 按时间戳切割/保留/静音，交叉淡化，纯 DSP（旗舰例子 US7 依赖） |
| FR-A10 | 审计日志 | 每条命令→计划→执行结果落盘，并入诊断包导出 |

### 4.3 v2（Could）

| # | 需求 |
|---|------|
| FR-20 | 应用内一键训练（GUI 包装训练器） |
| FR-21 | 多音色热切换快捷键 |
| FR-22 | 唱歌转换档（So-VITS SVC） |
| FR-23 | 第三方引擎插件接口 |
| FR-A9 | 说话人分离 diarization（「只保留某人的话」类命令） |
| FR-A3' | 确认策略可配置化（白名单自动执行等，架构已预留） |

### 4.4 Won't（明确不做，防范围失控）

云端音频处理、账号体系、遥测、移动端、主程序内置训练、实时人声分离（**不做——用户决策，2026-10-14**；技术注记：理由不是「算力不现实」——CPU 实时方向已被 Band-SCNet 证明可行（RTF 0.478，`docs/reference/research-audio-algorithms-frontier.md`），纯属产品取舍）、Agent 绕过命令层直接操作文件。

---

## 5. 非功能需求（全部可测量）

### 5.1 延迟（D2：≤150ms 全链路，麦克风采样→输出可听，参考机 RTX 3060；v1.2 依反证审查重写）

**软件内链**（注入法可测段）。捕获现实口径：**OEM 驱动普遍钳制 10ms 周期，按 20–45ms/向预算；IAudioClient3 小周期（~3ms/向）仅特定驱动可达，作上限优化不作承诺**（MS 文档 "It's up to the OEMs to decide" + miniaudio#949 实测钳制，见 `docs/reference/falsify-realtime.md`）。

| 环节 | 降噪链（DFN3） | 变声链（RVC） | 手段 |
|---|---|---|---|
| 输入捕获缓冲 | 20–45ms（OEM 现实；小周期驱动 3–10ms） | 同左 | WASAPI 共享 + IAudioClient3（miniaudio Win10+ 自动启用） |
| 重采样/格式转换 | ≤5ms | ≤5ms | 块内完成，全链路至多一次重采样 |
| 算法延迟（STFT/chunk） | ~40ms | 64–96ms（chunk **替换**降噪链的 40ms，非叠加） | DFN3 结构 ~40ms；RVC chunk 下限 64ms |
| GPU 推理执行 | ≤30ms/chunk | ≤30ms/chunk | ORT CUDA EP；F0 与特征并行 |
| 重叠相加+输出缓冲 | ≤30ms | ≤30ms | 小块输出，WOLA 直投输出缓冲 |
| **软件内合计** | **125–150ms（典型～压线）** | **149–181ms（仅最小 chunk+全项压线才达标；最坏超支 31ms）** | 注入法实测，UI 显示实测值，不宣称「零延迟」 |

**用户侧链**（软件外，不计入 NFR-L1、但计入用户体验）：虚拟声卡环回 10–30ms（VB-CABLE 官方指南下限 ~5ms + 相邻实测 ~29ms；缓冲错配可至数百 ms）；消费端（OBS/Discord）缓冲另计。

- **变声链警示**：最坏算术 20+5+96+30+30=181ms **超预算**——150ms 对变声链属「未证实的激进目标」，须小周期驱动 + chunk ≤64ms + 其余项全部压线才成立（与 O3/R2 联动，真机实测前不得宣称可达）。
- **NFR-L1**：实时 round-trip ≤150ms（口径 = 软件内链，打拍注入法测量），UI 实测显示。
- **NFR-L2**：音频线程零堆分配（`processInto` 预分配 API），抖动 ≤5ms。现有 `process()` 返回 vector 的接口必须扩展。
- **NFR-L3**：设备采样率自动协商，全链路至多一次重采样。

### 5.2 算力与性能

- **NFR-P1**：GPU 8GB VRAM 底线；实时链路 VRAM ≤4GB。
- **NFR-P2**：离线 RTF（参考机）：AI 降噪 ≥5× 实时；MDX 分离 ≥1× 实时；RVC 变声 ≥3× 实时。
- **NFR-P3**：无 GPU 环境：DSP 降噪/抽取/变调/转码全部可用，AI 功能显式提示而非崩溃。
- **NFR-P4**：2 小时音频处理峰值 RSS ≤2GB（严格流式，禁止整文件载入。现有代码已达标，须保持）。
- **NFR-P5**：空闲态 GPU 显存占用为零（模型按需加载/卸载）。

### 5.3 质量（引擎准入线）

- **NFR-Q1**：每个推理引擎对官方参考实现 parity ≤ −60dB RMS（GTCRN↔sherpa-onnx；MDX↔audio-separator；DFN3↔官方 Rust runtime；RVC↔官方推理；ASR↔whisper.cpp 参考输出）。**不过不上线。**
- **NFR-Q2**：DSP 降噪在 200Hz–20kHz 通带内 |H| 波动 ≤±0.1dB。
- **NFR-Q3**：视频源 48kHz 全程不经过 44.1k 往返（canonical 改 48k 可配置；修正现有 `data.h` 的 44100）。
- **NFR-Q4**：浮点内部链，导出可选 limiter/normalize，保证无削波。

### 5.4 可靠性

- **NFR-R1**：取消 ≤1s 生效；失败任务自动清理半成品。
- **NFR-R2**：坏文件/非媒体文件不崩溃（fuzz 样本组进 CI）。
- **NFR-R3**：实时会话 ≥4h 无内存增长、无延迟漂移（自动化 overnight 测试）。
- **NFR-R4**：模型缺失/损坏 → 引导重新下载，不崩溃。
- **NFR-R5**：崩溃留本地 minidump + 分级日志，可一键导出诊断包（不上传）。

### 5.5 可维护性

- **NFR-M1**：引擎注册表——新增引擎不改 GUI/服务层代码（FR-24 的实现载体）。
- **NFR-M2**：每引擎 parity 单测 + Agent eval 回归，CI 必绿才可发版。
- **NFR-M3**：依赖版本矩阵锁定（FFmpeg/ORT/CUDA/Qt 具体版本组合写入文档）。
- **NFR-M4**：干净仓库单命令出安装包（CI 产出，消灭 `D:/qt6` 硬编码）。
- **NFR-M5**：日志分级。

### 5.6 合规与隐私

- **NFR-C1**：音频处理全本地，无遥测无账号；云 LLM 仅用户显式开启（D6），开启时明确告知命令文本将发往所配 API。
- **NFR-C2**：随包分发 THIRD-PARTY 清单：逐依赖 + 逐模型列来源与许可证。
- **NFR-C3**：LGPL 合规：FFmpeg/Qt 动态链接；**SoundTouch 静态链 `.a` 必须改动态或提供目标文件**；修正 gyan.dev 导入库 + BtbN DLL 混用，统一同一份 LGPL 构建三件套。v1.4 增解法候选：评估以 Signalsmith Stretch（MIT，仓库页已核验）替换 SoundTouch（FR-6），替换则本项 SoundTouch 许可问题消失（是否可行随 WP-1.7 构建统一时一并定）。
- **NFR-C4**：模型下载源可配置镜像（HuggingFace + hf-mirror；⚠ Xet 仓库 CAS 流量绕过 HF_ENDPOINT，镜像兼容性以 resolve 契约逐镜像实测为准——见 FR-8 注）。
- **NFR-C5**：变声功能内置使用须知（禁止冒充他人违法使用）。

### 5.7 Agent（v1.x 准入线）

- **NFR-A1 计划准确率**：100 条中/英命令评测集 → 期望管线匹配 ≥95%，进 CI 回归（temperature=0）。**Agent 的 eval 集就是它的 parity 测试。**
- **NFR-A2 确定性**：同一命令+同一上下文 → 同一计划；执行层永不改写输入文件（测试强制）。
- **NFR-A3 失败遏制**：意图不明 → 追问或指引手动操作，绝不静默执行错误动作；幻觉参数 100% 被 schema 拦截。
- **NFR-A4 响应预算**：Tier-1 路由 ≤1s；LLM 计划生成 ≤5s（GPU LLM）/ ≤15s（CPU LLM）；单次生成硬超时，超时降级为追问。
- **NFR-A5 资源调度**：本地 LLM 默认 CPU 推理（Qwen 4B 级量化），VRAM 不与音频引擎争用；用户可选 GPU 并承担争用提示。
- **NFR-A6 可核验性**：每道确认闸必须展示该步的可人工核验产物（转写文本/标记区段/波形预览），不允许「黑盒继续」。

---

## 6. 约束条件

**许可证红线表**（公开发布前逐项核验，⚠=待核验，见第 9 节）：

| 组件 | 许可证 | 再分发 | 风险 |
|---|---|---|---|
| Qt6 / FFmpeg(LGPL) / ORT / miniaudio / SoundTouch | LGPL/MIT 等 | ✅ 动态链接即可 | SoundTouch 静态链不合规（NFR-C3；替换候选 Signalsmith Stretch，MIT，v1.4） |
| GTCRN 模型（sherpa-onnx 分发） | **MIT**（2026-10-14 核验：出处链闭环到上游仓库 LICENSE，倾向结论） | ✅ | 低：sherpa release 页无逐字模型许可字样，发版前向官方书面确认一次 |
| DeepFilterNet3 权重 | MIT 系 | ✅ | 低 |
| UVR-MDX-NET-Voc_FT | UVR 生态惯例免费再分发 | 有先例，须标注来源 | 中 |
| ContentVec/HuBERT（RVC 特征提取器必需） | **MIT**（2026-10-14 溯源+反证双重核验：仓库 MIT + 作者 issue #34 确认预训练权重同许可；HuBERT 系**教师标签来源非权重初始化**，无许可继承链；Meta 官方 hubert-* 仓库全为 apache-2.0，2021 年起一贯） | ✅ | 低（原「Meta 权重许可未闭合」盲点已闭合）；商业化前对作者声明做一次法务确认 |
| 社区 RVC 音色模型 | 五花八门 | **不内置**，用户自行导入 | 转嫁给用户 + 使用须知 |
| Qwen 量化模型（GGUF） | **Apache-2.0**（2026-10-14 核验；⚠ 例外：Qwen3-Omni 三仓库 license:other 且无 LICENSE 文件；2026 已有 Qwen3.8-Max/Flash-Next 转自定义许可先例、且同代混用） | ✅ 限官方/unsloth 已核验为 apache 的仓库；走模型管理器按需下载 | 低，但**选型硬规则：逐仓库核对 LICENSE 文件原文**（HF 元数据 license 标签不可尽信） |

其他约束：Windows 10/11 x64 主目标（Linux 保持可编译，CI 双跑）；C++20 + Python 3.10（训练器独立打包，不污染用户环境）；CUDA 12.x；参考机 RTX 3060/8GB。

虚拟声卡（FR-11，2026-10-14 官网条款 + **官方驱动包 EULA 原文**核验）：VB-CABLE 允许**原样拷贝分发（EULA 原文 "copy and diffuse AS IS"）与引导用户自装**（官网推荐方式）；⚠ **将 VB-CABLE 集成进本产品安装流程须先取得 VB-Audio 书面同意**（EULA 禁止 "integrate … in another software installation procedure without Author agreement"，与官网宽松口径冲突——从严执行）。署名与捐赠：标注 vb-cable.com 来源 + donationware 提示 + 保留付费通道（官网措辞 will、EULA 措辞 might，从严）；A+B / C+D 组合禁止捆绑；企业场景按量授权。开源替代无干净许可方案（OBS-Virtual-Mic-Plugin 为 GPL，红线禁用）。详见 `docs/reference/falsify-licenses.md`。

组件选型补充（2026-10-14 架构审视落档）：**AGPL 与 GPL 同禁**（D1）。实时化队列选型注意 JUCE AbstractFifo 为 AGPLv3（双许可原文核实）**禁用**——SPSC 自研或选 rigtorp（MIT）/ readerwriterqueue（BSD-2）；Mixxx/LMMS/Hydrogen 为 GPL 系，只可参照思路、不可复制代码。证据链 `docs/spec/architecture-review.md` §4。

---

## 7. 验收标准（示例，全部可执行）

- **FR-5**：10 首风格各异测试曲，人声输出相对 audio-separator 参考的 SDR 差 ≥ −1dB；RTF 达 NFR-P2。
- **FR-11**：注入法实测 round-trip ≤150ms 截图存档；4h 稳定性通过；拔插音频设备自动恢复或明确报错。
- **FR-13**：10 分钟素材训练出的模型，主程序加载后实时延迟不劣化、主观可用（MOS ≥3.5 自评 5 人）。
- **FR-A3 / US7（旗舰验收）**：对 Agent 说「去掉叙述部分」→ 计划正确生成 → 每步闸门展示可核验产物 → 输出与预期 EDL 一致 → 原件字节级不变（哈希校验）。
- **全局门禁**：全部 parity 测试（NFR-Q1）+ Agent eval（NFR-A1）绿 → 才允许打 release tag。

---

## 8. 风险清单

| # | 风险 | 概率 | 影响 | 对策 |
|---|------|------|------|------|
| R1 | ContentVec 许可证残余风险（2026-10-14 溯源+反证：MIT，且 HuBERT 系教师标签来源非权重初始化——**无许可继承链，「传递链复核」已无对象**） | 低 | 低 | THIRD-PARTY 清单登记证据链（NFR-C2）；商业化前对作者 MIT 声明做一次法务确认（流程性动作） |
| R2 | RVC 实时在参考机压不进 150ms（2026-10-14 反证：w-okada#796 反例 + 捕获链 OEM 现实 20–45ms/向 + 变声链最坏算术 181ms 超支；v1.4 补锚：RVC 系 3060 级真机锚点仍缺，但**架构级正锚点已现**——StreamVC 端到端 70.8ms（Pixel 7 CPU）/ MeanVC2 110ms（Apache-2.0）/ Zero-VC 20ms（闭源，仅论文），均非 RVC 架构、无一可直用；Seed-VC 为 GPL-3.0 **禁用**） | 高 | 高 | **写实时代码前先跑最小 benchmark 实测 chunk 推理耗时**；chunk 调优/ORT 优化（v1.4 增：INT8 量化兜底，见 M4 前置实验）；兜底如实公布实测延迟 |
| R3 | 训练器 Python 环境在用户机器不可用 | 高 | 中 | 独立打包（内嵌运行时）；训练器失败不阻塞主程序 |
| R4 | 模型下载被墙/失联 | 高 | 中 | 镜像可配置（NFR-C4） |
| R5 | 单人项目范围失控 | 高 | 高 | 严格执行 MoSCoW；v1 = 4.1 节清单一寸不加 |
| R6 | FFmpeg 构建混用（gyan+BtbN）的 LGPL 违规 | 中 | 高 | 统一同一 LGPL 构建三件套（NFR-C3） |
| R7 | 变声被用于诈骗引发舆情 | 低 | 高 | 使用须知 + 可选隐式水印（NFR-C5） |
| R8 | 本地小模型函数调用不可靠 | 中 | 高 | 结构化约束解码；两级路由把简单命令挡在 LLM 外；eval 回归；云 key 兜底 |
| R9 | LLM 与音频引擎争 VRAM | 高 | 中 | LLM 默认 CPU；按需加载调度 |
| R10 | Agent 使范围失控风险翻倍 | 高 | 高 | 严格分层：v1 只做工作区+无头层，Agent 本体 v1.x（D5） |
| R11 | 幻觉参数造成破坏性操作 | 中 | 高 | 三道校验闸 + 原件只读 + 每步确认（D7），机制兜底不依赖 LLM 靠谱 |
| R12 | 离线转码内存无界破 NFR-P4（2026-10-14 架构审视 A1 实锤：引擎输出缓冲曾全程驻留内存，2h@44.1k/2ch 实测 2.80GiB > 2GB 红线） | 已发生 | 高 | **已关闭（2026-10-14）**：feedAi 分块后就地排水热修；Linux 复验 2h 峰值 57MB/−40% 耗时/逐字节一致（`docs/tmp/linux-build/smoke-results.txt`）；Windows round-3 复验 2h 峰值 denoise 22.1MiB / ai-denoise 47.9MiB（`docs/acceptance/wp-1.0-hotfix/round-3-windows.md`） |
| R13 | 引擎运行期异常无屏障致进程崩溃（架构审视 A2：AiDenoiser `sess->Run` 可抛 Ort::Exception 穿透转码器与 worker 槽直达 std::terminate；同库 vocalseparator 却有 catch，错误通道不一致） | 低 | 高 | **已关闭（2026-10-14）**：三层屏障落地（streamengine.h 契约「不得抛出」/ AiDenoiser process/flush catch 归口新覆写 lastError() / runJob 兜底 catch）；Linux 错误注入验收通过（round-2：维度错位模型修复前 SIGABRT exit134 + 半成品残留 → 修复后 FAIL exit1 无残留）；Windows round-3 注入回归全绿（垃圾模型/不存在路径/分离器通道：FAIL exit1 无残留；GUI jobDone 正常投递） |

---

## 9. 版本规划与现状差距

```
v1.0（出货线）= FR-1..13 + FR-A4（工作区版本化）+ FR-24（无头命令层）+ 适用 NFR
  现有代码已覆盖：FR-2/5/6/12、FR-3/4 的一半、NFR-P4/R1（流式与清理做得好）
  主要缺口：FR-7 变声（最大新增）、FR-11 实时、FR-13 训练器、
            FR-8/9/10 模型管理与管线串联、FR-A4/FR-24、
            以及全部 P0 级质量债（parity 测试、FFT 重写、48k canonical、shape 断言）
  执行顺序：正确性兜底 → 离线闭环 → 变声 → 实时化

v1.x = FR-14..19 + Agent 全家（FR-A1/A2/A3/A5/A6/A7/A8/A10）
  铺垫已就位：FR-A4 工作区 + FR-24 命令层在 v1 完成，Agent 是纯增量

v2 = FR-20..23 + FR-A9 + 确认策略可配置化
```

---

## 10. 待核验事项（开放问题）

| # | 事项 | 阻塞什么 | 何时必须关闭 |
|---|------|---------|------------|
| O1 | GTCRN 模型确切许可证 — **已关闭（2026-10-14）**：上游仓库 LICENSE=MIT（倾向结论）；残余=发版前向 sherpa 官方书面确认一次 | 原阻塞随包分发（NFR-C2），已解除 | — |
| O2 | ContentVec 替代特征提取器可行性 — **已关闭（2026-10-14）**：CC-BY-NC 前提被推翻（仓库 MIT + 作者 issue #34 确认权重同许可）；R1 降级，替代方案降为效果选项 | 原阻塞 R1，已解除 | — |
| O3 | RVC chunk 推理延迟实测（用户 GPU）——**闸门保留且强化**：区间证据仅 RVC 官方 README 声明 170ms / ASIO 90ms（未标 GPU；⚠ v1.1 误归 w-okada，2026-10-14 反证已更正）；另有 w-okada#796 反例（i9+RX6800XT 实时不可用）、捕获链 OEM 现实 20–45ms/向、变声链最坏算术 181ms 超支（§5.1）；v1.4 补锚（2026-10-14 调研）：**架构级正锚点已现**——StreamVC 70.8ms（Pixel 7 CPU，已确认）/ MeanVC2 110ms（Apache-2.0，已确认）/ Zero-VC 20ms（闭源，仅论文），但均非 RVC 架构、无一可直用；Seed-VC 为 GPL-3.0 禁用；RVC 系 3060 级真机锚点仍缺——**150ms 属未证实（RVC 真机锚点仍缺），必须真机脉冲法实测** | R2 / 实时架构决策 | v1 实时化开发前 |
| O4 | Qwen 量化模型再分发许可选型 — **已关闭（2026-10-14）**：Qwen3 全系 Apache-2.0，官方/unsloth GGUF 均可再分发，选型限此二源；⚠ 例外与先例（2026-10-14 反证）：Qwen3-Omni 三仓库 license:other 且无 LICENSE 文件（排除）；2026 年 Qwen3.8-Max/Flash-Next 已转自定义许可且同代混用——**选型硬规则：逐仓库核对 LICENSE 文件原文，排除例外** | 原阻塞 FR-A5，已解除 | — |

证据链与残余盲点汇总：`docs/reference/research-index.md`。

---

## 修订记录

| 日期 | 版本 | 变更 |
|------|------|------|
| 2026-10-13 | v1.0 | 首版基线：D1–D7 决策、FR/NFR/风险/版本规划、Agent 增量（D5–D7） |
| 2026-10-14 | v1.1 | 外部调研结论落基线（证据链 `docs/reference/research-index.md`）：§6 GTCRN→MIT、ContentVec CC-BY-NC 推翻为 MIT、Qwen→Apache-2.0（限官方/unsloth 源）、新增 VB-CABLE 分发条款；§8 R1 降级（高×高→低×中）；§10 O1/O2/O4 关闭、O3 表述更新（闸门保留）；§5.1 注 WASAPI 官方延迟数字；FR-15 注 ONNX 可得性硬约束 |
| 2026-10-14 | v1.2 | 反证审查结论落基线（证据链 `docs/reference/research-index.md` §7 / falsify-*.md）：§5.1 延迟预算表重写（拆降噪/变声两链，捕获按 OEM 现实 20–45ms/向、小周期改「上限非承诺」，变声链最坏 181ms 显式超支，补用户侧链与 NFR-L1 测量口径）；§10 O3 归属更正（170/90ms 出自 RVC README，非 w-okada）+ w-okada#796 反例，下调为「未证实」；§8 R2 中→高、R1 残余闭合（无 HuBERT 权重继承链）；§6 Qwen 补 Omni 例外与 2026 自定义许可先例（逐仓库核对升为硬规则）、VB-CABLE 收窄（原样拷贝/自装 OK，集成安装器需书面同意）、ContentVec 残余更新；FR-8 定位为自研纯 HTTP 下载器（HF 官方无跨运行续传）；FR-15 改官方导出工具存在、不发成品；FR-A6 注 `-ml 1` 启发式前提 |
| 2026-10-14 | v1.3 | 架构审视结论落基线（证据链 `docs/spec/architecture-review.md` + `docs/reference/research-{streaming-pipelines,realtime-threading,qt-audio-apps,cpp-engine-patterns}.md`）：§8 新增 R12（转码内存无界破 NFR-P4，A1 实锤，已热修待 Windows ≥2h 素材 RSS 复验）、R13（引擎异常屏障缺失可致 std::terminate，A2，未修，随 WP-1.0 收尾）；§6 组件选型补 AGPL 同禁（JUCE AbstractFifo 禁用，SPSC 自研或 rigtorp/readerwriterqueue；Mixxx/LMMS/Hydrogen 只可参照思路）。同批热修 A11（关窗假死）/A12（流复制不可取消）/A14（override 卫生项）已进源码待 Windows CLI 冒烟；开发计划增 WP-1.0 热修包 |
| 2026-10-14 | v1.4 | 音频算法前沿调研落基线（证据链 `docs/reference/research-audio-algorithms-frontier.md`，候选清单 `research-index.md` §7.5 用户确认）：① §4.4 实时人声分离「算力不现实」→「不做」（先经 Band-SCNet CPU RTF 0.478 反证原表述、落「暂缓」，同日用户明确否决该功能，终态「不做——用户决策」，技术注记保留防错误理由回潮）；② §8 R2 / §10 O3 补锚——架构级正锚点已现（StreamVC 70.8ms / MeanVC2 110ms Apache-2.0 / Zero-VC 20ms 闭源，均非 RVC 架构不可直用），Seed-VC GPL-3.0 禁用，RVC 真机锚点仍缺，闸门与真机脉冲法实测不变；③ FR-4 质量档增 FastEnhancer（ICASSP 2026，MIT，流式 ONNX）与 DFN3 并列候选，M2 评估；④ NFR-C3 增 Signalsmith Stretch（MIT）替换 SoundTouch 候选（随 WP-1.7 一并定）；⑤ M4 增 INT8 量化前置实验（CPU 3.3× 论文级实证，实时预算兜底）——开发计划 M2/M4 段同步 |
