# FR-9 处理管线串联（开工 spec）

- 日期：2026-11-03
- 性质：开工前快照（M2 第五个工作包，依赖序承接 FR-8；进展追踪见 `docs/acceptance/fr9-pipeline/goal.md`——实现开工时才建目录）
- 本文 D 编号（D1–D10）仅限本 spec，与 REQUIREMENTS.md 决策记录（D1–D7）无关。
- 现状基线：HEAD `fc69173`（FR-24 命令层 + NFR-M1 注册表/ErrInfo + FR-A4 工作区 + FR-8 模型管理已全落地并验收）。
- 本包不含：FR-7 RVC 变声本体（M3）、FR-10 批处理队列、FR-11 实时链、FR-17 预设系统——变声槽位以「注册表零改自动入链」前瞻（D2/§5），批处理以接口友好为限（D10）。

## 0. 需求锚点与约束

| 锚点 | 内容 | 对本包的约束 |
|---|---|---|
| FR-9（REQUIREMENTS.md:94） | 「降噪→分离→变声链式一键执行（核心差异化）」 | 三段链中**变声=FR-7 在 M3 才有**（REQUIREMENTS.md:92、v1-development-plan.md M3 段）：v1.0 步骤词表只有五个引擎动词，须显式裁剪 + 变声槽位前瞻（D2）；「一键」= GUI 预设链（D8） |
| 产品差异化（REQUIREMENTS.md:28） | 「一站式 + 管线串联」对 Python 单点工具的差异化 | 管线是命令层普通动词（CLI/GUI 同通路），不做 GUI 专用后门 |
| 架构基线（REQUIREMENTS.md:62-63） | 无头命令层：「每个**引擎/管线**可 JSON 进 JSON 出」 | `pipeline` 动词是 fr24 spec §5.2 留缝的兑现（fr24-command-layer.md:225「不做 engines 数组——多引擎链需要级间规格协商……是完整工作包，v1 预实现无法验证」——本包即该工作包） |
| 依赖序（v1-development-plan.md:38） | FR-24 → NFR-M1 → FR-A4 → FR-8 → **FR-9** → FR-10 | 接口对 FR-10 批处理友好（steps 是数据、input 可换）但不做队列本身（D10） |
| NFR-M1（REQUIREMENTS.md:185）+ fr-a4 留缝（fr-a4-workspace.md:211） | 「版本目录是一版本多产物的天然容器；parent 可扩展为数组（多父）——**管线包定义其版本形态**」 | 工作区集成语义须显式裁定（D5/D6），两代留缝（多产物目录 vs 每步一版本）择一 |
| NFR-P4（REQUIREMENTS.md:165） | 2h 音频峰值 RSS ≤2GB、禁止整文件载入 | 多遍链峰值内存论证（D1 内存小节 + PIPE-MEM 抽测） |
| FR-12 / NFR-R1（REQUIREMENTS.md:97,177） | 全任务可取消 ≤1s、失败不留半成品 | 链中段取消与部分失败语义裁定（D4/D5）；与 append-only 的张力须给明确解 |
| 红线（CLAUDE.md） | 新落盘动词提供 workspace 模式且不改写源文件；旧动词零漂移；流式内存有界；不引 GPL | §8 红线自查逐条对齐 |
| 回归面 | CMD 36 条 + CMD-EQV + GUI-EQV（fr24 matrix.csv）+ WS 族（fr-a4）+ MDL 族（fr8）+ ci-smoke 全量 + PAR-SR 三调用点 | 全部只打既有动词；`pipeline` 为纯增量动词，**既有动词响应逐字节零漂移是硬门禁**（D10/§6） |

## 1. 现状盘点（证据，HEAD fc69173 实读）

**转码底座承担全部重采样，引擎只声明规格**——架构主轴决策的核心事实：

- `transcodeStreamWav` 内建两个 SwrContext：`toAi`（解码源 → 引擎期望规格）与 `toOut`（引擎输出 → 输出规格），引擎接口只有 `inRate()/inChannels()` 声明（fftranscoder.cpp:140-141,162-179；streamengine.h:15-16），引擎内部不做重采样。
- 链中相邻引擎工作点互异是常态：AiDenoiser 16k 单声道（aidenoiser.h:7,16）、VocalSeparator 44.1k 立体声（vocalseparator.h:7,19-20）、DspDenoiser/PitchShifter 跟随源（engineregistry.cpp:40-54 工厂闭包直接传 srcRate/srcCh）。**进程内引擎直连没有现成的级间转换通路**（如 48k 源 denoise 输出 → aiDenoise 要 16k 单声道，中间必须有人做 swr）。

**命令层双查表与单动词路径**：

- 引擎表 5 条目（transcode/denoise/aiDenoise/pitch/separate，engineregistry.cpp:135-189）+ 内置表 7 条目（probe/extractAudio/extractVideo/wsInfo/modelList/modelDownload/modelImport，commandlayer.cpp:334-343）；execute 信封校验（未知字段白名单、`output==input` 原件只读防线、workspace 与 output 互斥）在 commandlayer.cpp:370-437。
- 单动词路径 `runEngine`（commandlayer.cpp:181-226）：validateParams（:184）→ semanticCheck（:186-189）→ 模型存在性（:190-195）→ probeSourceSpec（:197）→ create/setup 载模型（:198-205）→ direct 分叉 outputOr（:208-209）或 runInWorkspace（planVersion → runTranscode → commitVersion，:221-225）。**前半段全是静态校验（不碰模型不碰解码），后半段才执行——天然可分**（D3 的抽取缝）。
- `execute` 是纯函数、无全局态、每次调用自建/自毁引擎实例（commandlayer.h:20-21）——**管线执行器可以直接逐步组合 execute**（D9），串行重入性质是 FR-24 立下的既有承诺。

**工作区来源链已能表达「步步为链」，零改动即用**：

