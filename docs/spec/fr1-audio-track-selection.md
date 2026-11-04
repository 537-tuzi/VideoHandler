# FR-1 多音轨选择（音频轨）设计方案

> **设计轮产物，§7.1 主线程拍板位留空。** 基线 commit `1d61dcf`（src 冻结），本稿只写 `docs/spec/` 不改代码。
> 行号均对 `1d61dcf`。主线程实测证据标注「（主线程实测 2026-11-04）」。

## 0. 需求锚点与定位

- `docs/REQUIREMENTS.md:86`：**FR-1 媒体载入与探测——含多音轨视频的轨道选择（现仅取 best stream），需改**。
- M2 离线闭环最后一包：FR-24 命令层 / NFR-M1 注册表 / FR-A4 工作区 / FR-8 模型管理 / FR-9 管线 / FR-10 批处理 / FR-4 质量档均已落地，本包无未就绪前置。
- 术语约定：**track = 容器内音频流的绝对流索引**（`AVStream::index`，与 probe `streams[].index` 同域）。
- 范围：多**音**轨选择。视频轨选择（vtrack）不在本包（见 §5 留缝）。

## 1. 现状盘点（缝在哪里）

| # | 现状 | 位置 | 缝 |
|---|------|------|----|
| 1 | 三处 `av_find_best_stream(fmt, TYPE, -1, -1, nullptr, 0)` 恒取 best stream | `ffextractor.cpp:34`（`open_input`，extractAudio/extractVideo 共用）；`fftranscoder.cpp:36`（`open_decoder`，全部转码族引擎动词共用）；`fftranscoder.cpp:123`（`probeSourceSpec`，`runEngine` 取源规格用） | 调用方无法指定轨 |
| 2 | probe 已枚举全部流，但结果不可寻址 | `ffextractor.cpp:275-309` `probeInfo` 遍历流填 `ProbeStream{index,mediaType,codec,width,height,sampleRate,channels}`（`ffextractor.h:15-29`） | 缺 language/title/default 旗标，Agent/GUI 无法据其决策 |
| 3 | GUI 探测只回文本 | `doProbe`（`ffextractor.cpp:409-413`）→ `probeDone(bool,QString)` → `MainWidget::onProbeDone`（`mainwidget.cpp:299-302`）只渲染 `txtInfo` | 无结构化数据，无法建轨道下拉 |
| 4 | 引擎动词参数校验在注册表 | `engineregistry` 六条目（transcode/denoise/aiDenoise/aiDenoiseHq/pitch/separate），`validateParams` 以整请求拷贝补缺省（`filled`） | 新键若为信封字段会自然流入 `filled` |
| 5 | 管线步/批模板字段白名单 | `validateSteps`（`commandlayer.cpp:281-333`）：步字段 = `{cmd} ∪ isDeclaredParam`，禁收 input/output/workspace/v | track 不在 isDeclaredParam → 步内天然拒收（零代码） |
| 6 | 版本记录 params 来源 | `recordParams`（`commandlayer.cpp:110-115`）从 `filled` 剥 `v/cmd/input/output/workspace/model` | 剥除清单外的键自然留存——信封新键默认进版本记录 |
| 7 | extractAudio 缺省命名跟随选中流编码 | `ffextractor.cpp:363`：`audio_ext(in->streams[idx]->codecpar->codec_id)` | 换轨后扩展名自动跟随，零额外代码 |
| 8 | 旧 7 CLI 薄壳组 JSON 无轨参 | `main.cpp` `runCli` | FR-4 先例：不加薄壳，契约面 = `--json`/GUI |

**关键现状（best-stream 评分序，主线程实测 2026-11-04，本机 avformat、双音轨 aac mkv 直测）**：无 default 旗标且声道持平时选 index 0（首现），采样率不破平；第二轨带 default 旗标则压倒 index 顺序被选中。即缺省语义 = **default 旗标 > 声道数 > 首现**，不等于「音轨 0」。零漂移护栏必须按此设计（缺省路径不得偷换成首轨）。

## 2. 设计决策

### D1 track 是信封字段（与 input/workspace 同级），不是引擎 ParamSpec

