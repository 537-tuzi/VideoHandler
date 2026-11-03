# FR-8 模型管理器（开工 spec）

- 日期：2026-11-03
- 性质：开工前快照（M2 第四个工作包，依赖序承接 FR-A4；进展追踪见 `docs/acceptance/fr8-model-management/goal.md`——实现开工时才建目录）
- 本文 D 编号（D1–D9）仅限本 spec，与 REQUIREMENTS.md 决策记录（D1–D7）无关。
- 现状基线：HEAD `e579a50`（FR-24 命令层 + NFR-M1 注册表/ErrInfo + FR-A4 工作区已全落地）。
- 本包不含 FR-4 质量档（DFN3/FastEnhancer）本体——那是独立工作包；本包只提供其模型落地后的目录登记位。

## 0. 需求锚点与约束

| 锚点 | 内容 | 对本包的约束 |
|---|---|---|
| FR-8（REQUIREMENTS.md:93） | 首次使用下载（HF/镜像可配）、本地列表、从文件导入；**自研纯 HTTP 下载器**（2026-10-14 反证：HF 官方现行无跨运行断点续传，Xet 临时文件进程唯一）——基于 resolve 契约：Range 续传 + sha256 校验已实测支持；⚠ Xet 仓库 CAS 流量绕过 HF_ENDPOINT，镜像兼容按 resolve 契约逐镜像实测 | 三能力面逐一落决策：下载器（D3/D4）、列表（D2/D5）、导入（D6）；断点续传是自研责任，不得以「对齐 HF 官方」为依据 |
| NFR-C4（REQUIREMENTS.md:196）/ R4（:249） | 模型下载源可配置镜像（HF + hf-mirror） | 镜像切换 = resolve URL 前缀替换（research-model-mgmt-llm-infra.md §1.2 源码级契约）；配置承载面见 D4（注意 CLAUDE.md 工作原则 6「参数归参数」） |
| NFR-R4（REQUIREMENTS.md:181） | 模型缺失/损坏 → 引导重新下载，不崩溃 | 现状仅报错（E_MODEL「找不到模型 %1」，commandlayer.cpp:175-179）；「引导」的兑现点 = D8 首次使用引导 |
| §6 红线表（REQUIREMENTS.md:217-222） | GTCRN=MIT（倾向）；UVR-MDX=生态惯例免费再分发**须标注来源**；社区 RVC 模型**不内置**、用户导入+使用须知；Qwen 逐仓库核对 LICENSE 原文（HF 元数据标签不可尽信） | 内置目录条目必带 license 字段（D2）；导入的许可提示义务（D6）；v1 无 RVC/Qwen 条目，但自由导入是它们的前门 |
| v1-development-plan.md:38-39 | M2 依赖序：FR-24 → NFR-M1 → FR-A4 → **FR-8** → FR-9；「FR-A4 落地后一切新功能必须走工作区，不得改写源文件」 | 模型动词与工作区的关系须显式裁定（D7）；CLAUDE.md 红线句「新功能的落盘动词应提供工作区模式」的偏离裁定 → OQ-8 |
| fr24 spec :85（留缝） | 「`model` 必填是刻意收紧……把模型路径解析策略上移给调用方薄壳……**FR-8 模型管理器落地时在此点接管**」 | 接管方式：壳解析升级（模型库→appDir 两级缺省），命令层 model=绝对路径契约零改（D1），不破坏 CMD 回归面 |
| fr-a4 spec :215（留缝） | 「版本记录 model 摘要（路径+size+mtime）字段可平滑升级为模型 id + sha256（管理器职责）」 | 本包不升级该字段（零漂移优先），登记入留缝（§5） |
| CLAUDE.md 红线 | `third_party/` 与模型权重永不入库；不引 GPL；模型许可证逐个核验 | 目录表只含 URL/sha256/许可证字符串常量；Qt Network 属 Qt 全家 LGPL（无新第三方库） |
| 调研证据 | research-model-mgmt-llm-infra.md §1（HF 下载规范/镜像机制/桌面软件布局对照）+ 可借鉴点（:193-197）；falsify-hf-llm.md 判级表（1a 被推翻/1b 站得住/1c 站得住/1d 需修正）+ §5 限速 | 下载器骨架 = resolve URL + Range + sha256 + `.part` 临时文件（实测背书）；Xet CAS 绕 HF_ENDPOINT、hf-mirror 对海外出口 308 回源（CN 视角 vantage 盲点）两条注记进 D3/D4 与 §8 风险 |

## 1. 现状盘点（证据，HEAD e579a50 实读）

**模型路径解析三套不同源（fr24 spec :85 指认的「接管点」）**：

- GUI 壳：appDir 相对（`QCoreApplication::applicationDirPath() + "/" + kAiModelRelPath`，ffextractor.cpp:349；separate 同型 :372），缺模型前置检查 → `jobDone(false, "找不到 AI 降噪模型：<路径>")`（:350-353；separate :373-376）——该文案被 GUI-EQV 断言逐字锁定。
- CLI 薄壳：third_party 相对 CWD（main.cpp:96-97、:127-128）。
- 命令层：`model` 必填字符串 + 存在性检查 → E_MODEL（commandlayer.cpp:175-179，由条目 `requiresModel` 驱动，engineregistry.h:36）。
- 常量：data.h:12 `models/denoiser/gtcrn_simple.onnx`、:16 `models/separator/UVR-MDX-NET-Voc_FT.onnx`；Windows 侧两模型随包分发（README.md:52、:63-64、:77 POST_BUILD 自包含）；Linux CI 走 `fetch-deps.sh`（scripts/fetch-deps.sh:14-19 URL/sha256 矩阵，curl `--retry 3` + sha256sum 校验 + `.part` 中转 :36-38——与自研下载器同款骨架的脚本先例）。

**模型装载校验已完整存在于引擎 setup（导入校验的复用面）**：

