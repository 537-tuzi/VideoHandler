# FR-10 批处理队列（开工 spec）

- 日期：2026-11-03
- 性质：开工前快照（M2 第六个工作包，依赖序承接 FR-9；进展追踪见 `docs/acceptance/fr10-batch-queue/goal.md`——实现开工时才建目录）
- 本文 D 编号（D1–D10）仅限本 spec，与 REQUIREMENTS.md 决策记录（D1–D7）无关。
- 现状基线：HEAD `e2e1b07`（FR-24 命令层 + NFR-M1 注册表 + FR-A4 工作区 + FR-8 模型管理 + FR-9 管线已全落地并验收）。
- 本包不含：交互式队列（运行中增删件/跨批持久化队列——提交时清单即终态）、多件并发（worker 单线程一次一任务模型不动，fr-a4 D7 单写者假设保持）、FR-17 预设持久化（批模板复用管线预设静态表）、批内逐件模型差异化（模板共享，见 D2）。

## 0. 需求锚点与约束

| 锚点 | 内容 | 对本包的约束 |
|---|---|---|
| FR-10（REQUIREMENTS.md:95） | 「多文件排队、逐个执行、失败不中断后续」 | 三个硬点逐一落决策：排队形态（D2 schema）、逐个执行（D3 组合复用）、失败不中断（D4 结果信封与续跑语义） |
| 依赖序（v1-development-plan.md:38） | FR-24 → NFR-M1 → FR-A4 → FR-8 → FR-9 → **FR-10** → FR-4 | 承接 FR-9 全部契约；接口为 FR-4 质量档（新引擎入注册表即自动可批）与 Agent（v1.x 计划的多文件原语）留缝 |
| FR-9 D10 留缝（fr9-pipeline.md:136） | 「steps 是自包含数据模板（不含 input 绑定）、execute 纯函数可串行重入——FR-10 落地时『同链多文件』= 调用方循环换 input，队列/失败不中断语义归批处理包设计」 | steps 模板 + execute 重入是铺好的路；队列语义（逐件结果/失败续跑/取消）是本包的增量设计面（D2–D7） |
| FR-9 §5 留缝（fr9-pipeline.md:198） | 「不做队列/失败不中断/并发锁（QLockFile 归批处理包，fr-a4 OQ-6 口径）」 | 并发锁是否随本包上——须对照 fr-a4 OQ-6 拍板口径裁定（§7 OQ-5 备注：本包串行单件，锁仍无真实并发可测，倾向维持留缝） |
| fr24 §5.4 留缝（fr24-command-layer.md:227） | 「CI 批量 = bash 循环调 --json，同步模型够 CI 用。队列/失败不中断语义是 GUI 服务层（mediaservice 演进）与 Agent 的事」 | 该表述写于 FR-9 之前；FR-9 落地证明编排动词放命令层三壳等价可行（D1 重新裁定此缝，交付报告交叉对照节） |
| 架构基线（REQUIREMENTS.md:73） | 「GUI 与 Agent 是同一命令层的两个客户端，能力完全对等」 | 批处理不能做成 GUI 专属队列；CLI/GUI/Agent 共用 `batch` 动词（D1） |
| NFR-P4（REQUIREMENTS.md:165） | 2h 音频峰值 RSS ≤2GB、禁止整文件载入 | 批 = 逐件串行，峰值 = 全部件·全部步最大值的论证与验收断言（D1 内存小节 + BATCH-MEM） |
| FR-12 / NFR-R1（REQUIREMENTS.md:97,177） | 全任务可取消 ≤1s、失败不留半成品 | 整批取消语义与已完成件保留的张力裁定（D7，沿 pipeline D5 部分失败契约） |
| fr-a4 D7（fr-a4-workspace.md:144） | 同工作区索引 v1 单写者假设；「FR-10 批处理多进程形态落地时再上 QLockFile」 | 本包形态 = 单进程单 worker 串行逐件——单写者假设继续成立，锁维持留缝（§5） |
| 红线（CLAUDE.md） | 批产物一律走工作区、版本目录 append-only；原件只读；流式内存有界；旧面逐字节零漂移 | §8 红线自查逐条对齐；零漂移触碰点枚举（D10/§6 BATCH-ZERO） |
| 回归面 | CMD 36 + CMD-EQV + GUI-EQV + WS 族 + MDL 族 + PIPE 族（97 条）+ ci-smoke 全量 | 全部只打既有动词；`batch` 为纯增量动词，**既有动词与 pipeline 响应逐字节零漂移是硬门禁**（D10） |

## 1. 现状盘点（证据，HEAD e2e1b07 实读）

**cmdPipeline 已是完整的「循环组合 execute」编排器**——批处理执行器的形态蓝本：

- 结构四段（commandlayer.cpp:267-368）：① steps schema（数组/≥2/元素对象/cmd 命中引擎表/步骤未知字段，:269-307）；② 整链静态预校验（staticCheck × n，不调 create，:308-318）；③ workspace 必填检查（:319-322）；④ 循环组合 execute 子请求（进度等分 `(i*100+pct)/total`、每步开工前查取消、失败即 stepError 改造返回，:323-361）。stepError（:250-265）：E_* 与 message 在产生点透传 + 缀「步骤 N（cmd）：」前缀 + `step`/`stepCmd` 归属键；**E_CANCELLED 例外——message 逐字「已取消」不缀前缀**（GUI onJobDone 全文相等特判，mainwidget.cpp:689）。
- staticCheck（commandlayer.cpp:185-199）已从 runEngine 抽出为共用段（validateParams → semanticCheck → 模型存在性），runEngine 与管线预校验双消费——**FR-9 风险 1（抽取消息序漂移）已被 PIPE-NEG 19 条验证过一遍，本包同型抽取有成熟门禁配方**。
- Ctx 已有管线内部标记通路：`pipelineIndex/pipelineTotal`（commandlayer.h:21-22），runEngine 组装 marker、runInWorkspace 按 plan.pipeline 存在性给版本记录插 `pipeline:{index,total}` 键（commandlayer.cpp:233-238、workspace.h:25）——批标记若采纳走完全同型通路（OQ-3）。

**信封机制的三个既有先例，batch 全部踩在既有路径上**：

- requiresInput 豁免先例：model 族 `requiresInput=false`（BuiltinVerb 末位缺省字段，commandlayer.cpp:473），豁免后 `input` 不进白名单、给了即「未知字段：input」（:539-542）——batch 自带 `inputs`，顶层免 input 落同型分支。
- extraKeys 先例：wsInfo/model 族/pipeline 各带专有键白名单（:476-490）。
- workspace 必填先例：pipeline 的 `allowsWorkspace=true` + cmdPipeline 内检空串 → E_PARAMS「pipeline 必须带 workspace」（:319-322）——batch 镜像同型（D5）。

**工作区身份机制天然隔离件与件**：

- 工作区身份 = 输入内容哈希（`<stem>.<hash8>`，fr-a4 D1）：**不同源文件 → 不同 hash8 → 不同工作区目录**，各自的 workspace.json/版本号/head 完全独立；同内容换路径 = 同一工作区（身份在内容）。版本号 per-workspace 单调递增跳过孤儿不复用（workspace.cpp 版本规划段）。根级 index.json 路径缓存按 root 共享，批内逐件串行读写无并发。
- parentVersion 链 + commitVersion 回填缓存（链式输入定位免哈希）是 pipeline 步与步之间的机制；批的件与件**各自从用户源起步**，不构成链——件的「链」只存在于其内部（多步模板时）。

**GUI 通路现状**：