- planVersion 对「输入是本工作区某版本产物」记 parentVersion（findParentVersion，workspace.cpp:171-179,265）；commitVersion 把产物回填根级缓存（workspace.cpp:320-323），链式下一步可缓存命中免哈希定位工作区；缓存丢失时 resolveWsDir 按 hash8 扫根目录复用既有工作区（workspace.cpp:42-47）；版本号跳过孤儿目录单调递增不复用（workspace.cpp:181-190）。
- 版本目录内产物命名 = 输入 stem + 条目后缀（commandlayer.cpp:219-224）——链式输入自动得到可读的链式名（`x_denoised.wav` → `x_denoised_vocals.wav`）。

**GUI 通路**：七槽经 runRequest 组请求（workspace 注入、E_MODEL→modelMissing 结构化信号、取消/进度经 Ctx，ffextractor.cpp:419-448）；busy 锁一次一任务（mediaservice.h:26-27）；`onJobDone` 对「已取消」有**全文特判**（mainwidget.cpp:628——管线取消文案必须逐字兼容，D4）；模型两级缺省解析 resolveDefaultModel（ffextractor.cpp:147-170）。

**内存既有证据**：feedAi 分块后就地排水，2h 素材转码峰值 57MB（CLAUDE.md P0 #8）；分离路径 DisableCpuMemArena 后 120s 峰值 1.48GB（CLAUDE.md P0 #3）。引擎实例随 execute 调用建立/销毁——**多遍链峰值 = 逐步最大值而非总和**（D1 内存小节）。

## 2. 设计决策

### D1 架构主轴：多遍执行（每步 = 一次完整单动词作业，中间产物落盘 WAV(f32)）——不做进程内单遍串联

**候选**：
- (a) 多遍执行：管线执行器循环驱动既有单动词路径，每步产出中间 WAV 落盘，下一步以上一步产物为 input；✅
- (b) 单遍执行：进程内引擎串联（复合引擎/管线执行器直接串 process 调用，引擎间插 swr）；
- (c) 混合：相邻引擎规格一致段内直连、规格变更处落盘分段。

**论证（对照 §1 核心事实）**：
- (b) 的三重硬伤：① 级间重采样需新 DSP 底座——引擎间 swr + 引擎滞后（process 输出可滞后于输入，streamengine.h:8）的分段缓冲管理 + flush 顺序编排，fr24 spec §5.2 已论证这是完整工作包的量级，且本包没有实时化需求牵引（实时链是 FR-11/M4 的事，届时走 NFR-L2 `processInto` 路线，与本包正交）；② **全引擎同驻内存，峰值 = 各步之和**——wp-1.4 实测分离单独峰值 1.48GB，叠加 GTCRN 会话与第二个 ORT arena 直逼 2GB 红线（NFR-P4），而多遍逐步建毁引擎，峰值 = 最大值；③ 错误归属/进度/取消/版本化/p parity 面全部要在新复合执行器里重新发明，多遍则**逐项复用已验收机制**（CMD/WS 验收网自动成为管线的回归网）。
- (c) 否决：可直连的规格对只在少数组合成立（denoise→pitch 同跟随源可直连，aiDenoise/separate 工作点锁定必分段），分段判定逻辑 + 两套执行路径 = 双源漂移，收益仅省部分中间 I/O。
- (a) 的代价如实列出：① 中间 WAV 盘占用（2h@48k 立体声 f32 ≈ 2.76GB/步，见 OQ-4）；② 每步多一遍解码 + WAV 编码 + 登记哈希 I/O——f32 PCM 中间**无量化损失、无世代损失**，重采样次数两案相同（多遍下一步的 toAi = 单遍的引擎间 swr，同一 swr 库同参数），离线分钟级场景可接受。
- 反证自查（对 (a) 最强的反方论点「实时链终究要单遍，现在做多遍是白做」）：实时链（FR-11）的串联发生在**音频线程内**（processInto 零分配 + 块间直投），与命令层离线路径是两条代码路径，架构基线（REQUIREMENTS.md:76「实时与离线共用引擎接口」）共用的是 IStreamEngine 而非转码器——本包选多遍不封锁实时化，反而以「引擎不动、底座不动」保住了实时化要复用的引擎层。

**推荐 (a)**。执行器实现形态见 D9（逐步组合 execute，不是复制单动词代码）。

### D2 动词契约：`cmd:"pipeline"` + `steps` 数组；步骤词表 = 引擎注册表枚举；变声槽位零改前瞻

**请求示意**：

```json
{"v":1, "cmd":"pipeline", "input":"vlog.mp4",
 "workspace":"/home/u/VideoHandler-Workspace",
 "steps":[{"cmd":"aiDenoise", "model":"/abs/models/denoiser/gtcrn_simple.onnx"},
          {"cmd":"separate", "stem":"vocals", "model":"/abs/models/separator/UVR-MDX-NET-Voc_FT.onnx"},
          {"cmd":"pitch", "semi":2.0}]}
```