**采纳**：`track` 作为信封可选字段，由转码/提取**底座**消费（解码发生在引擎之前，track 决定引擎收到哪条流的音频，引擎自身不感知）。

- 接受面判定：
  - 引擎动词（六条目）**全体接受**——都是音频解码消费者，未来引擎动词（RVC 等）同为音频入，无需逐条目声明。`execute` 信封白名单循环加一行 `entry && key=="track"` 即可，**engineregistry 零改**。
  - 内置动词经 `BuiltinVerb::extraKeys`（既有机制，pipeline 的 `steps` 同型先例）：`extractAudio` 增 `"track"`，`pipeline` 增 `"track"`（顶层）。
  - 其余动词（probe/extractVideo/wsInfo/model 族/batch）不收 → 现有「未知字段：%1」E_PARAMS 自动拦截，正是 NFR-A3 幻觉参数拦截想要的形态。

**反证自查（ParamSpec 路线的错位，为何不采）**：
1. steps 白名单 = `{cmd} ∪ isDeclaredParam`（`commandlayer.cpp:281-333`），ParamSpec 化会让 track **自动入步**——但步 ≥2 的输入是单流 f32 WAV，track 语义无效；而步 1（源视频选轨）恰恰有效。同一字段在链内两处语义相反，schema 表达不了，只能靠文字约定。
2. batch 共享模板对异构文件施加同一绝对索引是错的（件 A 三轨件 B 一轨，track=2 对 B 即越界）。
3. `create` 闭包不消费 track——注册表里躺一条无人消费的声明，违背「注册表 = 引擎参数校验唯一权威」的层边界（track 是底座参数非引擎 DSP 参数；`humBase` 是引擎参数的正面先例，由 `create` 真实消费）。
4. ParamSpec 的 `Kind::Number` 无「非负整数」约束（无 range 时负数放行），表达力不够，还得加 Kind。

**对信封路线的最强反对与回应**：信封字段本应全动词通用或表驱动，track 是动词子集接受，也要新机制。但 extraKeys 已是「动词级信封扩展」同型机制（steps 先例），且引擎动词全体接受使引擎表零改——净机制增量小于 ParamSpec 路线（后者要动六条目 + steps/batch 语义打补丁）。

### D2 寻址 = 绝对流索引；语言只作辅助渲染不作寻址键

**采纳**：`track` 为非负整数，语义 = probe `streams[].index` 的绝对流索引。

- 调用闭环自描述：`probe` → 读 `streams[]` → 取目标 `.index` → 作为 `track` 传回。同域无换算层；mkv 音轨 index 非连续（视频 0、音轨 1、字幕 2、音轨 3）天然正确。
- **ProbeStream 增补三字段**（供 GUI 下拉渲染与 Agent 决策）：`language`（`AVStream` metadata `language`）、`title`（metadata `title`）、`defaultTrack`（`disposition & AV_DISPOSITION_DEFAULT`）。probe JSON **条件插键**：值空/假不插——老素材（无 language/title 元数据）probe 响应**逐字节零漂移**；主线程代查 fr24 `run-json.sh` CMD-001 对 probe 结果用 contains/numrange 语义不锁键集，增键不假红。
- 反证（音轨序数寻址）：第二套编号，与 probe 输出不同域需换算层；且「第 1 条音轨」≠ 缺省 best-stream（评分序 default 旗标 > 声道数 > 首现，主线程实测）——序数 1 与缺省语义错开半步，更易混。
- 反证（语言寻址）：可空、可重复（双英语评述轨）、无语言轨存在——无法枚举校验，只进下拉标签。

### D3 动词覆盖面

