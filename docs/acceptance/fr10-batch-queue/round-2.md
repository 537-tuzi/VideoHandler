# FR-10 批处理队列 round-2 验证记录（独立测试轮）

- 日期：2026-11-04。执行者：独立测试代理（与 round-1 实现代理不同实例，不复用其结论，全部复跑）。
- 范围：spec §6 十一族用例面的独立复跑 + 红绿论证（断言非空洞证明）+ 补缺口用例（同根等价 /
  非批零漂移字节不变 / widget 面 BATCH-GUI 族）。src 冻结零改动；本轮全部产出在
  `docs/acceptance/fr10-batch-queue/`（run-batch.sh 三处补测 + 探针两新文件）。

## 1. 门禁独立复跑（结论：全绿）

| 门禁 | 结果 | 说明 |
|---|---|---|
| 构建 `cmake --build --preset linux-test --clean-first` | 27/27 零警告 | 独立复跑 |
| run-batch.sh（round-2 扩展版） | **99 PASS / 0 FAIL**，EXIT=0 | round-1 的 89 条全复绿 + 本轮补测 11 条（见 §3） |
| run-json.sh 零漂移 | 全绿 | 串行复跑 ×2 全绿；首跑 1 败系与红绿驱动并发所致（见 §5 观察项），串行排除 |
| run-ws.sh 零漂移 | 58 PASS 全绿 | workspace 键面零漂移 |
| run-pipe.sh 零漂移（TMPDIR=磁盘卷 .pipe-tmp） | 77 PASS 全绿 | PIPE-NEG 19 条逐字零漂移 = validateSteps 抽取护栏实证 |

零漂移三件套与 run-batch 全程串行执行（无并发套件）。

## 2. 红绿论证（结论：断言非空洞，翻转即红）

方法：run-batch.sh 字节级副本，仅翻转 BATCH-NEG 8 条的期望值/期望消息，跑 NEG 段——
预期「恰好被翻转的条目转红、其余照绿」。结果：8 红 + 22 绿 + EXIT=1，逐条红样例
（期望 vs 实为，证明断言真实对照引擎输出）：

| 用例 | 翻转后期望（红） | 实际（引擎真实输出） |
|---|---|---|
| NEG-003 | inputs 至少需要 1 件 | inputs 至少需要 2 件 |
| NEG-006 | 重复文件：…/f2.wav | 重复文件：…/f1.wav（先到者在消息里） |
| NEG-017 | steps[0] 未知字段：output | steps[0] 的 cmd 不可链式：batch（仅引擎动词可入链） |
| NEG-020 | steps[0] 未知字段：output | steps[0] 未知字段：workspace |
| NEG-023 | error.step=2 | error.step=1（预校验首步即拦） |
| NEG-024 | 找不到模型 /nonexistent2.onnx | 找不到模型 /nonexistent.onnx |
| NEG-025 | pipeline 必须带 workspace | batch 必须带 workspace |
| NEG-029 | 工作区根存在目录 | 零目录（附加断言转红） |

另 NEG-030 修复验证：把基线哈希换成异文件集后转红（原版恒真，见 §4 缺陷 T-1）。

## 3. 补缺口用例（vs spec §6 十一族；本轮新增 11+22 条）

### 3.1 run-batch.sh 三处补测（run-batch.sh 内联，全部 PASS）

| 用例 | 断言点 |
|---|---|
| BATCH-EQV-SR1 | 同根等价批侧：批跑 eqvS 根 3 件（denoise→pitch 模板）okCount=3 |
| BATCH-EQV-SRs0..2 | 逐件手动 pipeline **同一 workspace 根**：定位批已建工作区续号 v003+（append-only 跨通路） |
| BATCH-EQV-SRc0..2 | 同根下批跑 vs 手动逐件终产物逐字节 `cmp` 一致（同根身份/缓存共享不改变产物） |
| BATCH-WS-011 | 非批单动词（pitch f3）先落 zws 根，记录 workspace.json sha256 基线 |
| BATCH-WS-012 | 同根批 2 件（不同内容 → 独立工作区）okCount=2 跑通 |
| BATCH-WS-013 | 批后该单动词工作区 workspace.json sha256 逐字节不变（批不触碰他人工作区索引） |
| BATCH-NEG-030（修正） | 源 sha256 基线前移到一切请求之前（原版两侧都在请求后=恒真，见 §4 T-1） |