**候选（步骤词表）**：(a) 引擎注册表全体枚举（运行期查 engineTable()，不做第二份白名单）✅；(b) 管线专设白名单子集；(c) 扩到内置动词（extract/probe/model 族亦可入链）。
**取 (a)**：(b) 是双源——每加引擎要同步两处，直接违背 NFR-M1「新增引擎零改」对管线的延伸承诺；且 transcode 入链无害且有用（视频源第一步容器规整化），没有理由裁掉。(c) 否决：内置动词无音频流处理语义（probe 无产物、extract 是流复制非引擎、model 族无素材语义），入链无意义徒增校验分支；**嵌套 pipeline 也不在引擎表，天然拒收**（递归深度有界为 1，D9 安全性依据）。
**步骤对象校验（严格风格与既有动词同律）**：steps 必填、必须是数组、**至少 2 步**（链式动词的语义下限；单步=直接调单动词，Agent 计划归一时映射到单动词即可）；每步必须是对象、`cmd` 必填且须命中引擎表（否则 E_PARAMS「steps[i] 的 cmd 不可链式：%1（仅引擎动词可入链）」）；步骤允许字段 = `{cmd} ∪ isDeclaredParam(entry, key)`（engineregistry.cpp:255-260），**步骤内禁收 input/output/workspace/v**（链位次决定输入、信封决定 workspace）→ 未知字段一律 E_PARAMS（fr24 D2「幻觉参数 100% 被 schema 拦截」的前站延伸）。
**变声槽位前瞻（FR-9 需求原文「降噪→分离→变声」的 v1.0 裁剪）**：v1.0 词表 = 引擎表现有五动词；M3 RVC 落地 = 新引擎 .h/.cpp + 表条目 + CMake（NFR-M1 既有承诺），**管线执行器与步骤校验零改即自动可链**，GUI 侧增量仅是加一条预设（D8）——「变声槽位」不是本包的代码，而是注册表机制自带的空位。GUI/CLI 在 RVC 落地前的变调（pitch）可代变声位。
**steps 是数据不是引用（FR-10 前瞻）**：steps 数组是自包含的参数模板，不含任何与本次 input 绑定的信息——换 input 重放同一 steps 即批处理的单件形态（D10）。
**消息模板（沿既有风格，步骤号 1 基与用户直觉一致；steps 索引 0 基仅用于「steps[i] …」schema 类错误）**：`steps 缺失` / `steps 必须是数组` / `steps 至少需要 2 步` / `steps[1] 缺少 cmd` / `步骤 2（separate）：model 缺失`（静态校验失败前缀，见 D3/D4）。

### D3 整链预校验：开工前静态校验全部步骤（抽取 staticCheck 复用，绝不调 create）

**候选**：(a) 全链静态预校验通过才开工（validateParams + semanticCheck + 模型存在性，对每步先跑一遍）✅；(b) 逐步暴露失败（第 3 步参数错要等前两步跑完才发现）。
**取 (a)**：管线单步可能是数十分钟任务（2h 素材 MDX RTF ~1×），「跑完两步才发现第 3 步 `semi:15` 越界」是不可接受的体验；且静态校验零副作用（不碰文件不建目录），预跑成本毫秒级。运行期错误（解码失败/推理失败）无法预校验，按 D4/D5 逐链暴露——静态预校验管「可预知的错」，运行期管「不可预知的错」，边界清晰。
**实现缝（零漂移关键）**：runEngine 的 :183-195 静态段抽成 `staticCheck(entry, req) → QString`（或直接产出错误响应）供 runEngine 与管线预校验共用——校验顺序（validateParams → semanticCheck → 模型存在性）与消息文案单一来源，不复制逻辑（双源漂移是本包最大零漂移风险，§8 风险 2）。**预校验不调 create**：setup 要载模型（MDX 查表 + ORT 会话，秒级 + 上百 MB），不是静态检查；模型装载错误留给该步开工时的既有 E_MODEL 路径。

### D4 错误归属与进度：error 增 `step`/`stepCmd` 键（纯增量）；E_CANCELLED 文案逐字透传；进度等分映射

- **错误归属**：失败步的响应改造为 `{"ok":false,"cmd":"pipeline","error":{"code":"<透传 E_*>","message":"步骤 N（<stepCmd>）：<原 message>","step":N,"stepCmd":"<stepCmd>"}}`。E_* 七类码在失败步产生点赋值后原样透传（NFR-M1 片 B 机制），**本包不新增错误码**；`step`/`stepCmd` 是 error 对象内的增量键，既有消费者只读 code/message 不受影响。
- **E_CANCELLED 例外（零漂移硬约束）**：取消消息**不缀步骤前缀、逐字透传「已取消」**——GUI `onJobDone` 对「已取消」是全文相等特判（mainwidget.cpp:628），缀前缀会把静默取消变成警告弹窗，即 GUI 行为回归；步骤归属由 `step` 键承载。
- **进度分配**：候选 (a) 等分（第 i 步占 `[(i-1)*100/n, i*100/n)`，步内进度线性重映射，末步 100 即整链 100）✅；(b) 按预估时长加权。取 (a)：(b) 需要每步 RTF 权重表——硬编码引擎耗时是注册表明确不做的「复制引擎工作点」（nfr-m1 spec D2「能力声明最小化」同款反例），且预估误差大时加权反而更失真；等分确定性好断言（PIPE-PROG）。GUI 状态行在提交时提示「管线处理中（共 N 步）」，进度条跳变属已知取舍。
- **取消透传**：管线与各步共用同一 `Ctx.cancel`（转码器分块粒度检查点不变，fftranscoder.cpp:242-245），执行器在**每步开工前**加查一次取消标志（步间空隙不漏检，NFR-R1 ≤1s 语义保持）。

### D5 工作区集成：每步一版本（vNNN_&lt;stepCmd&gt; + parentVersion 链）+ 可选 `pipeline` 标记；部分失败 = 失败步清理、已提交步保留

**候选（版本形态，fr-a4 留缝的两条路）**：
- (a) 每步一版本：链 = v001_denoise → v002_separate → v003_pitch，parentVersion 链自动成立；✅
- (b) 一链一版本多产物：单个版本目录装全部中间产物 + 终产物，记录 steps 数组（fr-a4 留缝字面的「多产物容器」形态）。