| 动词 | track | 依据 |
|------|-------|------|
| transcode / denoise / aiDenoise / aiDenoiseHq / pitch / separate（六引擎动词） | **收** | 全部解码输入音频；信封一行接受 |
| extractAudio | **收** | 抽指定轨是本需求最直接形态；缺省扩展名已跟随选中流编码（§1-7） |
| pipeline **顶层** | **收，仅作用于步 1 的源解码** | GUI 预设在已载入文件上执行——若 pipeline 拒收 track，用户在下拉选轨 2 后一键执行会**静默处理缺省轨**（错轨跑完才见），功能面必接 |
| pipeline steps（步内） | **拒**（零代码） | track 非 isDeclaredParam，`validateSteps` 现白名单即报「未知字段：track」；且步 ≥2 输入是单流 WAV |
| batch 顶层 / 模板步 | **拒** | 绝对索引是文件级属性，共享模板跨异构文件无意义；件级 track 需 `items[{input,track}]` 形态 = schema 扩展，留缝（§5） |
| probe | **拒** | 枚举者自身，轨道列表就是它的输出 |
| extractVideo | **拒** | 视频轨选择（vtrack）是另一特征，留缝 |
| wsInfo / model 族 | **拒** | 只读查询/共享资产，与源解码无关 |
| 旧 7 CLI 薄壳 | **零触碰** | FR-4 同型拍板：不加 `--track`，契约面 = `--json`/GUI |

### D4 校验与错误面（schema 层 + 运行期层，TOCTOU 自然报错不防）

- **信封 schema 校验**（`execute` 公共段，对全部接受动词统一）：`track` 可选；必须是整数（Number 且无小数部）→ 违者 `E_PARAMS「track 必须是整数」`；负数 → `E_PARAMS「track 不能为负」`。
- **运行期校验**（须开容器后才能判，发生在选轨点）：越界 → `E_FILE「track %1 超出流范围（共 %2 条流）」`；指向非音频流（视频/字幕/封面）→ `E_FILE「track %1 不是音频流」`。E_FILE 归属依据：与「文件里没有音频流」同域（open 点、文件结构事实）；对立面（Agent 改参重试语义主张 E_PARAMS）列 **OQ-1** 不盲拍。无音频输入带 track：开容器即拒（先于「文件里没有音频流」）。
- **fail-fast 通道（本设计的关键安全点）**：`probeSourceSpec` 增 track 参与硬失败出口。现状它探测失败**静默回落兜底规格**、不拦任务（P0 #4 设计）；track 给定而不可解析时若仍静默回落 = **静默错轨处理**，比报错更糟，必须硬失败。且该失败必须发生在 `create`（模型加载，秒级）与 `planVersion`（全量哈希，大文件分钟级）**之前**——`runEngine` 在 staticCheck 后的 `probeSourceSpec` 调用点（`commandlayer.cpp:210`）恰在两者之前，借道即可。
- **纵深**：`open_decoder`/`open_input` 在解码点重验 track（两次开容器间文件可能被换——TOCTOU 不防，第二次失败照常走既有错误路径）。

### D5 GUI 面

- **载入流程**：`openFile` → `media->probe` → worker **单遍探测双发射**：`probeDone(ok, probeText)`（**逐字零漂移**）+ 新增 `probeInfoDone(ok, probe result 的 Compact JSON)`（`wsInfoDone` 同型只读信号）→ `MainWidget::onProbeInfoDone` 建轨道下拉。失败时 probeInfoDone 发 false + 空对象（下拉不建/清空，不留旧文件轨）。
- **轨道下拉 `cmbTrack`**：
  - 缺省项「**自动（默认音轨）**」= 组包时**完全不带 track 键**（best-stream 语义零漂移，**不得**偷换成「音轨 0」）；itemData = -1。
  - 音频流 ≥2 才显示（单轨文件隐藏 = 单轨 UI 零漂移；对立面恒显示，**OQ-2**）。
  - 条目标签从 ProbeStream 渲染：「流#%1 · 语言 · 编解码 · 采样率Hz · 声道ch」（language/title/defaultTrack 有则显示）。
  - **不进 QSettings**：track 是文件级属性非用户偏好（`aiDenoiseTier` 是偏好先例，此处反向）；换文件/重载即重置「自动」（`openFile` 必做，防跨文件 stale 索引）。
- **组包接入 = typed 参数**：六个任务槽 + pipeline 槽签名尾部加 `int track = -1`（`doExtractAudio(inPath, track=-1)`、`doDenoiseWav(inPath, humBase, wind, track=-1)`…），GUI 侧 `trackValue()` 单源取值（`aiVerb()` FR-4 单源先例）随请求传入；worker 槽内 `track>=0` 才 `req.insert("track")`。**不取** setTrack 成员注入：有状态（跨文件 stale）、`runRequest` 单点注入会误伤 doExtractVideo（拒收 track 的动词）；「参数归参数」。
- **probe JSON 单源**：`cmdProbe` 的 result 组装搬为 `FFExtractor::probeJson(const ProbeInfo&)` 静态共享（命令层与 doProbe 共用，防双源漂移）；`probeText` 不动。

