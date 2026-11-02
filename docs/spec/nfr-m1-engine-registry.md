# NFR-M1 引擎注册表：元数据驱动的命令层分派 + 结构化错误通道（开工 spec）

- 日期：2026-11-03
- 性质：开工前快照（M2 第二个工作包，依赖序承接 FR-24；进展追踪见 `docs/acceptance/nfr-m1-engine-registry/goal.md`）
- 本文 D 编号（D1–D7）仅限本 spec，与 REQUIREMENTS.md 决策记录（D1–D7）无关。
- 现状基线：HEAD `0a1cf8c`（FR-24 片 D 已合入：GUI 七槽走 `CommandLayer::execute`）。

## 0. 需求锚点与约束

| 锚点 | 内容 | 对本包的约束 |
|---|---|---|
| NFR-M1（REQUIREMENTS.md:185） | 引擎注册表——**新增引擎不改 GUI/服务层代码**（FR-24 的实现载体） | 验收金标准 = 注册一个假引擎，diff 不触 GUI/服务层/命令层分派代码即可全链跑通（§6 REG 族） |
| 架构基线（REQUIREMENTS.md:56-68） | 命令层（引擎注册表）：「新增引擎注册即枚举，不改 GUI/服务层」 | 注册表是命令层的内部结构，不是新传输面；信封/响应契约不动 |
| FR-24 已落地（`docs/spec/fr24-command-layer.md` §5.1:224） | 留缝：每个 `cmdXxx` 签名一致 + `execute` 集中分发，「NFR-M1 换自描述条目，分发层重写为零」 | 本包兑现该缝：动词元数据上移、分发改查表；**信封校验顺序与响应契约逐字不变** |
| P3-1 挂账（`docs/acceptance/fr24-command-layer/goal.md:24`、`round-1.md:72`） | `classifyTranscodeError` 依赖**中文文案前缀匹配**之脆 + E_INTERNAL 承载面与 D3 行文漂移，归本包消灭 | 结构化错误通道是本包第二硬目标：错误码随错误产生点走，前缀匹配退役 |
| FR-9（REQUIREMENTS.md:95）/ FR-7 RVC（:92） | 管线串联 / 变声引擎是**潜在注册方** | 条目 schema 必须装得下「新引擎 + 新参数」而不改分派代码；管线级间协商本包不做（§5 留缝） |
| FR-23（REQUIREMENTS.md:128） | 第三方引擎插件接口（v2） | 本包只做编译期注册，运行期注册 API 是 v2 的对象，不预建 |
| NFR-R4（REQUIREMENTS.md:183） | 模型缺失/损坏 → 引导重下载，不崩溃 | `E_MODEL` 语义（含 WP-1.4 P1-1 拍板：输出契约违规 → E_MODEL）必须保真迁移（D4） |
| CMD 回归网（`docs/acceptance/fr24-command-layer/matrix.csv`，36 条 + CMD-EQV） | 严格校验语义（未知字段拒收/类型/枚举/范围/NaN 拒收）与错误码断言已固化 | 元数据驱动后**校验语义与消息文案逐字不放松、不漂移**（D2） |
| 红线 | 不引 GPL/AGPL；`third_party/` 与模型权重不入库；一切处理流式、内存有界（NFR-P4）；原件只读；注释与文案中文 | §8 红线自查 |

## 1. 现状盘点（证据，HEAD 0a1cf8c 实读）

- **分发结构**：`commandlayer.cpp:306-359` `execute`——`kVerbs` 静态白名单（:313-317）+ 信封校验（cmd 先于 v，未知字段白名单 `verbFields` :80-86，input/output 公共校验 :334-344）+ 尾部 if/else 分派到 8 个 `cmdXxx`（:345-353）。**这就是 fr24 spec §5.1 留的注册表缝。**
- **五个转码族处理器各自硬编码**：`cmdTranscode`（:170-176，DspDenoiser 直通）/ `cmdDenoise`（:178-203，humBase 枚举 + wind 布尔 + off+off 跨字段拒绝）/ `cmdAiDenoise`（:205-216，model 必填 + 存在性 → E_MODEL）/ `cmdPitch`（:218-232，semi 范围 + 非零，setup 失败 → **E_ENGINE**）/ `cmdSeparate`（:234-255，model + stem 枚举，setup 失败 → **E_MODEL**）。setup 错误归类在调用点逐个写死，且两引擎不同（:230 vs :253）。
- **引擎 setup 签名异构**（`streamengine.h` 契约 + 四实现）：`DspDenoiser::setup(int, int, HumBase, bool)` void 返回（dspdenoiser.h:13）；`AiDenoiser::setup(const QString&, QString*)` bool（aidenoiser.h:17）；`PitchShifter::setup(int, int, double, QString*)` bool（pitchshifter.h:14）；`VocalSeparator::setup(const QString&, QString*)` bool + `setInstrumental(bool)` 前置（vocalseparator.h:15-16）。
- **错误通道是纯文本**：`IStreamEngine::lastError()` 返回 QString（streamengine.h:16）；转码器在两处查询（fftranscoder.cpp:230,290）；底座错误也只有文本（fftranscoder.cpp:30-107 十余条、ffextractor open_*/write_header 家族）。`classifyTranscodeError`（commandlayer.cpp:46-57）靠「已取消」全等 + 引擎 lastError 非空 + **中文前缀表 `isFileError`**（:31-42，11 条前缀）分类；WP-1.4 P1-1 拍板的「模型」前缀 → E_MODEL 在 :52。
- **WP-1.4 固化文案**（负例测试已锁、必须保真迁移）：vocalseparator runModel 输出契约三条「模型…」（vocalseparator.cpp:217,222,230）现靠 `startsWith("模型")` 归 E_MODEL；推理异常「人声分离推理失败：%1」（:237）与「音频太短，人声分离至少需要约 %1 秒」（:446-447）归 E_ENGINE；AiDenoiser「AI 降噪推理失败：%1」（aidenoiser.cpp:344,355）归 E_ENGINE。
- **GUI/CLI 壳刚迁完（片 D，0a1cf8c）**：`ffextractor.cpp:263-286` runRequest 组请求 → execute → 文案逐字重建（`error.message` 直通，mainwidget.cpp:229「已取消」特判）；`main.cpp:57-137` 旧 7 动词薄壳；`main.cpp:24-40` `--json`。**本包对它们的承诺见 D3。**
- **回归面**：CMD 36 条 + CMD-EQV 薄壳等价 + GUI-EQV 槽级等价（fr24 matrix.csv）；ci-smoke 全量（parity 四族 + 冒烟 + 体检 + run-json）；`engine_test` DUT（engine_test.cpp:8-13，直调引擎、不引用 lastError、不经命令层）。
- **CMake**：engine 静态库源列表 `CMakeLists.txt:14-32`（GUI 与 engine_test 共用）。