**取 (a)**，论证：
- **机制零改动**：parentVersion 链（workspace.cpp:171-179）、链式输入缓存定位（:320-323）、孤儿版本号跳过（:181-190）全部现成，WS-CHAIN 用例验过的就是这条链；(b) 要扩 workspace.json 记录 schema（多产物记录）、改 GUI 历史对话框（现按每版本单一 output 渲染，mainwidget.cpp:266-279）、定义多产物 discard 语义——FR-A4 刚验收零漂移优先，改动面全在刚封板模块上。
- **语义更诚实**：中间产物不是垃圾而是**可寻址、可复算、可「用此版本继续」的版本**（GUI 历史对话框既有按钮直接适用于链中间任意步）；(b) 把它们埋进单版本目录反而剥夺了逐步寻址能力。
- **部分失败契约（append-only 与「不留半成品」张力的裁定）**：第 3 步失败 → 该步在途半成品由底座删除（既有）、其版本目录 discardVersion（既有，workspace.cpp:327-329）；**v001/v002 保留**——它们是已完成、已登记、来源链完整的任务产物，不是半成品；「不留半成品」约束的对象是**在途任务的未完成产物**，不是已提交版本（append-only 不可回卷是 FR-A4 D2 不可变性原文）。用户可从 v002 产物手动续跑（「用此版本继续」即达），失败响应的 `step` 键指明断点。
- **可选 `pipeline` 标记（增量键）**：版本记录增可选 `pipeline:{index,total}` 键（commitVersion 组装处 ~10 行，workspace.cpp:278-295 邻域；非管线任务不含该键——workspace.json 逐字节零漂移）。价值：历史可辨识「一键任务 vs 三次手动任务」（同一 parentVersion 链两者同形）；不加也能由链重建，但 GUI/FR-A10 审计的区分展示缺数据源。成本一个可选键，取加。
- **truncated 传播**：某步源中途读错时该步仍 ok（可解前缀 + truncated 标志，FR-A4 OQ-5 语义）——链继续跑（前缀处理是既有单动词语义，管线不另立更严判据），终 result 的 `truncated` = 各步之或，各步真值在 `steps[]` 内逐项可查。

### D6 direct 模式裁定：管线强制 `workspace`（缺省即 E_PARAMS）——OQ-1 拍板

**候选**：(a) 强制 workspace（不带 → E_PARAMS「pipeline 必须带 workspace」）✅倾向；(b) 允许 direct：中间产物进 QTemporaryDir 自动清理、终产物落显式 output/缺省命名；(c) 允许 direct：中间产物落源目录后缀链。
**倾向 (a)**：(c) 直接否决（污染源目录 + 覆写风险 + 违红线精神）。(b) 的中间产物无版本无登记 = 不可追溯不可断点续跑（与 FR-A4 精神相悖），终产物单文件表达不了链语义，且 kill -9 时 temp 残留无人担保；(a) 的成本是调用方多传一个字段——GUI 恒注入根（fr-a4 片 B 机制，ffextractor.cpp:419-421），CI/脚本传 mktemp 根（WS 族同款）。反证自查：是否存在真实需要 direct 管线的场景？「只想快速跑两步链」= 两次单动词 direct 调用已覆盖，链的差异化价值恰在版本化追溯——(a) 不损失真实能力。红线句「新落盘动词应提供 workspace 模式」由 (a) 以最强形式满足（只提供 workspace 模式）。

### D7 separate 在链中：续链 stem = 步骤 `stem` 参数（缺省 vocals）；另一 stem 不产出

引擎一次一 stem 是现状契约（setInstrumental 前置声明，vocalseparator.h:16；副 stem = mix − compensate×主 stem 在引擎内合成单输出）。**候选**：(a) 链式语义 = 「本步产物即下步输入」，产出哪个 stem 由步骤 `stem` 参数决定（缺省 vocals，正合需求「降噪→分离→变声处理人声」的意图）✅；(b) 管线内 separate 步强制双 stem 产出（要改引擎双输出 + 版本目录多产物——连锁引爆 D5(b) 全部改动面）。
**取 (a)**：零引擎改动、零工作区改动；要伴奏就再跑一个 separate（独立任务或另一条链）——「双 stem 各取所需」本来就是两个产物两个任务，不是一条线性链。链是线性的（每步恰一输入一输出），此语义在 D2 步骤校验里自然成立，无需特判。

### D8 GUI 一键：预设链下拉 + 一键按钮（编译期预设表单一来源）；自由编排对话框不做

**候选**：(a) 预设下拉 + 一键执行按钮，预设 = 代码内静态表（id/显示名/步骤模板）✅；(b) 自由编排对话框（步骤列表增删改排序/逐步参数）；(c) 只做 CLI 不做 GUI。
**取 (a)**：FR-9 原文「一键执行」的兑现面就是预设；(b) 是 FR-17 预设系统 + Agent 编排（v1.x）的形态，v1.0 五动词词表下自由编排的排列空间小而校验面大（工作原则 5）；(c) 不满足「一键」（FR-9 是用户可见差异化功能，M2 DoD 含 GUI 全操作走命令层）。
**实现要点**：预设表 = mainwidget 内静态常量（显示名 + 步骤模板；模板中的动态参数——denoise 的 humBase/wind 取当前勾选、pitch 的 semi 取当前 spinSemi——提交时取值拼进 steps）；mainwidget 把 steps 组为 JSON 字符串经 mediaservice 新增一对信号槽传 worker（wsInfoDone/modelListDone 以 QString 传 JSON 的既有先例）；ffextractor 新增 `doPipeline(inPath, stepsJson)` 槽：解析 steps → 对 requiresModel 动词逐个两级缺省解析（resolveDefaultModel，ffextractor.cpp:147-170；verb→bundled 相对路径映射与七槽同款写法）→ 组 pipeline 请求走 **runRequest**（workspace 注入、jobDone 文案「管线完成：<终产物路径>」、E_MODEL→modelMissing、进度/取消全复用，ffextractor.cpp:419-448）。busy 锁/取消按钮/进度条零新增机制（set_busy 现有面板覆盖）。**链中 E_MODEL 的 modelMissing 信号 forVerb/模型名取 `error.stepCmd`**（§7.1 附裁 1：pipeline 请求的 cmd=pipeline，壳照搬「cmd→动词」映射会引导下载错用途模型——这是双报告交叉对照补的缝）。
**预设清单（v1.0 具体条目 → OQ-3）**：倾向三条——「AI 降噪 → 人声分离」「DSP 降噪 → 人声分离」「人声分离 → 变调」；M3 RVC 落地后加需求原味预设「降噪 → 分离 → 变声」。

### D9 注册表关系与代码落点：pipeline 进内置表（非引擎表）；执行器 = 逐步组合 execute；零新文件

