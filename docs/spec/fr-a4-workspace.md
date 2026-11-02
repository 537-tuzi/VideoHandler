# FR-A4 工作区版本化与任务记录（开工 spec）

- 日期：2026-11-03
- 性质：开工前快照（M2 第三个工作包，依赖序承接 NFR-M1；进展追踪见 `docs/acceptance/fr-a4-workspace/goal.md`——实现开工时才建目录）
- 本文 D 编号（D1–D8）仅限本 spec，与 REQUIREMENTS.md 决策记录（D1–D7）无关。
- 现状基线：HEAD `0c9fc6c`（NFR-M1 已全落地：FR-24 命令层 + engineregistry 注册表 + ErrInfo 错误通道）。

## 0. 需求锚点与约束

| 锚点 | 内容 | 对本包的约束 |
|---|---|---|
| FR-A4（REQUIREMENTS.md:99） | 「原件只读；**手动+Agent 任务统一走此机制**；输入哈希+参数+版本化输出；可回退」（Agent 的地基，v1 先行落地） | 四个硬点逐一落决策：只读不变式（§6 WS-RO）、GUI 手动任务与 CLI/Agent 同一通路（D5）、输入哈希+参数进版本记录（D3）、版本化输出与可回退语义（D2） |
| 核心原则 3（REQUIREMENTS.md:75） | 不可破坏性靠**机制**不靠自觉：原件只读 + 工作区版本化 + 计划三道校验闸 | 工作区是机制本体，不是 UI 功能；红线自查（§8）逐条对齐 |
| D5（REQUIREMENTS.md:18） | v1 只做铺垫（工作区 + 无头命令层），Agent 本体 v1.x | 不做意图路由/计划；但 FR-A3（REQUIREMENTS.md:114）已写明 Agent 的「备份、版本登记」是自动执行的基础设施步骤——本包产出的登记机制就是该步骤的实现位 |
| 红线（CLAUDE.md） | 「原件只读是新工作区机制（FR-A4）的根基：任何新功能不得改写用户源文件」 | 本包自身首先不得违反：哈希只读、落盘只在工作区、防线保留（§8） |
| NFR-A2（REQUIREMENTS.md:202）/ §7 旗舰验收（:237） | 执行层永不改写输入文件（测试强制）；US7 验收含「原件字节级不变（哈希校验）」 | 验收用例以 sha256 前后对照为判据（WS-RO 族），口径直接复用 §7 |
| v1-development-plan.md:38-39 | M2 依赖序：FR-24 → NFR-M1 → **FR-A4** → FR-8 → FR-9 → FR-10；「FR-A4 落地后一切新功能必须走工作区，不得改写源文件」 | 接面要给 FR-9 管线（版本目录多产物）与 FR-10 批处理（串行重入）留缝（§5）；FR-8/9/10 尚未开工，本包不预建其结构 |
| FR-24 spec §5.3 留缝（fr24-command-layer.md:226） | 「输出路径决策收敛为命令层内单一函数（显式 output 优先，缺省走源目录后缀规则）；**工作区包替换该函数即可接管全部动词的落盘位置**。v1 已立原件只读防线：output==input → E_PARAMS」 | 本包兑现该缝：`outputOr`（commandlayer.cpp:43-48）是替换点；信封防线（:251-256）原样保留 |
| NFR-M1 spec §5 留缝（nfr-m1-engine-registry.md:211） | 「`outputOr` 缺省命名保持命令层单点（片 A 仅后缀参数化），工作区包照旧替换该函数接管落盘」 | 条目 `outputSuffix`（engineregistry.h:39）继续是缺省后缀单一来源，工作区命名复用它而非另立一套 |
| P3-3 挂账（fr24 round-1.md:74） | extract 显式 output 指名覆盖既有文件、失败会删除该路径；「拒绝覆写或临时名落盘」随工作区包收紧 | 工作区模式下版本目录天然不覆写；direct 模式是否一并收紧 → OQ-4 |
| P3-2 挂账（fr24 round-1.md:73） | 截断源 transcode ok:true（av_read_frame 中途 I/O 错被当 EOF）；OutInfo.truncated 属底座小改 | 工作区版本记录是该标志的天然消费者（诚实记录）→ OQ-5 |
| NFR-R1（REQUIREMENTS.md:177）/ NFR-P4（:165） | 取消 ≤1s、失败删半成品；2h 峰值 RSS ≤2GB、禁止整文件载入 | 取消/失败不得留版本目录与索引残迹（D2 原子性）；哈希/索引全程流式小缓冲（D1） |
| 红线 | 不引 GPL/AGPL；`third_party/` 与模型权重不入库；注释与文案中文 | §8 自查（哈希用 Qt Core 自带 QCryptographicHash，零新依赖） |

## 1. 现状盘点（证据，HEAD 0c9fc6c 实读）

**输出散落源目录 + 后缀命名，无版本概念：**