## 2. 设计决策

### D1 注册机制：编译期中央静态表（一个 `engineregistry.cpp` 持全部条目）

**候选**：
- (a) 编译期静态表：条目集中在一个新文件，函数内惰性构造的 const 表，`commandlayer` 查表分派。✅
- (b) 运行期注册 API：`registerEngine(entry)` 供 GUI/插件动态注入。
- (c) 显式初始化函数：`initEngineRegistry()` 在各宿主 main 里逐个注册。
- (d) 自注册静态构造：每个引擎文件里放静态注册对象，程序启动自动登记。

**取舍与理由**：
- **(b) 否决**：唯一真实客户是 FR-23 第三方插件（v2，明确不做，REQUIREMENTS.md:128）。运行期注册引入静态析构顺序、线程安全、重复注册/覆盖语义等一整类问题，而 v1 没有任何动态注册方——为 v2 预写空 API 是 fr24 spec D5 已经否决过的「为下一个包预写空架子」。缝已留好：表是数据不是协议（§5），v2 加运行期表只需在查表层并联，分派代码仍零改。
- **(c) 否决**：所有宿主（video_handler、engine_test、未来 Agent 宿主）都必须记住调用 init，漏调 = 引擎静默缺席，且查表处还要处理「未初始化」态——比数据表多一个出错维度，零收益。
- **(d) 否决（技术硬伤）**：`engine` 是**静态库**（CMakeLists.txt:14）。静态库中未被引用的 .o 不会被链入，自注册对象的构造函数可能整段消失——引擎恰好只在注册表条目的工厂里被引用，属「仅被数据引用」的高危形态；跨翻译单元静态初始化顺序又不确定。这是 C++ 静态库注册的经典陷阱，不赌。
- **(a) 取胜**：新增引擎的改动面收敛为「新引擎 .h/.cpp + `engineregistry.cpp` 追加一个条目 + CMake 两行」——注册表数据文件既不是 GUI/服务层也不是命令层分派代码，**NFR-M1「不改 GUI/服务层代码」与 fr24 留缝「分发层重写为零」同时成立**，且可用 git diff 白名单断言机械化验收（§6 REG 族）。
- 表不持引擎实例、只持工厂函数指针：无静态构造期创建 ORT env / Qt 重对象的风险（AiDenoiser 构造在 Impl 中建 Ort::Env，必须留在运行期）。

### D2 注册条目 schema：参数校验上移元数据驱动，消息模板逐字兼容

**条目结构（示意级，实现可微调字段名）**：

```cpp
// engineregistry.h —— 引擎注册表：转码族动词的自描述条目（名字/参数 schema/工厂/错误归类）
struct ParamSpec {
    const char* name;          // 字段名（如 "humBase"）
    enum class Kind { String, Number, Bool, Enum } kind;
    bool required = false;     // 必填
    double min = 0, max = 0;   // Number：范围
    bool hasRange = false;
    bool rejectZero = false;   // Number：拒绝 0（qFuzzyIsNull 语义）
    QList<const char*> values; // Enum：合法值
    const char* defStr = nullptr;// 缺省值（字符串形态；nullptr = 无缺省）
};

struct EngineEntry {
    QString id;                // 动词名 = cmd（ASCII 小驼峰，与现动词同风格）
    QList<ParamSpec> params;   // 参数 schema（严格校验：未知字段拒收、类型/枚举/范围/NaN）
    bool requiresModel = false;         // model 必填字符串 + 存在性检查 → E_MODEL「找不到模型 %1」
    const char* setupErrorClass;        // create/setup 失败归类：E_MODEL（模型引擎）/ E_ENGINE（pitch 等）
    bool lockCanonicalOutput = false;   // 输出锁兜底 44.1k/2ch（模型工作点，P0 #4 有意保留）
    QString (*outputSuffix)(const QJsonObject& args);// 缺省输出后缀（可依赖参数，如 separate 按 stem）
    QString (*semanticCheck)(const QJsonObject& args);// 跨字段校验，返回错误文案，空=通过（如 off+off）
    std::unique_ptr<IStreamEngine> (*create)(const QJsonObject& args, int srcRate, int srcCh, QString* err);
};
const QList<EngineEntry>& engineTable();// 函数局部静态惰性构造（D5）
```