### D6 工作区与版本记录（来源链可复现）

- `recordParams` 剥除清单**不加 track** → 引擎动词的 `filled`（整请求拷贝+缺省）自然留存 track：**同文件不同轨 = 不同 params**，版本记录可复现；无 track 任务 params 逐字节零漂移（键缺席）。`workspace` 核心零改。
- pipeline：顶层 track 注入步 1 子请求（`cmdPipeline` 子请求组装处仅 index==0 插键）→ 步 1 版本记录 params 含 track。
- extractAudio 工作区模式现状 params 为空对象（「extract 无引擎参数」）→ 带 track 时 params=`{"track":N}`（口径一致）；无 track 仍空对象零漂移。

### D7 底座改动形态（缺省分支零漂移的实现纪律）

- 三处选轨点统一 `resolveAudioStream(AVFormatContext*, int track, ErrInfo*)` 小函数：**track<0 → `av_find_best_stream` 原式（现状逐字节）**；track≥0 → 校验+直取索引。
- `probeSourceSpec` / `transcodeStreamWav` / `open_input` 签名尾部加 `int track = -1`（+ `ErrInfo*` 出参，仅 probeSourceSpec）：既有调用方与 engine_test 零改。
- `probeSourceSpec` 新语义：返回 false **仅当** track≥0 且不可解析（含容器打不开）；track<0 = 现状（探测失败静默回落兜底、不报错）。

### D8 验证与 CI 接线

TRK 族（`docs/acceptance/fr1-audio-track-selection/`：matrix.csv + round-N.md + scripts/run-trk.sh + gui 探针）随 ci-smoke 接线（run-json 同型）；Windows 真机随 round-4 挂账（口径同 FR-4/FR-9/FR-10）。详见 §6。

## 3. 方案（关键数据流与签名）

```
--json / GUI 请求 {v, cmd, input[, output][, workspace][, track], ...引擎参数}
  │
  ├─ execute 信封段（commandlayer.cpp）
  │    白名单：引擎动词收 track（一行）/ extractAudio、pipeline 经 extraKeys / 其余「未知字段：track」E_PARAMS
  │    类型：整数且 ≥0，否则 E_PARAMS
  │
  ├─ runEngine：staticCheck → probeSourceSpec(input, track, &rate, &ch, &err)
  │    track≥0 且不可解析 → 立即 E_FILE 响应（先于 create 模型加载与 planVersion 全量哈希）★fail-fast
  │    → create → direct / runInWorkspace（recordParams 不剥 track → 版本记录 params.track）
  │    → runTranscode → transcodeStreamWav(..., track) → open_decoder → resolveAudioStream
  │
  ├─ cmdPipeline：顶层 track 仅注入步 1 子请求（index==0）；步内 track 由 validateSteps 现白名单拒收
  ├─ cmdExtractAudio → extractVerb：透传 track；workspace 模式 params 带 track
  └─ GUI：openFile → probe → doProbe 双发射（probeDone 文本零漂移 + probeInfoDone JSON）
         → onProbeInfoDone 建 cmbTrack（≥2 音轨才显示，缺省「自动」=不带键）
         → 七槽/管线组包 trackValue() 单源注入
```

关键签名（实现轮基线，可在等价范围内调整）：

```cpp
// fftranscoder.h —— 既有调用方零改（缺省参）
static bool probeSourceSpec(const QString& inPath, int track = -1,
                            int* outRate = nullptr, int* outChannels = nullptr,
                            ErrInfo* err = nullptr);   // false 仅当 track≥0 且不可解析
static bool transcodeStreamWav(/*既有参*/, int track = -1, ...);
// fftranscoder.cpp / ffextractor.cpp 内部
static int resolveAudioStream(AVFormatContext* fmt, int track, ErrInfo* err);
// ffextractor.h
static QJsonObject probeJson(const ProbeInfo&);        // cmdProbe 搬家共享（单源）
// ProbeStream 增：QString language; QString title; bool defaultTrack = false;
```