### 3.2 GUI widget 面 BATCH-GUI 族（gui-batch-probe.cpp + gui-batch.sh，round-2 新建，23 用例）

主路径零 QFileDialog 依赖：探针直填 QListWidget（UserRole=绝对路径）后
`QMetaObject::invokeMethod` 直调对话框私有槽 `doBatchStart`，显式清单直接进组包通路；
捕获 `MediaService::batchRequested(inputsJson, stepsJson)` 深度比对（gui-slot-eqv 方法论）。

| 用例 | 断言点 |
|---|---|
| GUI-001 | 控件面就绪：lstBatchFiles/cmbBatchAction 8 条 = 5 单步 + 3 预设（OQ-6(a)，无 transcode） |
| GUI-002..012 | 组包深度比对：den60/denoff（humBase 60/off + wind true/false 独立注入）、ai、pit（semi=2.5 取面板）、sepv/sepi（stem 双值）、pre0/1/2（预设链 + separate 步显式 stem=vocals + pitch 步 semi=-3.5）、mixed（清单含不存在件原样透传）、cancel |
| GUI-003b | denoff 失败面：预校验「步骤 1（denoise）：humBase 为 off 且 wind 为 false：没有可执行的降噪项」原文透传警告框（消息名点双标志 = 注入面到达引擎的证明）+ 行不渲染 |
| GUI-013 | busy 锁全批覆盖：11 case 提交即锁/完成即解（七槽五键 + 管线行 + 批入口 + 对话框四键 + btnCancel 反向；无 objectName 控件按唯一文案锚定，缺一即断言面不成立） |
| GUI-014 | 批运行中锁持续：首个进度回调时刻再采样全锁（mid=1）；denoff 预校验失败零进度（mid=-1） |
| GUI-015/016 | 完成文案「批处理完成：2/2 / 1/2 件成功」信息弹窗逐字（附裁 5）+ 状态行同步 |
| GUI-017 | 清单逐件渲染：行 0「st.wav —— 成功（版本 N）」/ 行 1「no-such.wav —— 失败：E_FILE：打不开文件：<path>」（index 对位，k=okCount） |
| GUI-018 | 「已取消」静默：零弹窗 + 状态行逐字 + 行不渲染（附裁 5）+ 取消确已点击（进度 50 点 btnCancel） |
| GUI-019 | 批进度终值 100（主进度条满格代理；denoff 零进度） |
| GUI-020 | 缺模型开工前速断：警告+问询双弹窗齐 + 点「是」开模型管理 + busy 已释放（unlocked=1）。B→M 顺序以 worker 面 GUIE-002 order=BM + 本面 unlocked=1 为证——问询框在警告框 exec 内嵌套打开，探针 activeModalWidget 栈顶采样不锁 record 序（采样面局限，非产品行为） |
| GUI-021 | 缺模型整批速断零件启动：零工作区目录 |
| GUI-022 | 多选对话框应答（尽力段）：行编辑引号多填 "f1" "f2" + AcceptRole，清单 2 件序正确（offscreen 落定 PASS；不落定则 NOTE 降级为主路径，Windows round-4 手工） |

环境口径：offscreen + AA_DontUseNativeDialogs + XDG_CONFIG_HOME 隔离 + QSettings 受控根注入
（kSettingsWorkspaceRoot/kSettingsModelRoot 于 MainWidget 构造前）+ bin-full/bin-empty 双布局
（appDir 缺省解析）；模型链慢任务真等满（waitUnlock 修为真 180s 上限，见 §4 T-2）。

### 3.3 族面覆盖对照（spec §6）

OK/EQV(+同根)/FAIL(三种坏件配方：不存在=FAIL-001、无音频流=FAIL-003、坏模型=FAIL-004+NEG-024
+件内步失败归属=FAIL-005)/NEG(30+红绿)/CANCEL/WS(+字节不变)/PROG/ZERO(3 复跑+3 挂账)/
GUIE(round-1 复绿)/GUI(本轮新增)/REG(未跑，spec §6.10 分支证据制)/MEM(2+2 复绿)——无缺口。

## 4. 缺陷与修复（全部为**测试侧**缺陷 T-1..T-5，src 零缺陷）

- **T-1（中，已修）BATCH-NEG-030 恒真断言**：原版基线 sha256 与比对 sha256 都在全部 NEG
  请求之后计算（run-batch.sh 物料段末尾 vs NEG 段末尾），永不相等不了→恒 PASS。修复：基线
  前移到物料生成后、一切请求前（run-batch.sh `SRC_SHA_BASE`）。验证：翻转基线文件集→红。