- busy 锁一次一任务：提交槽 busy 拒收、jobDone/modelOpDone 复位（mediaservice.cpp:43-55）；pipeline 占 busy 同锁（:139-144）。
- doPipeline（ffextractor.cpp:533-570）：解析 stepsJson → 模型步两级缺省解析（resolveDefaultModel，:147-170）→ 组 pipeline 请求走 runRequest（workspace 注入/E_MODEL→modelMissing(stepCmd)/进度取消复用，:419-452）。**批处理的模板解析可直接复用此循环**（抽取共享 helper，D9）。
- mainwidget.doPipeline（mainwidget.cpp:651-673）：预设表 kPipePresets 三条（:40-44）+ 动态参数取当前面板；成功文案「管线完成：<终产物路径>」（附裁 5）；「已取消」特判两处（:564,:689）。
- 单文件态：openFile 设 curFile、七槽+管线按钮解禁（mainwidget.cpp:208-226）——**GUI 全部任务入口绑定单文件**，批处理需要独立多文件入口（D8）。
- QString 传 JSON 的信号先例：wsInfoDone/modelListDone（ffextractor.cpp:191-198）——批清单/结果同型传递，免自定义 QMetaType。
- 探针应答配方（fr9 report 环境注记）：offscreen 下 QFileDialog 用**行编辑直填 + AcceptRole 确认键**应答（pipe-gui-probe.cpp:157-175，iso9660 挂载期 selectFile 死泊的加固形态）——多选应答的确定性钩子沿此配方扩展（D8）。

**内存既有证据**（fr9 report.md:16-18）：免模型 60,880KB、带模型 1,517,592KB，均 ≤ 最重单动词 + 32MB 且 <2GiB——「逐步建毁引擎 → 峰值 = 逐步最大值而非总和」已实证；批再串一层同构论证（D1）。

## 2. 设计决策

### D1 架构主轴：批 = 命令层编排动词（逐件组合 execute），纯增量不碰引擎/底座/工作区内核——重新裁定 fr24 §5.4 的「队列归服务层」旧缝

**候选**：
- (a) 命令层编排动词 `batch`：执行器循环逐件组合既有 execute（单动词或 pipeline 子请求），失败不中断在编排层实现；✅
- (b) GUI 服务层队列：mediaservice 演进为任务队列（QList 待办 + worker 逐个取），CLI/Agent 各自实现循环；
- (c) 混合：命令层动词 + GUI 另造交互式队列。

**论证**：
- (b) 否决的核心理由是架构基线（REQUIREMENTS.md:73「GUI 与 Agent 是同一命令层的两个客户端，能力完全对等」）：队列做在 mediaservice 意味着 CLI 与 Agent 拿不到「失败不中断 + 逐件结果」语义，要么复制实现（双源漂移）要么降级为 bash 循环（无逐件结构化结果、N 次 JSON 往返）。fr24 §5.4 写「队列归服务层」时（2026-11-03 上午）pipeline 尚未落地——FR-9 已证明**编排动词放命令层**（cmdPipeline 循环组合 execute）可行且三壳等价、CI 可断言（PIPE 97 条），本缝据此演进；mediaservice 在本包仍只做投递（一次投递 = 一件批，busy 锁覆盖全批），不造队列。
- (c) 否决：两套批形态违工作原则 5；交互式队列（运行中增删）无需求牵引（本包不做清单）。
- (a) 的复用清单（对照 §1）：steps 模板校验（D3 抽取共享）、staticCheck、进度等分映射、取消检查点、stepError 归属形、runInWorkspace 两段提交、模型步两级缺省解析（GUI 壳）——**批执行器自身新增逻辑 ≈ 件循环 + 结果聚合**。
- **内存论证（NFR-P4，沿 pipeline D1 同构再串一层）**：件串行、件内步串行、引擎实例随每次 execute 子调用建毁 → 批峰值 = 全部件·全部步的逐步最大值，而非件间叠加或步间叠加；批执行器新增内存 = inputs/steps 两个 JSON 小对象 + items[] 结果累积（KB 级，每件一条摘要）。BATCH-MEM 抽测设防。
- 反证自查（对 (a) 最强反方论点「批 10 件 = 调用方循环 10 次 pipeline 已覆盖，动词是空壳」）：循环调用覆盖「逐个执行」但不覆盖**失败不中断的单任务语义**（调用方循环中一件失败是否继续是调用方纪律，不是机制）与**逐件结构化结果**（N 个独立响应 vs 一份 items[] 清单——Agent/CI 的断言面、GUI 的清单渲染都吃后者）；且 GUI 侧调用方循环 = N 次 busy 锁往返，期间可插入其他任务击穿「一批一任务」心智。动词不是空壳，是把「失败不中断」从约定升为机制（核心原则 3 同构）。

### D2 动词契约：`cmd:"batch"` + `inputs[]` + 共享 `steps[]` 模板（同模板多文件）；信封 requiresInput=false

**请求示意**：

```json
{"v":1, "cmd":"batch",
 "workspace":"/home/u/VideoHandler-Workspace",
 "inputs":["/media/vlog01.mp4", "/media/vlog02.mp4", "/media/vlog03.mp4"],
 "steps":[{"cmd":"denoise", "humBase":"60"},
          {"cmd":"separate", "stem":"vocals", "model":"/abs/models/separator/UVR-MDX-NET-Voc_FT.onnx"}]}
```

**候选（schema 形态）**：
- (a) `inputs:[string...]` + 顶层共享 `steps` 模板（同模板多文件）：✅
- (b) `items:[{input, ...}]` 异构批（每件可不同动词/参数/链）；
- (c) (a)+(b) 双形态。

**取 (a)**：
- FR-9 D10 留缝的原话就是它：「换 input 重放同一 steps 即批处理的单件形态（D10）」——steps 是为批铺的自包含模板，`inputs × steps` 是被铺好的路；(b)/(c) 是另起炉灶。
- 真实场景对位：「一批素材统一降噪/统一出人声」是批的 90% 场景（GUI 多选 + 预设即此形态）；异构需求（不同文件不同处理）= 多次调用，每次调用本就是一个原子批——队列价值（无人值守续跑）在同模板批里已完整兑现。
- (b) 的每件要复刻动词字段或 steps 双形态 → 校验分支翻倍、逐件模板预校验逻辑翻倍（D3），v1 无消费者（GUI 无异构 UI、Agent 计划可拆多次调用）；(c) 违工作原则 5。
- **代价如实列**：批内不能混verb混参（混 = N 次调用）；参数逐件差异化（如逐件不同 semi）不支持——v1 无此需求，FR-17/Agent 届时以「模板参数化」演进（§5 留缝）。

**schema 校验（严格风格与 pipeline 同律）**：`inputs` 必填、数组、**至少 2 件**（「队列」语义下限，沿 pipeline「steps 至少需要 2 步」附裁 2 同构：单件批 = 直接调单动词/pipeline，不产生「走批/不走批」两套记录）、元素必须是字符串且非空（消息模板 `inputs[2] 必须是字符串` / `inputs[2] 不能为空`，1 基用户直觉与 0 基 schema 索引的分工沿 pipeline D2 约定）；**重复件拒收**（绝对路径 `QFileInfo::absoluteFilePath` 字符串级两两比对，命中 → E_PARAMS「inputs 含重复文件：%1」——GUI 多选天然无重复，CLI 手写清单的重复是失误不是意图，误伤面为零；不做 symlink 解析归一，文档明示口径）。`steps` 校验复用 pipeline 的全部规则但下限为 **1 步**（见 D3——单动词批是最常见批形态；与 pipeline ≥2 的差异是 verb 语义差异不是不一致，两处消息分别为「steps 至少需要 2 步」/「steps 至少需要 1 步」）。
**信封落位（零信封代码改动）**：BuiltinVerb 一条 `{batch, cmdBatch, allowsOutput=false, allowsWorkspace=true, extraKeys={"inputs","steps"}, requiresInput=false}`——`requiresInput=false` 使顶层 `input` 不进白名单（给了即「未知字段：input」，model 族先例，commandlayer.cpp:539-542）；`allowsOutput=false` 拒 output（批产物是多件工作区版本，单 output 无语义——probe/wsInfo/pipeline 同型）；workspace 必填在 cmdBatch 内检（D5，pipeline 同型）。