- 转码族缺省输出 = 源目录 + `completeBaseName` + 条目后缀 + `.wav`：`outputOr`（commandlayer.cpp:43-48，被 runEngine 于 :148 调用；后缀由注册表条目 `outputSuffix` 供给，engineregistry.cpp:19-27）。同源同动词**第二次执行直接覆盖第一次产物**（无版本、无提示）。
- extract 族缺省输出同样落源目录，命名规则在底座内部：extractAudio = `源目录/名_audio.<按源编解码器扩展名>`（ffextractor.cpp:214）、extractVideo = `源目录/名_mute.<源后缀>`（ffextractor.cpp:242）——扩展名依赖打开输入后的编解码器，命令层无法提前复算（fr24 goal.md:21 已注明「工作区包接管落盘位置时需连底座规则一起评估」）。
- 显式 `output` 时直接写指名路径，**覆盖既有文件不设防**（P3-3）；失败删除该路径（ffextractor.cpp:107-119 finish 统一删半成品）。
- 原件只读唯一防线：信封段 `output==input` 绝对路径比较 → E_PARAMS「output 不能与 input 相同（原件只读）」（commandlayer.cpp:255-256）。GUI 缺省不传 output（ffextractor.cpp:346,353 注释「不传 output：缺省自动命名」），对 GUI 生效的是「缺省名不与源同名」这一弱保证。

**无来源链、无任务记录：**

- 请求是纯函数单发：`CommandLayer::execute(request, ctx)` 无全局态、不落任何记录（commandlayer.h:20-22）。`--json` 每请求独立（main.cpp:24-40），旧 7 动词薄壳同理（main.cpp:57-137）。拿到 `x_vocals.wav` 后无从得知：从哪个源、哪个动词、什么参数、哪个模型、哪个中间产物得来——多轮处理链（denoise→separate→pitch）的产物全部平铺且互相不可追溯。
- 结果对象只回当次输出：`result{output/sourceSpec/outputSpec/engineResampled/frames/durationSec}`（commandlayer.cpp:60-73），OutInfo 是解码真值但用后即弃（fftranscoder.h:19-23）——版本记录可复用的现成数据源。

**GUI 侧：**

- 七槽任务形态：mainwidget 七个 do* 槽（mainwidget.cpp:155-214）→ mediaservice 七对信号槽（mediaservice.h:26-43）→ ffextractor worker 槽组 JSON 请求走 execute（ffextractor.cpp:289-355，片 D）。**一次一任务**：busy 锁 + 取消原子量（mediaservice.h:21-22；提交槽 busy 拒收 mediaservice.cpp:53-56 等，jobDone 复位 :36-40）——单 GUI 内不存在并发写同一工作区的形态。
- QSettings 现只记一个键：`humBaseHz`（mainwidget.cpp:47 读、:117-119 写）；组织/应用名只在 GUI 分支设置（main.cpp:150-151）——**CLI 无 QSettings 语境**，命令层本体不读配置（fr24 D2 已立「缺省策略上移给壳」原则）。

**回归面（动命令层信封/命名前必读）：**

- CMD 36 条 + CMD-EQV 薄壳等价 + GUI-EQV 槽级等价（`docs/acceptance/fr24-command-layer/matrix.csv`；run-json.sh 在 ci-smoke 内）——全部**不带 workspace 字段**，本包必须保证 direct 模式（不带 workspace）响应逐字节零漂移，含 CMD-021r 的全文逐字节比对用例。
- ci-smoke（wp-1.2）w1–w7 动词、PAR-SR 三调用点（engine-parity/scripts/run_parity.sh:125-127）——同样全走 direct 模式。
- engine_test DUT 直调引擎不经命令层，零影响。

## 2. 设计决策

### D1 工作区物理布局：集中式根目录 + 内容哈希身份 + 目录式版本 + 双层 JSON 索引

**候选**：
- (a) 集中式工作区根（QSettings 可配，缺省用户主目录下固定名）→ 每源文件一个工作区子目录（`<stem>.<hash8>`，内容哈希身份）→ 每版本一个子目录（`vNNN_<cmd>`，产物在内）+ 工作区内 `workspace.json` 权威索引 + 根级 `index.json` 路径缓存；✅
- (b) 就近模式：工作区放源文件所在目录（`<源名>.vhws/`），产物与源同目录树；
- (c) 无物理约定：用户逐任务选输出目录（现状 QFileDialog 都没有，等于维持散落）。

**取舍与理由**：
- (b) 否决：①「产物散落污染媒体目录」的痛点只解决一半（源目录被塞进隐藏树）；②源目录可能是只读挂载/受保护目录（光盘、相机卡、受控工程目录），就近写直接失败，集中式无此脆弱面；③可移动媒体换盘符后路径身份漂移。就近的「产物好找」优点由「GUI 完成消息展示完整路径 + 历史对话框一键打开目录」（D5）补足。
- (c) 否决：等于不做；且 Agent（v1.x）无从自动化选目录。
- (a) 取胜：布局示意（示意级，实现可微调命名）：

```
<工作区根>/                          # QSettings "workspaceRoot"，缺省 ~/VideoHandler-Workspace（OQ-1）
  index.json                         # 路径缓存（可重建）：[{path,size,mtimeMs,ws}] —— 查找加速层
  vlog_final.a1b2c3d4/               # 工作区 = 源内容身份（stem 净化 + sha256 前 8 hex）
    workspace.json                   # 权威索引：源登记(路径/哈希/规格) + 版本清单 + head
    v001_denoise/
      vlog_final_denoised.wav        # 版本产物命名沿用注册表 outputSuffix（单一来源）
    v002_separate/
      vlog_final_vocals.wav
    v003_pitch/                      # 以 v001 产物为输入 → 记 parentVersion=1（D3）
      vlog_final_denoised_pitch.wav
```

