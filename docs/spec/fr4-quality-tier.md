# FR-4 质量档选型（DFN3 集成 + FastEnhancer 评估记档）

- 日期：2026-11-04。性质：开工前快照（M2 工作包，依赖序承接 FR-10 批处理队列，`docs/spec/v1-development-plan.md:38`；spec 不回头维护，事后复盘归 acceptance）。
- 决策编号 D1–D7 仅限本 spec；未收敛项进 §7 OQ 清单供主线程拍板，本 spec **不含 §7.1**。
- 现状基线：HEAD `3066010`（FR-10 批处理队列落地），git status clean。
- 本包不含：FastEnhancer 本体集成与 VB-DMD 复测（仅评估记档，准入门槛见 D1/§5）、实时链与延迟实测（FR-11/M4、开放问题 O3）、INT8 量化（M4 前置实验）、RVC 变声（M3）。

## 0. 需求锚点

| 锚点 | 内容 | 与本包关系 |
|---|---|---|
| `docs/REQUIREMENTS.md:89` | FR-4 离线 AI 降噪：快速档 GTCRN + 质量档 DeepFilterNet3；v1.4 注（2026-10-14）：FastEnhancer（ICASSP 2026，MIT，流式 ONNX）列为质量档**并列候选**，M2 与 DFN3 一起评估选型；现状「半有（缺质量档）」 | 本包直接承接，落地后 FR-4 现状改「全有」 |
| `docs/REQUIREMENTS.md:301` | v1.4 修订③：FR-4 质量档增 FastEnhancer 并列候选，M2 评估 | 选型结论必须显式覆盖 FastEnhancer（D1） |
| `docs/REQUIREMENTS.md:170` | NFR-Q1：DFN3↔官方 Rust runtime parity ≤ −60dB RMS，**不过不上线**；「其余引擎随各自工作包落地时同标准接入」 | 本包验收硬门禁（D7） |
| `docs/REQUIREMENTS.md:163` | NFR-P2：AI 降噪离线 RTF ≥5× 实时（参考机） | DFN3 论文 RTF 0.19 ≈5.3× 贴线，需实测裁定（OQ-5） |
| `docs/REQUIREMENTS.md:165/:166` | NFR-P4：2h 音频峰值 RSS ≤2GB 严格流式；NFR-P5：模型按需加载 | 引擎内部定长块缓冲（D2） |
| `docs/REQUIREMENTS.md:172` | NFR-Q3：输出跟随源；AI 路径输出仍 44.1k 属「后续可选优化」 | DFN3 原生 48k 是首个不锁兜底档的 AI 引擎（D2） |
| `docs/REQUIREMENTS.md:148` | 降噪链（DFN3）算法延迟 ~40ms，在 ≤150ms 实时预算内 | 离线包不实测延迟，数字挂 M4（§5 留缝） |
| `docs/REQUIREMENTS.md:217-218` | §6 红线表：GTCRN 模型 MIT（倾向，发版前书面确认）；DFN3 权重 MIT 系 ✅ 风险低 | DFN3 同样挂 NFR-C2 法务复核（D5） |
| `docs/reference/research-audio-algorithms-frontier.md:27-38` | §1.1 FastEnhancer 全量数字（MIT、官方 ONNX、全因果 6.25–16ms、ORT 单线程 RTF 0.012–0.03、Base PESQ 3.13 > GTCRN 2.87@VB-DMD、五档参数量 22K–1105K、有 48k 变体） | D1 评估记档的证据基座 |
| `docs/reference/research-audio-algorithms-frontier.md:207` | §5.1 落点：DFN3 集成照旧作质量档；FastEnhancer 列快速档接棒候选 + M4 实时链候选，**准入条件 = Windows/ORT 冒烟 + VB-DMD 复测 PESQ** | 本包范围裁定直接引用（D1） |
| `docs/reference/research-deepfilternet3.md:208-238` | DFN3 集成可行性结论分级（前端复刻可行、官方包可直用、parity 对拍目标 = 官方 deep-filter CLI） | D2/D7 证据基座 |
| `docs/spec/fr8-model-management.md` §5 留缝行 | 「FR-4 质量档：目录表加条目即可；forVerb 已支持同动词多模型；不做档位选择参数（随选型包定）」 | 档位参数归属本包裁定（D4/D6） |
| `docs/spec/canonical-rate-48k.md:17-18` | GTCRN/MDX 输出锁 44.1k 有意保留；「DFN3 质量档原生 48k……跟随源策略与其工作点天然对齐」 | D2 采样率策略依据 |

## 1. 现状盘点（锚点均按 HEAD 3066010 实读核实）

### 1.1 引擎注册表与命令面

- 注册表契约：`src/service/engineregistry.h:3-6` 头注——新增引擎 = 新 `.h/.cpp` + `engineTable()` 追加条目 + CMake 两行，命令层/GUI/服务层零改；`ParamSpec`（`engineregistry.h:20-30`，Kind String/Number/Bool/Enum + `defStr` 缺省单源）、`EngineEntry`（`engineregistry.h:32-43`：id/params/requiresModel/setupErrorClass/lockCanonicalOutput/outputSuffix/semanticCheck/create）。
- aiDenoise 条目现状：`src/service/engineregistry.cpp:159-168`——`params = {}`（无参数）、`requiresModel = true`、`setupErrorClass = ErrCode::Model`、`lockCanonicalOutput = true`（输出锁兜底 44.1k/2ch，模型工作点锁定）、`create = createAiDenoise`；工作点由模型 metadata 锁定、srcRate/srcCh 不参与 setup（`engineregistry.cpp:55-59`）。
- 命令层分发：双查表 `src/service/commandlayer.cpp:675-684`（引擎表 → 内置小表 → E_PARAMS）；信封未知字段白名单 `:693-706`；staticCheck = validateParams→semanticCheck→模型存在性（不调 create，`commandlayer.cpp:186-200`）；runEngine 取源规格 `:210`（probeSourceSpec）并把 `lockCanonicalOutput` 传入转码底座 `:222/:247`（`runTranscode` 内 `kCanonicalRate` 兜底 `:95-106`）。
- 管线/批词表 = 引擎表枚举：FR-9 D2（`docs/spec/fr9-pipeline.md:74-78`，步骤字段 = `{cmd} ∪ 该引擎 isDeclaredParam`，:77 有「变声槽位零改」前瞻）、FR-10 D2/D3（`docs/spec/fr10-batch-queue.md:71-105`，validateSteps 共享段 minSteps 参数化）——**新引擎条目落地即自动可入 pipeline steps 与 batch steps 模板，命令层零改**（FR4-REG 验收点）。

