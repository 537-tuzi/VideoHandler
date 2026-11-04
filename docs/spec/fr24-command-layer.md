# FR-24 无头命令层：JSON 进 / JSON 出（开工 spec）

- 日期：2026-11-03
- 性质：开工前快照（M2 第一个工作包；进展追踪见 `docs/acceptance/fr24-command-layer/goal.md`）
- 本文 D 编号（D1–D9）仅限本 spec，与 REQUIREMENTS.md 决策记录（D1–D7）无关。

## 0. 需求锚点

| 锚点 | 内容 | 对本包的约束 |
|---|---|---|
| FR-24（REQUIREMENTS.md:100） | 每个引擎/管线 JSON 进 JSON 出；**GUI 全部操作改走此层**；同时是 CI 测试面 | 命令层核心必须与传输解耦（GUI 不经 argv）；本包就要迁 GUI，不是只做 CLI |
| 架构基线（REQUIREMENTS.md:56-77） | GUI 与 Agent 是同一命令层的两个客户端，能力对等 | 核心是纯函数式调用，CLI/GUI 只是两种壳 |
| D5（REQUIREMENTS.md:18） | v1 只做铺垫（工作区 + 无头命令层），Agent 本体 v1.x | 不做意图路由/计划生成，只做确定性命令执行 |
| NFR-M1（REQUIREMENTS.md:185） | 引擎注册表是 **FR-24 的实现载体**，但是**下一个工作包** | 本包只留缝（§5.1），不实现注册表 |
| v1-development-plan.md:37-39 | M2 依赖序：FR-24 → NFR-M1 → FR-A4 → FR-8 → FR-9 → FR-10 → FR-4 → FR-1 | 接口要支撑后续包（引擎链/输出路径管理/任务队列），但只留缝不实现 |
| v1-development-plan.md:13 | M2 DoD 含「GUI 全操作走命令层」 | GUI 迁移是本包验收项（D6） |
| NFR-Q3 / P0 #4（已落地，基线 4657ccd） | transcode/denoise/pitch 输出跟随源；ai/separate 锁兜底规格 | 结果对象必须把输出规格**可观测**（D4） |
| FR-3 / HumBase（P0 #5） | hum 陷波 50/60Hz 参数化已全链贯通 | 命令层参数必须贯通制式枚举（D2） |
| FR-12 取消 / NFR-R1 | 全任务可取消、失败删半成品 | 取消语义透传到命令层核心（D2 上下文） |
| 红线 | 原件只读、流式内存有界（NFR-P4）、不引新依赖、LGPL 组合不动 | §8 红线自查 |

## 1. 现状盘点（证据）

- CLI 入口：`src/main.cpp:18-128` `run_cli`——7 个动词 + `argc>=3` 分支（`main.cpp:130-134`）；`main.cpp:120` else 分支把**任何未知动词当 extractAudio**（宽松陷阱）；argv 经 `QString::fromLocal8Bit`（Windows 本地码页，非 UTF-8）。
- 动词缺省策略：`--denoise-wav` 缺省 50Hz + wind=true（`main.cpp:40,53`）；`--ai-denoise`/`--sep-*` 模型缺省 `third_party/models/...`（相对 CWD，`main.cpp:61-62,103-104`）。
- 输出规格策略分叉（本包必须保持并可观测）：transcode/denoise/pitch 跟随源（`main.cpp:30-33,50-54,88-96`）；ai/separate 显式传兜底 44.1k/2ch（`main.cpp:74-75,117-118`、`ffextractor.cpp:262,291-293`）。
- GUI 路径：`mainwidget → mediaservice（busy 锁/取消原子量/QThread 调度，mediaservice.cpp:9-42）→ ffextractor worker 槽`（`ffextractor.cpp:216-308`）；`runJob` 是线程壳（兜底 catch + 进度转发 + 输出命名，`ffextractor.cpp:222-239`）；GUI 模型缺省走 `applicationDirPath()` 相对（`ffextractor.cpp:250,278`，与 CLI 缺省不同源）。
- 转码底座：`fftranscoder.h:16-21`——`probeSourceSpec` + `transcodeStreamWav`（输出规格参数已具备，P0 #4），**但只返回路径，实际生效规格/帧数不外传**（可观测缺口）。
- 探测：`ffextractor.cpp:130-162` `probe` 返回中文拼接文本（GUI 直接展示），无结构化形式。
- 引擎契约：`streamengine.h:7`「process/flush 不得抛出，失败归口 lastError()」——命令层只需处理「返回空路径 + err 文本」的统一失败形。
- 回归面（改 CLI 前必读，全部依赖动词原样）：
  - `docs/acceptance/wp-1.2-linux-build/scripts/ci-smoke.sh:54-61`（w1–w7 动词 + 退出码）与 `:63-78`（ffprobe 体检断言输出规格）；
  - `docs/acceptance/engine-parity/scripts/run_parity.sh:125-127`（PAR-SR 三调用点，`>/dev/null 2>&1` 只看退出码）；
  - `docs/acceptance/wp-1.0-hotfix/round-4-windows.md:28-34,44,58`（Windows 真机 runbook：冒烟 + 错误注入 + RSS 测量）。
  - PAR-DS 族走 `engine_test` DUT（`run_parity.sh:44-61`），不经 CLI，不受影响。