- **内容哈希身份**：FR-A4 原文即「输入哈希」；同内容换路径=同一工作区（版本链不断），同名不同内容=不同工作区。目录名 stem 仅助读，身份在 hash8。
- **目录式版本而非平铺文件名**：一版本一目录给 FR-9 管线（一段多产物/中间产物）与 FR-A3/NFR-A6（Agent 可核验中间产物落位）留了天然容器；平铺文件名在多产物形态下立刻不够用。版本序号 per-workspace 单调递增（`v%03d`），**不做同请求去重**（确定性已由 CMD 确定性用例背书，重跑=新版本：append-only 最诚实，去重省算力是 FR-10/Agent 的优化题，v1 不做）。
- **索引 JSON 而非 SQLite**：与命令层同栈（QJsonDocument 已在用，commandlayer.h:8）、无 Qt Sql 新模块依赖、CI 可用 python3 stdlib 断言（run-json.sh 同款）；规模（每工作区几十条版本）远不到需要数据库。写入用 **QSaveFile**（Qt Core 自带，跨平台原子提交：临时文件+rename，Windows 覆盖语义内建）。
- **根级路径缓存 index.json**：内容寻址的查找代价是全文件 sha256（流式 64KiB 缓冲，RSS 有界，但 2h 素材首查多一遍全读 I/O）。缓存键 = 路径+size+mtimeMs：命中且 stat 一致 → 直接复用工作区目录（免哈希）；miss/不一致 → 全量哈希定身份后回填缓存。缓存**可丢弃可重建**（删了只是下次多算哈希），损坏按空缓存处理不报错。GUI「处理历史」查询同走此路（大文件首查数秒，在 worker 线程跑，D5）。

### D2 版本语义：一次成功任务 = 一个不可变版本；登记与产物两段提交；「可回退」= 历史可寻址可再入，非破坏性回退

**候选（什么触发新版本）**：
- (a) 每次成功产生落盘产物的任务 = 新版本；失败/取消不产生版本（半成品与版本目录全清）；✅
- (b) 同 cmd+参数+输入哈希去重（复用既有版本，跳过计算）；
- (c) 用户显式「保存版本」才算，任务只是跑。

**取 (a)**：(b) 为省算力引入「复用还是复算」的判定负担（且偷懒跳过会让引擎回归不可见——CI 确定性用例的存在意义就是重算应逐字节一致，没必要在产品路径缓存它）；(c) 与「手动+Agent 任务统一走此机制」矛盾（Agent 自动登记是 FR-A3 的自动步骤，不能等人点）。

**不可变性**：版本目录一经登记成功即只读语义（不覆写、不修改记录）；修正/重试 = 追加新版本。这是「可回退」与取证（NFR-A2 精神）的前提。

**登记顺序（两段提交）**：定位/建工作区（读索引）→ 规划版本目录（mkdir，占位）→ 任务执行（产物写进版本目录，底座流式通路一行不动）→ 成功后原子更新 workspace.json（QSaveFile）→ 响应 ok。任一环失败/取消：底座已删产物半成品（fftranscoder.cpp 失败删半成品、ffextractor.cpp:107-119 finish，NFR-R1 既有），工作区层补删版本目录、索引不动（索引只在成功后写）。**索引写失败视为任务失败**（删版本目录 + E_FILE「工作区索引写不了」）：任务成功但无记录对 Agent 是断链，宁可重算——与「失败不留半成品」同构。
**head**：workspace.json 记 `head`（最新成功版本号），仅为展示/缺省定位方便，不参与一致性。

**「可回退」的语义裁定**（候选：仅历史可寻址 vs 可移动 head 指针 vs 恢复到源——第三者违红线直接排除）：**任何历史版本永远在盘、可寻址、可作新输入**（GUI「用此版本继续处理」= 把该产物路径设为输入，产生新版本）；不做「移动 head 回滚」——版本链 append-only 才有完整任务史，「回退=回滚指针」会让历史歧义。GUI 是否提供「用此版本继续」按钮 → OQ-3。

### D3 来源链内容：input 哈希 + cmd + 补缺省参数 + 模型摘要 + 输出规格真值 + 同工作区 parent 链

**候选（记录粒度）**：
- (a) 完整链：版本记录 = {序号, 时间, cmd, **params（validateParams 补缺省后的 filled 副本**）, input{路径, sha256, size}, model{路径, sizeBytes, mtimeMs}（无模型动词为 null）, parentVersion（同工作区内且输入是本工作区某版本产物时）, output{相对路径, outputSpec, frames, durationSec}}；跨工作区链靠哈希衔接（新工作区的源哈希 = 旧版本产物哈希，走哈希即可重建全链）；✅
- (b) 最小链：只记 cmd + 输入路径 + 输出路径（参数/哈希不记）；
- (c) 全量快照：另存请求原文 + 响应原文 + 模型 sha256。

**取 (a)**：(b) 不满足 FR-A4「输入哈希+参数」的明文，且参数缺省不显式化则记录不可复现（humBase 缺省 50 一年后缺省变了，旧记录就错读）；(c) 的模型 sha256 每任务多读一遍大模型文件（几十 MB～上百 MB），收益是「模型内容也指纹化」——v1 以路径+size+mtime 摘要够用（模型内容校验是 FR-8 模型管理器 sha256 校验的职责，REQUIREMENTS.md:93），请求/响应原文则冗余（filled 参数 + OutInfo 已是无损超集）。`filled` 副本是现成的：validateParams 出参（engineregistry.h:50-51），runEngine 内部已产出（commandlayer.cpp:126），顺路传给登记器即可，零重复解析。OutInfo 规格真值同样现成（transcodeResult 已消费，commandlayer.cpp:60-73）——**来源链的全部字段在命令层现有数据流里都已存在，登记器只做组装落盘**。