### 1.2 AiDenoiser 先例（DFN3 引擎的形态模板）

- metadata 键勿硬编码：`src/service/aidenoiser.cpp:251-261` 读 sample_rate/n_fft/hop_length/window_length/window_type/三个 cache_shape；规格断言 `:262-277`；ONNX 名字→槽位映射 `:278-305`。
- **窗函数已含 vorbis 窗型**：`aidenoiser.cpp:73-74`（makeWindow 分支）——DFN3 前端 STFT/WOLA 复用点。
- 流式契约：`process()` 输出可滞后、`flush()` 收尾（`src/service/streamengine.h`；engine_test 分块不变性验收口径见 `src/test/engine_test.cpp:2-3`）；帧网格/WOLA÷Σw² 先例 = WP-1.1 两债修复。
- 屏障与隔离：PIMPL（`aidenoiser.h:21-22`）；process/flush `catch (Ort::Exception)` 归口 lastError（`aidenoiser.cpp:343-345/:355-357`，R13 三层屏障）；`SetIntraOpNumThreads(1)` `:233`。

### 1.3 模型管理与两级缺省解析

- 目录表现状：`src/service/modelmanager.cpp:39-50` CatalogEntry 字段（id/name/forVerb/relPath/hfSource/hfRepoPath/directUrl/sha256/license/sizeBytes）；编译期两表目 `:53-91`（gtcrn：GitHub Releases 直连 directUrl + sha256；uvr：hfSource 镜像）；`Q_ASSERT` forVerb ∈ requiresModel 动词集 `:80-89`。
- purposeDir 自适应：`modelmanager.cpp:373-374` 注释——「目录暂无该动词条目时以动词名兜底（**FR-4 质量档等后续条目落目录后自动归位**，spec §5 留缝）」→ 目录表加条目即自动归位，无需改 purposeDir。
- 两级缺省解析：`src/service/ffextractor.cpp:139-170` resolveDefaultModel——库内 installed 且非 bundled 优先、appDir 随包兜底；`:142` 注释「v1 每用途一模型无歧义（**FR-4 多模型档位是 §5 留缝**）」→ 两模型共存的前提是**各自独占 forVerb**（D4 裁定）。
- 下载器现状：单文件流（HEAD→ETag→Range 续 .part→64KiB 增量→流式 sha256→同卷 rename→登记）；幂等「登记 sha+size 即信」`modelmanager.cpp:478-510`；导入两形态（by-id 指纹 / 自由导入 for+create 试开）`:638-666`。**只支持单文件产物**——DFN3 官方包是 tar.gz 多文件（OQ-4）。
- GUI 模型面：五列树（含许可证列）`src/view/mainwidget.cpp:378-466`；用途文案映射 kForText `:545-547`（aiDenoise→AI 降噪 / separate→人声分离）；onModelMissing 用途映射 `:599-601`（MIST 族逐字锁定）；自由导入用途组合框 `:504-505`；QSettings 两键只主线程读写 `:131-134`。

### 1.4 GUI 三个通路（档位必须三面同达）

- 单步：`src/view/mainwidget.cpp:818-823` doDenoiseAiWav（worker 槽壳在 `src/service/ffextractor.cpp:467-485`，缺模型文案「找不到 AI 降噪模型：%1」`ffextractor.cpp:475` GUI-EQV 锁定）。
- 管线：预设三条 `mainwidget.cpp:45-49` + 动态参数取当前面板注入（humBase/semi 先例）`:854-876`；worker 侧 resolveStepModels verb→bundled 映射 `ffextractor.cpp:533-559`。
- 批处理：动作组合 8 条（含 {AI 降噪, aiDenoise}）`mainwidget.cpp:56-63`、逐件组包 `:698-749`。
- 降噪行现状（档位 UI 落点）：denRow `mainwidget.cpp:167-172`（chkHum/cmbHumBase/chkWind/btnDenoise/btnAi=「AI降噪导出WAV」`:89`）；档位记忆先例 = cmbHumBase QSettings humBaseHz `:100/:213-215`。

### 1.5 验收设施

- parity 框架：`docs/acceptance/engine-parity/scripts/run_parity.sh`（gen_inputs.py 确定性合成、依赖分级 SKIP、`PARITY_REQUIRED=1` 强制、换参考实现 `gen_golden.py --force` 重生成）。
- 无头 DUT：`src/test/engine_test.cpp:4-7` 动词面 dsp/ai/pitch/sep-voc/sep-inst（ai 输入 16000Hz 1ch、参数=模型路径 `:86-96`）；分块不变性 = 同输入不同块帧数逐字节一致（`:2-3`）。
- 依赖矩阵：`scripts/fetch-deps.sh:14-19`（ORT/GTCRN/UVR 三行 URL+sha256；DFN3 进 CI 需加行——CI 域改动）。

### 1.6 采样率策略

- 输出跟随源已落地（P0 #4）：transcode/denoise/pitch 48k 源全链免重采样；aiDenoise/separate 显式锁兜底 44.1k（`lockCanonicalOutput=true`，模型工作点）——`docs/spec/canonical-rate-48k.md:17-18`：AI 路径回源率属后续可选优化，且明确预留「DFN3 原生 48k 与跟随源天然对齐」。

### 1.7 DFN3 关键事实（全部引自 `docs/reference/research-deepfilternet3.md`，行号为该文档）