- GTCRN：`AiDenoiser::setup(modelPath, err)` 读 ONNX metadata（sample_rate/n_fft/hop/window_type，aidenoiser.cpp:251-256），失败置 err——setup 即完整校验器。
- MDX：`VocalSeparator::setup` 按「文件末 10000KiB MD5 查内置 UVR 官方表」取参，查不到拒绝（vocalseparator.cpp:337-345；表本体 :20-26）+ 输出契约校验（WP-1.4）。
- 即：**导入校验零新增引擎代码**——setup 试开就是权威校验（D6）。

**命令面接面现状**：

- `execute` 信封段对**一切动词** input 必填（commandlayer.cpp:342-344）——modelList/modelDownload 无输入语义，信封段需豁免开关（本包唯一触碰信封公共校验的改动，零漂移硬门禁见 §6）。
- 内置动词表 `BuiltinVerb{id, handler, allowsOutput, allowsWorkspace, extraKeys}`（commandlayer.cpp:263-269）现有四条：probe/extractAudio/extractVideo/wsInfo（:271-279）——模型动词进此表（非引擎表：无 IStreamEngine 参与）。
- 错误码六类封闭枚举（errinfo.h:10-18，`errCodeText` 是串值唯一来源 :34-46）——**无网络错误类**（DNS/连接拒绝/HTTP 4xx5xx/sha256 不符现无处安放），扩展要拍板 → OQ-1。
- engine 库只链 Qt Core + Widgets（CMakeLists.txt:11）——Qt Network 是新启用组件（仍在 qtbase/LGPL 内，无新第三方库）。
- CLI 模式无 QCoreApplication（main.cpp:146「CLI 模式无需 QApplication」）——QNAM 异步模型需要事件循环与应用实例，壳须最小补（§4）。

**GUI/服务现状**：

- busy 锁一次一任务（mediaservice.h:23-24）；七对业务信号槽；probe/wsInfo「只读不占 busy」先例（mediaservice.cpp:55-61）——模型查询/下载的占锁语义照此分型（D5）。
- QSettings 现两键：`humBaseHz`（mainwidget.cpp:57）、`workspaceRoot`（:70-73）；「QSettings 只属于 GUI 壳」边界（fr-a4 D7）——镜像/模型库根配置同型处理（D4）。
- `onJobDone` 对「已取消」有文案特判（mainwidget.cpp:352）——失败引导不能走文案前缀匹配（NFR-M1 片 B P3-1 前缀匹配已删的教训），须结构化信号（D8）。

**回归面（动信封段前必读，全部带 input 且不涉模型动词）**：CMD 36 + CMD-EQV 薄壳等价 + GUI-EQV 槽级等价（`docs/acceptance/fr24-command-layer/matrix.csv`，CMD-021r 全文逐字节比对是现成断言）+ WS 族（fr-a4）+ ci-smoke（wp-1.2）+ PAR-SR 三调用点。本包以「带 input 的既有动词响应逐字节不变」为硬门禁。

## 2. 设计决策

### D1 存储布局：用户目录集中模型库（按用途分目录 + models.json 清单）+ 内置随包模型不迁移；缺省解析两级（库 → appDir）

**候选**：
- (a) 沿用 appDir `bin/models/`，下载也进 appDir；
- (b) 用户目录集中模型库 `<根>/<用途>/<文件>` + 根级 `models.json` 清单（QSaveFile 原子写），内置随包模型原地不动、以 `source="bundled"` 并列展示；✅
- (c) HF 式 blobs/snapshots 内容寻址 / LM Studio 式 publisher/model 树。

**取舍**：(a) 否决——Windows 安装到 Program Files 时 appDir 不可写（UAC 虚拟化陷阱），「首次下载」在正式安装形态下直接失败；绿色解压版可行但不可据以设计。(c) 否决——调研原文对 4–6 个文件的规模判「过度设计，不建议」（research-model-mgmt-llm-infra.md:195）；内容寻址的收益（同 blob 去重）在两三个模型时为零。
**(b) 布局示意**：

```
<模型库根>/                        # QSettings "modelRoot"（GUI 壳读写），缺省 ~/VideoHandler-Models（OQ-3）
  models.json                      # 本地已装清单（权威索引）：[{id, for, path, sha256, sizeBytes, license, source, time}]
                                   # 损坏按空处理（可重建：以磁盘扫描+内置目录对照），QSaveFile 原子写
  denoiser/gtcrn_simple.onnx       # 按用途分目录——沿 data.h 既有两级布局，bundled 命名对齐
  separator/UVR-MDX-NET-Voc_FT.onnx
  separator/xxx.onnx.part          # 下载/导入在途临时文件（同卷 rename 原子落位）
```

- **内置随包模型不迁移不复制**：随包分发是既定产品行为（README.md:77），迁移破 Windows 自包含形态与已验收 CLI 冒烟（ci-smoke 用 third_party 路径，fetch-deps.sh 零改）。modelList 把 bundled 条目并列列出（path=appDir 实测路径、source="bundled"）；其「已装」判定 = 存在性 + size 匹配（bundled 不逐次算 sha256——每次全量读 20–70MB 不可取；损坏检测交给引擎 setup 的 E_MODEL 路径，运行期发现 → D8 引导重下，正合 NFR-R4「缺失/损坏→引导」语义）。
- **引擎动词缺省解析升级（fr24 :85 接管点的兑现）**：壳（GUI doDenoiseAiWav/doSepVocals、CLI 薄壳不动照旧）从「appDir 相对」升级为「模型库根该用途已装模型 → appDir bundled」两级。v1 每用途一模型无歧义；FR-4 质量档带来多模型时再引入显式档位选择（§5 留缝）。命令层 `model` 字段仍收绝对路径——契约零改，CMD 回归面不动。
- **modelImport 复制进库、绝不移动**：原件只读（模型库自持：用户删原文件不影响已导入模型；与工作区「产物自持」同哲学）。

### D2 内置模型目录：编译期静态条目表（id/用途/URL/sha256/许可证/尺寸）；引擎关联单向声明（条目 for=动词，注册表零改）