**候选（参数如何从 JSON 到达引擎）**：
- (a) 元数据校验产出**已验证的 QJsonObject**，工厂闭包内自行做类型化抽取（QString→HumBase 映射等）；✅
- (b) 元数据连带「字段→构造参数」的绑定描述（反射式组装 QVariant 再下发）；
- (c) 不做元数据，各条目自带校验 lambda（校验逻辑仍每引擎一份，只是搬了家）。

**取 (a)，理由**：(b) 等于发明一个迷你反射 DSL——humBase 要绑 HumBase 枚举、semi 要绑 double、model 要绑 QString，描述语言很快长得比命令式代码还复杂（工作原则 5「不堆抽象」）；(c) 没消灭重复——严格校验语义分散在每个条目里，新增引擎仍要手写校验，「未知字段拒收」这类信封级规则没有单一权威。**(a) 让 schema 是唯一校验权威**：通用校验器（`validateParams(entry, req) -> QString 错误文案`，与条目同居 engineregistry.cpp）按 `params` 声明序逐项检查，工厂拿到的是「已通过 schema 的参数对象」，只做类型化转换，校验重复为零、消息单一来源。

**严格校验语义不放松（CMD-020..035 是回归网）——四条硬约束**：
1. **未知字段拒收**：动词级允许字段 = 条目 `params` 名集 ∪（`requiresModel` ? {model} : {}）；信封级 v/cmd/input/output 规则不变（execute 信封段原样保留，`verbFields`/`verbAllowsOutput` 删除，改由条目供给）。
2. **消息模板逐字兼容**：校验消息是 CMD 断言与 GUI 直通文案的一部分，通用渲染必须复现现文案——`%1 缺失` / `%1 必须是字符串` / `%1 不能为空`（requireString，commandlayer.cpp:68-74）、`%1 必须是布尔值`（:193）、`%1 只接受 %2`（枚举值顿号连接、末值前加「或」→「humBase 只接受 50、60 或 off」「stem 只接受 vocals 或 instrumental」，:188/:246）、`%1 须在 [%2, %3] 内`（:224）、`%1 不能为 0`（:225）、`找不到模型 %1`（:211,249）、semanticCheck 原文返回（「humBase 为 off 且 wind 为 false：没有可执行的降噪项」:197）。**条目按声明序校验、首个错误即返**，保证多错并存时报错与现状逐字一致（denoise：humBase→wind→semantic；separate：model→stem→存在性）。
3. **NaN 拒收**：范围判定一律否定式 `!(x >= min && x <= max)`（沿用 :223 与薄壳 main.cpp:110 的审查 P2-1 手法）——JSON 本无 NaN，但薄壳 argv 面 `toDouble("nan")` 可造出，否定式使 NaN 无论从哪个入口进来都落拒收。
4. **缺省值不变**：humBase 缺省 "50"、wind 缺省 true、stem 缺省 "vocals"、semi/model 无缺省必填——全部进 ParamSpec 的 defStr。

**五引擎条目落表**（现状语义的逐字段转录）：

| id | params | requiresModel | setupErrorClass | lockCanonical | outputSuffix |
|---|---|---|---|---|---|
| `transcode` | （无） | 否 | —（setup 不会失败） | false | `_transcoded` |
| `denoise` | humBase 枚举{50,60,off} 缺省 50；wind 布尔 缺省 true；semanticCheck=off+off 拒绝 | 否 | — | false | `_denoised` |
| `aiDenoise` | （无额外参数） | 是 | E_MODEL | true | `_ai` |
| `pitch` | semi 数字 必填 [-12,12] 拒 0 | 否 | **E_ENGINE**（:230 现状） | false | `_pitch` |
| `separate` | stem 枚举{vocals,instrumental} 缺省 vocals | 是 | E_MODEL（:253 现状） | true | 按 args：vocals→`_vocals` / instrumental→`_instrumental` |

**工厂闭包统一异构 setup**：`create` 内部各自调原生签名——denoise 条目闭包做 `QString→HumBase` 映射后 `setup(rate, ch, humBase, wind)`；transcode 条目返回 `setup(rate, ch, Off, false)` 的直通 DspDenoiser；pitch 闭包取 semi 传 `setup(rate, ch, semi, err)`；separate 闭包先 `setInstrumental(stem=="instrumental")` 再 `setup(model, err)`。**引擎头文件一行不动**，异构性被封在闭包里，不污染契约。