## 2. 设计决策

### D1 传输通道：`--json` 读 stdin，stdout 恒回一个 JSON 对象

**候选**：
- (a) argv 内联随动词（`--denoise-json '<payload>'`）；
- (b) `--json` 子命令 + payload 作 argv 参数；
- (c) `--json` 从 **stdin** 读请求字节，stdout 写单个 JSON 响应文档。✅

**取舍与理由**：
- argv 两案共同死穴在主平台 Windows：命令行 32K 长度上限、cmd/PowerShell 引号转义地狱、`QString::fromLocal8Bit` 码页转换会破坏 JSON 的 UTF-8 契约（现动词已有此隐患，`main.cpp:19-20`）；且未来 FR-9 管线/FR-10 批处理的请求体只会更大。stdin 字节流 → `QJsonDocument::fromJson`（UTF-8 直解）彻底绕开三坑。
- (c) 对 CI 最顺手：`video_handler --json <<'EOF' ... EOF` 或 `echo '<json>' | video_handler --json`，bash 原生能力，零引号摩擦（工作原则 4）。
- 反例权衡：argv 内联对「人手敲一条命令」更直观——但本层的主要客户是 CI 与 GUI（进程内直调核心），交互式手敲是低频场景，不值得为主平台引入编码风险；且只保留一条通道（工作原则 5），不搞「argv+stdin 双入口任选」。
- **GUI 复用方式与传输无关**：核心是 `CommandLayer::execute(QJsonObject, Ctx) -> QJsonObject` 纯函数（D5），CLI 壳只做「stdin→parse→execute→stdout」三步胶水。

**输出形态**：候选「stdout 单对象」vs「NDJSON 事件流（进度+终态）」。取**单对象**：
- v1 的 stdout 消费者只有 CI，单文档 `python3 json.load` 即断言，无「过滤最后一行」的脆弱性；
- GUI 的进度走进程内回调（`Ctx.onProgress`），根本不经过 stdout；
- NDJSON 的真实客户是 v1.x Agent 子进程化/外部编排——届时加「`--events` 开关输出 NDJSON」是纯增量，核心事件回调缝已留（D5 的 Ctx）。现在做即「未来也许用到」的过度设计。

**进度输出**：v1 CLI 不输出进度（stdout 只有一个 JSON 文档是硬契约；进度不进 stderr——CI 安静，人工长任务无反馈为已知取舍，见开放问题 OQ-2）。
**编码契约**：请求/响应字节均 UTF-8（`QJsonDocument::toJson(Compact)` 天然 UTF-8；键序由 Qt 字典序保证，输出确定性可断言）。响应恰一个 JSON 文档 + 行尾 `\n`，**任何失败（含 JSON 解析失败）也回合法 JSON**——负例可断言的前提。
**入口分支**：`argc==2 && argv[1]=="--json"` 进 JSON 模式；多余参数 → 用法错误（仍回 E_PARAMS JSON，exit 1）。

### D2 命令面：9 个动词、平铺参数、严格校验

**请求信封**（示意）：

```json
{"v":1, "cmd":"denoise", "input":"in.mp4", "output":"out.wav", "humBase":"60", "wind":false}
```