**候选（「可下载什么」的知识放哪）**：
- (a) 编译期内置 C++ 静态表（modelmanager.cpp 匿名命名空间，与 vocalseparator 内置 UVR 参数表同型先例 vocalseparator.cpp:20-26）；✅
- (b) 随包运行期清单文件（models-catalog.json，可随更新热替换）；
- (c) 远端目录（appcast 风格在线拉取）。

**取 (a)**：v1 条目就 2 个（GTCRN/UVR），FR-4 选型后 +1~2，RVC/ASR/VAD/LLM 各归其工作包逐个加——**目录变更 = 发版**，无「运营加模型不发版」的需求（工作原则 5）；(b) 引入「清单文件丢失/被改」新失败面与两份事实源；(c) 踩遥测边缘（NFR-C1）且 v1 无消费者。条目示意：

```cpp
struct CatalogEntry {
    QString id;        // "gtcrn" / "uvr-mdx-voc-ft"（ASCII 小写连字符，命令面引用键）
    QString name;      // 显示名（中文）
    QString forVerb;   // 关联引擎动词："aiDenoise" / "separate"（单向声明，见下）
    QString relPath;   // 库内相对路径 "denoiser/gtcrn_simple.onnx"
    bool hfSource;     // true=HF resolve 契约（endpoint 可换前缀）；false=直连 URL（镜像不适用）
    QString hfRepoPath;// hfSource 时：<repo>/resolve/<rev>/<file>（endpoint + 此段拼 URL）
    QString directUrl; // 非 HF 源完整 URL（GTCRN 走 GitHub release，镜像字段对其无效——文档注记）
    QString sha256;    // 期望指纹（校验 + etag 对照）
    QString license;   // "MIT（倾向，发版前官方书面确认）" / "UVR 生态：免费再分发，须标注来源"
    qint64 sizeBytes;  // 展示与磁盘占用预估
};
```

- **sha256/URL 单一事实源对齐 fetch-deps.sh:14-19**（同一组值迁移进表，脚本与表并存期各自维护、值相同；脚本属 CI 域不动）。
- **引擎关联单向声明**：条目 `forVerb` 指向引擎表动词，`EngineEntry.requiresModel`（engineregistry.h:36）零改；开发期 Q_ASSERT 校验 forVerb ∈ {requiresModel 动词集}（engineregistry.cpp:190-195 id 唯一性断言同型）。反向（引擎表声明可用模型清单）不做——那会让注册表条目依赖模型目录，两表耦合无收益。
- **modelList 合并三视角**：内置目录（可下载什么）∪ models.json（装了什么）∪ 磁盘实况（QFileInfo::exists 现场验证——清单有条目但文件没了 → `status:"stale"`，不静默当已装；models.json 不是真理只是缓存性登记）。

### D3 下载器：QNetworkAccessManager + resolve 契约（跟随重定向）+ Range 跨运行续传 + sha256 完整性 + `.part`→rename 原子落位

**传输栈候选**：QNAM（Qt 自带，LGPL 内）✅ / libcurl（新第三方依赖 + 分发面）/ 自研 socket（重新发明 TLS，不可）。取 QNAM；代价 = engine 库启用 Qt6::Network + CLI 壳补 QCoreApplication（QNAM 异步模型需事件循环，§4）。

**流程（五步）**：
1. **HEAD 解析**：GET/HEAD `{endpoint 或 directUrl}` 跟随重定向（`QNetworkRequest::setRedirectPolicy(NoLessSafeRedirectPolicy)` 显式设置；Xet 仓 resolve 302 → CAS bridge CDN 是纯 HTTP，`accept-ranges: bytes`、etag==sha256 实测成立，falsify-hf-llm.md:41-43）。取最终 URL + Content-Length + ETag。
2. **指纹对照**：ETag（LFS/Xet 文件即 sha256）与目录条目 sha256 不一致 → 远端文件与目录不符（条目过期或被改）→ 拒绝下载报错（错误类随 OQ-1）。git 普通小文件的 ETag 是 sha1——本包只下权重文件（皆 LFS/Xet），契约成立；注记防将来误用于小文件。
3. **Range 续传**：`.part` 在途文件存在 → 取其大小 N → 请求 `Range: bytes=N-`；服务器回 200（不支持 Range / 文件已变）→ 弃 `.part` 从头重写（HF 历史实现同款处理，research §1.1）。**跨运行续传是本包与 HF 官方现行实现的差异点也是卖点**（HF 已不做，issue #4196 open；falsify-hf-llm.md:22-29）。
4. **增量写盘 + 取消**：readyRead 增量 append `.part`（64KiB 级缓冲，内存有界）；块间检查 `Ctx.cancel` → abort + E_CANCELLED，**`.part` 保留**（下次续传；与 NFR-R1「失败删半成品」的关系裁定 → OQ-6——`.part` 是下载缓存非任务产物）。连接级失败（非 HTTP 4xx/5xx）自动重试至多 2 次（fetch-deps.sh:36 `--retry 3` 同款；每次从 `.part` 续传天然增量）；4xx/5xx 不重试，429 给「稍后再试或换镜像」文案（匿名 Resolvers 3,000/5min 配额本身余量巨大，共享出口 IP 才会撞，falsify-hf-llm.md:143-150）。超时 `setTransferTimeout`。
5. **完成收尾**：流式 sha256 全量校验（QCryptographicHash 64KiB，workspace 同款）→ 匹配 → 同卷 `rename` 到最终名（原子落位；QSaveFile 是整写提交模型不适用增量下载，`.part`+rename 等价达成）→ QSaveFile 原子更新 models.json。**sha256 不符 → 删 `.part` 报错**（内容坏，续传无意义——与「取消/断流保留」区分）。

**并发**：单模型单连接顺序拉（v1）。分段并发（aria2 `-x4` 式）与多模型并行不做（OQ-4）——20–70MB 规模单连接够，GB 级 Qwen 是 v1.x 的事，届时再评。

**CLI/GUI 同一通路**：下载器是同步函数（局部 QEventLoop 驱动 QNAM，块间检查取消），命令层 `execute` 单发同步契约不变；GUI 进度经 `Ctx.onProgress`（received/total），CLI `--json` 无进度输出（fr24 OQ-2 同款取舍：stdout 纯 JSON 硬契约）。