### D3 单件执行语义：件 = 「同模板换 input」——steps≥2 时件 = pipeline 子请求，==1 时件 = 单动词子请求；模板静态预校验一次全批共享

- **件的可执行形态**：模板 steps 是引擎动词序列（词表 = 引擎注册表枚举，pipeline D2(a) 同律；内置动词/pipeline/batch 自身都不在引擎表 → 天然拒收，**递归深度有界：batch → pipeline → 引擎 = 3 层，batch 嵌 batch 天然非法**）。逐件执行 = 组子请求调 `execute(sub, subCtx)`：
  - steps ≥2：`{v:1, cmd:"pipeline", input:<本件源>, steps:<模板原样>, workspace:<root>}`——pipeline 内部的 schema 段/预校验段/逐步组合/版本链全部复用，**批执行器对多步件零额外逻辑**；
  - steps ==1：`{v:1, cmd:<stepCmd>, input:<本件源>, <步骤参数>, workspace:<root>}`——单动词路径复用。
  - 两形态都是 execute 纯组合（FR-9 D9「逐步组合 execute，不是复制单动词代码」同款约束的批版），子请求组装单点函数（链级键只在此并入，防 steps/inputs 泄漏进子请求——FR-9 风险 2 同型防护）。
- **模板静态预校验（一次，全批共享）**：开工前对模板做 steps schema + 逐步 staticCheck（validateParams → semanticCheck → 模型存在性；不调 create）——模板是全批共享的，**一次预校验覆盖全部件**；「件 1 跑 30 分钟才发现模板 semi:15 越界」不可接受（pipeline D3 同款论证，批场景只会更痛：乘以件数）。件级差异面（input 文件不存在/损坏/无音频流）是运行期错误，无法也不应预校验——留给该件开工时的既有 E_FILE 路径，按 D4 续跑。
- **实现缝（零漂移关键，本包最大触碰）**：cmdPipeline 的 steps schema 段（commandlayer.cpp:269-307）与预校验段（:308-318）抽成 `validateSteps(steps, minSteps, ...) → 错误响应或逐步条目表`，cmdPipeline 与 cmdBatch 双消费、minSteps 参数化（2/1）——消息模板单一来源不复制（FR-9 风险 1 同型：纯搬家 + PIPE-NEG 19 条复跑为硬门禁）。
- **共享参数的单点真相**：模板只解析一次、逐件原样注入子请求——不存在「逐件各解析一遍模板」的漂移面。

### D4 失败不中断与结果信封：`ok` = 编排层成功（批跑完即 true）；逐件成败在 `result.items[]`；错误归属 = 结构键不缀消息前缀

**（REQUIREMENTS.md:95 明文「失败不中断后续」的机制化）**

**响应示意（2 成 1 败）**：

```json
{"v":1,"ok":true,"cmd":"batch","result":{
  "total":3, "okCount":2, "truncated":false,
  "items":[
    {"index":1,"input":"/media/vlog01.mp4","ok":true,
     "output":"/ws/vlog01.a1b2c3d4/v002_separate/vlog01_vocals.wav",
     "workspace":{"root":"...","dir":"...","version":2},
     "steps":[…pipeline 步骤摘要…], "truncated":false},
    {"index":2,"input":"/media/bad.mp4","ok":false,
     "error":{"code":"E_FILE","message":"无法打开输入文件：…"}},
    {"index":3,"input":"/media/vlog03.mp4","ok":true, …}]}}
```

- **失败续跑语义**：件 k 运行期失败（E_FILE/E_MODEL/E_ENGINE/E_INTERNAL 等一切非取消错误）→ 记入 `items[k-1].error`（产生点透传，多步件内已含「步骤 N（cmd）：」前缀与 step/stepCmd 键——批不二次包装）→ **继续件 k+1**；件内部分失败沿 pipeline D5 契约（失败步 discard、已提交步保留为可续跑版本）。
- **ok 聚合口径**：✅取「**编排层成功**」——批跑完（含部分失败）即 `ok:true`；`ok:false` 仅整批性失败：schema/预校验拒收（E_PARAMS）、取消（E_CANCELLED）、编排器自身异常（E_INTERNAL）。理由：① 失败不中断是需求明文，部分失败是批的**正常结果形态**而非批失败，混入 ok:false 会把「批没跑完」与「跑完但有败件」两种性质不同的事实压进一个布尔；② E_* 是产生点单码语义，多件多码强挑一个进顶层 error 必然丢信息或造假码；③ 逐件成败无损活在 items[].ok，CI 断言器逐件可查（BATCH-FAIL 断言 okCount=2 且 items[2].error.code=E_FILE）；④ 退出码随之 =「批是否跑完」，败件细查 JSON——与 fr24 D3「退出码只保非零即败的 shell 语义，CI 判定走 JSON 字段」一脉。**代价如实标注**：全败批退出码 0，脚本只看退出码会漏——文档明示「判批成败看 okCount/items，不看退出码」（OQ-1 给对立候选，主线程拍板）。
- **错误归属（结构键不缀前缀）**：`items[i]` 自带 `index`（1 基）与 `input` 字段即归属，error.message 保持产生点原文不加「第 N 件」前缀——pipeline 的步骤前缀存在是因为其错误是**顶层单错误**（前缀是 message 里唯一归属信号），批的件错误是**结构化清单成员**（归属在字段里，前缀反而冗余且破坏「产生点原文」透传）；GUI 汇总行由壳拼「第 k 件失败：<message>」。整批性错误的 schema 消息带 `inputs[i]`/`steps[i]` 定位（D2 模板）。
- **truncated 聚合**：顶层 `truncated` = 各件之或（pipeline 顶层同型），件内真值在 items[].truncated / steps[] 逐项可查。
- **items[i] 成功载荷 = 件响应 result 原样**（单动词件：output/sourceSpec/outputSpec/engineResampled/frames/durationSec/truncated/workspace；多步件：另含 steps 摘要）+ `index`/`input` 归属两键——pipeline 步骤摘要剥 workspace（因顶层只留末步的），批件**保留各自 workspace**（批无「唯一末工作区」概念，每件自报）。

### D5 工作区口径：批强制 `workspace`（缺即 E_PARAMS）；件与件版本目录互不干扰 = 内容哈希身份的天然性质

- **强制 workspace（沿 pipeline D6/OQ-1(a) 拍板口径镜像）**：不带 → E_PARAMS「batch 必须带 workspace」。批产物一律走工作区是红线明文；direct 批 = N 个散落产物无版本无登记，pipeline D6 否决 direct 的全部理由（不可追溯/不可续跑/kill -9 残留）对批逐条成立且被件数放大。GUI 恒注入根（既有机制）、CI/脚本传 mktemp 根（WS/PIPE 族同款）。
- **件间互不干扰论证（三情形穷尽）**：
  1. **不同源不同内容**（常态）：hash8 不同 → 不同工作区目录 → 各自 workspace.json/版本号序列/head 独立，零共享可写状态；
  2. **同内容不同路径**（拷贝/重命名后再批）：内容身份 → 同一工作区 → 顺次 append 新版本（append-only 语义正确：同源两次处理本就该两个版本）；
  3. **同一路径重复**：被 D2 的重复件拒收挡在开工前（不进此情形）。
  根级 index.json 缓存为全 root 共享，但批内逐件串行读写（单 worker 单 execute 链）无并发——fr-a4 D7 单写者假设在批形态下依然成立（批 = 一次一任务的加长任务，不是多写者）。