### D4 命令层接面：执行面 = 请求字段 `workspace`（与 `output` 互斥）；查询面 = 新内置动词 `wsInfo`；probe 拒收；extract 族底座加 outDir 参数

**候选（工作区如何挂上 execute）**：
- (a) 请求字段：信封新增可选 `workspace`（字符串 = 工作区根绝对路径），所有落盘动词（转码族 5 + extract 族 2）生效；带 workspace 时 `outputOr`/底座命名让位于工作区路径规划；不带 = direct 模式，行为逐字节照旧；✅
- (b) 新动词 `wsRun` 包一层（请求体嵌套 cmd）；
- (c) 两者都要（字段 + 包装动词）。

**取 (a)，理由**：
- 这正是 fr24 §5.3 / nfr-m1 §5 两代 spec 留的缝：「工作区包替换 outputOr 即可接管全部动词」——字段方案是缝的最小兑现：动词面零新增（执行面），信封校验/分发/条目全复用；GUI 壳只多插一个键（与片 D 组请求同型）。
- (b) 把 8 个动词复制成 8 个 ws 动词或一层嵌套分发，双通路漂移恰是 fr24 消灭过的形态；(c) 违工作原则 5（两种方式任选）。
- **互斥规则**：`output` 与 `workspace` 同给 → E_PARAMS「output 与 workspace 不能同给」（单一路径决策：工作区模式由命令层定路径，direct 模式由调用方定）；`workspace` 非字符串/空串 → E_PARAMS；`probe` 带 workspace → E_PARAMS「probe 不接受 workspace」（无落盘语义，allowsOutput=false 的既有逻辑同型，commandlayer.cpp:240）。
- **查询动词 `wsInfo`**（进内置表，与 probe/extract 族同型，commandlayer.cpp:185-192）：请求 {root*, input*} → 结果 {工作区目录, 源登记, head, versions[]}；无工作区 → ok:true + 空清单（「查询不落盘」，绝不顺手建目录）。只读查询，Agent/GUI 共用。`wsList`（枚举根下全部工作区）v1 不做——GUI 只按当前文件查，Agent 枚举留缝（§5）。
- **extract 族的落盘接管**（缝的边界，fr24 goal.md:21）：其缺省扩展名依赖源编解码器，只有底座打开输入后知道。**底座 extractAudio/extractVideo 加可选 `outDir` 参数**（缺省空=现状源目录规则，行为不变）：outDir 非空时缺省命名规则原样跑在该目录内——命名规则仍是底座单一来源，不复制到命令层/工作区层（工作区层只给目录，不给文件名）。约 6 行底座改动（ffextractor.h:41-46 签名 + 两处 path 计算 :214,:242）。工作区模式下 extract 写进版本目录，天然新路径不覆写（P3-3 的工作区面自然收口）。
- **版本产物命名**：转码族沿用条目 `outputSuffix`（engineregistry.cpp:19-27 单一来源）拼 `<stem><suffix>.wav` 放进版本目录——文件被单独拷走时仍自描述；extract 族由底座规则命名（同上）。版本目录名 `vNNN_<cmd>` 提供唯一性，文件名不需再编码版本号。

### D5 GUI 集成：七槽缺省走工作区（root 经服务下传，QSettings 只在 GUI 线程读）+ 「处理历史」对话框；无开关

**候选（GUI 形态）**：
- (a) 全自动无 UI：任务静默进工作区，历史无界面（只能去文件管理器翻）；
- (b) 最小 UI：七槽任务缺省组 `workspace` 字段；顶部工作区根展示/可改（QFileDialog + QSettings）；新增「处理历史」按钮 → wsInfo → 对话框列版本（序号/时间/动词/参数摘要/输出路径）+「打开目录」+「用此版本继续」（后者是否进 v1 → OQ-3）；✅
- (c) 完整工作区面板（树形浏览/对比/恢复按钮）。

**取 (b)**：(a) 让「多轮产物难追踪」痛点换个地方复现（工作区里翻不清）；(c) 是 v1.x Agent 面板的形态，v1 无消费者。FR-A4「手动+Agent 统一」要求 GUI 手动任务也走工作区，所以七槽**缺省开启、无开关**（不加「关闭工作区」选项——开关意味着双模式并存的用户心智 + GUI-EQV 断言翻倍；要 direct 产物的用户场景由后续 FR-16 输出格式/位置选择承接，那是产品功能不是开关）。
**root 的线程边界**（QSettings 只在 GUI 线程摸，文档安全模式）：mainwidget 读 QSettings（与 humBaseHz 读法同位，mainwidget.cpp:47）→ `MediaService::setWorkspaceRoot(path)` → 队列信号转发 worker 槽 `FFExtractor::setWorkspaceRoot`（装配期 setCancelFlag 同型注入，ffextractor.h:37）——mediaservice 只加一对信号槽 + 一个成员，七对既有业务信号槽签名零改。worker 槽组请求时插入 `workspace` 字段（runRequest 收尾与文案重建零改动：jobDone 文案格式「降噪完成：<路径>」不变，路径值变为工作区路径，GUI-EQV 断言按路径前缀归一化，§6）。「处理历史」查询走 worker 线程（probeRequested/probeDone 同型新增一对：首查大文件哈希数秒不冻 UI）。