- 参数与延迟：48k 全频带原生；fft_size=960（20ms）/hop=480（10ms）/nb_erb=32/nb_df=96/df_order=5/lookahead=2；算法延迟 1440 样本=30ms（实现口径，论文口径 ~40ms 与 `REQUIREMENTS.md:148` 一致）（`:22-46`）。
- 模型包：`DeepFilterNet3_onnx.tar.gz` = 7,983,136 B，内含三个 ONNX 子图 + `config.ini`；参数 ≈950K（fp32 ≈3.8MB）（`:51/:214`）。
- **图结构关键约束**：三子图（enc/erb_dec/df_dec）无环状态 I/O——GRU 内化，每次 `Run()` 沿动态 S 轴从零态出发；官方流式状态在 Rust 运行时（tract SimpleState/DFState/DfTract 滚动缓冲）（`:53-59`）；逐帧 `Run(S=1)` 会清零状态结果错误；流式二选一 = **块式 Run 接受接缝误差** 或 **改图暴露状态重导出**（社区 shimondoodkin/deepfilter-rt 无 LICENSE 仅可参照）（`:93-97`）。
- 张量形状与勘误：enc 输入 feat_erb `[1,1,S,32]`、feat_spec `[1,2,S,96]`；df_dec 输出 coefs 末维 **10 = df_order 5 × 2（实虚）**——另一输出语义须实现期对图签名自检（`:87-91`）。
- 官方对拍资产：export.py 以 ORT CPU 对拍（rtol 1e-6/atol 1e-5）且随包产出 npz 输入输出夹具可直接当 golden（`:81-85`）；parity 对拍目标钉死 = 官方 `deep-filter` CLI，lsnr 阈值 −15/35/35 四方一致（`:98`）；CLI 有 Releases 预编译二进制（win/linux/mac/arm，只接受 48kHz wav）`:76`——**golden 生成不需要本地 Rust 工具链**。
- 许可证：代码 MIT/Apache-2.0 双许可；models/ 目录无独立 LICENSE【倾向覆盖，发版前书面确认，挂 NFR-C2】（`:108-114`）。
- 算力：RTF 0.19（论文，笔记本单线程）vs GTCRN 0.07@i5-12400（官方 README）；参数 950K vs 48.2K（约 20 倍）——「DFN3=质量档、GTCRN=快速档」分工成立（`:122-131`）。
- 无 RSS 实测数据【未确认】（`:208-238` 结论分级）；上游仓库休眠（最后 release 2023-08）——无上游修复可依赖。

## 2. 设计决策

### D1 包范围：DFN3 集成 + FastEnhancer 评估记档

候选：
- ✅ **(a) DFN3 质量档集成 + FastEnhancer 评估结论记档（不集成）**。
- (b) 双引擎同时集成（DFN3 质量档 + FastEnhancer 快速档接棒）。
- (c) 只做选型结论不集成（纯评估包）。

论证：
- (b) 否决：frontier `:207` 给 FastEnhancer 定的准入条件 = **Windows/ORT 冒烟 + VB-DMD 复测 PESQ**，而 Windows 真机验证正随 round-4 挂账，本包执行期内该门槛不可闭合；FastEnhancer 的 PESQ 3.13 数字来自 16k VB-DMD，其 48k 全频带变体官方未给质量数字，直接接棒快速档证据不足（反证自查：若 48k 变体质量显著劣化，接棒即伤害现有可用档位）。包体翻倍（两套前端+两套 parity）且重复解决同一问题。
- (c) 否决：选型结论实质已由 v1.4 注（`REQUIREMENTS.md:301`）与 frontier `:207` 给出（DFN3 集成照旧 + FastEnhancer 列接棒候选），纯选型无增量；M2 DoD 要求 FR-4 验收落地，NFR-Q1 明写「其余引擎随各自工作包落地时同标准接入」。
- (a) 成立：DFN3 调研已完备到可实现（config 实测、图结构、npz 夹具、阈值钉死、官方模型包直用）；FR-4 基线文案就是「快速档 GTCRN + 质量档 DFN3」。FastEnhancer 的评估记档（准入门槛 + 证据链引 frontier §1.1）落本 spec §5 留缝与验收目录 report，M2 内可随 Windows round-4 解挂后补跑冒烟。

代价（如实列）：质量档 RTF 贴 NFR-P2 下限（OQ-5）；FastEnhancer 接棒延后，快速档在接棒前维持 GTCRN（现状无退化）。

### D2 引擎形态：dfn3denoiser 块式 Run + 前端 DSP 自持 + 引擎内部定长块

新增 `src/service/dfn3denoiser.{h,cpp}` 实现 IStreamEngine，形态裁定点：

**(1) 流式路径**（research `:93-97` 二选一）：
- ✅ **块式 Run**：引擎内部把样本缓冲成定长内部块（量级 30s 级，候选 10/30/60s → OQ-2），攒满一块对三个子图各做一次大 S 的 `Run()`（GRU 状态在图内沿 S 演进，块内数学正确），flush 收尾块。
- (b) 状态化图重导出（自研导出脚本把 GRU 状态提为图 I/O，参照官方 export.py + 无 LICENSE 社区 shim 仅作思路参照）：任意块长状态连续、严格满足现有分块不变性口径、且 M4 实时化必需——但成本高（改官方导出 + 自验），且离线包非必需。
- (c) 每次调用重放全部历史特征恢复状态：O(N²) 计算量，否决。
- (d) 攒整段 flush 一次 Run：违反 NFR-P4 严格流式红线（2h 素材全驻留），否决；「整段单 Run」仅作为 parity 专项用例的 DUT 模式（短夹具，见 D7）。

**关键性质自查（反证）**：块式 Run 是否破坏 IStreamEngine 分块不变性（engine_test 口径，`src/test/engine_test.cpp:2-3`）？——不破坏**对调用方**的不变性：接缝只发生在引擎自己的内部块边界，与调用方（FFTranscoder/engine_test）如何分块喂入无关；引擎内部缓冲吸收调用方分块 → 同输入不同调用块帧数输出仍逐字节一致。真正变化的是「与整段单 Run 的等价性」：内部块边界 GRU/DF 滚动缓冲/lsnr 平滑清零 → 接缝误差（每块一次）。因此验收拆两层：整段单 Run 模式对官方 CLI 走 −60dB RMS 硬判据；生产块模式新增接缝专项用例量化误差（FR4-PAR-SEAM），若可闻再回退加大块长或提前启动状态化图重导出（OQ-2）。

**(2) 前端 DSP 复刻清单**（全部自研在引擎内，状态自持）：
- vorbis 窗 STFT/WOLA（窗函数复用 `aidenoiser.cpp:73-74` vorbis 分支；WOLA÷Σw² 沿用 WP-1.1 口径，但 DFN3 帧网格 = 960/480 无 center-reflect，含 lookahead=2 的帧对齐约定实现期对官方 npz 夹具校准）。
- ERB filterbank（32 带，每带最少 2 bin）；feat_erb log 域 + 指数均值归一（alpha）；feat_cplx 单位范数（96 维）。
- lsnr 三档门控 apply_stages（阈值 −15/35/35，research `:98` 钉死）；DF 滚动复数滤波（coefs 末维 10 = 5×2 实虚）；post_filter；atten_lim 混合；静音跳过（rms<1e-7 ×5 帧）。
- 中间特征逐段对拍官方 npz 夹具（enc_input.npz 等）作 parity 排障梯子（D7）。