**能力声明最小化**：条目**不复制**引擎工作点（16k 单声道 / 44.1k 立体声）——那是引擎内从模型 metadata / MD5 查表取得的真值（CLAUDE.md 关键约定「勿硬编码」），复制即双源漂移。`engineResampled` 继续用运行期 `engine.inRate()/inChannels()` 对比源规格计算（commandlayer.cpp:117-118 现逻辑不动）。条目只声明命令层必须知道的策略位：schema、model 要求、错误归类、输出规格策略、后缀。未来 RVC 引擎 = 新 .h/.cpp + 一个条目 + CMake，命令层/GUI/服务层零改（若要 GUI 按钮，改的是壳自己的 UI 代码，与注册表无关）。

### D3 动词面划分：5 转码族动词入表，probe/extract 族保留专有逻辑；GUI/CLI 壳零改动

- **入表（5）**：transcode / denoise / aiDenoise / pitch / separate——全部走「查表 → 信封校验 → validateParams → semanticCheck → model 存在性 → create → runTranscode」通用路径。`runTranscode`（:126-137）与 `transcodeResult`（:110-123）保留为通用路径的收尾件，lockCanonical 与后缀由条目供给。
- **保留专有（3）**：probe / extractAudio / extractVideo **不是引擎**（探测与流复制，无 IStreamEngine 参与、无引擎参数、错误分类独立），保留现有处理器（:141-168, :263-281）。它们同样收敛进一张内置小表（id→处理函数指针），`execute` 尾部 = 注册表查表 → 内置表查表 → 未命中 E_PARAMS，if/else 链与 `kVerbs`/`verbFields` 硬编码全部消失。
- **transcode 也入表（不在内置表留特例）**：直通转码注册为「直通引擎条目」（工厂返回 Off/false 的 DspDenoiser，现状 cmdTranscode 的原实现 :170-176 原样搬进闭包）。若不入表，execute 要为它保留一条硬编码分支 + verbFields 特判——与「分发层重写为零」矛盾。代价是接受「直通也是一种引擎配置」的表述，现状代码本来就是这么做的（直通 = 无滤波的 DspDenoiser）。
- **9 动词形态不变**：8 个 cmd（5 注册 + 3 内置）+ separate 双 stem 形态。
- **GUI/CLI 壳零改动承诺（精确改动面）**：`main.cpp`、`ffextractor.{h,cpp}`（片 A）、`mediaservice.{h,cpp}`、`mainwidget.{h,cpp}`、`streamengine.h`、四引擎头文件、`engine_test`、全部 parity 脚本——**片 A 零改动**，验收用 `git diff --stat` 范围断言（同 fr24 GUI-DIFF 方法论）；片 B 对 `streamengine.h`/两 ORT 引擎/转码底座的改动见 D4 与 §4，GUI 面仍零改动（响应契约与 `error.message` 文案逐字不变）。`CLAUDE.md` 架构节同步一行（文档非代码）。

### D4 结构化错误通道（P3-1 收口）：`ErrInfo{code,message}` 全链贯通，前缀匹配退役

**候选**：
- (a) 错误码枚举 + 中文文案**并存**于一个 `ErrInfo` 结构，随错误产生点填码，文案继续承担 GUI 展示；✅
- (b) 只加码不保文案（GUI 改读码再本地化）；
- (c) 保留文本通道，仅在命令层加一张「文案→码」的完整穷举表（把前缀匹配升级成全等匹配）。

**取 (a)，理由**：GUI 合同是「`error.message` 直通 jobDone、文案逐字不变」（片 D 验收 GUI-EQV 已锁，含 mainwidget.cpp:229 特判），(b) 违反该合同且要求 GUI 引入码→文案映射（改 GUI，违 NFR-M1 精神）；(c) 没消灭脆点——文案一改表就漏（fr24 首版 QLatin1String 装中文全表失配就是实证，goal.md:17），穷举表仍是要人工同步的双源。**(a) 让码在产生点诞生，文案与码同源同点，单一事实。**

**通道设计**：
1. 新建 `src/service/errinfo.h`（仅 QString 依赖，被 streamengine.h / fftranscoder.h / ffextractor.h / engineregistry.h / commandlayer.cpp 共用的最底层头）：
   ```cpp
   enum class ErrCode { None, File, Model, Engine, Cancelled, Internal };// 与响应码 E_* 一一对应
   struct ErrInfo { ErrCode code = ErrCode::None; QString message; };// message 中文文案（GUI 直通）
   ```