### D4 镜像与来源配置：请求字段 `endpoint`（参数归参数）+ GUI QSettings 承载缺省；不读 HF_ENDPOINT 环境变量

**候选**：
- (a) 请求字段 `endpoint`（可选字符串，缺省 `https://huggingface.co`）；GUI 壳从 QSettings 读缺省注入；CLI 调用方显式传；✅
- (b) 兼容读 `HF_ENDPOINT` 环境变量（Python 生态用户习惯）；
- (c) 只做 GUI 设置不进请求面（命令层写死读 QSettings——违 fr24 D2「命令层无环境依赖」）。

**取 (a)**：与 workspace root 的承载完全同型（fr-a4 D7：QSettings 只属于 GUI 壳，命令层状态从请求进）；(b) 违工作原则 6（参数归参数，不混环境变量）且形成双通道漂移——HF_ENDPOINT 是 huggingface_hub 生态的进程级约定，我们是单请求命令面，语义不同；(c) 直接违既有架构原则。**镜像只对 `hfSource=true` 条目生效**（前缀替换）；GTCRN 走 GitHub release 直连，文档与 UI 注记「该源不受镜像影响」。
**镜像兼容性的诚实边界**（NFR-C4 注记原文继承）：Xet 命中时 CAS 流量绕过 HF_ENDPOINT（换前缀 ≠ 全部流量走镜像）；hf-mirror 对海外出口 308 回源、CN 视角未实测（falsify-hf-llm.md:57-58 vantage 盲点）——自研纯 HTTP resolve+Range 路线本身实测站得住（:41-43），逐镜像人工实测为准，CI 只钉协议层不测真网（§6）。

### D5 命令面：三个内置动词 modelList / modelDownload / modelImport；信封 input 必填豁免；E_NET 新错误类（OQ-1 拍板）；查询不占 busy、下载/导入占

**动词契约**（进 builtinVerbs 表，与 probe/wsInfo 同型）：

| cmd | 字段 | 语义 | result（示意） |
|---|---|---|---|
| `modelList` | `root?` | 只读查询（**不落盘、不建目录**——wsInfo 同款纪律）；合并内置目录 ∪ models.json ∪ 磁盘实况 | `{root, models:[{id,name,for,status:installed/available/stale,bundled,path?,sizeBytes,sha256,license}]}` |
| `modelDownload` | `id*`, `root?`, `endpoint?` | 同步下载→校验→落位→登记；**幂等**：库或 appDir 已在位且指纹/大小符合 → ok 返回现路径不重复下载 | `{id,path,sha256,sizeBytes}` |
| `modelImport` | `path*`, `root?`, `for?` | 见 D6 | `{id,path,sha256,sizeBytes,license}` |

- **`root` 可选、缺省内置缺省根**（与 wsInfo 的 root 必填不对称，理由：工作区根是用户项目级选择须显式；模型库是应用级单一约定——Ollama/LM Studio 均有缺省模型目录，research §1.3）。缺省根让 CLI 裸调可用；显式传 root 覆盖（Agent/脚本多环境）。
- **信封豁免（唯一信封改动）**：`BuiltinVerb` 加 `requiresInput` 标志（四既有动词 true、modelList/modelDownload/modelImport false；引擎动词恒 true），commandlayer.cpp:342-344 公共校验按标志分叉。既有动词路径逐字节零漂移由 CMD-021r 复跑锁死（§6）。
- **错误码映射**：未知 id / for 非法 / path 空 / endpoint 类型错 → E_PARAMS；导入源不存在/不可读 → E_FILE；导入 setup 试开失败 → E_MODEL（setupErrorClass=Model 先例，engineregistry.cpp:163）；网络不可达/DNS/HTTP 4xx5xx/下载 sha256 不符/ETag 与清单不符 → **E_NET（新增，OQ-1）**；取消 → E_CANCELLED。不新增 E_NET 的代价：网络错误挤进 E_FILE（刚被 OQ-4 收窄为「源/输出侧文件与媒体 I/O」，语义浑）或 E_MODEL（会误导 NFR-R4 的「引导重下载」识别——断网不是模型坏了）。
- **busy 锁关系（GUI 侧）**：modelDownload/modelImport 是长任务 → 占 busy + 可取消 + 进度条（七槽同型）；modelList 快查询不占 busy（probe/wsInfo 先例，mediaservice.cpp:55-61）。CLI 单发无此问题。
- **不做**：`modelDelete`（FR-8 无删除需求；用户可在文件管理器删，modelList 以磁盘实况为准不残留假状态——清单 stale 语义已覆盖）。

### D6 导入：复制进库 + setup 试开（引擎既有校验器）+ sha256 记档；两形态（按 id 对指纹 / 自由导入带 for）；许可提示义务

**校验深度候选**：
- (a) 浅校验（存在 + 扩展名）；
- (b) sha256 匹配内置目录才收；
- (c) setup 试开 + sha256 记档；✅

**取 (c)**：setup 就是权威校验器（GTCRN metadata 断言 aidenoiser.cpp:251-256、MDX 官方表 + 输出契约 vocalseparator.cpp:337-345，WP-1.4 已把契约校验做进 setup）——零新增校验代码且比 sha256 匹配更强（能收目录外模型）。(a) 会把废件收进库；(b) 只收目录内模型，堵死社区模型前门（RVC M3 / Qwen v1.x 的导入入口就是本动词）。