**候选（动词归属）**：(a) 内置表一条（probe/extract/wsInfo/model 族同列）✅；(b) 引擎表条目（伪装成引擎）。
**取 (a)**：pipeline 无 IStreamEngine、无 inRate/create——塞引擎表要造假工厂假 schema，破坏条目契约语义（nfr-m1 D3「内置动词 = 无引擎参与的专有处理器」原分类标准）。条目配置：`allowsOutput=false`（终产物路径由工作区版本化决定，output 无意义——与 probe/wsInfo 拒 output 同型）、`allowsWorkspace=true`、`requiresInput=true`、`extraKeys={"steps"}`。
**执行器形态（D1 的落地）**：cmdPipeline 处理器 = 静态预校验（D3，复用 staticCheck）→ 循环组合：每步构造子请求 `{v:1, cmd:<stepCmd>, input:<前步产物或源>, <步骤参数>, workspace:<root>}` 直接调 `execute(subReq, stepCtx)` → 拆响应：ok 则取 result.output 作下步 input 并收集 steps 摘要；失败则按 D4 改造归属后立即返回（该步的版本目录已由其内部 runInWorkspace discard，commandlayer.cpp:130-133 既有）。**单动词代码零复制**——预校验、落盘、版本登记、错误码、取消、进度全部走 execute 既有路径；递归深度有界为 1（步骤词表限引擎表，D2）。
**进度包装**：stepCtx.onProgress = 等分重映射（D4）；cancel 指针原样传递。
**改动面收敛**：本包**零新文件、零 CMake 改动**——commandlayer.cpp（cmdPipeline + staticCheck 抽取 + 内置表一条）、workspace.{h,cpp}（pipeline 标记）、mediaservice.{h,cpp}（一对信号槽）、ffextractor.{h,cpp}（doPipeline）、mainwidget.{h,cpp}（预设下拉与按钮）。
**NFR-M1 零改承诺保持（验收可机械化）**：管线执行器只依赖 EngineEntry 公共契约（validateParams/semanticCheck/requiresModel/create/outputSuffix/lockCanonicalOutput）与 execute 子请求路径，不 switch 任何具体引擎 id（GUI 壳的 verb→bundled 模型路径映射除外——那是壳的既有形态，ffextractor 七槽同款）；M3 假想验证：新增 RVC 引擎 + 表条目后 pipeline 零改可链（与 REG 族同方法论，§6 PIPE-REG）。

### D10 零漂移验证面与批处理前瞻

- **零漂移（I，既有铁律）**：本包触碰既有代码的三处全部要求行为零漂移——① staticCheck 从 runEngine 抽取（校验顺序与消息逐字不变）；② workspace 版本记录加可选键（非管线任务 workspace.json 逐字节不变）；③ 信封段零触碰（pipeline 是新动词，既有动词的白名单/消息/响应一个字不动）。硬门禁 = CMD-021r 全文逐字节比对 + WS 族 + MDL 族 + ci-smoke 全量复跑（§6 PIPE-ZERO）。
- **批处理前瞻（J，不过度设计）**：steps 是自包含数据模板（不含 input 绑定）、execute 纯函数可串行重入（commandlayer.h:20-21）——FR-10 落地时「同链多文件」= 调用方循环换 input，队列/失败不中断语义归批处理包设计（fr24 §5.4 同款边界）。本包不做：批处理动词、队列、steps 命名引用/预设 id 间接层（Agent/FR-17 的对象）。

## 3. 方案（模块图 + 数据流）

```
 ┌─ GUI 壳（D8）────────────────────┐   ┌─ CLI 壳 ─────────────────────────┐
 │ mainwidget：预设下拉 + 一键按钮      │   │ --json: {"cmd":"pipeline",        │
 │ （静态预设表；提交时取当前 UI 参数    │   │   input, workspace, steps:[…]}    │
 │  拼 steps，JSON 字符串下行）         │   │ （无旧式薄壳——wsInfo/model 族同款  │
 │ mediaservice：pipeline 一对信号槽   │   │  只走 --json，CLI 面不膨胀）        │
 └────────────┬─────────────────────┘   └──────────────┬───────────────────┘
              ▼ worker 槽 doPipeline                     │
        （模型两级解析 per 模型步 → 组请求 → runRequest：   │
          workspace 注入 / modelMissing / 进度取消复用）    │
              │ QJsonObject{cmd:pipeline, input,         │
              │             workspace, steps[]} + Ctx     │
              ▼                                          ▼
      ╔═══════════════════════════════════════════════════════╗
      ║ CommandLayer::execute                                   ║
      ║  信封校验（既有段零改）→ 内置表命中 pipeline               ║
      ║  cmdPipeline：                                           ║
      ║   ① steps schema（数组/≥2/对象/cmd 命中引擎表/未知字段）   ║
      ║   ② 整链静态预校验（staticCheck × n：validateParams →     ║
      ║      semanticCheck → 模型存在性；不调 create）             ║
      ║   ③ workspace 必填检查（D6）                              ║
      ║   ④ 循环：execute({cmd:stepCmd, input:前步产物,           ║
      ║      步骤参数, workspace:root}, 等分进度 Ctx)              ║
      ║      → 每步内部：runEngine → planVersion（parentVersion    ║
      ║        自动链）→ runTranscode → commitVersion(+标记)      ║
      ╚═══════════════╤═════════════════════════════════════════╝
                      ▼
      响应：result{output=终产物, workspace=末步版本, steps[]摘要,
                  truncated=或} / error{code 透传, message 步骤前缀
                  （E_CANCELLED 除外）, step, stepCmd}
```