- **版本记录标记（OQ-3，倾向采纳）**：件版本记录增可选 `batch:{index,total}` 键（Ctx 增 `batchIndex/batchTotal` 缺省 0，runEngine/runInWorkspace 按存在性插键——pipeline 标记同型通路，commandlayer.cpp:233-238）。价值：批件散在不同工作区（内容哈希隔离的代价），**跨工作区的批关联只有标记可承载**（parentVersion 链跨不过去、时间窗+同参数只能模糊反推）——FR-A10 审计与 GUI「这版是批产物」辨识缺此无数据源；多步件的版本记录可同时含 `pipeline` 与 `batch` 两键（键名不冲突，各自按存在性插入）。成本 ~15 行 + 非批任务 workspace.json 逐字节不变的既有验证配方（WS 族 + PIPE-CHAIN 复跑）。

### D6 进度语义：两层等分嵌套映射（件层 ÷ n、件内步层 ÷ m）；GUI 状态行承载件位

**候选**：(a) 等分嵌套（件 i 内部进度 p ∈ [0,100] 映到 `(i*100+p)/n`；件内若为 pipeline 已自带步层等分——两层复合后粒度 = 件×步）✅；(b) 件粒度整数步（完成 k 件 → `k*100/n`，件内不动进度条）；(c) 按件时长加权。

**取 (a)**：(b) 在件数少（2–5）且件长（分钟级）时进度条长时间僵死——pipeline 选连续映射的体验理由（OQ-2 拍板）对批逐条成立；(c) 的权重表反例（硬编码引擎耗时，注册表「不复制引擎工作点」，fr9 OQ-2 已否决）原样适用，且批还多一维「件时长不可预估」（素材长度各异）。
**实现**：cmdBatch 组件子请求时 `subCtx.onProgress = [i*100+pct]/n` 重映射（cmdPipeline 等分 lambda 同型，commandlayer.cpp:342-344），pipeline 子调用内部的步层映射自动复合；取消指针原样透传。**GUI 承载**：主进度条走既有 progress 信号零改动；状态行提交时「批处理中（共 N 件）…」、当前件号 k 由 `pct*n/100+1` 派生显示（或维持静态「共 N 件」——实现期取简单者，PIPE 同款取舍）；件粒度实时清单状态见 D8（OQ-4）。

### D7 取消语义：整批停（不跳过当前件继续）；message 逐字「已取消」零漂移；完成度走结构化键（OQ-2）

**候选**：(a) 整批停：当前件按既有取消路径收尾（半成品删/版本目录 discard），已完成件保留，响应 `error{code:E_CANCELLED, message:"已取消", …}` ✅；(b) 跳过当前件继续后续件（「取消」变「跳过」）；(c) 两段取消（取消当前件 / 取消整批）。

**取 (a)**：(b) 语义错位——用户按「取消任务」的意图是停（FR-12「全任务可取消」），跳过当前件继续是**另一个功能**（skip，v1 无按钮无需求）；(c) 双按钮双语义超 v1 范围。取消检查点：**每件开工前**查一次（cmdPipeline 步间检查点同型，commandlayer.cpp:332-333）+ 件内步间/引擎分块既有检查点——≤1s 生效语义保持（NFR-R1）。
- **「已取消」逐字零漂移（硬约束）**：批级取消响应的 message 逐字「已取消」，不缀「（完成 k/n 件）」——GUI 特判是**全文相等**（mainwidget.cpp:689 `msg != "已取消"` 即弹警告），任何后缀都把静默取消变警告弹窗，即行为回归；pipeline E_CANCELLED 例外（fr9 D4）对批原样继承。完成度信息若需要，走 error 对象的结构化增量键（候选 `completed`/`total`，既有消费者只读 code/message 不受影响）——是否值得加见 OQ-2。
- **已完成件保留**（append-only 与「不留半成品」的张力，pipeline D5 契约逐字适用）：件 1..k-1 是已提交、已登记、来源链完整的版本，不是半成品；「不留半成品」约束在途任务的未完成产物——当前件在途半成品由底座删除 + 版本目录 discard（既有路径）。

### D8 GUI 片 B：批处理对话框（多选清单 + 动作组合 + 开始）；批占 busy 全批一锁；多选应答配方给验收钩子

**候选（入口形态）**：(a) 主面板「批处理…」按钮 → 非模态对话框（模型管理对话框同型：buildModelDialog 一次创建复用、busy 同锁）——清单 QListWidget（增/移除/清空）+ 动作组合框 + 开始按钮 ✅；(b) 无对话框：多选后立即以当前管线预设跑批（零审查一击即跑）；(c) 主面板单文件态改多文件列表（伤既有单文件心智，动大）。

**取 (a)**：(b) 无确认窗口误触即跑数十分钟任务、且单动词批无入口；(c) 与「一次一文件」的既有交互（openFile/七槽/历史查询按 curFile）全面冲突。对话框要素（objectName 供探针 findChild，模型管理对话框同配方）：
- **清单**：QListWidget `lstBatchFiles` +「添加文件…」（`QFileDialog::getOpenFileNames` 多选，过滤器与 doOpen 同串）+「移除选中」+「清空」；重复路径在添加时按 absoluteFilePath 去重（与命令层 D2 拒收同口径，壳层先拦 UX 更好，命令层防线仍在）。
- **动作组合**：QComboBox `cmbBatchAction` = 5 个引擎单动词槽（DSP 降噪/AI 降噪/变调/人声分离/伴奏提取——extract 两槽非引擎动词不可入模板，pipeline D2 词表口径；transcode 无 GUI 槽不进）+ 3 条管线预设（kPipePresets 复用）= 8 条。动态参数（humBase/wind/semi）开始时取当前面板注入（doPipeline 同律）。
- **开始**：组 `{inputs, steps}`（单动词 → 1 步模板）→ `mediaservice.batch(inputsJson, stepsJson)`；占 busy 全批一锁（pipeline 同锁先例——批是「一次一任务」的一个任务）；取消按钮复用既有 btnCancel（整批停，D7）。
- **结果落地**：新信号对 `batchFinished(bool, QString json/message)`（modelListDone 的 QString 传 JSON 先例）——成功时 json = result Compact JSON，对话框清单逐件渲染状态列（待处理→成功（版本号）/失败（错误码+消息））+ 状态行「批处理完成：k/N 件成功」+ 信息弹窗（onJobDone 同型）；失败（整批性）message 直通警告框；「已取消」静默特判同 onJobDone。逐件实时状态（跑完一件刷一行）需额外 `batchItemDone` 信号对——v1 不做，完成态清单 + 进度条件号派生够用（OQ-4 给候选）。
- **多选应答的验收钩子（探针配方，沿 pipe-gui-probe.cpp:157-175 加固形态）**：① 主路径（确定性，全平台）：探针**直调对话框填充槽/开始槽**传显式清单，绕开 QFileDialog——组包断言（捕获 batchRequested 深度比对）与 busy/结果断言全在此路径，零对话框依赖；② 对话框应答段（尽力）：行编辑多填——ExistingFiles 模式下文件名行编辑接受引号分隔多路径 `"f1.wav" "f2.wav"` 后点 AcceptRole 键（纯文本解析不依赖文件模型，规避 iso9660 停滞坑）——round-1 探针自检验证该应答在 offscreen 落定，不稳则记档降级为主路径 + Windows round-4 手工（fr9 report 环境注记同款处理）。

### D9 注册表关系与代码落点：batch 进内置表；零新文件零 CMake；两处抽取的零漂移控制