### D6 迁移与共存：旧散落产物不吸收；direct 模式全保留（回归面零漂移）

**候选**：
- (a) 不吸收：源目录里的旧 `x_denoised.wav` 等一律不管（仍是普通文件，可正常作输入）；不带 workspace 的调用（CI/薄壳/显式 output 用户）行为逐字节照旧；✅
- (b) 导入吸收：提供扫描/登记工具把旧产物录进工作区索引（标 legacy 版本）；
- (c) 首次运行自动扫描吸收。

**取 (a)**：(b)/(c) 登记的 legacy 版本没有真实来源链（参数/哈希/所用源状态均不可考），录入的是**假链**——对「取证与可复现」是负资产；且自动扫描用户媒体目录本身就踩「别动我目录」的逆鳞。共存面即回归面：CMD 36 + CMD-EQV + GUI-EQV + ci-smoke + PAR-SR 全走 direct 模式，本包以「不带 workspace 的响应逐字节不变」为硬门禁（CMD-021r 全文比对是现成断言）。旧 7 动词薄壳零改动（不传 workspace）。

### D7 持久化与并发边界：QSettings 只属于 GUI 壳；索引单写者假设；文件锁留缝

- **QSettings 边界**：命令层/工作区模块**不读任何配置**（execute 纯函数原则，commandlayer.h:20-22；fr24 D2「缺省策略上移给壳」同款）：GUI 壳从 QSettings 解析 root 注入请求字段；CLI `--json` 调用方显式传 root；旧薄壳不传。工作区状态全部在请求参数（root）+ 磁盘（索引）里，不在进程内存——Agent 子进程/重启/双壳并发天然同源。
- **并发写**：同一工作区索引 v1 **单写者假设**（GUI busy 锁 mediaservice.h:21 一次一任务；Agent D7 逐步串行；CI 串行）——不做跨进程文件锁（QLockFile 是留缝项，§5：FR-10 批处理多进程形态落地时再上）。文档明示：「同工作区并发提交未定义」。这是声明而非兜底：双进程并发 append 索引会丢版本记录，宁可写进已知限制也不加无人测试的锁。
- **登记时机在 execute 内而非壳**：CLI/GUI/Agent 三壳零重复、机制不靠自觉（核心原则 3）。execute 的「纯函数」不受损——状态从请求进（root）、往磁盘去（索引），无进程级全局态，串行重入性质保持（FR-10 前提不变）。

### D8 分片与验证接线：片 A = 核心+CLI+验收基建，片 B = GUI；run-ws.sh 全绿后进 ci-smoke

与 fr24（片 C 核心/片 D GUI）同型切分：片 A 触命令层信封与底座签名（回归面 = CMD 36 + 薄壳等价 + ci-smoke，direct 模式零漂移为门禁）；片 B 触 GUI 壳与服务装配（回归面 = GUI-EQV 槽级等价复跑 + 新增工作区路径归一化断言）。两片各自独立验收、独立回滚点；片 A 单独交付即兑现 FR-A4 的机制本体（CLI 可全量验证），片 B 只是 GUI 消费面。验证细节见 §6。

## 3. 方案（模块图 + 数据流）

```
 ┌─ GUI 壳 ─────────────────────────┐   ┌─ CLI 壳（零改动）─────────────────────┐
 │ mainwidget（读 QSettings root；    │   │ 旧 7 动词薄壳: 不传 workspace（direct）│
 │  「处理历史」→ wsInfo 异步查询）     │   │ --json: 调用方显式传 workspace 字段     │
 │ mediaservice: setWorkspaceRoot    │   └──────────────┬───────────────────────┘
 │  队列转发 → worker 注入            │                  │
 └────────────┬─────────────────────┘                  │
              ▼ worker 槽组请求（片 D 形态 + workspace 键）│
        ffextractor runRequest（收尾/文案重建零改动）      │
              │ QJsonObject 请求（含 workspace:root）     │
              ▼                                          ▼
      ╔═════════════════════════════════════════════════════╗
      ║ CommandLayer::execute                                 ║
      ║  ① 信封校验（+workspace 字段：类型/与 output 互斥/     ║
      ║     probe 拒收；output==input 防线原样保留）            ║
      ║  ② 双查表分发（引擎表 5 + 内置表 probe/extract×2       ║
      ║     /wsInfo —— 查询动词新增于此）                       ║
      ║  ③ 落盘决策：带 workspace → workspace 模块规划版本路径； ║
      ║     不带 → outputOr（源目录后缀规则，逐字节照旧）         ║
      ╚═════════════════════╤═════════════════════════════════╝
                            ▼
      workspace.{h,cpp}（新，engine 库）：根级缓存查找（stat 命中免哈希）→
      sha256 流式定身份 → 建/定位工作区 → 规划 vNNN 目录 →
      （任务执行：transcodeStreamWav / extract(outDir=版本目录)，底座通路不动）→
      成功后组装版本记录（filled 参数 + OutInfo + 模型摘要 + parent 链）→
      QSaveFile 原子更新 workspace.json；失败/取消 → 删版本目录、索引不动
                            ▼
      响应：result + {workspace:{root,dir,version}}（新增键为纯增量）
```