2. **引擎面**：`IStreamEngine::lastError()` 返回类型 QString → `ErrInfo`（streamengine.h:16）。DspDenoiser/PitchShifter 不覆写（默认空 ErrInfo，头文件不动其作者语义）；AiDenoiser/VocalSeparator 错误点随赋文案同时赋码：推理异常 catch → `Engine`（aidenoiser.cpp:344,355；vocalseparator.cpp:237）；**WP-1.4 三条输出契约文案（vocalseparator.cpp:217,222,230）→ `Model`（P1-1 拍板保真迁移，不再依赖 startsWith("模型")）**；「音频太短…」（:446-447）→ `Engine`（现状即 E_ENGINE，不变）。
3. **底座面**：`transcodeStreamWav` 的 `QString* err` 出参 → `ErrInfo*`（fftranscoder.h:26-29）；extractAudio/extractVideo 同（ffextractor.h:39-44）。十余个错误点逐一赋码（映射表进 §6 ERR 族）：open_*/无流/不认识的编码/建容器/建流/输出建不了/写头 → `File`；「已取消」（fftranscoder.cpp:241）→ `Cancelled`；内存不够/重采样器起不来 → `Internal`；送包解码失败/写包失败/写文件尾失败 → **`File`**（P3-1 记录的语义修正：源损坏与输出侧 I/O 失败报 E_INTERNAL 误导 Agent，源/媒体 I/O 族归 File——非 CMD 断言路径，行为变化进对照表，见 OQ-4）。引擎 lastError 查询点（fftranscoder.cpp:230,290）从「文本非空即用」改为「ErrInfo 非空即透传其 code+message」。
4. **命令层面**：`classifyTranscodeError`/`isFileError`/`classifyExtractError`（commandlayer.cpp:31-63）**删除**；错误码直接取自 ErrInfo（setup 失败仍按条目 `setupErrorClass` 归类——setup 错误走 err 出参而非 lastError，现状即如此）。响应序列化仍是六类 `E_*` 字符串，**对外契约零变化**。
5. **共存与迁移策略**：本仓库引擎与底座全部 in-tree、无第三方实现者（FR-23 是 v2），故**硬切一刀**：接口改型、全部调用点同包内更新，不留「文本优先、码兜底」的双轨过渡（双轨会让前缀匹配活下去，P3-1 收不了口）。C++20 + 全量在树，无 ABI 兼容包袱。

### D5 注册表初始化时序：函数局部静态惰性构造

**候选**：(a) `engineTable()` 函数局部静态（C++11 magic statics，首次调用构造，线程安全）；(b) `Q_CORE_APP_BOOTSTRAP` 钩子；(c) namespace 级全局静态表。

**取 (a)**：无初始化顺序问题（首次使用点必然晚于全部依赖就绪）、无静态析构顺序问题（QString 字面量成员无害，但函数局部静态把风险面收敛到零）、engine 库不引入对 QCoreApplication 生命周期语义的依赖（engine_test 无 QApplication 也能用）。(b) 过重且要求 Qt 专属入口；(c) 跨翻译单元顺序无保证（与 D1(d) 同理）。表内 id 唯一性以 `Q_ASSERT` 防御（开发期断言，静态表由维护者保证，运行期不做降级逻辑）。

### D6 代码落点：新建 `src/service/engineregistry.{h,cpp}`（进 engine 静态库）

与 fr24 D5 同型论证：注册表是命令层与引擎之间的新职责（自描述数据 + 通用校验器），`commandlayer.cpp` 装不下也不该装（塞进去则「新增引擎不改命令层文件」的更强目标都达不成）；`engineregistry` 依赖五引擎头（工厂要构造引擎），`commandlayer` 依赖 `engineregistry`，方向无环。`errinfo.h` 是零依赖叶子头，放 `src/service/` 与其他底座头同级。CMake：engine 源列表 +3 文件（§4）。

### D7 分片实施：两片——先注册表（片 A），后错误通道（片 B）

**候选**：(a) 一片到位；(b) 两片：片 A = 注册表 + 元数据驱动校验（错误分类维持现状前缀匹配）；片 B = 结构化错误通道 + 前缀匹配退役 + P3-1 承载面修正。✅

**取 (b)，理由**：两片触不同的回归面——片 A 动命令层校验/分派（回归面 = CMD 36 条 + 薄壳/槽级等价），片 B 动引擎接口与底座错误通道（回归面 = parity 四族 + engine_test 分块不变性 + CMD 负例 + GUI 文案直通）。合一片 = 一次 diff 同时压两套门禁，出错时回滚边界模糊、归因困难；分片则各有一轮独立验收与独立回滚点。片 A 单独交付即兑现 NFR-M1 主目标（注册表），片 B 单独兑现 P3-1——两目标验收互不阻塞。顺序 A→B：条目的 `setupErrorClass` 字段片 A 就位，片 B 只是让它旁边的分类机制从「文本推断」换成「结构透传」，字段语义不变。

## 3. 方案（模块图 + 数据流）

```
 ┌─ GUI 壳（零改动）──────────┐   ┌─ CLI 壳（零改动）────────────────────────┐
 │ mainwidget → mediaservice  │   │ 旧 7 动词薄壳: argv→组请求→execute→OK/FAIL │
 └────────────┬───────────────┘   │ --json: stdin→execute→stdout 单 JSON      │
              ▼ worker 槽          └──────────────┬───────────────────────────┘
        ffextractor runRequest（零改动，片 D 已就位）
              │ QJsonObject 请求 + Ctx{cancel, onProgress}
              ▼
      ╔═══════════════════════════════════════════════════════╗
      ║ CommandLayer::execute(request, ctx)                     ║
      ║  ① 信封校验（原样：cmd→v→未知字段→input/output 防线）      ║
      ║  ② 查表：engineTable()（5 转码族动词）→ 内置表（probe/     ║
      ║     extract 族 3 个）→ 未命中 E_PARAMS「未知命令：%1」      ║
      ║  ③ 引擎路径：validateParams（元数据驱动，消息模板逐字兼容）  ║
      ║     → semanticCheck → model 存在性 → create(工厂闭包)     ║
      ║     → runTranscode（lockCanonical/后缀由条目供给）          ║
      ╚═══════════════┬═════════════════════════════════════════╝
                      ▼
        engineregistry.cpp（中央静态表，D1/D5）
        条目 = {id, ParamSpec[], requiresModel, setupErrorClass,
                lockCanonical, outputSuffix, semanticCheck, create}
                      │ create → 五引擎（setup 异构封在闭包内）
                      ▼
        FFTranscoder::transcodeStreamWav(…, ErrInfo*（片 B）)   FFExtractor::extract{Audio,Video}
                      ▼
        QJsonObject 响应（契约不变；error.code 由 ErrInfo 结构化透传，片 B 后无前缀匹配）
```