## 4. 改动面清单（文件级）

| 文件 | 改动 |
|------|------|
| `src/service/fftranscoder.h/.cpp` | `resolveAudioStream` 新增；`open_decoder`/`transcodeStreamWav`/`probeSourceSpec` 加 track（+err 出参）；track<0 分支逐字节现状 |
| `src/service/ffextractor.h/.cpp` | `ProbeStream` 增 language/title/defaultTrack；`probeInfo` 填充（metadata/disposition）；`probeJson` 新增（cmdProbe 组装搬入）；`probe`/`probeText` 不动；`open_input`/`extractAudio` 加 track；`doProbe` 双发射 + 新信号 `probeInfoDone`；六个任务槽 + `doPipeline` 加 `int track = -1` 尾参（`track>=0` 才组键） |
| `src/service/commandlayer.cpp` | `execute` 信封：track 白名单（引擎动词一行 + extraKeys）与整数/非负校验；`runEngine` 透传 track + probeSourceSpec 硬失败短路；`cmdPipeline` 顶层 track 仅注入步 1；`cmdExtractAudio` 透传 + workspace params 带 track；`BuiltinVerb` 表 extractAudio/pipeline 的 extraKeys 增 `"track"`；`recordParams` **不改** |
| `src/service/mediaservice.h/.cpp` | 六对业务信号槽 + pipeline 槽加 `int track = -1` 尾参；`probeInfoDone→probeInfoFinished` 直通转发（wsInfoDone 同型，`mediaservice.cpp:50` 先例） |
| `src/view/mainwidget.h/.cpp` | `cmbTrack`（onProbeInfoDone 建、openFile 重置「自动」、≥2 音轨才显示）；`trackValue()` 单源；六个任务调用点 + `doPipeline` 组包传 track |
| `src/model/data.h` | 零改（不加 QSettings 键） |
| `docs/spec/fr24-command-layer.md` | 动词表补 track 列（六引擎动词/extractAudio/pipeline 顶层收，其余拒）与 probe streams[] 条件增键说明 |
| `CLAUDE.md` / `README.md` | 实现轮落码时同步（FR-1 条目 + CLI 用法不变声明） |

## 5. 留缝（明确不做与理由）

- **vtrack（extractVideo 视频轨选择）**：FR-1 锚点是音频轨；对称特征需求未立项，另开包。
- **batch 件级 track**（`items[{input,track}]`）：FR-10 §7.1 拍板模板=共享、inputs 为字符串数组；本包不破该 schema，需求出现再扩。
- **GUI 批处理对话框逐件选轨**：同上。
- **语言寻址键**（`track:"jpn"`）：歧义不可枚举校验（D2 反证）。
- **probeText 增语言渲染**：文本面逐字零漂移红线；信息由下拉承载。
- **track 回显进 result**：sourceSpec（跟随源动词）已可观测实际解码规格，版本记录 params 承载 provenance——不加键（OQ-3 备案）。

## 6. 验证方案（TRK 族；结构参照 fr4 §6）

素材合成（`run-trk.sh` 内配方，**先造 wav 再 mux**——`-ac 2` 夹在两个 `-i` 之间会落到下一输入，位序陷阱，主线程实测；语言标签用 `-metadata:s:a:N language=xxx`）：

```bash
# 各轨规格刻意错开（44.1k/2ch、48k/1ch、32k/2ch）使选轨错配必炸、输出 sourceSpec 可观测
ffmpeg -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=5" -ac 2 a1.wav
ffmpeg -f lavfi -i "sine=frequency=880:sample_rate=48000:duration=4" -ac 1 a2.wav
ffmpeg -f lavfi -i "sine=frequency=1320:sample_rate=32000:duration=3" -ac 2 a3.wav
ffmpeg -f lavfi -i testsrc2=... -i a1.wav -i a2.wav -i a3.wav -map 0:v -map 1:a -map 2:a -map 3:a \
       -c:v libx264 -c:a:0 aac -c:a:1 mp2 -c:a:2 ac3 \
       -metadata:s:a:0 language=eng -metadata:s:a:1 language=jpn -metadata:s:a:2 language=chi \
       -disposition:a:1 default multi.mkv        # 轨 index 3 带 default 旗标（压倒 index 序）
ffmpeg ... sparse.mkv   # 视频 0 + 音轨 1 + 字幕 2 + 音轨 3（index 非连续）
# cover.mkv（attached_pic 封面流）、单轨 tiny.mp4（复用 ci-smoke）、无音频文件、纯音频 st.wav
```