**候选**：平铺动词 + 每动词专有字段 ✅ vs 统一 `{engine, params}` 嵌套形 vs 请求带 `engines` 数组（管线形）。
**取舍**：嵌套形是注册表/管线的形，v1 没有注册表支撑，等于为下一个包预写空架子（过度设计）；`engines` 数组明确不做（理由见 §5.2）。平铺形对 CI 断言最浅、对 Agent 结构化解码约束最简单（NFR-A3 的 schema 闸将来直接套用），后续加 `pipeline` 动词是纯增量不破坏契约。

**动词与参数表**（`*`=必填；缺省值全部对齐现有 GUI/CLI 行为）：

| cmd | 字段 | 类型/取值 | 缺省 | 语义 |
|---|---|---|---|---|
| `probe` | `input*` | string | — | 结构化流信息（D4 result）；FR-1 增 `streams[]` 音频流条件键 `language`/`title`/`defaultTrack`（有则插、空/假不插——老素材响应逐字节零漂移；muxer 缺省噪声归一：`und` 视同无、default 旗标全音轨命中视同无） |
| `transcode` | `input*`, `output`, `track` | int ≥0 | `-1`（best-stream） | 直通转码 WAV(f32)，输出跟随源；`track`=容器内音频流绝对流索引（FR-1，与 probe `streams[].index` 同域） |
| `denoise` | `input*`, `output`, `humBase`, `wind`, `track` | `"50"\|"60"\|"off"`；bool；int ≥0 | `"50"`；`true`；`-1` | DSP 降噪；`humBase:"off"` 且 `wind:false` → E_PARAMS（与 GUI 拒绝逻辑一致，`mainwidget.cpp:172-175`） |
| `aiDenoise` | `input*`, `output`, `model*`, `track` | string | **必填**；`-1` | GTCRN；输出锁兜底 44.1k/2ch（模型工作点，P0 #4 有意保留） |
| `aiDenoiseHq` | `input*`, `output`, `model*`, `track` | string（目录） | **必填**；`-1` | FR-4 质量档 DeepFilterNet3（模型=目录：config.ini+3 onnx）；工作点 48k/1ch、**输出跟随源**（`lockCanonicalOutput=false`，AI 引擎首个）；后缀 `_aihq`；引擎表条目自动入 pipeline/batch |
| `pitch` | `input*`, `output`, `semi*`, `track` | number ∈ [-12.0, 12.0] 且 ≠0；int ≥0 | —；`-1` | SoundTouch 变调；范围对齐 GUI（`mainwidget.cpp:53`）；0 拒绝对齐 GUI（`mainwidget.cpp:193-195`）——比旧 CLI **收紧**（旧 CLI 任意 double 都收），有意为之 |
| `separate` | `input*`, `output`, `model*`, `stem`, `track` | `"vocals"\|"instrumental"`；int ≥0 | `"vocals"`；`-1` | MDX 分离；输出锁兜底规格同上 |
| `extractAudio` | `input*`, `output`, `track` | int ≥0 | `-1`（best-stream） | 流复制抽**所选**音轨（输出后缀规则不变，`ffextractor.cpp:176`）；FR-1：`track` ≥0 时缺省名插 `_t<N>` 段（显式轨不同产物不得同名静默覆盖），无 `track` 命名逐字节不变 |
| `extractVideo` | `input*`, `output` | | 源目录规则 | 流复制抽无声视频 |

- **`model` 必填是刻意收紧**：现状 GUI（appDir 相对）与 CLI（third_party 相对 CWD）两套缺省不同源，且 `QCoreApplication::applicationDirPath()` 在无应用实例的 CLI 模式下行为不可用——把「模型路径解析」这个策略上移给调用方薄壳（GUI 壳沿用 appDir 规则、旧 CLI 壳沿用 third_party 规则、CI 显式传），命令层本体无环境依赖。FR-8 模型管理器落地时在此点接管。
- **`track`（FR-1 多音轨选择，仅引擎动词 + `extractAudio` 收）**：值域整数 ≥0（非整数「track 必须是整数」、负数「track 不能为负」→ E_PARAMS；越界「track N 超出流范围（共 M 条流）」、指向非音频流「track N 不是音频流」→ E_FILE，产生点单源 `resolveAudioStream`）。缺省 `-1` = `av_find_best_stream` 原式（行为零漂移）。`pipeline` 顶层 `track` 仅注步 1 子请求（中间产物单轨，链内不传）；`batch` 顶层与 steps 步内、`probe`/`extractVideo`/`wsInfo`/model 族一律「未知字段：track」拒收（绝对索引是文件级属性，跨异构文件的共享模板无意义）。
- **严格校验**：未知 `cmd`、未知字段（动词级+信封级）、类型错、枚举值非法、范围外、`input` 缺失/空、`output` 非空校验、`output` 绝对路径 == `input` 绝对路径（`QFileInfo::absoluteFilePath` 比较）→ 一律 E_PARAMS。未知字段拒收是给 NFR-A3「幻觉参数 100% 被 schema 拦截」打的前站。
- **`v` 字段**：请求可选、仅接受 1（缺省 1），响应恒带。此层是 Agent 的工具合同，注册表/管线两包必然扩字段，版本位是合同演化的最低成本保险。