**两形态**：
1. **按 id 导入**（`id` 给定）：sha256 对上目录条目 → 按条目登记（license 取条目值）；对不上 → 报错提示指纹不符（防「同名不同内容」混入）。
2. **自由导入**（无 id、`for` 给定）：用该动词注册表条目的 `create` 闭包对**源文件路径**试开（engineregistry.cpp:55-59 / :67-73 的 create 现成可用）→ 通过 → 复制进库（`.part` 中转 + rename，D3 同款原子性）→ 登记（id = stem 净化、**license = "unknown（用户自行核验）"**）。既无 id 又无 for → E_PARAMS（不猜用途——两个引擎 setup 语义不同，猜错是把废件当可用）。
- **原件只读**：复制不移动不改写；导入前后源文件 sha256 不变是验收断言（§6 MDL-IMPORT）。
- **许可提示义务**：modelList 恒带 license 字段（NFR-C2 THIRD-PARTY 清单的数据源；UVR「须标注来源」即在此兑现）；自由导入在 GUI 对话框展示一行提示「请确认您有权使用该模型（来源与许可证由您自行核验）」——通用版；RVC 专用使用须知（NFR-C5）M3 落地时按需追加。v1 无 RVC/Qwen 条目，不预写专用文案。

### D7 与工作区关系：模型是共享资产非任务产物——不进版本目录、不接 workspace 字段（红线句语义裁定 → OQ-8 追认）

- **排除的理由**：工作区版本目录是「一次任务的一组产物」（append-only 任务史）；模型是被多任务共享引用的资产——若进版本目录，同一模型每个「下载任务」各留一份拷贝，且任务引用模型要走跨工作区路径，机制拧巴。CLAUDE.md 红线句「新功能的落盘动词应提供工作区模式」的**意图是防覆写与可追溯**；模型动词以等效机制满足：新路径新建（目标名由目录条目预知，不覆写任何既有文件；在途仅 `.part`）+ models.json QSaveFile 原子写 + **清单登记失败 = 下载失败删产物**（与 workspace「索引写失败=任务失败」同构，fr-a4 D2）。
- **形式偏离须追认**：模型动词拒收 `workspace` 字段（与 probe/wsInfo 拒收同型，`allowsWorkspace=false`）——字面不符红线句，列 OQ-8 请主线程追认（或改判必须带 workspace——那会让模型库布局服从版本目录，本 spec 判断为误用）。
- **工作区侧零改**：版本记录的 model 摘要（path+size+mtime）不升级为 id+sha256（fr-a4 :215 留缝保持兑现但不此刻做——FR-A4 刚验收零漂移优先，无消费者需要 id；登记入 §5 留缝）。

### D8 GUI 面：模型管理对话框 + 缺模型结构化引导信号（不走文案前缀匹配）+ endpoint 下拉记忆

**候选（管理入口形态）**：
- (a) 独立「模型管理」按钮 → 非模态对话框：表格（名称/用途/状态/大小/许可证）+ 下载/导入/打开目录 + 对话框内进度条（下载占 busy，主进度条照旧联动）+ endpoint 下拉（huggingface.com / hf-mirror.com / 自定义）QSettings 记忆；✅
- (b) 每个引擎按钮旁内联下载入口；
- (c) 仅失败弹窗无常驻入口。

**取 (a)**：(b) 把七槽 UI 复杂化且 FR-4/FR-A6 多模型后无处放；(c) 无列表面（FR-8 明文「本地列表」）。**首次使用引导（NFR-R4 兑现）**：ffextractor 的 `runRequest` 拆响应时遇 `error.code=="E_MODEL"` → 除既有 `jobDone` 外**加发结构化信号 `modelMissing()`**（新信号对，wsInfo 同型增量；七对既有信号槽签名零改、失败文案逐字不动 → GUI-EQV 断言不破）；mainwidget 收到后弹 `QMessageBox::question`「模型不可用，是否打开模型管理下载？」→ 打开对话框。**不用文案前缀匹配**（P3-1 已删的形态回潮）。

### D9 分片：片 A = 清单+下载器+导入+命令面+验收基建（CLI 全量可验）；片 B = GUI

- **片 A**：modelmanager 核心 + 三个命令动词 + E_NET + 信封豁免 + CMake/main 壳 + `range_server.py`/`run-models.sh` 验收基建。modelImport 全部逻辑（复制/试开/登记）是命令层纯逻辑，无 GUI 依赖——放 A 使 B 只剩 UI 胶水，且 MDL 验收 round-1 全部 headless 可跑（与 fr24 片 C/D、fr-a4 片 A/B 同构）。
- **片 B**：模型管理对话框 + modelMissing 引导 + mediaservice/ffextractor/mainwidget 接线 + GUI 缺省解析两级升级（D1）。
- 两片独立验收、独立回滚点；片 A 单独交付即兑现 FR-8 机制本体（CLI 可全量验证），片 B 是消费面。

## 3. 方案（模块图 + 数据流）

```
 ┌─ GUI 壳（片 B）───────────────────────────┐   ┌─ CLI 壳 ─────────────────────────────┐
 │ mainwidget：模型管理对话框（列表/下载/导入/   │   │ --json:  {cmd:"modelDownload",       │
 │  endpoint 下拉→QSettings）；modelMissing     │   │          id:"gtcrn", endpoint?}     │
 │  引导问询；QSettings modelRoot/modelEndpoint │   │ （CLI 分支补 QCoreApplication：QNAM   │
 │ mediaservice：modelDownload 占 busy /        │   │   事件循环前提；旧 7 动词零改）        │
 │  modelList 不占 busy；modelMissing 转发      │   └──────────────┬──────────────────────┘
 └────────────┬───────────────────────────────┘                  │
              ▼ worker 槽（wsInfo 同型）                           │
      ╔══════════════════════════════════════════════════════════▼═══╗
      ║ CommandLayer::execute                                          ║
      ║  ① 信封校验（requiresInput 豁免模型动词；其余逐字节照旧）          ║
      ║  ② 双查表：引擎表 5 + 内置表 probe/extract×2/wsInfo              ║
      ║     + modelList/modelDownload/modelImport（新增）                ║
      ╚═════════════════════╤══════════════════════════════════════════╝
                              ▼
      modelmanager.{h,cpp}（新，engine 库，Qt Core+Network）
        内置目录静态表（CatalogEntry×2，fetch-deps sha256 同源）
        modelList：目录 ∪ models.json ∪ 磁盘实况 → 状态合并
        modelDownload：QNAM HEAD（重定向/etag 对照）→ Range 续 .part →
          增量写（块间查 cancel）→ 流式 sha256 → rename 落位 → QSaveFile 登记
        modelImport：create 闭包试开（engineregistry 复用）→ 复制（.part+rename）
          → 登记（license=条目值或 unknown）
                              ▼
      响应：result 或 error{code:E_NET/E_MODEL/E_FILE/E_PARAMS/E_CANCELLED}
```