**(3) 规格**：`inRate()=48000`、`inChannels()=1`（DFN3 只接受 48k 单声道——CLI 同口径 research `:76`；FFTranscoder 的 SwrContext 负责源→48k/1ch 换算）；**`lockCanonicalOutput=false`**——输出跟随源（48k 源全链免重采样，NFR-Q3 首个 AI 引擎增益；非 48k 源回源率由 swr 承担，与 transcode/denoise/pitch 同律）。候选对照：沿用 `lockCanonicalOutput=true`（与 GTCRN 一致）否决——那会强制 48k→44.1k→48k 往返，正是 NFR-Q3 要消灭的路径，且 canonical-rate-48k.md `:17` 已为此预留口径。
- 反证自查：非 48k 源（如 44.1k）会经历 44.1→48（入引擎）→44.1（回源）两次重采样——与 GTCRN 路径（44.1→16→44.1）代价同型，无劣化；48k 视频源（主场景）零重采样，收益定向。

**(4) 隔离与屏障**：PIMPL + ORT 会话×3（enc/erb_dec/df_dec 各一）；process/flush `catch (Ort::Exception)` 归口 lastError、`SetIntraOpNumThreads(1)`——逐条照抄 aidenoiser 先例（`aidenoiser.cpp:343-345/:355-357/:233`）；内存口径参照 wp-1.4 `DisableCpuMemArena()`（三会话 arena 叠加，FR4-RES 实测裁定）。

**(5) 参数来源**（独立裁定点，未收敛 → OQ-1）：DFN3 参数在包内 `config.ini` 而**不在 ONNX metadata**（与 GTCRN 不同、与 MDX「无 metadata 查表」也不同型——DFN3 有官方参数载体）。候选：(a) config.ini 伴读（解析 `[df]`/`[deepfilternet]` 段）+ 关键维度对图输入 shape 断言；(b) 参数内置常量表 + 图 shape 断言防错模型（MDX 先例推广）；(c) 纯图签名自推导（不完整——lsnr 阈值/atten_lim/post 参数不在图里）。倾向 (a)：官方载体单一来源、自由导入第三方 DFN3 包不依赖我方内置表；代价 = config.ini 解析器 + 缺文件错误路径（与 OQ-4 目录形态联动）。

代价（如实列）：前端 DSP 复刻是本包最大工作量与最大 parity 风险面（任何归一化/门控偏差直接 FAIL）；三会话内存叠加需实测；接缝误差未量化前存在可闻性风险（v0.5.1 audible artifacts 前车之鉴，research 结论分级段）。

### D3 命令面：新表条目动词（aiDenoise 保持零漂移）

候选：
- (a) aiDenoise 加 `quality=fast|quality` 枚举 ParamSpec。
- ✅ **(b) engineTable 新条目、独立动词**（命名候选 aiDenoiseHq / denoiseHq / denoiseQuality → OQ-3；下文暂记 `denoiseHq`）。
- (c) 只进 GUI 不进命令层。

论证：
- (a) 否决：`quality` 实为**模型选择策略**（选哪份权重、哪套工作点）而非引擎处理参数——条目 `create` 闭包将被迫按参数二选一引擎类，破坏「条目 = 单引擎工厂」的注册表语义（`engineregistry.h:32-43`）；且两级缺省解析按 forVerb 选模型（`ffextractor.cpp:139-170`），quality 参数与模型解析双轨并行会产生「quality=quality 但 model= 显式给了 GTCRN」这类组合矛盾需要仲裁。FR-8 spec §5 留缝也预定了「不做档位选择参数（随选型包定）」。
- (c) 否决：违反 FR-24 单核心架构（GUI 是命令层薄壳，`commandlayer.h:3-6`）。
- (b) 成立：NFR-M1 承诺形态的最小验证——表条目落地后 --json/pipeline steps/batch steps/GUI 全通路自动可达；aiDenoise 旧行为零漂移**平凡成立**（不触碰其条目）；新条目 `params={}`（v1 不暴露 lsnr/atten_lim 可调项，§5 留缝）、`requiresModel=true`、`setupErrorClass=ErrCode::Model`、`lockCanonicalOutput=false`（D2）、`outputSuffix` 新增（如 `_dfn3`，data.h 常量）。

代价：动词面 +1（契约文档与 MIST 文案面同步）；「AI 降噪」一词在 GUI 指两档需文案区分（D6）。

### D4 模型管理：目录表第三条目 + tar.gz 包形态

裁定点：
- ✅ **目录表加第三条目**：id=`dfn3`、forVerb=**新动词**（独占，见下）、relPath 落 `denoiser/` 用途目录（purposeDir 按 forVerb 首条目自动归位，`modelmanager.cpp:373-379`）、directUrl=GitHub Releases 官方 `DeepFilterNet3_onnx.tar.gz`（gtcrn 同源直连先例，hfSource=false 不受镜像前缀影响）、sha256=整包哈希（fetch-deps.sh 同源维护，`scripts/fetch-deps.sh:14-19` 加行）、license=`MIT 系（倾向，发版前书面确认）`（红线表 `REQUIREMENTS.md:218`，挂 NFR-C2）、sizeBytes=7,983,136（research `:51`）。
- **forVerb 独占的裁定**：若 dfn3 与 gtcrn 共用 forVerb=aiDenoise，resolveDefaultModel「首个 installed 且非 bundled 命中」变为目录表顺序敏感的脆弱仲裁（`ffextractor.cpp:142` 注释「v1 每用途一模型无歧义」被破坏）。forVerb=新动词使两级解析天然无歧义、modelMissing 引导文案天然分流——**多档位 = 多用途目录**，与 FR-8 D1「命令层 model=绝对路径契约零改」边界吻合。
- **多文件包形态**（tar.gz 内 3 onnx + config.ini，无逐文件官方 URL）：候选 (a) ✅ 单条目 tar.gz 下载后解包到 `denoiser/<包名>/`，幂等口径适配为「解包目录存在 + 关键文件（config.ini/三 onnx）在位即信」（sha256 校验对 tar.gz 整包，.part 续传复用）；(b) 多文件条目（CatalogEntry 扩文件清单字段）——模型清单/导入/渲染面全部要动，否决；(c) 预解包再分发到自管镜像——引入自管基础设施 + 偏离官方产物，否决。细节（解包失败清理/自由导入目录试开）未收敛 → OQ-4。
- GUI 模型面三处映射加行：kForText（`mainwidget.cpp:545-547`）、onModelMissing（`:599-601`）、自由导入用途组合框（`:504-505`）——文案「AI 降噪（质量档）」。

代价：下载器从纯单文件流加一段解包+目录校验（FR-8 主链改动最小化但非零）；models.json 登记形态从单文件变目录条目（listModels 三视角合并的磁盘实况视角需识别目录形态）。

### D5 GUI：档位下拉 + 三通路同面