正例数据流（denoise）：请求 → 信封校验 → 条目命中 → validateParams（humBase/wind 逐项）→ semanticCheck → probeSourceSpec → create（HumBase 映射 + DspDenoiser::setup）→ runTranscode → `{"ok":true,…,result{output/sourceSpec/outputSpec/engineResampled/frames/durationSec}}`。

失败流（片 B 后）：底座/引擎错误点产生 `ErrInfo{code,message}` → 命令层拆包 → `{"ok":false,"error":{"code":"E_*","message":…}}`——文案与片 A 前完全一致（除 OQ-4 修正的三处 E_INTERNAL→E_FILE 承载面）。

## 4. 改动面（file:line 级预估，基于 HEAD 0a1cf8c）

| 文件 | 片 | 改动 | 预估规模 |
|---|---|---|---|
| **新建** `src/service/engineregistry.h` | A | `ParamSpec`/`EngineEntry`/`engineTable()`/`validateParams` 声明 | ~70 行 |
| **新建** `src/service/engineregistry.cpp` | A | 5 条目 + 通用校验器（模板逐字兼容）+ 工厂闭包（含 transcode 直通、separate 双 stem 后缀、denoise off+off 语义） | ~200 行 |
| **新建** `src/service/errinfo.h` | B | `ErrCode` 枚举 + `ErrInfo` 结构（零依赖叶子头） | ~20 行 |
| `src/service/commandlayer.cpp` | A | 删 `verbFields`/`verbAllowsOutput`/`kVerbs`（:80-88,313-317）与 5 个 cmd 处理器（:170-255，逻辑入条目）+ execute 尾部改双查表 + `outputOr` 后缀参数化；保留信封段/cmdProbe/cmdExtract*/runTranscode/transcodeResult | 净变化约 ±0（删处理器换表调用） |
| `src/service/commandlayer.cpp` | B | 删 `isFileError`/`classifyTranscodeError`/`classifyExtractError`（:29-63）+ 错误码改取 ErrInfo | −50 行 |
| `src/service/streamengine.h:7,16` | B | 注释补结构化错误约定；`lastError()` 返回 `ErrInfo`（默认实现返回空） | ~4 行 |
| `src/service/aidenoiser.cpp`（:328 lastError、:344/:355 错误点） | B | ErrInfo 化，推理异常 → Engine | ~8 行 |
| `src/service/vocalseparator.cpp`（:217,222,230 契约三文案；:237 推理；:446-447 太短；:413 lastError） | B | ErrInfo 化：契约三文案 → **Model**（P1-1 保真）、其余 → Engine | ~10 行 |
| `src/service/fftranscoder.h:26-29` / `.cpp`（十余错误点 :30-107,165-184,241,250,310 + lastError 查询 :230,290） | B | `err` 出参 `ErrInfo*`，错误点逐一赋码（映射表见 §6） | ~30 行 |
| `src/service/ffextractor.h:39-44` / `.cpp`（extract 两静态 + finish） | B | err 出参 `ErrInfo*`（**唯一触碰本文件的一行级改动**；runRequest 与七槽零改动） | ~8 行 |
| `CMakeLists.txt:14-32` | A | engine 源列表 + `engineregistry.{h,cpp}`；B 片 + `errinfo.h` | 3 行 |
| `CLAUDE.md` 架构节 | A/B | `engineregistry`/`errinfo` 一行 + 错误通道约定一句（行为/接口变化同步文档） | 2 行 |

**不动的（两片合计）**：`main.cpp`、`mediaservice.{h,cpp}`、`mainwidget.{h,cpp}`、`ffextractor` 七槽与 runRequest、四引擎头文件与算法体、`data.h`、`engine_test`、`fft_test`、全部 parity 脚本与 ci-smoke、wp-1.0 runbook。

## 5. 留缝清单（本包不做，后续包的接口锚点）