| 用例 | 覆盖 | 断言要点 |
|------|------|----------|
| TRK-001 | probe 增键 | multi.mkv streams[] 条件插键 language/title/defaultTrack；tiny.mp4 probe 响应与基线**逐字节一致**（零漂移门） |
| TRK-002..007 | 六引擎动词 track=k | 输出 sourceSpec 跟随所选轨（素材规格错开可观测）；sparse.mkv track=3（非连续 index）正确选中 |
| TRK-008 | 内部一致性金标准 | `extractAudio(track=k)` 得流副本 → `transcode(副本)` vs `transcode(源, track=k)` 输出**逐字节一致**（同一解码器+同一 swr 链）；extractAudio(track=3) 产物扩展名 = 该轨编码（ac3） |
| TRK-009 | 缺省零漂移 | 不带 track 的六动词+extractAudio 对 multi.mkv 输出与基线逐字节一致；**default 旗标轨被缺省选中**（评分序 default>声道>首现，主线程实测锚定）——断言 sourceSpec=48k/1ch（default 轨）而非 44.1k/2ch（首轨） |
| TRK-010 | 错误面 | track="1"（字符串）/1.5/负数 → E_PARAMS；track=99 越界、track=0 指向视频流 → E_FILE；probe/extractVideo/wsInfo/modelList/batch 顶层带 track → 「未知字段：track」E_PARAMS；st.wav track=1 越界 E_FILE |
| TRK-011 | pipeline | 顶层 track=3 两步链 → 步 1 版本记录 params.track=3、产物从轨 3 起点；步内 track → 预校验拒（零执行零产物）；track=99 → 步 1 前 fail-fast、无版本目录 |
| TRK-012 | fail-fast 时序 | multi.mkv + track=99 + aiDenoise：错误码 E_FILE 且**无版本目录、无模型加载副作用**（响应在 staticCheck 后立即返回） |
| TRK-013 | 工作区记录 | 带 track 任务 params 含 track；不带任务 workspace.json 与基线逐字节一致 |
| TRK-GUI-001 | 探针（gui-slot-eqv 同型） | multi.mkv 载入 → 下拉 4 项（自动+3 轨）缺省「自动」；tiny.mp4 载入 → 下拉隐藏；选轨后点七槽 → 捕获请求 JSON 含 track:3；「自动」→ 请求无 track 键；换文件重置回「自动」 |
| TRK-GUI-002 | GUI 文本零漂移 | probeDone/probeText 与 jobDone 文案逐字一致（gui-ws/gui-pipe 既有断言不假红） |

归档：`docs/acceptance/fr1-audio-track-selection/{goal.md, matrix.csv, round-N.md, scripts/}`；TRK/TRK-GUI 随 ci-smoke 接线；Windows 真机随 round-4 挂账。

## 7. 开放问题（设计轮不拍板，主线程定夺）

| OQ | 问题 | 建议 |
|----|------|------|
| OQ-1 | track 越界/非音频的错误码：E_FILE（推荐，与「文件里没有音频流」同域、产生点在容器打开后）vs E_PARAMS（Agent 改参重试语义、与 NFR-A3「幻觉参数拦截」口径一致） | E_FILE，但接受 E_PARAMS 论点；影响 CLI/GUI 文案与 Agent 重试策略 |
| OQ-2 | GUI 单轨文件下拉：隐藏（推荐，单轨 UI 零漂移）vs 恒显示「自动」禁用态 | 隐藏 |
| OQ-3 | track 是否回显进动词 result（如 result.track） | 不回显（sourceSpec + 版本记录 params 已可观测） |
| OQ-4 | extractAudio direct 模式同文件多轨重复提取会**同名覆盖**（缺省命名 = 文件名+流扩展名，不含轨号；现状同文件二次提取也覆盖，非本包新引入）。是否在缺省名加轨标记（如 `.a3.`）？ | 不加（缺省命名单一来源零漂移；工作区模式天然分版本目录不撞） |