- **动词归属**：内置表一条（pipeline D9 同型：batch 无 IStreamEngine 无工厂无参数 schema——塞引擎表要造假条目）；配置见 D2。
- **执行器形态**：`cmdBatch(req, ctx)` = ① inputs schema（数组/≥2/元素串非空/重复检查）；② steps 预校验（validateSteps 共享，minSteps=1，含逐步 staticCheck）；③ workspace 必填检查；④ 件循环：取消检查 → 组件子请求（单点函数）→ execute → ok 则收 items 摘要、E_CANCELLED 则整批停（D7）、其余错误记 items 续跑（D4）；⑤ 聚合响应（total/okCount/truncated/items）。**引擎/底座/工作区内核零触碰**。
- **两处共享抽取**：`validateSteps`（cmdPipeline schema+预校验段外提，minSteps 参数化）与 `resolveStepModels`（ffextractor doPipeline 的模型步两级缺省解析循环外提，doBatch 复用）——均为「纯搬家不改逻辑」，零漂移门禁 = PIPE-NEG 19 条 + PIPE-GUI 组包断言复跑（§6 BATCH-ZERO）。
- **NFR-M1 零改承诺保持**：批执行器只依赖 validateSteps/staticCheck/execute 与条目公共契约，不 switch 引擎 id（GUI 壳 cmbBatchAction 的 verb→bundled 映射除外——壳既有形态）；M3 假想验证：RVC 入表后批模板零改可跑（BATCH-REG 同 PIPE-REG 方法论）。
- **信封与 Ctx 零漂移面**：信封代码零触碰（batch 走 requiresInput/extraKeys 既有机制）；Ctx 若加 batchIndex/batchTotal（OQ-3 采纳）为缺省 0 纯增量字段，既有调用方零感知；cmdPipeline 组步骤 subCtx 时**须显式拷贝批字段**（现构造 fresh Ctx 只填 pipeline 字段，commandlayer.cpp:338-341——批字段不拷贝则多步件的引擎子调用丢批标记，实现期以 BATCH-WS 标记断言拦截）。

### D10 零漂移面清单（硬门禁）与批处理前瞻

- **触碰点枚举（本包全部既有代码改动，逐点零漂移要求）**：
  1. validateSteps 抽取（commandlayer.cpp:269-318）：消息模板与校验序逐字不变——PIPE-NEG 19 条 + BATCH-NEG 复跑；
  2. Ctx 增批字段（OQ-3）：缺省 0 纯增量——编译期零行为变化；
  3. workspace 版本记录增可选 batch 键（OQ-3）：按存在性插键，非批任务 workspace.json 逐字节不变——WS 族 + PIPE-CHAIN 复跑；
  4. runEngine 标记组装邻域增批标记（pipeline 标记同型 ~8 行）：非批路径不触发；
  5. builtinVerbs 表增一行：纯增量；
  6. resolveStepModels 抽取（ffextractor.cpp:541-564）：doPipeline 行为零变——PIPE-GUI 组包断言复跑；
  7. mediaservice/ffextractor/mainwidget：新增信号槽与控件为纯增量行，七槽/管线按钮及其文案零触碰；
  8. main.cpp / 旧 7 薄壳 / --json 壳：**零触碰**（batch 不加旧式薄壳，wsInfo/model 族/pipeline 同款只走 --json）。
- **批处理前瞻（不过度设计）**：模板参数化（逐件不同参数）、交互式队列（运行中增删件）、批模板命名保存（FR-17 对象）、跨批持久化队列（重启续跑）全部不做（§5 留缝逐条）；Agent 的多文件计划以 items[] 逐件可寻址结果为消费面（fr9 §5 FR-A3 缝的批版延伸）。

## 3. 方案（模块图 + 数据流）

```
 ┌─ GUI 壳（D8）────────────────────┐   ┌─ CLI 壳 ─────────────────────────┐
 │ mainwidget：「批处理…」按钮 + 对话框 │   │ --json: {"cmd":"batch",           │
 │  （清单/动作组合/开始；结果逐件渲染）  │   │   workspace, inputs:[…],          │
 │ mediaservice：batch/batchFinished   │   │   steps:[…]}（无旧式薄壳）          │
 │  一对信号槽（QString 传 JSON）       │   └──────────────┬───────────────────┘
 └────────────┬─────────────────────┘                  │
              ▼ worker 槽 doBatch                       │
        （resolveStepModels 模型步两级缺省解析 →           │
          组 batch 请求 → execute：workspace 注入/       │
          E_MODEL→modelMissing/进度取消复用）             │
              │ QJsonObject{cmd:batch, inputs[],        │
              │             steps[], workspace} + Ctx    │
              ▼                                          ▼
      ╔═══════════════════════════════════════════════════════╗
      ║ CommandLayer::execute                                   ║
      ║  信封校验（既有段零改：requiresInput=false/extraKeys）     ║
      ║  内置表命中 batch → cmdBatch：                            ║
      ║   ① inputs schema（≥2/串非空/重复拒收）                    ║
      ║   ② 模板预校验（validateSteps min=1 + staticCheck×m，     ║
      ║      不调 create；一次覆盖全批）                            ║
      ║   ③ workspace 必填（D5）                                 ║
      ║   ④ 件循环：cancel 检查 → 组子请求 execute：               ║
      ║      steps≥2 → {cmd:pipeline, input, steps, ws}          ║
      ║      steps=1 → {cmd:<verb>, input, 参数, ws}             ║
      ║      （子调用内部：planVersion（内容哈希各自定位工作区，      ║
      ║        batch 标记随 Ctx）→ 转码 → commitVersion）          ║
      ║      件败 → items[].error 续跑；E_CANCELLED → 整批停       ║
      ╚═════════════╤═══════════════════════════════════════════╝
                      ▼
      响应：ok=true：result{total, okCount, truncated=或,
        items[{index, input, ok, output, workspace, steps?, error?}]}
        / 整批性失败：error{code, message（E_CANCELLED 逐字「已取消」）}
```

**正例数据流**（GUI 三文件批「DSP 降噪 → 人声分离」）：对话框添加 3 件 → 开始（模板 = 当前面板参数的两步 steps）→ doBatch 解析模型步（separate 两级缺省解析）→ runRequest 型收尾 → 信封/schema/预校验通过 → 件 1：pipeline 子请求执行（v001_denoise→v002_separate，batch:{1,3} 标记）→ 件 2、件 3 同模板各自工作区（batch:{2,3}/{3,3}）→ result.okCount=3 → batchFinished(true, json) → 对话框清单三行「成功」+ 状态行「批处理完成：3/3 件成功」。峰值 RSS = 最重件的最重步（分离步）单动词水位。

**失败续跑流**（件 2 源损坏）：件 2 execute 子响应 E_FILE「无法打开输入文件」→ items[1]={index:2, ok:false, error:{code:E_FILE,…}} → **继续件 3** → 终响应 ok=true okCount=2 → GUI 清单件 2 行「失败：无法打开输入文件…」。

**取消流**（件 3 中段取消）：件 3 的引擎分块检查点置位 → 该件在途半成品删 + 版本目录 discard → 子响应 E_CANCELLED → cmdBatch 整批停 → `error{code:E_CANCELLED, message:"已取消"}`（逐字，GUI 静默特判）→ 件 1/2 版本保留（append-only；历史对话框可逐件「用此版本继续」）。

## 4. 改动面（file:line 级预估，基于 HEAD e2e1b07）