- **档位 UI**：denRow 加档位下拉（「快速（GTCRN）/ 质量（DeepFilterNet3）」，`mainwidget.cpp:167-172` 落点），缺省快速 = 现状零漂移；QSettings 新键（如 `aiDenoiseTier`）只主线程读写（humBase 先例 `:100/:213-215`，键名进 data.h 常量）。
- **三通路组包**：doDenoiseAiWav（`:818-823`）按档位选动词（快速→aiDenoise / 质量→denoiseHq）+ resolveDefaultModel(对应 forVerb)；doPipeline（`:854-876`）与 doBatchStart（`:698-749`）组包时「AI 降噪」步按当前档位映射动词（与 humBase/semi 动态注入同型，FR-9/10 片 B 先例）；ffextractor 侧 resolveStepModels（`:533-559`）加新动词分支。批动作组合（`:56-63`）与管线预设（`:45-49`）的「AI 降噪」条目文案不变，档位是全局偏好而非逐条目参数——是否联动档位给两候选（联动 ✅ / 固定快速档+独立质量档条目），倾向联动（否则质量档在管线/批不可达，FR-4 落地不完整），未收敛 → OQ-6。
- **缺模型文案**：新动词问询文案沿 MIST 族逐字锁定先例新增（如「找不到 AI 降噪（质量档）模型：%1」）；旧 aiDenoise 文案（`ffextractor.cpp:475`）逐字不动。
- busy 锁/取消/进度：零改（走既有 worker 调度）。

### D6 验收：parity 对官方 CLI + 接缝专项 + 资源断言

- **parity 参考**：官方 `deep-filter` CLI Releases 预编译二进制（linux 跑 CI、win 随 round-4）生成 golden——无需本地 Rust 工具链，NFR-Q1「DFN3↔官方 Rust runtime」以此闭合；官方 npz 夹具（enc_input.npz 等）作中间特征梯子（research `:81-85`）。判据 −60dB RMS（全族同口径）。
- **两层判据**（对应 D2）：①整段单 Run 模式（engine_test dfn3 动词的专项参数）vs CLI golden → −60dB RMS 硬门禁；②生产块模式 vs 整段单 Run → 接缝误差专项（FR4-PAR-SEAM，量化每块一次的边界误差，给定阈值准入；阈值候选：全域 RMS −60dB / 接缝邻域局部判据，未收敛 → OQ-2）。
- **资源**：RTF 断言（论文 0.19 ≈5.3× 贴 NFR-P2 ≥5× 下线，机器口径不同必须实测；阈值与不达标处置 → OQ-5）；RSS 断言（NFR-P4，2h 素材峰值 ≤2GB，三会话 + arena，参照 wp-1.4 `DisableCpuMemArena()` 实测裁定）。
- **零漂移硬门禁**：旧 aiDenoise 全族回归（CMD-021r 逐字节、WS/MDL/PIPE/BATCH 族、GUI-EQV 含 `ffextractor.cpp:475` 文案）——D3 形态下应平凡通过，仍列硬门禁防回归。
- 用例族命名 FR4-*（§6）。

### D7 文档与 CI 接线

- `docs/spec/fr24-command-layer.md` 动词表加行；`fr8-model-management.md` 契约不回改（spec 不回头维护，登记点在 acceptance）；CLAUDE.md 架构节加 dfn3denoiser 行与 FR-4 落地条目。
- `scripts/fetch-deps.sh` 加 DFN3 tar.gz 行（URL+sha256）；`run_parity.sh` 加 PAR-DF 族（依赖缺失整族 SKIP、`PARITY_REQUIRED=1` 升级 FAIL，沿用分级口径）。
- engine_test 加 dfn3 动词：输入 48000Hz 1ch 交错 f32、参数 = 模型目录 + 内部块长（0=整段单 Run 供 parity），usage 同步（`src/test/engine_test.cpp:4-7` 先例）。

## 3. 方案（模块与数据流）

```
main.cpp --json ─────────┐
GUI worker 槽（壳）──────┼→ CommandLayer::execute（信封/校验零改）
                          │     └ engineTable()["denoiseHq"(暂名)]   ← 新条目 = 唯一命令层接触点
                          │           ├ create → Dfn3Denoiser（IStreamEngine，PIMPL）
                          │           │     ├ 前端 DSP（自研状态自持）：vorbis STFT/ERB/feat/lsnr 门控
                          │           │     ├ ORT 会话 ×3（enc/erb_dec/df_dec，官方图直用）
                          │           │     └ 引擎内部定长块缓冲（对调用方分块不变；块界=接缝）
                          │           ├ lockCanonicalOutput=false → 输出跟随源（48k 直通，NFR-Q3）
                          │           └ requiresModel=true → staticCheck 模型存在性（E_MODEL）
                          ├ probeSourceSpec → 48k 源免重采样 / 非 48k 源 swr 换算（FFTranscoder 既有）
                          └ 工作区版本链/批/管线编排（零改复用；词表=引擎表枚举自动入链）
模型侧：modelmanager 目录表第三条目（dfn3/tar.gz/解包目录）→ purposeDir 自动归位 denoiser/
        → resolveDefaultModel(forVerb=denoiseHq) 两级解析 → modelMissing 引导（新文案）
GUI：denRow 档位下拉（QSettings aiDenoiseTier）→ 单步/管线/批三通路按档位映射动词
```

正例（质量档单步，48k 视频源）：GUI 档位=质量 → doDenoiseAiWav(denoiseHq) → resolveDefaultModel 命中库内 dfn3（或 appDir 随包兜底）→ execute → probeSourceSpec=48k/2ch → swr 仅并声道至 1ch → 引擎内部块 Run → swr 回 48k/2ch → 工作区 `vNNN_denoiseHq/`，全链无重采样往返。

正例（管线联动）：档位=质量 + 预设「降噪+变调」→ steps=[{cmd:denoiseHq},{cmd:pitch,semi}] → resolveStepModels 解析 dfn3 → 整链预校验 → 逐步版本记录（pipeline 标记照旧）。

失败路径（模型缺失）：staticCheck 模型不存在 → E_MODEL → runRequest 结构化 modelMissing(forVerb=denoiseHq) → 问询文案「AI 降噪（质量档）」引导模型管理对话框 → 下载 tar.gz → 解包登记（幂等=目录+关键文件在位）。

## 4. 改动面