- **T-2（高，已修）gui-batch-probe waitUnlock 超时算错 20 倍**：`t < maxMs / 20` 把 180s 上限
  实际变成 9s（gui-batch-probe.cpp 原 waitUnlock）。慢任务（MDX separate ~17s）9s 超时后探针
  提前采样，下一 case 的 `media->batch` 被 `if (busy) return` 静默丢弃 → ACT 空、STATUS/ROWS
  串台上一 case 完成事件。修复：`t < maxMs`。诊断证据：时间戳探针（sepv SPIN 逐旋 20ms、
  WAITEXIT@9363 btn=DIS、BFIN@17502——退出时刻锁未放、真完成在 17.5s）。
- **T-3（低，已修）BATCH-GUI-020 断言把嵌套弹窗采样序当产品序**：问询框在警告框 exec 内
  嵌套打开，activeModalWidget 恒取栈顶 → record 序恒为问询先。修复：断言改双弹窗齐 +
  unlocked=1（顺序契约由 GUIE-002 worker 面 order=BM 承载），脚本注释记档。
- **T-4（中，已修）BATCH-GUI-013 UNLOCKED grep 锚错行尾**：`'^UNLOCKED .* all=1$'` 要求行尾
  是 all=1，而 UNLOCKED 行尾是 mid=N（all=1 在行中）→ 恒 0 永不匹配（探针输出本身全对，
  断言面空转）。修复：`'^UNLOCKED .* all=1 mid='`。三次套件跑全红同一处 = 确定性脚本 bug
  非环境抖动。
- **T-5（中，已修）BATCH-GUI-019 python 切片差一**：`line[4:]` 把「PROG 」前缀切剩一个前导
  空格 → split 首段为空串 → tag 恒空 → 8 个 tag 全不匹配。修复：`line[5:]`。与 T-4 同轮定位：
  现场保留的 gui.out 内容全对、用套件同款断言复现即红——**教训：验证断言要用套件自身的
  匹配式复跑，不能用近似手搓 grep「看着过了」当绿**（本轮 manual 检查用的恰是另一组能过的
  写法，两轮误导诊断方向）。
- **观察（不修）**：`MainWidget::doBatchStart` 先 `set_busy(true)` 后 `media->batch(...)`，
  而 `MediaService::batch` 对 busy 静默 return——若任何未来通路能在 busy 期间再次触发
  doBatchStart，UI 将锁死无解锁事件。现网所有批入口（btnBatchStart/btnBatch）busy 期间皆
  禁用，用户面不可达；作为防御深度观察记档（同构缺口在七槽 do\* 槽同型存在，非 FR-10 新引入）。
- **观察（不修）**：`doBatchStart` 预设链 separate 步显式 `stem=vocals`（mainwidget.cpp 组包
  段）而 `doPipeline` 依赖注册表缺省——语义等价（stem 缺省即 vocals），两条通路两种写法，
  复杂度中性，不动。

## 5. 环境与执行口径

- 全部套件串行执行。首跑 run-json 1 败（CMD-03x 区段）经查与本轮红绿驱动并发相关，串行
  复跑 ×2 全绿（matrix BATCH-ZERO-json 行记档）；教训已固化：验收套件一律串行。
- gui-batch 首两跑 GUI-013/019 红：非环境抖动，系 T-4/T-5 确定性断言 bug（现场保留 +
  套件同款匹配式复跑定位，见 §4）；修复后终跑全绿。
- batch/probe TMPDIR 一律磁盘卷 `build-linux/.batch-tmp`（/tmp 配额假象口径，run-batch 同）。
- 缺模型族 else-skip / PARITY_REQUIRED=1 升 FAIL 惯例沿 run-batch.sh 既有实现。

## 6. 挂账

- run-models.sh / gui-slot-eqv.sh / thinshell-eqv.sh 未随 round-2 复跑（round-1 全绿
  142/18/全绿；本轮 src 零改动、模型族与 GUIE 壳零触碰面，复跑优先级让位 GUI 新面）。
  下轮或 round-4 前补一次全量。