| 文件 | 片 | 改动 | 预估规模 |
|---|---|---|---|
| `src/service/commandlayer.cpp` | A | `validateSteps` 从 cmdPipeline :269-318 抽取（minSteps 参数化，cmdPipeline 改调零消息漂移）；`cmdBatch`（inputs schema + 模板预校验 + 件循环 + 聚合 + 取消/进度映射）；内置表一行（:476-490 邻域）；runEngine 批标记组装（:233-238 邻域，OQ-3 采纳时）；cmdPipeline 步骤 subCtx 拷贝批字段（:338-341） | ~150 行 |
| `src/service/commandlayer.h` | A | Ctx 增 `batchIndex/batchTotal`（缺省 0，pipeline 字段同型注释；OQ-3 采纳时） | ~4 行 |
| `src/service/workspace.h` / `.cpp` | A | `VersionPlan` 增可选 `batch` 字段；commitVersion 按存在性插键（pipeline 标记邻域；非批任务逐字节不变） | ~12 行 |
| `src/service/mediaservice.{h,cpp}` | B | `batch(inputsJson, stepsJson)` 请求信号 + `batchFinished(bool, QString)` 结果信号（busy 释放挂 batchFinished lambda，modelOpDone 先例） | ~16 行 |
| `src/service/ffextractor.{h,cpp}` | B | `resolveStepModels` 从 doPipeline :541-564 抽取（doPipeline 改调零漂移）；`doBatch` 槽（解析清单/模板 → 模型步解析 → 组 batch 请求 → 自有收尾：批无单一 output，成功组「批处理完成」+ result JSON、失败 message 直通、取消逐字、开工前模型双缺发 modelMissing(verb)） | ~55 行 |
| `src/view/mainwidget.{h,cpp}` | B | 「批处理…」按钮 + 批处理对话框（清单/动作组合/开始/逐件结果列；busy 同锁纳入 set_busy）+ onBatchDone 渲染；QSettings 零新键（根用既有 workspaceRoot 注入） | ~130 行 |
| `CLAUDE.md` | A/B | 架构段批处理一句 + `--json` 动词清单 + ok 口径注记（判批看 okCount 不看退出码） | ~3 行 |
| `docs/acceptance/wp-1.2-linux-build/scripts/ci-smoke.sh` | A | 追加一行调用 `run-batch.sh`（round-1 全绿后） | 1 行 |

**零新文件（源码侧）、零 CMake 改动、零引擎/底座/注册表触碰**：五引擎、streamengine.h、fftranscoder、engineregistry、modelmanager、workspace 内核（哈希/两段提交逻辑本体）、main.cpp 全部不动。

## 5. 留缝清单（本包不做，后续包的接口锚点）

| 缝 | 本包怎么留 | 不动什么 |
|---|---|---|
| **FR-4 质量档 / M3 RVC 入批** | 模板词表 = 引擎表枚举，新引擎注册即自动可批（BATCH-REG 可机械化验证） | 不预造条目/档位参数（单模板单动词序列的 v1 形态不变） |
| **模板参数化（逐件不同参数）** | items[] 异构形是自然演进位（D2 候选 (b) 的 schema 骨架已论证）；Agent 计划归一时逐件模板 = N 次批调用 | 不做 items[]/参数占位符/逐件 override |
| **交互式队列（运行中增删件）** | 批是命令层动词，GUI 队列 UI 届时对 batch 调用做编排即可（命令层面无队列态） | 不做队列持久化/断点续批/跨批调度 |
| **QLockFile 跨进程锁** | fr-a4 OQ-6 口径延续：批 = 单进程单 worker 串行，单写者假设成立；真并发（多进程批/Agent 并行）到来时连同队列语义一起设计 | 不加锁（不可测的锁是假安全） |
| **FR-17 预设系统** | 批动作组合复用管线预设静态表；用户自定义批模板保存 = 预设系统的批版扩展 | 不做批模板 CRUD/持久化 |
| **FR-A10 审计** | 版本记录 batch:{index,total} 标记（OQ-3）+ items[] 逐件结果是批级审计数据源 | 不做独立审计通道/批 id（时间戳+index/total 可定位） |
| **FR-A3 Agent（v1.x）** | batch 是 Agent「多文件计划」的确定性执行原语；items[].error 结构化归属支持逐件重试叙事 | 不做 NDJSON/逐步确认 |

## 6. 验证方案（目录 `docs/acceptance/fr10-batch-queue/`，风格对齐 run-pipe/run-json）

**目录骨架**（实现开工时建）：`goal.md` + `plan.md` + `matrix.csv`（前缀 `BATCH-`）+ `round-1.md`（片 A CLI）/`round-2.md`（片 B GUI）+ `scripts/run-batch.sh` + `scripts/gui-batch-probe.cpp` + `scripts/gui-batch.sh`（复用 fr24 的 json_check.py 断言器与 jr 配方、run-pipe 素材配方，交叉引用不复制）+ 全绿后 `report.md`。工作区走磁盘卷 TMPDIR（fr8 round-2 /tmp 配额教训，run-pipe 同款 `$BUILD/.batch-tmp`）；缺模型族 else-skip + `PARITY_REQUIRED=1` 升级 FAIL 门禁语义三处齐整。

**用例族（BATCH-\*）**：

1. **BATCH-OK（正例）**：3 件 × 免模型两步模板 e2e——result 结构断言（total/okCount=3/truncated/items[] 逐项 index/input/output/workspace/steps）；带模型模板（有 onnx 时）单动词批（1 步模板）+ 多步批各一；版本目录落位（每件各自工作区 vNNN_&lt;cmd&gt; 链）；件产物 ffprobe 体检。
2. **BATCH-EQV（编排等价，核心）**：同模板同 3 件「批跑」vs「逐件手动跑 pipeline/单动词（各带 workspace）」——各件终产物与中间产物**逐字节一致**（批 = 纯编排零数值漂移；确定性由 CMD 背书）。
3. **BATCH-FAIL（失败不中断，需求明文）**：3 件中件 2 为坏文件（不存在/无音频流/坏模型三配方）→ ok=true、okCount=2、items[2].error.code 逐配方断言（E_FILE/E_MODEL）+ message 原文无「第 N 件」前缀；件 1/3 产物完整落盘；多步模板的件中步失败（wp-1.0 维度错位模型）→ items[].error 含「步骤 N（cmd）：」前缀与 step/stepCmd（pipeline 归属透传）；全败批（3 件全坏）→ ok=true okCount=0（ok 口径断言）。
4. **BATCH-NEG（schema 负例）**：inputs 缺失/非数组/1 件/元素非字符串/空串/重复绝对路径；steps 缺失/非数组/空（0 步）/cmd 缺失/内置动词入模板（extractAudio/probe/pipeline/batch 嵌套——递归非法）/模板步含 input、output、workspace、v/未知参数字段/参数越界（semi:15）/模板模型路径不存在 → 逐条 E_PARAMS 消息断言 + **开工前拦截**（无版本目录、无产物、全部源 sha256 不变——预校验零副作用）；不带 workspace → E_PARAMS（D5）；output 同给 → 互斥违例（信封既有）；顶层 input 给了 → 未知字段（requiresInput=false 口径）。
5. **BATCH-CANCEL（取消）**：CLI 不可达（单发同步）——沿 PIPE-CANCEL 方法论：代码级证据（取消注入点/件间检查点）+ round-2 GUI 面承接；断言点：message 逐字「已取消」、件 1 版本保留、件 2 无残迹（版本目录不落、索引 head 不动）。
6. **BATCH-WS（工作区隔离与标记）**：3 件不同源 → 三个工作区各自独立版本序列断言；同内容不同路径两件 → 同一工作区顺次 v001/v002（append-only 正确语义）；batch:{index,total} 标记逐件断言（OQ-3 采纳时）+ 多步件版本记录同时含 pipeline 与 batch 两键；缓存删除（rm index.json）后批继续定位；**非批任务 workspace.json 逐字节不变**（WS 族复跑交叉引用）。
7. **BATCH-PROG（进度映射）**：GUI 语义经 Ctx 注入的代码级证据（件层等分 lambda 与 pipeline 步层复合）；进度条终值 100（round-2）。
8. **BATCH-ZERO（零漂移硬门禁）**：run-json（CMD 36 + CMD-021r 全文逐字节）+ run-ws + run-models + run-pipe 77 条 + gui-slot-eqv + thinshell-eqv + ci-smoke 全量复跑（validateSteps/resolveStepModels 两抽取 + Ctx/workspace 增键三触碰的回归网）。
9. **BATCH-GUIE（片 B 探针，round-1 编译自检）+ BATCH-GUI（round-2）**：对话框组包断言（直调槽传清单，捕获 batchRequested 深度比对——8 动作 × 动态参数注入面）；多选对话框应答段（行编辑引号多填 + AcceptRole，D8 配方，不稳则记档降级）；busy 锁全批覆盖（开始即锁/完成即解/运行中七槽与管线按钮全锁）；成功文案「批处理完成：k/N 件成功」；「已取消」静默；结果清单逐件渲染（k=okCount）；开工前模型双缺 → modelMissing(verb) 引导（MIST 同款文案锁）。
10. **BATCH-REG（注册表零改，M3 前瞻）**：PIPE-REG 同方法论——分支注入假引擎条目后 `git diff` 白名单断言批相关文件零改，假引擎即可入模板 e2e（正例 + 模板参数负例）。分支证据制不进 ci-smoke。
11. **BATCH-MEM（内存抽测）**：≥30min 素材 2 件批（免模型模板）+ 带模型件（有 onnx 时）峰值 RSS 与最重单动词同量级（/usr/bin/time -v 采样对照——件串行 × 步串行 → 峰值 = 全部件·步最大值的实证）。