## 7.1 主线程拍板（2026-11-04，设计轮收口）

交叉对照 = spec 作者（本稿 D1–D8）+ 对抗约束枚举（六组 26 条）+ 主线程锚点核实（四处 commandlayer
机制 + 三处选轨点 + 本机双轨实测）。**两报告在 D1 承载面上正面冲突，主线程核锚后裁决如下。**

### 拍板 A：D1 维持信封字段路线（否决对抗报告的 ParamSpec 主张）

对抗主张 ParamSpec（isDeclaredParam 自动进信封白名单与 steps 白名单、defStr=nullptr 防补键污染）。
主线程逐点核锚后不采，理由（锚点均已亲手核实）：

1. 信封循环已有 `entry &&` 守卫先例（`commandlayer.cpp:698`）——track 加一行引擎动词专属放行，
   probe/wsInfo/modelList 照样「未知字段：track」拒收。对抗 3.1「信封特检放行面失控」不成立。
2. recordParams 剥除清单（`commandlayer.cpp:110-115`）不含 track → 有则自然入 params、无则键缺席，
   零漂移由构造保证——完全绕开 ParamSpec 的 defStr 补键机制（对抗 3.2 的四条精确键集假红风险
   WS-016/WS-021/PIPE-CHAIN-005/BATCH-WS-004 从根上不存在）。
3. validateSteps 步字段 = {cmd} ∪ isDeclaredParam（`commandlayer.cpp:311-318`）——信封路线下步内
   **自动拒收**（零代码）；ParamSpec 路线则步 ≥2（单流 WAV 输入）track 语义悬空、batch 共享模板
   对异构文件施加同一绝对索引（对抗 3.5 自认的裂缝）。拒收优于语义悬空。
4. 整数校验（对抗 3.3）：信封段一处统一（六动词一致、文案在产生点诞生，NFR-M1 片 B 口径）；
   无需 semanticCheck 钩子或新 Kind。
5. 语义归位：track 是**输入解码属性**（与 input 同族），非引擎 DSP 参数（humBase 是对照先例）。
   未来引擎动词（RVC）自动获得 track，无需逐条目登记。

extraKeys 落点核实：`BuiltinVerb` 表（`commandlayer.cpp:628/:636`）extractAudio 加 `"track"`、
pipeline 加 `"track"`（与 `"steps"` 并列）。

### 拍板 B：OQ-1..OQ-4 裁决

| OQ | 裁决 | 理由 |
|----|------|------|
| OQ-1 | **E_FILE**（越界「track %1 超出流范围（共 %2 条流）」/ 非音频「track %1 不是音频流」） | 容器打开后才可判的文件结构事实，与「文件里没有音频流」（`fftranscoder.cpp:39`）同域同类；schema 层（类型/负数）仍 E_PARAMS。Agent 改参重试可先 probe 枚举后选轨，不依赖 E_PARAMS 语义 |
| OQ-2 | **隐藏**（音频流 <2 不显示下拉） | 单轨文件 UI 零漂移；恒显示禁用态徒增视觉噪音 |
| OQ-3 | **不回显** result | sourceSpec（跟随源动词）+ 版本记录 params.track 已可观测；加键破 CMD-002/BATCH 精确键集断言（对抗 5.3 六处清单） |
| OQ-4 | **翻转 §7 建议列，采纳对抗 4.6：显式 track 时缺省名加轨标记** | 现状同文件二次提取覆盖的是**同一** best-stream 产物（无损失）；本包使「轨 0 → 轨 1」不同产物落同名静默覆盖成为可能 = **新引入的数据丢失面**（avio_open 截断覆盖）。裁决：track≥0 时缺省名插 `_t<N>` 段（实现于底座命名单一来源处 `ffextractor.cpp:359-363`，规则仍单源）；无 track 命名逐字节不变（WS-020/021 保）。命名格式实现轮定，三点锁定：显式加标记/缺省零漂移/底座单源 |