### D3 错误码：六类，恒出 JSON，退出码两档

| code | 触发 | 现有对应文案来源 |
|---|---|---|
| `E_PARAMS` | schema/枚举/范围/未知字段/JSON 解析失败/output==input | `run_cli` 各 FAIL 用法分支（如 `main.cpp:46`） |
| `E_FILE` | 输入打不开/无音频流/无视频流/输出建不了 | `open_input`/`open_decoder`/`open_output` 文案（`ffextractor.cpp:26-55`、`fftranscoder.cpp:28-61`） |
| `E_MODEL` | 模型文件缺失/加载失败/输出契约不合规 | 「找不到模型」（`main.cpp:64`）+ setup 失败 + WP-1.4 契约校验文案 |
| `E_ENGINE` | 引擎运行期错（`lastError()` 非空） | streamengine.h:7 归口约定 |
| `E_CANCELLED` | 取消标志置位 | 「已取消」（`fftranscoder.cpp:238`、`ffextractor.cpp:83`） |
| `E_INTERNAL` | 兜底 catch（契约失效）/响应序列化失败 | `runJob` 兜底（`ffextractor.cpp:228-236`） |

- 分类独立出 `E_MODEL` 的理由：NFR-R4 要求模型缺失→引导重下载而非崩溃，Agent/FR-8 需要单独识别此类错误做恢复动作，与「文件坏了」语义不同。
- **响应形**：`{"v":1,"ok":true,"cmd":"...","result":{...}}` 或 `{"v":1,"ok":false,"cmd":"...","error":{"code":"E_MODEL","message":"..."}}`（`cmd` 能识别才回显）。`message` 沿用现有中文文案字符串（GUI 展示不变的前提，见 D6）。
- **退出码**：`ok:true`→0，任何 `ok:false`→1。不做按错误类分档退出码——CI 判定走 JSON 字段（信息更全），退出码只保「非零即败」的 shell 语义；分档是无人消费的复杂度。
- 取消在 CLI 不可达（单发同步、无信号处理，刻意不做）：`E_CANCELLED` 只出现在 GUI 进程内调用；CI 的取消覆盖维持现状（引擎级测试 + GUI 手工）。

### D4 结果对象：输出规格可观测（P0 #4 行为进合同）

转码族 result（示意）：

```json
{"output":"/abs/in_denoised.wav",
 "sourceSpec":{"sampleRate":48000,"channels":2},
 "outputSpec":{"sampleRate":48000,"channels":2,"format":"pcm_f32le"},
 "engineResampled":false,
 "durationSec":12.34}
```

- `engineResampled` := 引擎工作点 ≠ 源规格（`engine.inRate()!=srcRate || inChannels!=srcCh`）——即「必要重采样」标志。48k 源：transcode/denoise/pitch 为 `false`（全链直通，PAR-SR 的可观测化）；aiDenoise/separate 为 `true`（模型工作点重采样，属预期）。
- **依据的代码缝**：`transcodeStreamWav` 现在只返回路径（`fftranscoder.h:18-21`），生效规格在内部 `normalizeOutSpec` 后即丢失（`fftranscoder.cpp:151-153`）、帧数即 `samplePos`（`fftranscoder.cpp:137,191`）。加一个可选出参结构体（`{srcRate, srcChannels, outRate, outChannels, frames}`，缺省 nullptr 不动现有调用方），**只在成功路径回填**（失败路径半成品已删，回填即误报）。probe/extract 族 result 见动词表，extract 仅 `{output}`，不做多余字段。
- 候选权衡：复用 `probeSourceSpec` 在命令层自行拼装 vs 转码器出参回传——前者拿到的是「探测近似」（探测失败会回落兜底，`fftranscoder.cpp:119`），与实际解码规格可能不一致；后者是解码上下文的真值，一次打通。取出参。