**正例数据流**（GUI 预设「AI 降噪 → 人声分离」，2h 48k 视频源）：预设选择 → doPipeline 解析 aiDenoise/separate 两步模型路径（两级缺省解析）→ runRequest 注入 workspace → 信封/schema/预校验通过 → 步 1 execute：planVersion（源全量哈希定位/建工作区，v001_aiDenoise）→ transcode（48k→16k 单声道引擎段，输出锁 44.1k 立体声）→ commitVersion（parent=null，pipeline:{1,2}）→ 步 2 以 v001 产物为 input：缓存命中定位工作区 → v002_separate（parent=1，pipeline:{2,2}）→ 终响应 result.output = v002 产物 → jobDone「管线完成：<路径>」。峰值 RSS = max(步1, 步2) ≈ 分离步单动词水位。

**失败流**（步 2 推理失败）：步 2 的 E_ENGINE 在引擎产生点赋码 → execute 子响应 ok:false → cmdPipeline 缀「步骤 2（separate）：」+ step/stepCmd 键返回 → 步 2 版本目录已 discard、v001 保留、索引 head=1 → GUI 警告框展示归属文案；用户可从历史对话框对 v001「用此版本继续」。

**取消流**（步 2 中段取消）：转码器分块检查点置位 → 步 2 半成品删除 + 版本目录 discard → error{code:E_CANCELLED, message:「已取消」（逐字）, step:2} → GUI 静默（特判命中）→ v001 保留。

## 4. 改动面（file:line 级预估，基于 HEAD fc69173）

| 文件 | 改动 | 预估规模 |
|---|---|---|
| `src/service/commandlayer.cpp` | `staticCheck` 从 runEngine :183-195 抽取（runEngine 改调，顺序消息逐字不变）；`cmdPipeline`（steps schema + 预校验 + 循环组合 execute + 进度等分 + 归属改造）；内置表一条（:334-343 邻域，allowsOutput=false, extraKeys={"steps"}） | ~160 行 |
| `src/service/workspace.h` / `.cpp` | `VersionPlan` 增可选 `pipeline{index,total}` 字段；commitVersion 组记录处按存在性插键（:278-295 邻域；非管线任务不含键） | ~12 行 |
| `src/service/commandlayer.cpp`（runEngine 邻域） | runInWorkspace 调用点透传 plan.pipeline 标记（:221-225 邻域） | ~4 行 |
| `src/service/mediaservice.{h,cpp}` | `pipeline(inPath, stepsJson)` 一对信号槽（wsInfo 同型） | ~14 行 |
| `src/service/ffextractor.{h,cpp}` | `doPipeline(inPath, stepsJson)` 槽：解析 steps、模型步两级缺省解析（verb→bundled 映射同七槽写法）、组请求走 runRequest | ~45 行 |
| `src/view/mainwidget.{h,cpp}` | 静态预设表 + 下拉 + 「一键执行」按钮 + doPipeline（提交时取当前 UI 参数拼 steps）+ busy 面板纳入新按钮 | ~60 行 |
| `CLAUDE.md` | 架构段管线一句 + `--json` 动词清单 + 工作区标记说明 | ~3 行 |
| `docs/acceptance/wp-1.2-linux-build/scripts/ci-smoke.sh` | 追加一行调用 `run-pipe.sh`（round-1 全绿后） | 1 行 |

**零新文件、零 CMake 改动、零引擎/底座触碰**：五引擎、streamengine.h、fftranscoder.{h,cpp}、ffextractor 静态核心（extract 族）、engineregistry.{h,cpp}、modelmanager、main.cpp（pipeline 不加旧式薄壳）全部不动。

## 5. 留缝清单（本包不做，后续包的接口锚点）

| 缝 | 本包怎么留 | 不动什么 |
|---|---|---|
| **FR-7 RVC 入链（M3）** | 词表 = 引擎表枚举，RVC 注册即自动可链（D9 可机械化验证 PIPE-REG）；GUI 预设表是壳增量（加一条原味三段链） | 不预造 RVC 条目/参数枚举/占位预设文案（NFR-C5 使用须知随 M3） |
| **FR-10 批处理** | steps 自包含数据 + execute 串行重入（既有）；同链多文件 = 循环换 input | 不做队列/失败不中断/并发锁（QLockFile 归批处理包，fr-a4 OQ-6 口径） |
| **FR-17 预设系统** | 预设表是 mainwidget 静态常量——用户自定义链保存/复用届时把「静态表」升级为「配置源」即可，槽接口（stepsJson）不变 | 不做预设 CRUD/持久化 |
| **steps 的 model 引用库内 id** | v1 步骤 model = 绝对路径（与单动词同律，fr24 D2 铁律）；FR-8 models.json 的 {id,sha256} 是未来按 id 解析的数据源 | 命令层不解析模型 id（两级缺省解析留在 GUI 壳，FR-8 D1 边界不变） |
| **FR-A3 Agent（v1.x）** | steps 数组即「计划 → 确定性执行」的最小样例；error.step 支持逐步闸门的断点续跑叙事 | 不做 NDJSON/逐步确认/计划引用 versionId |
| **FR-16 输出格式** | 中间产物 f32 WAV（无损）；届时可选 FLAC 中间省盘（终产物格式选择同款机制位） | 不做中间产物格式参数 |
| **FR-A10 审计** | 版本记录 pipeline:{index,total} 标记是链级审计数据源 | 不做独立审计通道 |

## 6. 验证方案（目录 `docs/acceptance/fr9-pipeline/`，风格对齐 run-json/run-ws/run-models）

**目录骨架**（实现开工时建）：`goal.md` + `plan.md` + `matrix.csv`（前缀 `PIPE-`）+ `round-1.md`（CLI 面）/`round-2.md`（GUI 面）+ `scripts/run-pipe.sh`（复用 run-json 的 json_check 断言器与素材配方，交叉引用不复制）+ 全绿后 `report.md`。素材配方：无模型链（denoise→pitch，transcode→denoise→pitch）免 onnx 可跑；带模型链（aiDenoise→separate）复用 ci-smoke third_party 模型，缺模型整族 SKIP（PARITY 同款降级口径）。

**用例族（PIPE-\*）**：