| 文件 | 片 | 改动 | 预估规模 |
|---|---|---|---|
| `src/service/dfn3denoiser.{h,cpp}` | 新增 | 引擎本体：前端 DSP 复刻 + 三会话 ORT + 内部块缓冲 + config.ini 伴读（OQ-1） | ~700 行（本包主体） |
| `src/service/engineregistry.cpp` | 表 | denoiseHq 条目（params={}/requiresModel/Model/E_Model 类/lockCanonical=false/新后缀） | ~15 行 |
| `src/model/data.h` | 常量 | kDfn3ModelRelPath / 新后缀 / kSettingsAiTier | ~5 行 |
| `src/service/modelmanager.{h,cpp}` | 目录表+下载 | dfn3 条目 + tar.gz 解包段 + 目录形态幂等/登记/listModels 磁盘视角 | ~120 行 |
| `src/service/ffextractor.cpp` | worker 壳 | doDenoiseAiWav 档位参数 + resolveStepModels 新动词分支 + 新缺模型文案 | ~30 行 |
| `src/view/mainwidget.cpp` | GUI | 档位下拉+QSettings + 三通路组包 + kForText/onModelMissing/导入组合框三处映射 | ~60 行 |
| `src/test/engine_test.cpp` | DUT | dfn3 动词（48k/1ch、模型目录+内部块长参数） | ~25 行 |
| `CMakeLists.txt` | 构建两行 | engine 源列表 + 头文件 | 2 行 |
| `scripts/fetch-deps.sh` | CI | DFN3_URL/DFN3_SHA + 解包逻辑 | ~15 行 |
| `docs/spec/fr24-command-layer.md` / `CLAUDE.md` | 文档 | 动词表行 / 架构节行 | ~10 行 |
| `docs/acceptance/fr4-quality-tier/` | 验收 | goal/plan/matrix/round/scripts 骨架 + run_parity.sh PAR-DF 族 | 见 §6 |

零触碰清单：commandlayer.cpp 信封与校验序（新动词纯表驱动增量）、dspdenoiser/pitchshifter/vocalseparator、workspace 全部、FR-8 下载器主链（.part/Range/rename 不动，仅追加解包段）、旧 7 薄壳与 GUI 旧文案、fr9/fr10 编排器。

## 5. 留缝清单

| 缝 | 内容 | 触发条件 |
|---|---|---|
| FastEnhancer 接棒快速档 | 准入门槛记档：Windows/ORT 冒烟 + VB-DMD 复测 PESQ + 48k 变体质量数字（frontier `:207`/§1.1）；接棒形态 = 新表条目新动词（D3 同型），档位下拉改「快速档」指向，aiDenoise/GTCRN 届时去留再议 | Windows round-4 解挂后 M2 内补评，或 M4 前 |
| M4 实时链 | DFN3 状态化图重导出（逐帧状态连续版官方图，社区 shim 仅参照无 LICENSE）；DFN3 ~40ms 占 150ms 预算 vs FastEnhancer 6.25–16ms；O3 延迟真机脉冲实测 | M4 实时化启动 |
| INT8 量化 | ORT 1.30 CPU EP 对 GTCRN/DFN3/FastEnhancer QDQ INT8 前后 RTF/精度对比（Windows 真机单线程绑定），RTF 贴线时的兜底手段（frontier `:195-201`） | M4 前置实验；若 OQ-5 实测不达 5× 则提前 |
| 引擎可调参数 | lsnr 阈值/atten_lim/post_filter 强度不进 v1 命令面（params={}），留 ParamSpec 扩展位 | 用户需求出现 |
| 第三档位 | 档位下拉与 forVerb 独占设计天然容纳第三档（如 aTENNuate 类中间档），无需预留死代码 | 候选成熟 |
| AI 路径回源率 | GTCRN 44.1k 锁的后续可选优化（canonical-rate-48k.md `:18`）独立于本包 | 独立小包 |

## 6. 验证方案

目录骨架：`docs/acceptance/fr4-quality-tier/`（goal.md + plan.md + matrix.csv + round-N.md + scripts/；parity 资产归 `docs/acceptance/engine-parity/` 扩 PAR-DF 族，与其余四族同框架）。

用例族（FR4-*）：

| 族 | 覆盖 | 代表用例 |
|---|---|---|
| FR4-PAR | NFR-Q1 parity | FR4-PAR-001 整段单 Run vs deep-filter CLI golden ≤−60dB RMS；FR4-PAR-SEAM 生产块 vs 整段（接缝量化+阈值准入）；FR4-PAR-FEAT 前端中间特征 vs 官方 npz 夹具（排障梯子）；FR4-PAR-SR 48k 源全链免重采样 bit-exact（PAR-SR-001 同型） |
| FR4-OK/NEG | 命令面 | denoiseHq 正例（--json + 工作区版本 vNNN_denoiseHq）；NEG：未知字段/model 不存在 E_MODEL/workspace+output 互斥/output==input 原件只读 |
| FR4-EQV | 零漂移硬门禁 | 旧 aiDenoise CMD 全族逐字节（CMD-021r）；WS/MDL/PIPE/BATCH 族回归；GUI-EQV 含「找不到 AI 降噪模型：%1」逐字 |
| FR4-REG | 注册表连锁 | pipeline steps 含 denoiseHq 步（自动入链零改）；batch 模板单步 denoiseHq；词表外动词 E_PARAMS 照旧 |
| FR4-MDL | 模型管理 | modelList 三视角含 dfn3（含许可证列）；下载幂等（目录在位即信、断点续传）；导入 by-id/自由导入（目录试开）；modelMissing 引导弹流 |
| FR4-GUI | GUI 三通路 | 档位下拉记忆（QSettings）；单步/管线预设/批动作三面质量档可达；档位=快速时全链与 FR-10 基线零漂移 |
| FR4-RES | NFR-P2/P4/P5 | RTF 实测 ≥5×（阈值 OQ-5）；2h 素材峰值 RSS ≤2GB（三会话+arena，含 DisableCpuMemArena 对照）；模型按需加载卸载 |

门禁接线：FR4-PAR/F4-RES 进 ci-smoke（run_parity.sh PAR-DF 族，依赖缺失 SKIP、`PARITY_REQUIRED=1` 升级）；FR4-OK/NEG/EQV/REG 随 ci-smoke CMD 族；FR4-GUI 探针随 round-2 型 widget 面验证；Windows 真机全族随 round-4 挂账。

## 7. OQ 清单（供主线程拍板；本 spec 不含 §7.1）