正例数据流（GUI denoise 首次）：mainwidget 读 QSettings root → 七槽路径组请求 `{cmd:denoise, input, humBase, wind, workspace:root}` → 信封校验 → 条目命中 validateParams（filled）→ workspace 模块：缓存 miss → 流式 sha256 → 建 `vlog_final.a1b2c3d4/` + workspace.json（源登记）→ 规划 `v001_denoise/` → runTranscode 写产物 → 登记版本（params=filled、OutInfo 真值、parent=null）→ jobDone「降噪完成：<工作区路径>」。

失败流（任一环）：底座/引擎 ErrInfo 透传（码在产生点）→ 工作区层补删版本目录 → `{"ok":false,"error":{...}}`，索引无残迹。

查询流（GUI 历史）：wsInfo{root,input} → 缓存/哈希定位工作区 → 读 workspace.json → versions 清单 → 对话框渲染；未登记 → 空清单。

## 4. 改动面（file:line 级预估，基于 HEAD 0c9fc6c）

| 文件 | 片 | 改动 | 预估规模 |
|---|---|---|---|
| **新建** `src/service/workspace.h` | A | 工作区模块接口：身份解析（缓存+哈希）/版本规划/登记/查询（仅 QString/QJson/Qt Core 依赖） | ~60 行 |
| **新建** `src/service/workspace.cpp` | A | 根级缓存读写、QCryptographicHash 流式哈希、目录/索引布局、QSaveFile 原子写、版本记录组装、失败清理 | ~320 行 |
| `src/service/commandlayer.cpp` | A | 信封段加 workspace 校验（:247-256 邻域，类型/互斥/probe 拒收）；runEngine 落盘决策分叉（:148 workspace 模式走模块规划）；成功后登记（filled+OutInfo+model 摘要，:123-149）；cmdWsInfo + builtinVerbs 表一条（:185-192）；extract 路径传 outDir（:157-175） | ~90 行 |
| `src/service/ffextractor.h:41-46` / `.cpp:211-214,239-242` | A | extractAudio/extractVideo 加可选 `outDir` 参数（缺省空=源目录规则不变） | ~10 行 |
| `src/model/data.h` | A | 工作区常量：索引文件名/版本目录前缀/根缺省相对名 | ~4 行 |
| `CMakeLists.txt:14-35` | A | engine 源列表 + `workspace.{h,cpp}` | 2 行 |
| `src/service/mediaservice.{h,cpp}` | B | `setWorkspaceRoot` + 队列信号转发 worker（既有七对信号槽零改） | ~15 行 |
| `src/service/ffextractor.{h,cpp}` | B | `setWorkspaceRoot` 槽 + 成员；六任务槽组请求插入 workspace 键（:289-355）；「处理历史」查询槽 + wsInfoDone 信号 | ~25 行 |
| `src/view/mainwidget.{h,cpp}` | B | QSettings workspaceRoot 读/写 + 根展示/修改控件；「处理历史」按钮 + 版本清单对话框（列表 + 打开目录 [+用此版本继续，OQ-3]）；任务提交前 setWorkspaceRoot | ~150 行 |
| `CLAUDE.md` | A/B | 架构段 workspace 一行 + 红线句「FR-A4 已落地：新功能落盘走工作区」+ CLI `--json` workspace 字段说明 | ~4 行 |
| `docs/acceptance/wp-1.2-linux-build/scripts/ci-smoke.sh` | A | 追加一行调用 `run-ws.sh`（round-1 全绿后） | 1 行 |

**不动的**：五引擎、`streamengine.h`、`fftranscoder.{h,cpp}`（除非 OQ-5 收口 truncated）、`engineregistry.{h,cpp}`（outputSuffix 复用不改）、`errinfo.h`、`main.cpp`（旧薄壳与 --json 壳零改动——workspace 是请求字段，壳无需感知）、全部 parity 脚本、wp-1.0 runbook。

## 5. 留缝清单（本包不做，后续包的接口锚点）

| 缝 | 本包怎么留 | 不动什么 |
|---|---|---|
| **FR-9 管线串联** | 版本目录是一版本多产物的天然容器（目录式布局即为此留）；来源链 parent 字段可扩展为数组（多父） | 不做多产物登记/级间中间产物标记——管线包定义其版本形态 |
| **FR-A10 审计日志** | workspace.json 版本记录（cmd/参数/时间/哈希）就是审计数据源；审计包在其上加全局面（跨工作区汇总） | 不做独立审计通道/诊断包导出 |
| **Agent（v1.x）** | wsInfo 即 Agent 的上下文查询工具；版本目录可落可核验中间产物（NFR-A6）；登记即 FR-A3 的自动「版本登记」步骤（D7 免确认） | 不做 wsList/计划引用 versionId 的协议——Agent 包定义 |
| **FR-10 批处理** | execute 仍纯函数可串行重入；工作区状态全在磁盘，多进程可见 | 不做跨进程文件锁（QLockFile）与队列——批处理包上锁时索引格式不动 |
| **FR-8 模型管理器** | 版本记录 model 摘要（路径+size+mtime）字段可平滑升级为模型 id + sha256（管理器职责） | 不做模型哈希/下载 |
| **NFR-L2 / FR-23** | 与本包无交集（引擎接口/插件） | — |

## 6. 验证方案（目录 `docs/acceptance/fr-a4-workspace/`，脚本风格对齐 run-json/run_parity）