- BATCH-REG：spec §6.10 分支证据制（M3 假想验证），不进 ci-smoke，维持未跑。
- Windows 真机：随 round-4 挂账（run-batch + gui-batch + 零漂移三件套同单）。
- **ci-smoke 接线建议（本轮未动 ci-smoke.sh，交主线程）**：在既有 run-batch 行后追加
  `TMPDIR="$BATCHTMP" bash docs/acceptance/fr10-batch-queue/scripts/gui-batch.sh "$BUILD"`
  （gui-batch 自带缺模型整脚本 FAIL 口径，无 else-skip 面；BATCHTMP 沿用 run-batch 的磁盘卷
  变量名——若 ci-smoke 现行变量名不同，对齐即可）。

## 7. 结论

round-2 独立复跑全绿（99 PASS 门禁 + 零漂移三件套 + 构建零警告）；红绿论证 9 翻转全红证
断言非空洞；补缺口 11 条 CLI 面 + 23 条 widget 面全绿；缺陷 5 项全在测试侧且已修；src 零缺陷。
FR-10 验收面（除挂账项）闭环。

## 8. 主线程复审处置（round-2 收口，2026-11-04）

审查（十维）+ 测试双报告甄别，配方 #7 主线程亲手复核关键现象后处置：

| 来源 | 发现 | 处置 |
|---|---|---|
| 审查 P2-1 = 测试 T-1（双独立发现） | BATCH-NEG-030 sha 基线取在全部请求后 = 恒真断言（防 ok:true 虚绿类守卫空转） | 测试代理已修：SRC_SHA_BASE 前移至素材生成后一切请求前（run-batch.sh:68/:283）；主线程核实修复在位，翻转验证转红记档 §2 |
| 审查 P3-1 | BATCH-OK-003 for 循环后 `ck $?` 只看末轮迭代退出码——前件缺版本目录被遮蔽成 PASS | **修**：OK3_MISS 循环内累计 + `ex` 断言（run-batch.sh:121-124） |
| 审查 P3-2 | mediaservice.h:74 注释「失败 modelMissing 整批速断不占 busy」与「占 busy 整批一锁」矛盾（实际 busy 毫秒级释放） | **修**：改「失败 batchFinished 先发（busy 毫秒级释放）再发 modelMissing 引导，附裁 1」——与 GUIE-002 order=BM 实序一致 |
| 审查 P3-3 | doBatchAdd/doOpen 文件过滤器两份字面量拷贝（注释称同一实不同源，doOpen 改串批处理不同步） | **修**：提为匿名 ns `kMediaFilter` 单点常量，两处改引（mainwidget.cpp） |
| 审查 P3-4 = 测试观察 | 预设链 separate 步显式 stem=vocals vs doPipeline 走注册表缺省——语义等价（缺省即 vocals） | 记档不修（两报告独立同判；GUI-008 组包断言已锁形态） |
| 测试观察 | doBatchStart 先 set_busy(true) 后 media->batch（busy 期再触发将锁死无解锁事件；现网批入口 busy 期全禁用不可达；七槽同型非 FR-10 新引入） | 记档不修（防御深度观察；修它需连七槽一起动=超本包范围，随交互式队列一并设计） |
| 测试 T-2..T-5 | 探针/断言 4 处测试侧缺陷（超时 20 倍/嵌套弹窗采样序/grep 行尾锚/切片差一） | 测试代理已修 + 套件同款匹配式复跑验证（§4） |

三修复（P3-1/P3-2/P3-3）+ gui-batch 进 ci-smoke 接线（ci-smoke.sh run-batch 行后一行）后
全量门禁复跑（串行，磁盘卷 TMPDIR）：

| 门禁 | 结果 | 备注 |
|---|---|---|
| 构建（增量） | 零警告 | mainwidget.cpp/mediaservice.h 修复后 6/6 |
| run-batch.sh | **全绿 rc=0** | P3-1 修复后 OK-003 走累计断言；MEM 复测免模型 61116 ≤ 59544+32MB <256MB、带模型 1537120 ≤ 1514836+32MB <2GiB（件·步·模型会话三层串行零叠加复证） |
| gui-batch.sh | **全绿 rc=0** | kMediaFilter 同源化后探针面复绿（23 用例含 GUI-022 多选应答段） |
| ci-smoke.sh 全量 | **全绿 rc=0** | 含 run-batch + gui-batch 新接线段与既有全部门禁 |

（run-batch 的 FAIL 标志机制：任一断言败即 FAIL=1 非零退出，「== run-batch 全绿 ==」+ rc=0 为
逐段 PASS 证据。）