### D5 代码落点：新建 `src/service/commandlayer.{h,cpp}`（进 engine 静态库）

**候选**：
- (a) 扩展 `ffextractor` 静态核心（引擎任务搬进其静态函数）；
- (b) 新建 `commandlayer`，`ffextractor` 降级为纯线程壳。✅

**取舍（对照工作原则 3）**：`ffextractor.h:11` 的职责是「QObject 任务执行者：静态核心供 CLI 复用 + slots 供后台线程调度」——JSON 分发/校验/错误分类不是它的职责，塞进去会让一个类同时是 Qt worker 和命令合同实现；且命令层是未来 Agent 工具面与注册表（NFR-M1）的宿主，需要一个不背 Qt 信号槽的稳定住所。**新建理由成立**：现有文件确实装不下（职责正交），且依赖方向无环：`ffextractor → commandlayer → {引擎, fftranscoder, ffextractor 静态 extract}`。
(a) 的反例：ffextractor 静态区只有 extract 两函数，引擎构造散在 do* 槽里（与 QObject/信号耦合），扩它等于先抽槽体再塞 JSON——绕一圈还是新层。

**核心形态（示意级）**：

```cpp
// commandlayer.h —— 无头命令层核心：JSON 请求 → JSON 响应，与传输/线程模型解耦
namespace CommandLayer {
struct Ctx {
    const std::atomic<bool>* cancel = nullptr;      // GUI 注入取消标志（CLI 传空）
    std::function<void(int)> onProgress;            // GUI 注入进度回调（CLI 不接）
};
QJsonObject execute(const QJsonObject& request, const Ctx& ctx = {});
}
```

- 纯函数、无全局态、每次调用自建引擎实例（模型按需加载卸载，NFR-P5 语义保持）——这是 FR-10 批处理/Agent 复用的前提（可串行重入）。
- 分发 = `execute` 内对 `cmd` 的 if/else 链，每个动词体是匿名命名空间独立函数（`cmdDenoise(req, ctx)` …）。**这就是注册表的缝**：NFR-M1 包把每个 `cmdXxx` 换成自描述条目（名字/schema/工厂/能力声明），分发层重写为零。刻意不先造 map 注册框架——8 个动词的 if/else 造注册表是为下一个包预写空架子。

### D6 GUI 迁移：本包迁，最小步 = ffextractor 七槽改调 execute，mediaservice/mainwidget 接口零改动

**候选**：
- (a) 本包迁：`ffExtractor` 七个 do* 槽删掉各自的引擎构造/模型查找/兜底规格代码，改为「组 QJsonObject 请求 → `execute(req, {cancelFlag, progress→emit})` → 拆响应 → emit jobDone」；
- (b) 留待注册表包一起迁（届时一次性换成注册表调用）。

**取 (a)，理由**：FR-24 验收原文就是「GUI 全部操作改走此层」（M2 DoD，v1-development-plan.md:13）；推迟意味着整个注册表包开发期存在 GUI 直调引擎与命令层双路径漂移，且 CI 测的命令层不是 GUI 实际跑的代码——恰是 FR-24 要消灭的形态。(b) 的好处只是省一次小改，但把双路径风险拉长到整个下个包周期，不值。
**最小步边界**：`mediaservice.{h,cpp}`（信号槽面/busy 锁/线程）与 `mainwidget` **一行不动**——worker 槽的对外信号（`progress/jobDone/probeDone`）签名不变，只是内部实现换引擎。`runJob` 的兜底 catch 保留（进程内第二道屏障与命令层 E_INTERNAL 双保险不冗余：前者防 execute 本身抛出）。
**关键约束——GUI 文案逐字不变**：`jobDone(ok, message)` 的 message 字符串（「降噪完成：<路径>」「已取消」「找不到 AI 降噪模型：<路径>」等）由槽从响应的 `result.output` / `error.message` 重建，措辞与现状逐字一致（`mainwidget.cpp:229` 对「已取消」有特判，漂移即 GUI 行为回归）。probe 的 GUI 文本展示：`FFExtractor::probe` 拆成 `probeInfo()`（结构体）+ `probeText(info)`（现有中文渲染，`ffextractor.cpp:130-162` 平移），命令层与 GUI 各取所需——单一 FFmpeg 遍历实现，两个渲染端，不复制探测代码。