| 缝 | 本包怎么留 | 不动什么 |
|---|---|---|
| **FR-9 管线串联** | 条目即「可枚举的处理单元」（id + 参数 schema + 输出规格策略），管线包在此之上加 `pipeline` 动词与级间规格协商（每段 inRate/inChannels 不同，链上逐段重采样） | 不做条目组合校验、不做链式执行器——级间协商是完整工作包（fr24 §5.2 已论证） |
| **FR-7 RVC 变声引擎** | 新引擎 = `rvcengine.{h,cpp}` + 表条目（params 如 model/索引/音高偏移进 ParamSpec）+ CMake；GUI 按钮/CLI 薄壳是壳自己的增量，命令层零改 | 不预建 RVC 参数枚举、不预造占位条目 |
| **FR-23 插件（v2）** | 表是编译期数据、查表无全局可变态；v2 若需运行期注册，在查表层并联一张运行期表即可，分派与校验复用 | 不做 registerEngine API/dso 加载 |
| **FR-A4 工作区** | `outputOr` 缺省命名保持命令层单点（片 A 仅后缀参数化），工作区包照旧替换该函数接管落盘 | 不做版本化/哈希登记 |
| **NFR-L2 `processInto`** | 注册表只管「构造与参数」，不触碰 process/flush 缓冲契约 | 实时化包统一动接口 |
| **FR-8 模型管理器** | `requiresModel` 是布尔 + 存在性检查；管理器落地时把「存在性检查 + 缺省路径」升级为按 id 查库，条目可扩 `modelKind` 字段（纯增量） | 不做下载/镜像 |

## 6. 验证方案（目录 `docs/acceptance/nfr-m1-engine-registry/`，脚本风格对齐 run-json/run_parity）

**目录骨架**：

- `goal.md`：总目标（注册表 + P3-1 双硬目标）+ sub goal matrix（SG-1 片 A 注册表 / SG-2 片 B 错误通道）+ 重大决策节 + 关键信息节（模板同 fr24 goal.md）。
- `plan.md`：需求锚点表 + 决策摘要（指向本 spec）+ 改动面表 + 验证设计。
- `matrix.csv`：case × round 总表（下方用例族编号进表）。
- `round-1.md`（片 A）/ `round-2.md`（片 B）：PASS 证据；`scripts/`：`fake-engine-e2e.sh`（REG 族）+ 复用 fr24 的 run-json.sh / thinshell-eqv.sh / gui-slot-eqv.sh（交叉引用，不复制）。
- `report.md`：两片全绿后写。

**用例族**：

1. **REG 族（NFR-M1 金标准，片 A）**——在验证分支（不入 main）新增假引擎 `gain`（ParamSpec：gainDb 数字必填 [-24,24] 拒 0；直通跟随源；后缀 `_gain`；实现为样本乘 10^(dB/20) 的 ~40 行引擎）+ 一个条目 + CMake 两行：
   - REG-001 **改动面白名单断言**：`git diff --stat main -- src/` 仅允许 `src/service/gainengine.{h,cpp}`、`src/service/engineregistry.cpp`、`CMakeLists.txt` 三处——**commandlayer/ffextractor/mediaservice/mainwidget/main/streamengine 零改动即金标准达成**；
   - REG-002 假动词 e2e 正例：`--json {"cmd":"gain","input":…,"gainDb":6}` → ok:true + result 全字段（outputSpec 跟随源、engineResampled=false）+ 输出 RMS 比值 ≈ 10^(6/20) 断言；
   - REG-003..007 负例：gainDb 缺失/类型错（"abc"）/越界（30）/为 0/未知字段（bogus）→ E_PARAMS 且**消息模板与真引擎同构**；模型无关动词不受 requiresModel 误扰；
   - REG-008 未注册动词 → E_PARAMS「未知命令：%1」且响应不回显 cmd（CMD-021 语义在新分发路径下保持）。
2. **CMD 族（两片各全量重跑）**：fr24 run-json 36 条全绿零漂移（片 A 重点：CMD-020..028/031/033/034/035 校验文案逐字；片 B 重点：CMD-029/030/031/032/032f 错误码与文案）+ CMD-EQV 薄壳等价 + GUI-EQV 槽级等价复跑（main.cpp/ffextractor 零改动则等价应逐字成立，跑出证据而非推断）。
3. **ERR 族（片 B）——错误码迁移对照表**：穷举底座错误点与引擎错误点的「旧分类（前缀/调用点归类）→ 新 ErrCode」映射（§2-D4 第 3 条为基准），逐行核签进 round-2.md；e2e 可构造的（找不到模型/坏模型/无音频流/取消）以负例断言，不可构造的（写包失败/写尾失败/重采样器起不来/内存不够）以代码级证据 + 映射表收口（P1-1 同方法论）；OQ-4 的三处承载面修正（送包解码失败/写包失败/写文件尾失败 E_INTERNAL→E_FILE）单独列行为差异行。
4. **PARITY 族（片 B）**：engine-parity 四族全量重跑（引擎接口改型不得影响数值路径——错误点改动应使 parity 输出逐字节不变）+ engine_test 分块不变性 + ci-smoke 全量（含 run-json/体检）。

**门禁接线**：REG 族的假引擎分支不进 ci-smoke（分支证据制）；CMD/ERR 依赖的 run-json 已在 ci-smoke 内，零接线成本。Windows 真机复验随 WP-1.0 round-4（fr24 同款挂账口径）。

## 7. 开放问题（≤4，主线程拍板）