- **OQ-1 引擎参数来源**：(a) config.ini 伴读 + 图 shape 断言【倾向：官方载体单一来源，自由导入不依赖内置表】；(b) 内置常量表 + 断言（MDX 先例推广，少一个解析器但双源漂移）；(c) 混合（结构参数伴读、阈值类内置）。影响：D2 实现面、FR4-NEG 用例（缺 config.ini 错误路径）、OQ-4 目录形态。
- **OQ-2 内部块长与接缝判据**：块长候选 10s/30s/60s（Run 次数 vs 接缝频度 vs 首块时延）；接缝判据候选全域 −60dB / 接缝邻域局部阈值；若 SEAM 用例证明可闻，是否提前启动状态化图重导出（D2(b)）。影响：D2 核心、FR4-PAR-SEAM 准入线。
- **OQ-3 新动词命名**：aiDenoiseHq / denoiseHq / denoiseQuality【倾向 denoiseHq：短、与 aiDenoise 并列可读、避免 quality 一词与参数面混淆】。影响：D3/D4/D5 全部文案与契约文档、models.json forVerb 值。
- **OQ-4 tar.gz 包形态细节**：(a) 下载整包→解包目录→目录级幂等【倾向】；(b) CatalogEntry 扩多文件清单；(c) 解包后逐文件登记 models.json。连带：解包失败清理（登记失败删产物不留孤儿红线）、自由导入的目录试开口径（config.ini 缺失时按 OQ-1 决定拒绝或内置兜底）。影响：D4、FR4-MDL 用例。
- **OQ-5 RTF 断言与不达标处置**：论文 0.19 与 NFR-P2 ≥5× 贴线，Linux CI 机器口径未定。候选：实测阈值放宽至参考机标定值并注记 / 不达标时修订 NFR-P2 分档（快速档 ≥5×、质量档 ≥3×）/ 提前 INT8 兜底。影响：D6、NFR-P2 文本（需 REQUIREMENTS 修订流程）。
- **OQ-6 GUI 管线/批的档位联动**：(a) 「AI 降噪」步按全局档位映射动词【倾向：三面同达，FR-4 落地完整】；(b) 固定快速档 + 独立「AI 降噪（质量档）」条目（显式但动作组合翻倍）；(c) 仅单步通路支持质量档（否决倾向：管线/批不可达违背同面原则）。影响：D5、FR4-GUI 用例、MIST 文案面。

## 7.1 主线程拍板（2026-11-04，设计轮三报告交叉对照后）

三报告：spec 作者（D1–D7/OQ 六条）+ 对抗约束枚举（H/I/X/Y/Z/U/W 五面，八承重锚点主线程逐条实读核实无误）
+ 实测取证（`docs/reference/fr4-dfn3-measurement.md`，本机 i5-14400F/8vCPU、产线 ORT 1.30.0 直链 C++ 探针；
spec 五锚点亦核实）。实测关键数字：DFN3 三图链 RTF **0.0147（S=100 块，1T）/ 0.0169（S=10）/
0.0402（S=1 调用模式）**，加载 23.1ms，RSS 加载后 +20.8MB/稳态 S=100 +49.4MB（arena）；GTCRN 基线
RTF 0.030@16k、RSS +11.6MB。裁定：

- **D1 维持 (a)**：DFN3 集成 + FastEnhancer 评估记档。实测背书 DFN3 全档实时可行且远超 NFR-P2；
  FastEnhancer ONNX 因网络死路由（clash TUN fake-IP，github 全域不可达；境内渠道穷举无果——
  ModelScope 仅 NPU 专有 .axmodel 格式 ORT 不可载）未获取，契约面经官方驱动逐字取证=完美
  （wav 进/hop 出 + 显式 cache 回灌，GTCRN 同构）但无一手 RTF/质量数据，**不盲拍**——留缝附裁 D。
- **OQ-1 → (a)**：config.ini 伴读 + 图输入 shape 断言；目录缺 config.ini/三图不全 → E_MODEL 拒收
  （自由导入同口径，create=权威校验器原则不变）。
- **OQ-2 → 块长 30s（S=3000）缺省 + 附裁 C 接缝设计**；SEAM 判据=生产块模式 vs 整段单 Run 全域
  −60dB RMS；不达走梯子：K 升档（100→200→400）→ 块长升 → 状态化手术（触发即回主线程重开设计轮）。
  - **修订（round-3 主线程终裁，数据 fr4 round-1 §3 / round-2 §三）**：梯子走完实测——K 扫描
    100→3000 单调改善 −12.8→−24.9dB **不收敛**（根因=注意力记忆有效无限，K=5000 掩码偏差 0.10
    不归零），−60dB 在无状态图块式架构下物理不可达；块长升不消除单缝偏差且状态驻留涨；状态化手术
    定性 M4 实时化工作（设计级变更，非 FR-4 范围）。**裁定：维持 K=100/块 30s；SEAM −2.5dB
    （合成素材基线 −3.4+0.9 余量）定格为回归护栏，不作质量门槛**——偏差两端均为合法 DFN3 输出、
    无感知劣化证据；NFR-Q1「不过不上线」由整段单 Run −60dB 硬门守护（合成 −75.3dB / 真语音
    −74.1dB vs golden，生产总偏差=接缝主导 −12.8dB 已量化记档）。附裁 C 的「L=2 扣留后缀」在
    实现期重解释为「图已内化前瞻、行 p 随帧 p 即发」（正确性同上背书，round-1 记档）。
- **OQ-3 → aiDenoiseHq**（改 spec 倾向 denoiseHq：`denoise*` 前缀已被 DSP 降噪占用，DFN3 是 AI
  引擎，族名自文档且动词表排序相邻）；后缀 **`_aihq`**（语义后缀，防 FastEnhancer 接棒时 `_dfn3`
  陈旧）；data.h 常量 kSuffixAiHq / kDfn3ModelRelPath / kSettingsAiTier。
- **OQ-4 → (a)**：整包下载→解包目录→目录级幂等（关键文件集 config.ini+三 onnx 在位即信）；
  tar sha256=`c94d91f70911001c946e0fabb4aa9adc37045f45a03b56008cb0c8244cb63616`（实测登记）入
  目录表 + fetch-deps.sh 同源；解包/校验失败删产物不留孤儿；models.json 登记形态扩目录条目、
  listModels 磁盘实况视角识别目录形态；directUrl=官方仓 models/ 路径（Releases 无此 tar——附裁 F），
  镜像可达性按 R4 记档（jsDelivr 备选注释）。
- **OQ-5 → 不修 NFR-P2**：本机实测 68× 实时（0.0147）远超 ≥5×；FR4-RES 断言=可用性线 RTF ≤0.5 +
  基线数字登记（0.0147/0.0110@2T 注记机器口径；CI 不锁论文数字——对抗 W1）；引擎线程口径沿
  aidenoiser 单线程先例（2T −25% 记档不启用）。