1. **PIPE-OK（正例链）**：两步/三步链 e2e——result 结构断言（output=末步产物、steps[] 逐项含 version/output/outputSpec/engineResampled/frames、truncated=或、workspace=末步版本）；版本目录落位（v001_&lt;cmd&gt;/v002_&lt;cmd&gt;/…）；中间产物可被 ffprobe 体检（规格符合该步策略：AI/分离步锁 44.1k/2ch、跟随源步跟随源——canonical-rate 口径进断言）。
2. **PIPE-EQV（编排等价，核心）**：同参数「管线跑」vs「逐步手动跑单动词（各带 workspace）」——终产物与各中间产物**逐字节一致**（管线 = 纯编排、无数值漂移的直接证据；确定性由 CMD 确定性用例背书）。
3. **PIPE-CHAIN（版本链与标记）**：v001/v002/v003 的 parentVersion 逐级链断言（1←2←3）；pipeline:{index,total} 标记逐条断言；head=末步；链中手动插入无关任务后续跑链（版本号交错但 parent 链仍正确）；缓存删除（rm index.json）后链继续定位（resolveWsDir 扫描兜底）。
4. **PIPE-PREVAL（整链预校验）**：步 2 参数越界（semi:15）/模型路径不存在/未知步骤字段/off+off 语义错 → E_PARAMS 且**开工前拦截**（断言：无版本目录、无任何产物、源文件 sha256 不变）。
5. **PIPE-FAIL（部分失败）**：步 2 坏模型（wp-1.0 round-2 维度错位配方）→ E_MODEL + step=2 + 前缀文案；v001 保留且可独立复用（对 v001 产物跑单动词成功）；步 2 版本目录无残迹、head=1；失败后重跑整链 → v002 起新号正常。
6. **PIPE-NEG（schema 负例）**：steps 缺失/非数组/空数组/单步/元素非对象/cmd 缺失/cmd 非字符串/内置动词入链（extractAudio/probe/modelList/pipeline 嵌套）/步骤含 input、output、workspace、v/未知参数字段——逐条 E_PARAMS 消息断言；不带 workspace → E_PARAMS（D6）；output 同给 → 互斥违例（信封既有）。
7. **PIPE-CANCEL（取消）**：CLI 不可达（单发同步）——沿 WS-CANCEL 方法论：GUI 语义经 Ctx 注入的代码级证据 + round-2 GUI 面承接；断言点：E_CANCELLED message 逐字「已取消」+ step 键 + v001 保留。
8. **PIPE-ZERO（零漂移硬门禁）**：CMD 36 条 + CMD-021r 全文逐字节 + WS 族 + MDL 族 + ci-smoke 全量 + PAR-SR 三调用点复跑（staticCheck 抽取与 workspace 增键两处触碰的回归网）。
9. **PIPE-GUI（round-2）**：gui-slot-eqv 方法论——预设下拉三项各自组单断言（探针直调比对请求 JSON）；busy 锁覆盖新按钮；链中 E_MODEL → modelMissing 信号（引导模型管理）；「已取消」静默（特判兼容）；进度条终值 100。
10. **PIPE-REG（注册表零改，M3 前瞻金标准）**：REG 族同方法论——验证分支注入假引擎 + 条目后，`git diff` 白名单断言管线相关文件零改，假引擎即可入链 e2e（正例 + 步骤参数负例）。分支证据制不进 ci-smoke。
11. **PIPE-MEM（内存抽测）**：≥30min 素材两步链（denoise→pitch 免模型）+ 带模型链（有 onnx 时）峰值 RSS 与最重单动词同量级（/usr/bin/time -v 采样对照，逐遍有界=全程有界的实证）。

**门禁接线**：round-1（CLI 面 Linux）全绿后 `run-pipe.sh` 进 ci-smoke（run-json 同款一行）；Windows 真机随 WP-1.0 round-4 挂账口径（fr24/nfr-m1/fr-a4/fr8 同款）。

## 7. 开放问题（主线程拍板，拍板后记入 acceptance goal.md）

- **OQ-1 管线是否强制 workspace**：候选 (a) 强制（D6 推荐）——不带即 E_PARAMS；(b) 允许 direct + QTemporaryDir 中转自动清理。**倾向 (a)**：direct 中间产物不可追溯不可续跑、kill -9 残留 temp 无人担保；两步快速链用两次单动词 direct 已覆盖；GUI/CI 传根成本一行。影响：D6、PIPE-NEG 断言、Agent 未来计划的动词选择规则（无 workspace 语境须显式给根）。
- **OQ-2 进度分配策略**：候选 (a) 等分（D4 推荐）；(b) 按步型 RTF 权重加权。**倾向 (a)**：(b) 的权重表是硬编码引擎耗时（注册表「不复制引擎工作点」的反例，nfr-m1 D2），且预估失真时加权更误导；等分 + GUI 状态行「共 N 步」提示够用。影响：D4、PIPE-PROG 断言形态、GUI 体验。
- **OQ-3 GUI 预设清单（v1.0 条目）**：候选 (a) 三条：AI 降噪→人声分离 / DSP 降噪→人声分离 / 人声分离→变调（D8 推荐）；(b) 两条（只保留含 AI 的差异化链）；(c) 四条（加「AI 降噪→人声分离→变调」四步链）。**倾向 (a)**：覆盖差异化主场景与变调代变声位，条目数克制；(c) 的四步链与三条预设其中两条前缀重复，下拉噪音。影响：D8、mainwidget 预设表、PIPE-GUI 用例数。
- **OQ-4 中间产物的磁盘占用与保留策略**：候选 (a) 全保留（append-only 铁律 + 「用此版本继续」/可复算价值；磁盘占用如实文档化：2h@48k 立体声 f32 ≈2.76GB/步）✅倾向；(b) 链成功后自动清理中间版本（保留终步）——违 append-only 不可变性（FR-A4 D2「版本目录一经登记即只读语义」），且删掉的恰是可续跑资产；(c) 链成功后提示用户手动清理。**倾向 (a)**：(b) 破刚验收的核心机制语义；(c) 的提示面 v1 无处安放（完成消息已带终产物路径，历史对话框可开目录手动删）。影响：D5、README/CLAUDE.md 用户文档中磁盘占用说明。