正例数据流（GUI 首次 AI 降噪）：用户点「AI降噪」→ 库与 appDir 皆无模型 → E_MODEL → `modelMissing` 信号 → 问询 → 对话框 → `modelDownload{id:"gtcrn"}`（GUI 壳注入 QSettings 的 root/endpoint）→ 下载进度 → 落位登记 → 重试任务成功（版本记录 model 摘要 = 库内路径，来源链如实）。

失败流：断网 → QNAM 错误 → 重试 2 次（`.part` 续传）→ E_NET + 中文文案；取消 → E_CANCELLED，`.part` 保留；sha256 不符 → 删 `.part` + E_NET，库与清单无残迹。

## 4. 改动面（file:line 级预估，基于 HEAD e579a50）

| 文件 | 片 | 改动 | 预估规模 |
|---|---|---|---|
| **新建** `src/service/modelmanager.{h,cpp}` | A | 内置目录表 + models.json 读写 + QNAM 下载器（HEAD/Range/sha256/.part/重试）+ 导入（试开/复制/登记）；仅 QString/QJson/Qt Core+Network 依赖 | ~60 + ~430 行 |
| `src/service/commandlayer.cpp` | A | `BuiltinVerb` 加 `requiresInput`（:263-269）+ 公共校验分叉（:342-344）；三个 `cmdModel*` 处理器 + 内置表三条（:271-279） | ~130 行 |
| `src/service/errinfo.h` | A | `ErrCode::Net` + errCodeText 分支 + 头注释「六类」→「七类」（OQ-1 拍板后） | ~4 行 |
| `src/model/data.h` | A | 常量：models.json 文件名 / 缺省模型库根名 / 缺省 endpoint | ~4 行 |
| `src/main.cpp` | A | CLI 分支（`--json` 与旧动词）补 `QCoreApplication`（QNAM 前提；**不加**旧式模型动词薄壳——`--json` 直达，防 CLI 面膨胀） | ~6 行 |
| `CMakeLists.txt` | A | `find_package` 加 `Network`（:11）+ engine 链接 `Qt6::Network` + 源列表两行 | ~4 行 |
| `src/service/mediaservice.{h,cpp}` | B | modelDownload/modelImport 提交槽（占 busy）+ modelList 查询对（不占 busy）+ modelMissing 转发 | ~30 行 |
| `src/service/ffextractor.{h,cpp}` | B | doModel* 三槽 + 模型查询完成信号；`runRequest` 拆 E_MODEL 加发 `modelMissing`（:306-331 邻域）；doDenoiseAiWav/doSepVocals 缺省解析两级化（:349、:372） | ~55 行 |
| `src/view/mainwidget.{h,cpp}` | B | 「模型管理」按钮（wsRow :99-102 同型）+ 管理对话框（表格/进度/导入/endpoint 下拉+QSettings modelRoot/modelEndpoint）+ modelMissing 引导问询 | ~200 行 |
| `CLAUDE.md` | A/B | 架构段 modelmanager 一行 + `--json` 动词清单 + 模型动词不接 workspace 的裁定注记（OQ-8 后） | ~4 行 |
| `docs/acceptance/wp-1.2-linux-build/scripts/ci-smoke.sh` | A | 追加一行调用 `run-models.sh`（round-1 全绿后） | 1 行 |

**不动的**：五引擎与 `streamengine.h`、`engineregistry.{h,cpp}`（关联在目录侧单向声明）、`workspace.{h,cpp}`（版本记录 model 摘要不升级）、`scripts/fetch-deps.sh`（third_party 测试模型通路=CI 域）、旧 7 动词薄壳、全部 parity 脚本、wp-1.0 runbook。

## 5. 留缝清单（本包不做，后续包的接口锚点）

| 缝 | 本包怎么留 | 不动什么 |
|---|---|---|
| **FR-4 质量档（DFN3/FastEnhancer）** | 目录表加条目即可（id/for/sha256/license）；`forVerb` 已支持同动词多模型 | 不做档位选择参数（aiDenoise 的 model 档位枚举随选型包定） |
| **RVC（M3）/ ASR·VAD（FR-A6/A7）/ Qwen（FR-A5）** | 自由导入（D6 形态 2）就是社区模型前门；license=unknown 记档 + 通用提示 | 不内置 RVC/Qwen 条目、不写 NFR-C5 专用须知（M3） |
| **fr-a4 :215 model 摘要升级** | models.json 的 {id, sha256} 是升级的数据源 | 不改 workspace.json 记录形态（零漂移） |
| **FR-9 管线 / FR-10 批处理** | 模型动词独立于 input/output 语义，管线步骤引用模型照旧传路径 | 不做管线内嵌下载步骤 |
| **GB 级模型（Qwen GGUF）** | 增量写盘 + 流式哈希 + Range 续传即为其设计；单连接够否届时实测 | 不做分段并发（OQ-4 记录否决理由） |
| **gated 仓库 token** | endpoint 字段旁预留 `token?` 字段位（research §1.3 gated 流程）；v1 不实现 | 不做认证头注入 |

## 6. 验证方案（目录 `docs/acceptance/fr8-model-management/`，风格对齐 run-json/run-ws）

**目录骨架**（实现开工时建）：`goal.md` + `plan.md` + `matrix.csv`（前缀 `MDL-`）+ `round-1.md`（片 A）/`round-2.md`（片 B）+ `scripts/{range_server.py, run-models.sh}` + 全绿后 `report.md`。