**门禁接线**：round-1（CLI 面 Linux）全绿后 `run-batch.sh` 进 ci-smoke（run-pipe 同款一行）；Windows 真机随 WP-1.0 round-4 挂账口径（五包同款）。

## 7. 开放问题（主线程拍板，拍板后记入 acceptance goal.md）

- **OQ-1 ok 聚合口径**：候选 (a) 编排层成功（批跑完即 ok=true，成败逐件 items[].ok + okCount/total 汇总；整批性失败——schema 拒收/取消/内部异常——才 ok:false）——D4 推荐；(b) 全部成功才 ok=true（任一件失败 → ok:false + error 扩形载 items 明细——error 对象需承载清单形，破坏「error = 单错误产生点透传」的既有语义，且 GUI 对 ok=false 走警告弹窗，部分失败批每次都弹警告与「失败不中断是正常形态」的产品语义相抵）；(c) 至少一件成功即 ok=true（「全败才算败」——okCount=1/10 与 10/10 同为 true，聚合信息量最差）。**倾向 (a)**：部分失败是批的正常结果形态；退出码随之表「批是否跑完」，文档明示「判批成败看 okCount/items 不看退出码」。影响：D4、BATCH-FAIL/BATCH-OK 断言、GUI 弹窗类型、CI 脚本判定写法。
- **OQ-2 取消响应是否带完成度结构键**：候选 (a) 不带——error{code, message:"已取消"} 与 pipeline 取消响应同形（零新键零新语义）；(b) 带 `completed`/`total` 增量键（message 保持逐字不动，结构化完成度供 GUI/Agent 显示「已取消（完成 2/3 件）」类信息——信息放 error 增量键不进 message，GUI 特判不受影响）。**倾向 (b)**：批的取消代价远高于单动词（可能已跑数十分钟），完成度是用户取消后第一想知道的信息；成本 = error 对象两个增量键（pipeline step/stepCmd 增量键先例），既有消费者只读 code/message 零影响。影响：D7、BATCH-CANCEL 断言、GUI 取消后状态行。
- **OQ-3 版本记录 batch 标记**：候选 (a) 采纳 `batch:{index,total}`（Ctx 增两字段 + VersionPlan 增字段 + 按存在性插键 ~15 行；跨工作区批关联的唯一可承载面——parentVersion 链跨不过内容哈希隔离，时间窗反推不可靠；FR-A10 审计与 GUI 辨识「批产物 vs 手动产物」缺此无数据源）——D5 推荐；(b) 不加（省 15 行 + 一处零漂移验证面；代价 = 批产物在版本史中不可辨识，与 pipeline 附裁 4「{index,total} 数据不可再生」同款论证反向适用）。**倾向 (a)**：pipeline 附裁 4 已为同类权衡立例（成本一个可选键 vs 不可再生的审计数据），批的跨工作区分散使该数据更加不可再生。影响：D5、改动面、BATCH-WS 断言、BATCH-ZERO 验证面。
- **OQ-4 GUI 逐件实时状态**：候选 (a) 完成态清单（批结束后 batchFinished 一次渲染逐件状态；运行中靠进度条 + 状态行件号派生）——D8 推荐；(b) 实时逐件状态（新增 batchItemDone(int, bool, QString) 信号对，跑完一件刷一行——长批体验更好，代价 = 一对信号槽 + 清单并发渲染 + 信号乱序防护面）。**倾向 (a)**：v1 批规模（个位数件）下进度条已可派生件位；overnight 场景完成态渲染已够复盘；(b) 的增量价值随交互式队列（§5 留缝）才真正兑现，届时一并设计。影响：D8、mediaservice 改动面、BATCH-GUI 断言。
- **OQ-5 inputs 最小件数**：候选 (a) ≥2（「队列」语义下限，pipeline steps≥2 附裁 2 同构：单件 = 直接调单动词/pipeline，不产生两套记录路径）——D2 推荐；(b) ≥1（宽松：GUI 多选选 1 件也放行——但 GUI 添加时清单去重后单件可提示改走单文件面板，无需动词层宽容）。**倾向 (a)**：严格 schema 风格一贯（fr24 D2「幻觉参数 100% 拦截」精神同源），单件批无独立价值。影响：D2、BATCH-NEG 断言（1 件拒收）。**备注**：fr-a4 OQ-6 的 QLockFile 留缝本包维持不动（批 = 单进程串行，无真并发可测——不可测的锁是假安全，fr-a4 拍板原话），真并发随交互式队列/Agent 并行到来时连同队列语义一起设计。
- **OQ-6 GUI 批动作组合是否含 transcode**：候选 (a) 8 条 = 5 引擎槽 + 3 预设（transcode 无 GUI 单文件槽，批不首发）——D8 推荐；(b) 9 条加 transcode（「批量规整化容器」场景——视频批统一转 WAV 是真实场景，但 GUI 从未暴露 transcode，批首发引入新动词 UI 认知成本）。**倾向 (a)**：CLI/Agent 面模板词表含 transcode（引擎表全体，零裁剪），GUI 面克制；GUI 需求出现时加一条组合框条目即可（壳增量）。影响：D8、cmbBatchAction 条目、BATCH-GUI 用例数。

## 7.1 拍板（主线程，2026-11-03；spec 作者报告 × 对抗约束枚举报告交叉对照后）

两报告独立收敛的六轴（架构位置/请求形态/ok 聚合/单件下限/取消粒度/锁留缝）直接定案；对抗报告关键 file:line 锚点经主线程抽查核实成立（fr24 §5.4:227、REQUIREMENTS.md:50/:37、mediaservice busy 复位 :43-55 与 `if (busy) return` :79-91、cmdPipeline fresh subCtx :338-341、mainwidget「已取消」全文特判 :689）。

**OQ 全裁**：