### 7.1 拍板（主线程，2026-11-03；对照独立对抗约束清单交叉核验后）

- **OQ-1 → (a) 强制 workspace**。direct 三形态皆有真实缺陷（temp 不可追溯/源目录污染），红线句以最强形式满足；Agent 前瞻约束（无根语境须显式给根）随留缝传递。
- **OQ-2 → (a) 等分**。加权表=硬编码引擎耗时，违注册表「不复制引擎工作点」；「共 N 步」状态行缓解体验，round-2 GUI 面验证后如仍不可接受再议。
- **OQ-3 → (a) 三条预设**（AI 降噪→人声分离 / DSP 降噪→人声分离 / 人声分离→变调）。
- **OQ-4 → (a) 全保留 append-only**。磁盘占用如实文档化（~2.76GB/步@2h48k），不自动清理。
- **附裁 1（modelMissing 的 forVerb）**：链中 E_MODEL → GUI 壳 modelMissing 信号取 `error.stepCmd` 作 forVerb/模型名映射源（D8 已补）。PIPE-GUI 增此断言。
- **附裁 2（单步链与重复动词）**：steps≥2 拒收单步（同参数不产生「走链/不走链」两套记录）；同动词重复合法不加禁令（每步一版本 vNNN_<cmd> 天然无命名冲突，工作原则 5）。
- **附裁 3（哈希增量口不开）**：planVersion 对链式输入现算 sha256 的 I/O 代价如实承受（D1 已列；NVMe 上秒级 vs MDX 步数十分钟是噪声）——身份哈希是防伪真值，调用方「已知哈希」传参会绕过重算引入断链风险；将来若需优化归 FR-A4 域（缓存条目加 sha 字段是零语义演进）。
- **附裁 4（pipeline 标记采纳）**：{index,total} 成本 ~12 行，价值=「一键 vs 手动」可辨识且数据不可再生（parentVersion 链反推不出该区分）；FR-A10 审计数据源。
- **附裁 5（GUI 成功文案）**：「管线完成：<终产物路径>」沿七槽风格，round-2 冻结进 GUI-EQV 断言。
- **确认（链中「源」语义）**：「跟随源」的源 = 紧邻上一步产物（机制自然成立：每步 execute 以前步产物为 input，probeSourceSpec 探它）——PIPE-OK 的规格断言锚定此口径，无需额外代码。

## 8. 红线自查与实现风险

**红线自查**：
- **原件只读**：链中只有步 1 读用户源（既有的只读解码/哈希通路）；后续步输入全部是工作区版本产物；`output==input` 防线在每个子请求的 execute 信封段原样生效（commandlayer.cpp:435-436）；PIPE-PREVAL/PIPE-FAIL 断言源 sha256 前后不变。
- **新落盘动词提供 workspace 模式**：pipeline **只**提供 workspace 模式（D6/OQ-1），红线句以最强形式满足；版本目录 append-only 不覆写。
- **流式内存有界（NFR-P4）**：每步是既有有界通路（分块排水，fftranscoder.cpp:191-239），中间产物落盘不驻留；管线执行器新增内存 = steps JSON 小对象；引擎逐步建毁 → 峰值 = 逐步最大值而非和（D1 内存小节，PIPE-MEM 实证）。
- **不引入 GPL/新依赖**：零新文件零 CMake，全部 Qt Core + 既有底座；LGPL 组合零变化。
- **旧动词零漂移**：三处触碰（staticCheck 抽取 / workspace 增键 / 信封零触碰）全部以 PIPE-ZERO 为硬门禁；GUI 七槽文案与行为零触碰（管线是新增行）。
- **注释与文案中文**；E_CANCELLED 文案逐字兼容 GUI 特判（D4）。

**实现风险**：
1. **staticCheck 抽取的消息序漂移是最大零漂移风险**：runEngine 静态段抽出为函数时，任何校验顺序调整都会改变多错并存时的报错（CMD-020..035 断言具体文案）。缓解：纯搬家不加逻辑 + 模板旁注对应 CMD 用例编号（nfr-m1 风险 1 同款）+ PIPE-ZERO 全量复跑。
2. **execute 递归组合的子请求构造错误**：步骤参数并入子请求时若混入链级键（workspace 重复、steps 泄漏）会击穿信封校验或污染版本记录 params。缓解：子请求构造单点函数 + PIPE-NEG 对步骤内 input/output/workspace/v 的拒收用例 + PIPE-CHAIN 的 params 内容断言（步骤记录 params 不含链级键，recordParams 剥离逻辑复用）。
3. **workspace 增键的零漂移**：非管线任务的 workspace.json 必须逐字节不变（可选键只在管线步插入）。缓解：WS-PROV/WS-CHAIN 全量复跑比对 + 实现上以 plan.pipeline 是否设置为唯一插键条件。
4. **磁盘占用的用户感知**（OQ-4）：长素材多步链的中间版本占用可达数 GB/步，用户若无预期易当缺陷报。缓解：文档如实标注（CLAUDE.md/README）+ 历史对话框「打开目录」可手动清理；清理自动化不做（违 append-only）。
5. **GUI 预设与命令层的词表时差**：M3 前预设不含变声、M3 后加预设须同步 GUI 侧 verb→bundled 模型映射（doPipeline 内）——映射是壳的既有形态（七槽同款），漂移面收敛在 GUI 一处，PIPE-REG 的 diff 白名单断言守住命令层侧零改。
6. **Windows 真机验证滞后**（round-4 pending 挂账口径，四包同款）：GUI 预设一键的完整体验（下拉/进度/取消/弹窗）Windows 侧长期靠 runbook 手工。缓解：round-2 的 GUI 面用例在探针层断言请求 JSON（与平台无关），Windows runbook 增补管线段。