**测试基建核心：`range_server.py`**——python3 stdlib `http.server` 的 Range 扩展（标准 SimpleHTTPRequestHandler 不回 206，只回 200 全量，续传/重定向/错误注入都测不了）。~100 行：目录服务 + `Range`→206 处理 + 注入开关（`?fail=404|429|cutmid|badhash`）。`run-models.sh` 沿 run-json.sh 风格（set -uo pipefail、mktemp 临时区、ck() 记账）；素材 = 小尺寸假 onnx fixture（mktemp 生成随机块 + 现算 sha256 进伪清单）+ 复用 ci-smoke 真模型做导入正例（third_party 已有，零下载）。

**用例族（MDL-*）**：
1. **MDL-LIST**：空库（不建目录、bundled 条目在列）；下载后状态翻转；stale（清单有条目文件被删 → status=stale 不当已装）；license 字段恒在。
2. **MDL-DL**：完整下载（本地 server）→ sha256 外部 sha256sum 对照 → 原子落位（无 .part 残留）→ models.json 登记；幂等（二次调用 server 请求计数 = 0 新请求、ok 返回现路径）。
3. **MDL-RESUME（断点续传，本包核心差异点）**：注入中断（cutmid / 首跑 kill）→ `.part` 在 → 重跑 → **server 日志断言请求带 `Range: bytes=N-`** → 完成 sha256 对；服务器回 200 不支持 Range → 弃 .part 从头重下。
4. **MDL-NEG**：未知 id（E_PARAMS）；HTTP 404/429（E_NET）；无服务端口（E_NET）；坏 sha256（server 供内容与清单不符 → 删 .part + E_NET + 库无残迹）；endpoint 非字符串/未知字段拒收；modelList 带 input 正常忽略或拒收（信封白名单定死后按实现断言）。
5. **MDL-IMPORT**：按 id sha256 匹配登记；自由导入 for=aiDenoise setup 试开成功；坏模型导入（wp-1.0 round-2 维度错位配方）→ E_MODEL 且库/清单无残迹；**导入源文件 sha256 前后不变（原件只读）**；复制中断负例（.part 清理）。
6. **MDL-ENVELOPE（零漂移硬门禁）**：modelList 无 input ok（豁免正例）；CMD-021r 全文逐字节复跑 + WS 族 + ci-smoke 全量（带 input 动词逐字节不变）。
7. **MDL-GUI（片 B）**：gui-slot-eqv 方法论 + 对话框数据 = modelList 探针直调断言；modelMissing 信号路径（E_MODEL 触发、其他错误不触发）；下载占 busy（任务期七槽锁定）。
8. **MDL-CANCEL**：取消 → E_CANCELLED + `.part` 保留 + 重跑续传闭环（GUI 语义经 Ctx，CLI 不可达同 WS-CANCEL 方法论：代码级证据 + GUI-EQV 承接）。

**门禁接线**：round-1（片 A Linux）全绿后 `run-models.sh` 进 ci-smoke（run-json 同款一行）；Windows 真机随 WP-1.0 round-4 挂账口径（fr24/nfr-m1/fr-a4 同款）。**真网不进 CI**：镜像行为（Xet CAS/308 回源/CN vantage）按 NFR-C4 注记人工实测留档 `round-N.md` 附录，不设自动用例。

## 7. 开放问题（主线程拍板，拍板后记入 acceptance goal.md）

- **OQ-1 E_NET 是否新增**：候选 (a) `ErrCode` 加 `Net`（六类→七类，errinfo.h:10-18 封闭枚举显式扩展；既有动词无人产生 Net 码，零回归面）✅倾向；(b) 复用 E_FILE（OQ-4 刚把 File 收窄为本地文件/媒体 I/O，网络塞入语义浑）；(c) 复用 E_MODEL（断网≠模型坏，误导 NFR-R4 引导识别）。影响：errinfo.h、MDL-NEG 断言、Agent 恢复动作语义。
- **OQ-2 镜像配置承载面**：候选 (a) 请求字段 `endpoint` + GUI QSettings 缺省注入（workspace root 同型）✅倾向；(b) 兼容读 HF_ENDPOINT（Python 生态习惯，但违工作原则 6 + 双通道漂移）；(c) 仅 GUI 设置（命令层读配置，违 fr24 D2）。影响：D4、动词契约、GUI 设置面。
- **OQ-3 模型库根缺省位置**：候选 (a) `~/VideoHandler-Models`（与工作区根 OQ-1 先例同型：跨平台一致、CLI 文档好写）✅倾向；(b) QStandardPaths::AppDataLocation（buried，不利「模型是用户资产」直觉）；(c) appDir/models（Program Files 不可写，D1 已否决）。影响：D1 布局、data.h 常量、GUI 缺省。
- **OQ-4 下载并发**：候选 (a) 单模型单连接（20–70MB 规模够；GB 级 v1.x 再评）✅倾向；(b) 分段并发（aria2 -x4 式：Range 多段 + 合并，复杂度/中断恢复语义翻倍）；(c) 多模型并行（GUI 单任务语义需破）。影响：D3 实现规模、MDL-RESUME 用例形态。
- **OQ-5 自由导入的开放面**：候选 (a) 开放（for 必带 + setup 试开 + license=unknown，社区模型前门）✅倾向；(b) v1 仅按 id 导入（锁死目录内模型，RVC/Qwen 前门推迟到各自工作包）；(c) 全开放免 for（两个引擎 setup 各试一遍猜用途——废件可能蒙混过其一）。影响：D6、MDL-IMPORT 用例量。
- **OQ-6 `.part` 与 NFR-R1「失败删半成品」的关系**：候选 (a) `.part` 保留作续传缓存，显式注记「R1 约束任务产物，下载缓存豁免；sha256 不符/200 重下时仍删」✅倾向；(b) 失败即删（对齐 R1 字面，代价 = 慢链路 GB 级模型永远下不完——正是 HF 官方被骂的 issue #4196 场景）。影响：D3、MDL-CANCEL/RESUME 断言。
- **OQ-7 首次引导形态**：候选 (a) E_MODEL → modelMissing 信号 → 问询打开模型管理（结构化、不破文案锁定）✅倾向；(b) 自动开始下载缺省模型（不打招呼联网，隐私直觉差）；(c) 仅现有报错文案不动（NFR-R4「引导」不兑现）。影响：D8、片 B 规模。
- **OQ-8 模型动词不接 workspace 字段的红线句裁定**：候选 (a) 追认 D7 裁定（红线句意图=防覆写可追溯，模型库以等效机制满足：新路径新建 + 原子写 + 登记失败删产物）✅倾向；(b) 判违——模型下载也走工作区版本目录（同模型多份拷贝、共享引用断链，本 spec 判断为机制误用）。影响：D7、CLAUDE.md 红线注记、动词 allowsWorkspace。