**目录骨架**（实现开工时建）：`goal.md`（总目标 + sub goal matrix SG-1 片 A / SG-2 片 B）+ `plan.md` + `matrix.csv`（用例前缀 `WS-`）+ `round-1.md`（片 A）/`round-2.md`（片 B）+ `scripts/run-ws.sh`（复用 fr24 的 json_check.py 断言器与素材配方，交叉引用不复制）+ 全绿后 `report.md`。

**用例族（WS-*）**：

1. **WS-CREATE（创建）**：首任务自动建工作区：目录结构断言（`<stem>.<hash8>/v001_<cmd>/`）+ workspace.json 源登记（sha256 与外部 sha256sum 对照、size、spec）+ 根级缓存回填；同内容不同路径 → 同一工作区；同名不同内容 → 不同工作区。
2. **WS-CHAIN（追加与链）**：同源第二动词 → v002 追加、v001 不变（不可变性）；以 v001 产物为输入 → 新任务在其工作区记 parentVersion（同工作区）或新工作区源哈希=v001 产物哈希（跨工作区链，D3）；head 更新断言。
3. **WS-PROV（来源链完整性）**：版本记录字段逐项断言：params 为补缺省后的 filled（不传 humBase → 记录含 "50"）、outputSpec/frames 与 result 一致、model 摘要在/不在（有/无模型动词）、时间戳存在；wsInfo 返回与 workspace.json 一致。
4. **WS-RO（原件只读不变式，§7 旗舰验收口径）**：任务前后源文件 sha256 逐字节不变（含 extract/transcode/带模型路径）；`output==input` 仍 E_PARAMS；workspace 模式下 output 字段同给 → E_PARAMS；probe 带 workspace → E_PARAMS。
5. **WS-CANCEL/FAIL（取消与失败）**：取消（GUI 语义经 Ctx 注入不可达 CLI，用 engine_test/代码级证据 + GUI-EQV 承接；CLI 可造失败）：坏模型/无音频流/输出目录只读（chmod 555 工作区根 → 索引写失败路径：任务产物清理、无版本目录、索引无记录、E_FILE）；失败后重跑同任务 → 新 vNNN 正常（残迹不阻后续）。
6. **WS-QUERY（查询面）**：wsInfo 已登记/未登记（空清单不建目录）/参数校验负例（缺 root、root 非字符串、未知字段）。
7. **WS-DIRECT（共存零漂移）**：run-json 36 条全量复跑逐字节比对（direct 模式零变化）+ 薄壳等价 + ci-smoke 全量；`output`+`workspace` 同给负例。
8. **WS-GUI（片 B，槽级）**：gui-slot-eqv 方法论复用 + 路径归一化（工作区根以占位符替换后 diff）：七槽走工作区、jobDone 文案格式不变路径值在工作区、产物落版本目录、历史对话框数据 = wsInfo（探针直调断言）。
9. **WS-MEM（内存有界抽测）**：大素材（≥30min 合成）首任务（含全量哈希）峰值 RSS 与 direct 模式同量级（哈希 64KiB 流式，不破 NFR-P4；用 /usr/bin/time -v 采样对照）。
10. **WS-IDX-ATOMIC（索引原子性）**：杀进程注入难构造 → 以「只读根目录」失败注入 + QSaveFile 语义代码级证据收口（P1-1 同方法论：可构造的 e2e、不可构造的代码级+映射表）。

**门禁接线**：round-1（片 A Linux）全绿后 `run-ws.sh` 进 ci-smoke（run-json 同款一行接线）；Windows 真机随 WP-1.0 round-4 挂账口径（fr24/nfr-m1 同款）。

## 7. 开放问题（主线程拍板，拍板后记入 acceptance goal.md）

- **OQ-1 工作区根缺省位置**：候选 (a) `~/VideoHandler-Workspace`（主目录固定名）；(b) QStandardPaths::MoviesLocation 下子目录（跟随系统「影片」位）；(c) AppDataLocation（ buried，不利于「产物是用户资产」直觉）。**倾向 (a)**：跨平台行为一致、不依赖系统区域配置、CLI 文档好写；(b) 在中文 Windows 常被重定向到非预期盘。影响面：D1 布局、GUI 缺省、文档。
- **OQ-2 GUI 硬切无开关**：候选 (a) 七槽缺省走工作区、无开关（D5 推荐）；(b) QSettings 加「使用工作区」开关（缺省开）。**倾向 (a)**：FR-A4「统一走此机制」的语义就是机制化而非可选；开关带来双模式心智 + GUI-EQV 断言翻倍。影响面：D5、片 B 验收面、既有用户习惯（产物不再出现在源目录——行为变化需在完成消息里路径可见 + 历史对话框补位）。
- **OQ-3 「可回退」的 GUI 形态**：候选 (a) 仅历史可寻址（列表+打开目录）；(b) 加「用此版本继续处理」按钮（把版本产物设为输入，等价 doOpen 该文件——实现极薄）；(c) head 指针可移动的「回滚」语义（D2 已论证否决）。**倾向 (b)**：可回退的产品语义闭环（能找到也能用），成本一个按钮一次 setText；(a) 半闭环。影响面：片 B 规模（+~20 行）。
- **OQ-4 P3-3 direct 模式覆写语义**：候选 (a) 本包只收工作区面（版本目录天然不覆写），direct 模式维持覆写现状（挂账继续）；(b) direct 模式显式 output 一并改「拒覆写」（存在即 E_FILE，需调用方显式删）。**倾向 (a)**：改 (b) 触碰全部薄壳/CMD 语义之外的新行为面，且 direct 模式的存在意义就是「调用方全权指定」；(b) 的安全收益已被工作区模式覆盖（推荐路径）。影响面：D4、CMD 用例是否新增负例。
- **OQ-5 P3-2 OutInfo.truncated 是否随包收口**：候选 (a) 收口：OutInfo 加 truncated 标志（底座 av_read_frame 错误码区分 EOF/IO 错，~10 行）+ 版本记录与 result 如实携带（截断版本可辨识，来源链诚实）；(b) 不收口留底座包。**倾向 (a)**：workspace 是该标志唯一现成消费者，版本记录先立后补字段会有历史版本歧义；构造负例（截断素材）配方 fr24 round-1 已有思路。影响面：fftranscoder.cpp（唯一底座触碰）、WS-PROV 断言。
- **OQ-6 索引并发锁**：候选 (a) v1 单写者假设 + 文档声明 + QLockFile 留缝（D7 推荐）；(b) 现在就上 QLockFile（Qt Core 自带，写侧锁三行）。**倾向 (a)**：当前全部宿主形态（GUI busy 锁/Agent 串行/CI 串行）都单写者，锁无真实并发可测——不可测的锁是假安全；FR-10 批处理包带来真并发时连同其队列语义一起设计。影响面：D7、FR-10 留缝。