- **OQ-1 假引擎验收载体**：候选 (a) 验证分支 + diff 白名单断言（证据进 round-N.md，不入 main）；(b) 常驻测试引擎（testOnly 条目随产品码入库，加构建开关）；(c) 独立测试宿主可执行文件动态拼表。**倾向 (a)**：(b) 让产品码背测试负载且「关闭时如何证明关得干净」成新问题，(c) 与中央表设计冲突（表在 engine 库内，宿主拼表即改表）。分支证据 + 白名单断言已足以机械化验收 NFR-M1。
- **OQ-2 前缀匹配退役节奏**：候选 (a) 片 B 硬切一刀（全 in-tree 调用点同包更新）；(b) 保留双轨过渡（ErrInfo 优先、文本前缀兜底一版再删）。**倾向 (a)**：(b) 使 P3-1 名义收口实际未收（兜底路径无测试覆盖即死代码，或被隐性依赖即活脆点）；无外部实现者，双轨无服务对象。
- **OQ-3 transcode 入表与否**：候选 (a) 入表为直通条目（D3 所取）；(b) 留内置特例处理器。**倾向 (a)**：(b) 在 execute/verbFields 保留硬编码特判，「分发层重写为零」打折；直通=无滤波 DspDenoiser 本就是现实现（commandlayer.cpp:173-175），入表只是把这个事实放进数据。
- **OQ-4 P3-1 承载面修正范围**：候选 (a) 仅消灭双源（前缀匹配退役），分类逐条保持现状（送包解码失败等仍 E_INTERNAL，行为零变化）；(b) 随结构化一并修正语义（送包解码失败/写包失败/写文件尾失败 → E_FILE，内存不够/重采样器起不来 → E_INTERNAL）。**倾向 (b)**：P3-1 原文即指「报 E_INTERNAL 有误导」；三处修正均非 CMD 断言路径、无 e2e 回归风险，映射表单列行为差异即可透明收口；(a) 把已知误导留给 Agent 包再返工。

### 7.1 主线程拍板（2026-11-03，开工前）

- **OQ-1 → (a) 验证分支 + diff 白名单断言**，落地形态改为**临时 detached worktree**（单 `main` 分支红线不动）：worktree 内以未跟踪文件 + 两处修改的形态注入假引擎，`git status`/`git diff` 即白名单证据，build+e2e 后整树丢弃——与 thinshell/gui-slot-eqv 的基线 worktree 方法论同构，main 历史零痕迹。(b) 让产品码背测试负载且引入「关得干不干净」的新证明问题，(c) 与中央表设计冲突。
- **OQ-2 → (a) 片 B 硬切一刀**。全 in-tree、无外部实现者；双轨的唯一效果是让前缀匹配以「兜底」名义活下去，P3-1 名义收口实际未收。
- **OQ-3 → (a) transcode 入表为直通条目**。现实现本就是「无滤波 DspDenoiser」，入表只是把这个事实放进数据；留特例则 execute/verbFields 硬编码残留，「分发层重写为零」打折。
- **OQ-4 → (b) 随结构化一并修正承载面**。P3-1 原文即指「送包解码失败（源损坏）报 E_INTERNAL 有误导」；三处修正均非 CMD 断言路径（CMD-029/030 只断「打不开/没有音频流」），e2e 零回归风险，ERR 映射表单列行为差异行透明收口；内存不够/重采样器起不来保持 E_INTERNAL。

## 8. 红线自查与实现风险

**红线自查**：不引任何新第三方依赖（全部 Qt Core + 既有引擎/底座）；流式与内存有界零触碰（注册表只过小对象与工厂指针，转码/提取通路与引擎缓冲契约一行不动，NFR-P4 维持）；原件只读防线（output==input 校验在信封段，原样保留）；LGPL 组合零变化；`third_party/` 与模型不入库（假引擎是源码测试资产，不含权重）；注释与校验文案中文（消息模板逐字兼容本身即锁死中文文案）。

**实现风险**：
1. **校验消息模板兼容是最大脆点**：36 条 CMD 中十余条断言具体文案，通用渲染器（顿号+「或」的枚举拼接、`[-12, 12]` 的区间格式）任何一处格式化差异即回归。缓解：模板表随片 A 实现进 round-1（每条模板旁注对应 CMD 用例编号），CMD 全量重跑零漂移为硬门禁。
2. **片 B 引擎接口改型触及 parity 敏感文件**：vocalseparator/aidenoiser 是 parity 四族之二。缓解：改动仅限错误赋值点（runModel 三处 + 推理 catch + flush 太短），数值路径零触碰；parity 四族 + engine_test 分块不变性全量重跑，输出应逐字节不变。
3. **工厂闭包吞掉 setup 异构性的边界**：闭包内类型化抽取假定输入已过 schema（如 humBase 必为合法枚举串），若条目 schema 与闭包抽取不一致，错误会以「内部异常」而非 E_PARAMS 暴露。缓解：条目与闭包同居一文件一屏内（engineregistry.cpp），REG 族假引擎用例覆盖每种 ParamSpec Kind 的正/负例，新增 Kind 时校验器与模板同文件同步。
4. **GUI 等价依赖「文案逐字」的隐式耦合**：片 B 改 ffextractor 静态签名（唯一触碰点），若 ErrInfo 迁移中任何 message 文案被顺手润色，GUI-EQV 即红。缓解：gui-slot-eqv 复跑 + 迁移对照表把「文案不变」列为逐行核对项；修复仅限 code 字段，message 原样搬运。