### D7 既有 7 动词：保留为 CommandLayer 薄壳（回归面零改动）

**候选**：(a) 动词改写为「argv 解析 → 组等价 JSON 请求 → execute → 按 `OK: <路径>`/`FAIL: <msg>` 旧格式打印 + 旧退出码」的薄壳 ✅；(b) 直接改写动词语义/删除，ci-smoke 与 runbook 同步改 JSON 形。

**取 (a)，理由**：回归面（§1 清单：ci-smoke w1–w7 + ffprobe 体检、PAR-SR 三调用、wp-1.0 round-4 Windows runbook）零脚本改动即全量复验——旧动词薄壳路由经命令层，等于**现有 CI 门禁自动变成命令层的回归网**（单一代码路径，无第二套实现可漂移）。(b) 要同时改三处脚本 + Windows runbook，换来的是删除 ~60 行兼容壳——等 Agent/GUI 全走新面后再清理不迟（本包不删）。
**行为变化两处（有意收紧，随实现同步 CLAUDE.md）**：
1. 未知动词从「静默当 extractAudio」（`main.cpp:120` else 分支）改为用法 FAIL exit 1——宽松分支是陷阱不是兼容；
2. `--pitch-wav` 半音数新增 [-12,12] 非 0 校验（旧 CLI 任意值都收）；ci-smoke/PAR-SR 只用 ±2.0，无回归。
**旧缺省全保留**：`--denoise-wav` 缺省 50+wind=true；`--ai-denoise`/`--sep-*` 模型缺省 third_party 相对路径（薄壳显式传入 `model` 字段，即 D2 的「缺省策略上移到壳」）。

### D8 验证：`docs/acceptance/fr24-command-layer/`，脚本风格对齐 run_parity，进 ci-smoke

详见 §7。判定工具 python3（已是 parity 依赖，无新依赖；不引 jq——CI 环境不保证）。

### D9 同步构建面：commandlayer 进 engine 静态库

`CMakeLists.txt:14-30` engine 源列表加两行——命令层只依赖 Qt Core + 既有底座，engine 库（GUI 与 DUT 共用）是其唯一合理位置；不进 video_handler 专有源（否则 engine_test 未来无法复用）。

## 3. 方案（模块图 + 数据流）

```
 ┌─ GUI 壳（不变）────────────┐   ┌─ CLI 壳（main.cpp）─────────────────────┐
 │ mainwidget → mediaservice  │   │ 旧 7 动词: argv→组请求→execute→OK/FAIL  │
 │ （busy 锁/取消原子量/QThread）│   │ --json:   stdin→parse→execute→stdout   │
 └────────────┬───────────────┘   └──────────────┬─────────────────────────┘
              ▼ worker 槽                          │
        ffextractor（线程壳：取消/进度转发/兜底 catch/GUI 文案重建）
              │  QJsonObject 请求 + Ctx{cancel, onProgress}
              ▼
      ╔════════════════════════════════════╗  ←── 注册表缝（§5.1）
      ║ CommandLayer::execute(request,ctx) ║     v1 = if/else 分发
      ║  ① schema 校验（严格，E_PARAMS）      ║
      ║  ② cmdXxx 处理器（匿名 ns 独立函数）  ║
      ╚═══════┬═════════════════════════════╝
              ▼
  probeInfo ┊ DspDenoiser ┊ AiDenoiser ┊ PitchShifter ┊ VocalSeparator
              │（每调用自建实例；工作点策略：跟随源 / 锁兜底规格）
              ▼
  FFTranscoder::transcodeStreamWav(…, OutInfo* )   FFExtractor::extract{Audio,Video}(…, outPath)
              │  OutInfo 回传（srcSpec/outSpec/frames）——D4 可观测缝
              ▼
      QJsonObject 响应 ──→ GUI: jobDone/progress（文案逐字重建）
                      ──→ CLI: stdout 单 JSON 文档（UTF-8/Compact/键序确定）
```

单次调用的数据流（正例 denoise）：stdin 字节 → `QJsonDocument::fromJson` → 信封/参数校验 → `probeSourceSpec`（工作点跟随源）→ `DspDenoiser::setup` → `transcodeStreamWav`（取消/进度经 Ctx 透传；OutInfo 回填）→ `{"ok":true,…,"result":{outputSpec…}}` → stdout。