- **OQ-6 → (a)**：全局档位下拉（「快速（GTCRN）/质量（DeepFilterNet3）」缺省快速=零漂移，
  QSettings aiDenoiseTier）；单步/管线预设/批动作三通路组包时 aiDenoise↔aiDenoiseHq 按当前档位
  映射（与 humBase/semi 动态注入同型）；组合条目文案不变。

附裁（对抗报告与实测校准）：

- **附裁 A（对抗 X5，spec 漏项）输出长度约定**：官方 delay=1440 补偿是 opt-in；实现期对 golden
  实测钉死本引擎输出长度约定，PAR-DF 判据显式含长度匹配（对齐后 −60dB RMS）。
- **附裁 B parity 参考主链改官方 CLI 本地构建（实测推翻 spec D6 两处前提）**：官方 tar 仅 4 文件、
  npz 夹具不随包（export.py 导出期 torch.randn 现场生成从不入库）且 Releases 无预编译 CLI 二进制。
  环境已探明：apt（aliyun）rustc/cargo 1.93.1 可装 + crates.io CDN 可达 + 官方完整克隆在手
  （`build-linux/.fr4-tmp/DFN3-official/`，commit f1d19bff，含 checkpoint）→ **golden 主链 =
  cargo 构建官方 deep-filter CLI**（NFR-Q1「DFN3↔官方 Rust runtime」字面兑现）；备选 pip
  deepfilternet 0.5.6（需 py≤3.13，本机 3.14 无 torch wheel）与网络恢复后 Releases 预编译。
  依赖分级 SKIP / PARITY_REQUIRED=1 升 FAIL 口径照旧；已生成图级回归夹具 7 件（sha256 登记，
  third_party/models/candidates/dfn3/fixtures-graphlevel/）作图行为回归梯子——只锁图不锁特征
  管线，特征正确性由端到端 −60dB 承载。
- **附裁 C 接缝设计（D2 实现面强化，回应对抗 X1/W6）**：块 Run 须带 **warmup 前缀 K=100 帧
  特征重放**（特征出自我方前端可缓存；S=K+块帧数+L 一次 Run，重放令图内 5 个 GRU（enc1+erb2+df2）
  洗出，弃 washup 段输出，≈3% 算力开销；**首块无历史不重放**——官方冷启动即零态）+ **L=2 帧
  前瞻后缀**（conv_lookahead 块尾上下文， withheld 2 帧输出随下块/flush 发出）；mean_norm/
  unit_norm/rolling_spec_buf 宿主状态本就在我方前端跨块连续。「对调用方分块不变」仍由引擎内部
  缓冲保证（spec D2 论证不变）。
- **附裁 D FastEnhancer 留缝**：官方 Releases 资产清单已核（16k/48k × t/b/s/m/l + conv-stft 变体）；
  网络恢复 runbook 5 分钟获取（测量报告 §2.2）+ 1 小时补测（RTF/PESQ）；触发=接棒快速档评估或
  M4 实时链选型，届时按 D3 同型（新表条目新动词）再裁；其 48k hop512 契约与 canonical-48k 对齐
  记档（前沿档 §1.1）。
- **附裁 E 残余法务**：DFN3 权重目录无独立 LICENSE（代码双许可覆盖的灰色地带）+ FastEnhancer MIT
  为转录级（web reader 中转）——挂 NFR-C2 发版前复核，与 GTCRN/sherpa 书面确认同批（对抗 H1）。
- **附裁 F 研究档勘误记档**：research-deepfilternet3.md 两处与实测不符——npz 夹具「随包产出可当
  golden」实为导出期随机生成不入库；「CLI 有 Releases 预编译二进制」存在但 golden 语境改本地构建。
  勘误记本包验收 plan.md，reference 档不回改（falsify 先例为调研期内勘误，本轮不动）。

验收口径补充：FR4-PAR-FEAT 梯子改指图级夹具（7 件）+ 两层端到端判据（整段单 Run −60dB 硬门禁 +
SEAM 专项）；FR4-PAR-SR 48k 源全链免重采样 bit-exact（PAR-SR-001 同型）；零漂移硬门禁照 spec D6
（旧 aiDenoise 全族 + GUI-EQV 逐字文案）。

## 8. 红线自查与实现风险

红线自查：
- third_party/ 与权重永不入库：DFN3 权重走 fetch-deps.sh（Linux 测试）+ 模型管理下载（用户机），仓库零入库 ✓。
- 不引入 GPL：本包只消费官方 ONNX 权重 + 自研前端，不拷贝 DFN3 源码；DFN3 代码侧 MIT/Apache-2.0 双许可、权重 MIT 系（发版前书面确认挂 NFR-C2，与 GTCRN 同批）✓。
- 流式内存有界：引擎内部定长块（~30s 特征驻留），整段单 Run 仅限短夹具 parity 模式；FR4-RES 实测 2h 口径 ✓。
- 原件只读/append-only：新动词全走 execute 既有路径（workspace 模式、output==input 拒收 `commandlayer.cpp:733-734` 零改继承）✓。
- 共享资产动词不适用工作区：模型族照旧；denoiseHq 是任务产物动词、工作区模式默认支持 ✓。
- 分块不变性口径：对调用方不变性保持（D2 论证）；「与整段等价」口径变更为接缝专项用例，验收记录中显式登记 ✓。

实现风险（如实列）：
1. 前端 DSP 复刻精度是最大风险面（ERB 归一化/lsnr 门控/帧对齐任何偏差 → PAR 全红）；缓解：官方 npz 中间特征夹具逐段对拍（FR4-PAR-FEAT 梯子）+ export.py 对拍容差先例（rtol 1e-6）。
2. df_dec 输出语义勘误（coefs 末维 10 = 5×2 实虚；第二输出用途）——实现期必须对图签名自检，防「读法错」重演（vocalseparator 前车之鉴）。
3. 接缝可闻性未量化（v0.5.1 audible artifacts 前车之鉴）——FR4-PAR-SEAM 专项守门，失败回退路径已备（加大块长/状态化重导出）。
4. RTF 贴线（0.19 vs 5×）——OQ-5 处置预案齐全（阈值标定/NFR 分档/INT8 提前）。
5. 三 ORT 会话内存叠加——FR4-RES 实测 + DisableCpuMemArena 对照（wp-1.4 先例）。
6. 上游休眠（2023-08 后无 release）——图/包缺陷无上游修复，问题自扛；官方 npz 夹具是唯一对拍锚，golden 生成依赖 GitHub Releases 可达（CI 域 fetch-deps 同依赖，非新增风险）。
7. Windows 真机验证随 round-4 挂账叠加——本包 Linux 全绿 + Windows 挂账的口径与 FR-8/9/10 一致。