## 7.1 主线程拍板（2026-11-03，全部倾向项采纳，各附反证自查一句）

- **OQ-1 → (a) 新增 E_NET**。反证自查：封「六类」是 NFR-M1 片 B 当时未见网络域，E_NET 是真实新语义域（传输层）而非既有类细分；既有动词无人产生 Net 码，零回归面。
- **OQ-2 → (a) 请求字段 endpoint + GUI QSettings 承载缺省**。与 workspace root 承载完全同型（fr-a4 D7 先例）；不读 HF_ENDPOINT（工作原则 6，双通道漂移）。
- **OQ-3 → (a) `~/VideoHandler-Models`**。与 `~/VideoHandler-Workspace` 同型（跨平台一致、CLI 文档好写）；AppDataLocation 埋太深不利「模型是用户资产」直觉。
- **OQ-4 → (a) 单模型单连接**。20–70MB 规模实测够用；GB 级（Qwen）v1.x 届时实测再评，不预造复杂度。
- **OQ-5 → (a) 自由导入开放（for 必带 + setup 试开 + license=unknown）**。反证自查：许可红线义务在「如实呈现」（unknown 显式标注 + GUI 通用提示 + NFR-C2 清单如实转录），核验责任如实转移用户——与 RVC 社区模型「用户导入+使用须知」定位一致；免 for 猜用途（候选 c）会让废件蒙混，否决。
- **OQ-6 → (a) `.part` 保留作续传缓存（显式豁免注记）**。R1 约束任务产物（半成品 WAV/MP4），下载缓存不是任务产物；跨运行续传是 FR-8 需求原文的差异点，删 `.part` 直接阉割该能力（HF issue #4196 正是被骂场景）。sha256 不符 / 200 强制重下时仍删。
- **OQ-7 → (a) modelMissing 结构化信号 + 问询引导**。NFR-R4「引导」兑现且不自动联网（隐私直觉）、不破 GUI-EQV 文案锁定、不走文案前缀匹配（P3-1 教训）。
- **OQ-8 → (a) 追认 D7：模型动词不接 workspace，以等效机制满足红线意图**。反证自查：红线第二句语境是「任务产物版本史」（fr-a4 的 plan/commit 就是任务两段提交）；模型是被多任务共享的资产——进版本目录 = 同模型多份拷贝（append-only 不可去重）+ 跨工作区引用断链，无任何消费者需要（FR-9 管线传路径即可，§5 留缝）。红线第一句（不改写用户源文件）模型动词严格满足。**CLAUDE.md 红线句随片 A 落地时加注记**（共享资产动词以等效机制满足），消文档自相矛盾。

## 8. 红线自查与实现风险

**红线自查**：
- **权重/third_party 不入库**：内置目录表只含 URL/sha256/许可证字符串常量；验收 fixture 在 mktemp 临时区生成或复用 CI 既有 third_party 模型（已 gitignore）；无新第三方库（Qt Network 在 qtbase，LGPL 组合零变化；range_server.py 用 python3 stdlib）。
- **原件只读**：导入复制不移动不改写（MDL-IMPORT 断言源 sha256 不变）；下载只新建（目标名预知，rename 前唯一在途 `.part`）；模型动词不改写用户源目录任何文件。
- **模型许可证逐个核验**：目录条目 license 字段必带（GTCRN「MIT 倾向+发版前书面确认」、UVR「免费再分发须标注来源」如实转录 REQUIREMENTS §6）；自由导入 license=unknown 显式标注 + GUI 提示。
- **NFR-C1 无遥测**：只连用户显式配置的 endpoint/directUrl；无任何上报；重定向策略显式 NoLessSafe（防降级）。
- **NFR-P4 流式内存有界**：下载增量写盘（64KiB 级缓冲）+ 流式 sha256；模型库索引 KB 级 JSON；无整文件载入语义。
- **NFR-R1**：见 OQ-6 的显式裁定（`.part` 豁免边界写进注释与文档）。

**实现风险**：
1. **Xet/镜像行为的不确定性是最大外部风险**（falsify-hf-llm.md 判级 1d：CAS 绕 HF_ENDPOINT + hf-mirror 308 回源 + CN vantage 盲点）——缓解：下载器只依赖实测站得住的 resolve+Range+etag 契约（判级 1b），镜像逐个**人工**实测留档（§6），CI 钉协议层；endpoint 可配让坏镜像可绕开。
2. **QNAM 异步模型与 execute 同步契约的整合**：局部 QEventLoop 正确性（信号到达/退出条件/异常路径）、CLI 需补 QCoreApplication（壳改动波及 main.cpp CLI 分支——六行但触碰全部 CLI 入口，ci-smoke 全量复跑护住）；取消粒度在块间检查（QNAM abort 时机）。
3. **信封豁免触碰全部动词的公共校验段**：CMD-021r 全文比对 + WS 族 + ci-smoke 全量是硬门禁；`requiresInput` 默认值取 true（新动词须显式声明 false）防将来误加。
4. **GUI-EQV 文案锁定 vs 首次引导**：结构化 modelMissing 信号而非文案前缀匹配（P3-1 教训）；既有失败文案逐字不动。
5. **models.json 与磁盘实况的双源漂移**：清单登记失败即任务失败（删产物）压住写入侧；读取侧以磁盘实况为准（stale 语义）压住删除侧——两侧都有显式用例（MDL-LIST/MDL-DL）。