- **OQ-1 → (a) 编排层成功**（= 对抗 U3-A）：批跑完即 `ok:true`，成败在 `items[].ok` + `okCount/total`；`ok:false` 仅整批性失败（schema 拒收/取消/编排器异常）。退出码表「批是否跑完」，CLAUDE.md/文档明示**判批成败看 okCount/items 不看退出码**（风险 5 缓解随附）。
- **OQ-2 → (b) completed/total 增量键**（补 D7）：`error{code:"E_CANCELLED", message:"已取消"（逐字）, completed, total}`。**completed 口径**（关闭 spec 弱点 3 的缝）：= 已完整落定的件数（成功、或失败已记入该件 error）；被取消的当前件**不计**（未完成，在途半成品已删+版本目录 discard）；取消落在件开工前检查点时该件同样不计。取消响应不带 items[]（error 信封无 result，pipeline 取消同形）。
- **OQ-3 → (a) 采纳 `batch:{index,total}`**（= 对抗 I5）：Ctx 增 `batchIndex/batchTotal` 缺省 0、VersionPlan 增可选 batch 字段、按存在性插键；**cmdPipeline 组步骤 subCtx 处显式拷贝批字段**（风险 4，BATCH-WS 多步件双键断言拦截）。
- **OQ-4 → (a) 完成态一次渲染**：batchItemDone 信号对不做（随交互式队列一并设计，§5 留缝）。
- **OQ-5 → (a) inputs ≥2**（= 对抗 U5-A）；**QLockFile 维持留缝**（= 对抗 U7-A，fr-a4 OQ-6 口径：批=单进程串行无真并发可测，不可测的锁是假安全）。
- **OQ-6 → (a) 8 条不含 transcode**：CLI/Agent 模板词表=引擎表全体零裁剪，仅 GUI 组合框克制。

**附裁五项**（对抗报告独立提出、spec 未收或需显式化的轴）：

- **附裁 1（= 对抗 U4/Y5 毒缝封死）—— modelMissing 只在 doBatch 开工前发，件循环内禁止**：模板全批共享，模型缺是模板级事实——开工前 resolveStepModels 失败即发 `modelMissing(<引擎动词id>)` 并整批快速失败（busy 毫秒级释放，MIST-001..011 已证的引导窗口形态）；件循环中段**不得** emit modelMissing（busy 长持 → 引导弹窗「是」→ 模型管理下载撞 mediaservice `if (busy) return` 静默吞 = 对抗报告 Y5 实锤）。件循环中的件级模型类错误（如批跑中模型文件被删）→ `items[].error(E_MODEL)` 续跑不发引导；用户补模型后重跑。forVerb = 模板中缺模型的引擎动词 id（两级缺省解析归属键，fr9 附裁 1 同型），非件级。
- **附裁 2（= 对抗 U6）—— inputs 无件数上限**：件串行执行，峰值 RSS 与件数正交（D1 内存论证）；items[] 线性累积 KB 级摘要。不设上限不加校验（省一条假约束），文档明示口径。
- **附裁 3（= 对抗 I3 裁定）—— 件错误不缀「第 N 件」前缀**：D4 论证成立（pipeline 步骤前缀存在因其错误是顶层单错误、前缀是 message 内唯一归属信号；批件错误是结构化清单成员，归属在 index/input 字段，前缀反而破坏产生点原文透传）。BATCH-FAIL 断言 message 原文无前缀；GUI 汇总行由壳拼「第 k 件失败：<message>」（呈现层职责）。
- **附裁 4 —— fr24 §5.4 演进记档**：batch 落地时同步更新 `docs/spec/fr24-command-layer.md` §5.4 该句（队列/失败不中断语义已由 FR-10 命令层动词承载），防止后来者按旧缝另造服务层队列（工作原则 10）。
- **附裁 5 —— GUI 批路径「已取消」静默特判**：onBatchDone 与 onJobDone（mainwidget.cpp:689）同型（`msg != "已取消"` 才弹警告）；批完成弹 information「批处理完成：k/N 件成功」（OQ-1(a) 语义：ok=true 部分失败不弹警告，败件在清单内逐件呈现）；整批性失败 message 直通警告框。

## 8. 红线自查与实现风险

**红线自查**：
- **原件只读**：批中每件只读用户源（既有只读解码/哈希通路）；`output==input` 防线在每个子请求的信封段原样生效；BATCH-NEG/BATCH-FAIL 断言全部源 sha256 前后不变。
- **批产物一律走工作区 / append-only**：batch **只**提供工作区模式（D5，pipeline D6 最强形式同款）；件间工作区隔离由内容哈希身份天然保证；版本目录不覆写、失败件 discard 既有路径。
- **流式内存有界（NFR-P4）**：批执行器零音频缓冲（只过 JSON 小对象与 items 摘要）；件串行步串行引擎逐步建毁 → 峰值 = 全部件·步最大值（D1，BATCH-MEM 实证）。
- **不引入 GPL/新依赖**：零新文件零 CMake，全部 Qt Core 既有能力；LGPL 组合零变化；真网零涉及（批不碰模型下载——模型缺失走开工前 modelMissing 引导，runRequest 型既有路径）。
- **旧面逐字节零漂移**：八处触碰点逐点列明（D10），BATCH-ZERO 全量复跑为硬门禁；QSettings 零新键（D7 边界保持——根注入沿用既有 workspaceRoot 通路，QSettings 仍只在 GUI 主线程读写）。
- **注释与文案中文**；Bash/POSIX；「已取消」逐字兼容 GUI 特判（D7）。

**实现风险**：
1. **validateSteps 抽取的消息序漂移是最大零漂移风险**（FR-9 风险 1 同型）：schema 段与预校验段外提时任何顺序/模板改动都会漂移 PIPE-NEG 19 条断言。缓解：纯搬家 + minSteps 仅参数化下限消息一处 + 模板旁注对应 PIPE 用例编号 + PIPE-NEG/BATCH-NEG 双族复跑。
2. **件子请求构造错误**（FR-9 风险 2 同型）：链级键（inputs/steps/workspace/v）混入件子请求会击穿校验或污染版本记录。缓解：子请求组装单点函数 + BATCH-NEG 对模板步内 input/output/workspace/v 拒收 + BATCH-WS 的版本记录 params 断言（不含链级键）。
3. **取消路径的件间状态复杂度**：取消落在件内步中/件间空隙/预校验段三处，各处已完成件保留与在途件清理的正确性分散在 execute 既有路径 + cmdBatch 检查点。缓解：cmdBatch 取消检查收敛两处（件开工前 + 件响应判 E_CANCELLED），清理全部复用既有（底座删半成品 + discardVersion）；BATCH-CANCEL 代码级证据 + round-2 GUI 面。
4. **批标记的 Ctx 传递链断裂**：cmdPipeline 现构造 fresh subCtx（commandlayer.cpp:338-341），批字段不显式拷贝则多步件的引擎子调用丢 batch 标记（静默缺键，无报错）。缓解：实现期单点注释 + BATCH-WS 的多步件双键断言（pipeline+batch 同记录）专门拦截。
5. **全败批退出码 0 的误判面**（OQ-1(a) 的代价）：CI/脚本只看退出码会漏全败。缓解：CLAUDE.md/README 明示判定口径 + run-batch.sh 断言模板示范「okCount 逐件断言」写法。
6. **多选对话框 offscreen 应答的不确定性**：行编辑引号多填在 offscreen/挂载异常环境的稳定性未实证（单填配方实证过，多填是新增量）。缓解：主路径断言绕开对话框（直调槽传清单）；应答段 round-1 探针自检，不稳则记档降级为主路径 + Windows round-4 手工（fr9 环境注记同款处理）。
7. **Windows 真机验证滞后**（round-4 pending 挂账口径，六包同款）：批 GUI 完整体验（多选/清单/取消/结果渲染）Windows 侧靠 runbook。缓解：round-2 GUI 面用例在探针层断言请求 JSON 与清单渲染（平台无关），Windows runbook 增补批处理段。