### 拍板 C：对抗报告其余承重条目的归置

- **4.1 三处选轨点同轨**（两报告 TOP1 重叠，主线程设计轮预判亦同）：采纳 spec D7 `resolveAudioStream`
  统一助手 + TRK-002..007 素材规格错开（44.1k/2、48k/1、32k/2）使错配必炸。DSP 陷波系数按错误采样率
  计算 → 50Hz 实际落 ~54.5Hz 静默劣化（对抗给出量化机理）——fail-fast 时序（TRK-012）为第二道护栏。
- **素材 default 旗标实证**（对抗补测：mkv 封装器自动给首条音轨打 AV_DISPOSITION_DEFAULT）：
  「无 default 旗标」素材必须显式 `-disposition:a:0 0` 清旗制造；multi.mkv 的 default 旗标设计在
  **后轨**（非首轨）才能区分「DEFAULT 优先」与「首现」两条规则。跨版本（Windows FFmpeg 9 vs
  Linux 8.0.1）spec 只承诺「调同一函数 av_find_best_stream」，不复述评分算法。
- **GUI**：下拉挂 topRow（文件属性语义，对抗 2.2）；`openFile` **同步**重置「自动」（不等异步
  probeInfoDone，防旧文件显式轨静默套到新文件——比 spec D5 的「换文件重置」更强时序要求）；
  busy 期不锁（面板动态态族，对抗 2.4）；「自动」档不预标将选哪轨（对抗 1.2，不自估 best-stream）。
- **worker 槽签名**：新参一律尾加带缺省（FR-4 先例）——四个冻结探针脚本 g++ 直调全参形态
  （对抗 5.4），插参/重排即编译炸。
- **batch 拒 track 两面**（CLI 模板 + GUI 批对话框）显式拍板：件级 `items[{input,track}]` 留缝（§5）。
- **probeInputSpec 首轨规格登记既有瑕疵**（`workspace.cpp:155-168`，对抗顺带观察）：不动不恶化
  （工作原则 7）；FR-1 的 provenance 由 params.track 承载。记档备查。
- **exotic codec 轨**（mp4 内 PCM 等）：流复制失败走既有「写文件头失败」E_FILE 路径，负例验收
  归 TRK-010 扩展位（实现轮酌情，非门禁）。

### 偏离记档

本节拍板 B 之 OQ-4 与 §7 建议列相反（理由如上）；其余裁决均与建议列一致或补强。spec 正文（§1–§6、
§8）为设计轮快照不再回改，以本节为准。

## 8. 红线自查与实现风险

**红线自查**：原件只读 ✓（track 只影响解码选择不改源）；流式/内存有界 ✓（零缓冲变化）；旧 7 薄壳零触碰 ✓（D3）；LGPL ✓（纯 FFmpeg/Qt 既有 API）；不引入 GPL ✓；QSettings 零新键 ✓；probeText/jobDone 文案零漂移 ✓（双发射拆分而非改文案）。

**TOP3 风险**：
1. **两次开容器必须解析到同一轨**：`runEngine` 的 `probeSourceSpec`（定源规格）与 `open_decoder`（实际解码）是两次独立打开。若只改后者，跟随源引擎（transcode/denoise/pitch）会按**缺省轨** setup 规格、按**指定轨**解码 → 声道/采样率错配（引擎 setup 断言炸或静默错配）。护栏：TRK-002..007 素材规格错开让错配必炸；实现纪律 = 两点同走 `resolveAudioStream`。
2. **pipeline track 注入点过宽**：若实现成「整链透传」，步 2 输入是单流 WAV，track=3 → 中段 E_FILE（链跑一半才死）。必须只在步 1 子请求组装处插键。护栏：TRK-011 步内拒收 + 两步链带 track 全链跑通双向断言。
3. **GUI stale 轨道状态**：`openFile` 即解禁按钮，而 probeInfoDone 异步晚到（用户极早期点任务 → 下拉未建 → 取「自动」，无害）；真正的坑是**换文件未重置**——旧文件 track=3 对新单轨文件即越界 E_FILE（自然报错不防，但 UX 差且像 bug）。护栏：openFile 重置为必做项 + TRK-GUI-001 换文件断言。