失败流：任一环节出错 → 该层错误文本 + 分类码 → `{"ok":false,"error":{…}}`，半成品已被底座删除（`fftranscoder.cpp:318-322`、`ffextractor.cpp:108-120`，NFR-R1 保持）。

## 4. 改动面（file:line 级预估）

| 文件 | 改动 | 预估规模 |
|---|---|---|
| **新建** `src/service/commandlayer.h/.cpp` | `execute` + 9 个 `cmdXxx` 处理器 + schema 校验 + 错误分类 + 缺省输出命名规则（平移 `ffextractor.cpp:225`） | ~60 + ~380 行 |
| `src/service/fftranscoder.h:18-21` / `.cpp:131-153,300` | `transcodeStreamWav` 加可选 `OutInfo*` 出参（默认 nullptr）；成功路径回填规格与帧数 | ~25 行 |
| `src/service/ffextractor.h:18-22` / `.cpp:130-162` | `probe` 拆 `probeInfo`/`probeText`；`extractAudio/extractVideo` 加可选显式 `outPath` 参数（缺省自动命名，行为不变） | ~40 行重构 |
| `src/service/ffextractor.cpp:216-308` | 七个 do* 槽改「组请求 → execute → 拆响应」（引擎构造/模型查找代码上移进壳，`runJob` 兜底 catch 保留） | 净变化约 ±0（删直调码换 JSON 胶水） |
| `src/main.cpp:17-134` | `run_cli` 七动词改薄壳（保 OK/FAIL 文案与退出码，未知动词改 FAIL）；新增 `runJson`（stdin→execute→stdout）；main 分支加 `--json` | 净减 ~30 行 |
| `CMakeLists.txt:14-30` | engine 源列表 + `commandlayer.{h,cpp}` | 2 行 |
| `CLAUDE.md` | 架构段加 commandlayer 一行；CLI 用法段加 `--json`（行为/接口变化同步文档） | 2 行 |
| `docs/acceptance/wp-1.2-linux-build/scripts/ci-smoke.sh` | CLI 冒烟段后追加一行调用 `run-json.sh`（全绿后进，见 §7） | 1 行 |

不动的：`mediaservice.{h,cpp}`、`mainwidget.*`、`streamengine.h`、五引擎、`data.h`、全部 parity/parity 脚本、wp-1.0 runbook。

## 5. 留缝清单（本包不做，下一个包的接口锚点）

| 缝 | v1 怎么留 | 不动什么 |
|---|---|---|
| **5.1 注册表（NFR-M1）** | 每动词一个签名一致的独立处理器函数 + `execute` 集中分发；处理器无共享可变状态 | 不造 map/自描述条目/能力声明——8 动词造注册框架是空架子 |
| **5.2 管线串联（FR-9）** | 信封是 JSON 对象，`pipeline` 动词 + `steps` 数组纯增量加入，不破坏既有契约；**不做 `engines` 数组**——多引擎链需要级间规格协商（每引擎 inRate/inChannels 不同，链上需逐段重采样）与独立 parity，是完整工作包，v1 预实现无法验证 | `transcodeStreamWav` 单引擎签名不动 |
| **5.3 工作区（FR-A4）** | 输出路径决策收敛为命令层内单一函数（显式 `output` 优先，缺省走源目录后缀规则）；工作区包替换该函数即可接管全部动词的落盘位置。v1 已立原件只读防线：`output==input` → E_PARAMS（`extractAudio` 自动命名天然不与源冲突） | 不做版本化/哈希登记/回退 |
| **5.4 批处理（FR-10）** | ~~`execute` 无全局态、每调用自建引擎 → 可串行重入；CI 批量 = bash 循环调 `--json`，同步模型够 CI 用。队列/失败不中断语义是 GUI 服务层（mediaservice 演进）与 Agent 的事~~ **已由 FR-10 演进关闭（2026-11-04，附裁 4 记档）**：批队列/失败不中断/整批取消语义已落为命令层 `batch` 动词（`docs/spec/fr10-batch-queue.md`，inputs[≥2] × 共享 steps[≥1] 模板逐件组合既有 execute），CLI/GUI/Agent 三客户端同源 | 不做任务队列/异步 API/进度事件流（批内仍是同步串行；交互式队列留缝不变） |