### 7.1 主线程拍板（2026-11-03，六项全部追认设计倾向）

- **OQ-1 → (a) `~/VideoHandler-Workspace`**：跨平台一致、不依赖系统区域配置；GUI 首启即展示可改。
- **OQ-2 → (a) 无开关硬切**：FR-A4 是机制不是选项（工作原则：一条清晰路径）；配套两条——完成消息中
  产物路径须可见（行为变化对用户透明），GUI-EQV 断言用「前缀+结构」双段式归一化（风险①落法）。
- **OQ-3 → (b) 加「用此版本继续处理」**：可回退语义闭环（能找到也能用）；实现薄（等价 doOpen）。
- **OQ-4 → (a) 只收工作区面**：direct 模式语义零触碰保 CMD 36 零漂移硬门禁；P3-3 direct 侧挂账继续。
- **OQ-5 → (a) truncated 随包收口**：版本记录先立后补字段会造历史歧义；fftranscoder 为本包唯一底座
  触碰（~10 行），截断负例配方沿 fr24 round-1 思路。
- **OQ-6 → (a) v1 无锁 + 文档声明单写者**：不可测的锁是假安全；FR-10 真并发时连同队列语义一起设计。

## 8. 红线自查与实现风险

**红线自查**：
- **原件只读**：工作区模式落盘全在 `<root>` 下新路径（版本目录唯一性由序号保证，不覆写任何既有文件）；`output==input` 防线原样保留（commandlayer.cpp:255-256）；哈希登记对源只读（QCryptographicHash 流式 addData，64KiB 缓冲）；**本包不改写、不移动、不删除用户源目录任何文件**（迁移不吸收，D6）。
- **流式内存有界（NFR-P4）**：转码/提取通路一行不动；新增内存 = 64KiB 哈希缓冲 + JSON 索引小对象（每工作区 KB 级）；索引含全版本清单，极端长历史也是百 KB 级文档，一次性读写不构成整文件载入语义（WS-MEM 抽测设防）。
- **不入库权重 / third_party**：工作区是运行期用户数据；验收素材 mktemp 临时区生成（run-json 同款配方）；无任何新第三方依赖（QCryptographicHash/QSaveFile 均 Qt Core）。
- **非 GPL**：零新依赖，LGPL 组合零变化。注释与文案中文；GUI 文案格式不变（路径值变化除外）。

**实现风险**：
1. **GUI 产物位置突变是本包最大用户可见行为变化**（源目录 → 工作区）：等价验收方法论（gui-slot-eqv 逐字 diff）必须适配路径归一化，归一化本身写错会漏真差异；Windows 真机验证长期滞后（round-4 pending 模式下 GUI 行为风险累积）。缓解：路径断言「前缀=注入的 root + 结构=版本目录」双段式而非全文归一化一刀切；完成消息完整路径展示让变化对用户显式可见。
2. **两段提交原子性（产物 ↔ 索引）**：失败路径分散（任务失败/取消/索引写失败/进程被杀），任一环漏清理即孤儿目录或无记录产物；「索引写失败删产物」与用户直觉（算完了怎么没了）有张力。缓解：清理收敛到 workspace 模块单点（登记器 owns 版本目录生命周期）；失败注入用例覆盖可构造面（只读根/坏模型/无流），不可构造面（kill -9）以「孤儿目录可被下次任务安全跳过（序号递增不复用）」的设计 invariant 收口并代码级论证。
3. **哈希/查找缓存的性能与正确性**：大文件首任务多一遍全读（2h 素材 HDD 上可达数十秒，GUI 感知为任务前静默段）；缓存键 size+mtime 误命中（mtime 精度/同尺寸篡改）会把不同内容当同身份。缓解：GUI 在任务开始即显示「正在登记源文件…」状态行（progress 前置一段）；缓存命中后仍可在登记时做抽样校验不做（成本）——v1 接受 size+mtime 键的文档化取舍（碰撞后果=版本记错源哈希，可由完整哈希链复核发现），WS-CACHE 用例固化该行为。