## 6. 验证方案（概要，详表进 acceptance matrix）

- 目录：`docs/acceptance/fr24-command-layer/`（goal.md + matrix.csv + round-N.md + `scripts/run-json.sh`），用例前缀 `CMD-`。
- `run-json.sh` 风格与 run_parity 一致：`set -uo pipefail`、mktemp 临时区、`ck()` 记账、FAIL 统一收口退出码；断言用 python3 内联/小脚本解析 JSON（退出码 + `ok` + `error.code` + result 字段 + ffprobe 交叉验证），素材复用 ci-smoke 配方（sine wav + tiny.mp4）。
- 用例族（~20 条）：
  - 正例：9 动词各 ≥1（含 48k 源 transcode/denoise → `outputSpec` 跟随源断言；48k 源 aiDenoise → `engineResampled:true` + 输出 44.1k 断言——P0 #4 行为进合同；probe 双流 mp4 结构断言；extract 缺省落源目录断言）；
  - 负例：非法 JSON / 未知 cmd / 未知字段 / 缺 input / 类型错（`semi:"abc"`）/ 范围外（`semi:15`）/ `semi:0` / `humBase:"61"` / off+off / input 不存在 / 无音频流文件 / 模型缺失 / 坏模型（复用 wp-1.0 round-2 维度错位配方，断言 E_ENGINE 干净退出且无半成品）/ `output==input`；
  - 确定性：同请求 transcode 跑两遍输出 `cmp` 逐字节一致（NFR-A2 前味）；
  - 薄壳等价：旧 7 动词退出码与 OK/FAIL 文案不变（由既有 ci-smoke w1–w7 + PAR-SR 直接覆盖，matrix 记交叉引用不重写）。
- 门禁接线：round-1（Linux）全绿后，`ci-smoke.sh` 追加一行调用 `run-json.sh`（parity 同款模式）；Windows 真机复验随 WP-1.0 round-4 或本包 round-2（注意 §8 Windows stdout 事项）。

## 7. 开放问题（主线程已拍板，2026-11-03）

- **OQ-1 `semi:0`**：✅ 拒绝（对齐 GUI 拒绝逻辑；旧 CLI 宽收是陷阱非契约）。
- **OQ-2 CLI 进度输出**：❌ v1 不加 stderr 进度行（stdout 纯 JSON 是硬契约，CI 优先；人工长任务静默是已记录取舍，Agent/NDJSON 缝已留）。
- **OQ-3 `v` 字段**：✅ 入契约（响应恒带；注册表/管线两包必然扩字段，版本位是最低成本保险）。
- **OQ-4 Windows AttachConsole**：❌ 不做（保持薄壳；Windows 交互式控制台体验另立项）。
- **OQ-5 旧 7 动词退役**：发版前清理项（登记入 REQUIREMENTS 发版清单范畴），本包只保不删。

## 8. 红线自查与实现风险

**红线自查**：不引新第三方依赖（QJsonDocument 已在用，`vocalseparator.cpp:7-8`）；流式与内存有界不破坏（命令层零音频缓冲，只过 JSON 小对象；转码/提取通路一行不动，NFR-P4 维持）；原件只读（沿袭「从不写输入」+ 新增 `output==input` 显式防线）；LGPL 组合零变化。

**两大实现风险**：
1. **GUI 迁移的文案/行为回归无自动化护栏**：七槽改走 execute 后，`jobDone` 文案（含 `mainwidget.cpp:229` 对「已取消」的特判）、输出命名、进度节奏、模型缺省路径（appDir 相对）必须逐点不变，但 GUI 只有 Windows 真机手工验收（round-4 仍 pending）。缓解：round-1 用「旧动词薄壳 + 新 JSON 面等价输出 cmp」锁 CLI 侧；GUI 侧出逐槽对照清单进 round-N.md 手工过。
2. **`OutInfo` 回传缝的失败路径契约**：`transcodeStreamWav` 有约 6 个提前退出路径，规格回填点（normalize 后）与帧数回填点（成功收尾）分散，漏一处或失败路径误回填即产生「报告规格与实际文件不符」的静默错误——恰是 D4 要消灭的不可观测。缓解：回填只写成功路径单点（收尾 `av_write_trailer` 成功后），负例用例加「E_* 后响应不得带 result」断言。
