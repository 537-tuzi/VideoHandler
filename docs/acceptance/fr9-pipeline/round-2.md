# FR-9 round-2：审查+测试双轮处置修复 + 门禁复跑（2026-11-03）

round-1 见 `round-1.md`（75 PASS + gui-pipe 7 + REG-012/013）。本轮 = 双代理（审查 10 维度 +
测试 10 组）报告的甄别处置 + 四修复落地 + 全门禁复跑。

## 双报告处置总表（主线程甄别，配方 #7 亲手复现）

| 来源 | 发现 | 处置 |
|---|---|---|
| 审查 P2-1 | 带模型链 MEM 缺口：spec D1 明文「峰值=逐步最大值而非和」，run-pipe 只测免模型链；审查代理补测 120s aiDenoise→separate 得管线 1,517,716KB vs 单动词最大 1,515,008KB（比值 1.00，GTCRN 会话零叠加） | **修**：PIPE-MEM-003/004 固化为断言（防空转前置 + ≤max+32MB + <2GiB），主线程复跑实测 1,517,592 vs 1,515,180（差 2.4MB，与补测吻合） |
| 审查 P3-1 | modelMissing(forVerb) GUI 消费端丢参：信号带 forVerb（附裁 1 语义），mainwidget `onModelMissing()` 无参连接把「该下哪个用途的模型」丢成通用文案 | **修**：接参 + 用途映射（aiDenoise→AI 降噪 / separate→人声分离 / 未登记退通用），问询文案带用途 |
| 审查 P3-2 | MEM 断言裕量不敏感：32MB 裕量对免模型 regime（60MB 级）太宽 | 随 P2-1 消解：免模型链保持原口径（<256MB 绝对线），带模型链断言同时锁 +32MB 与 <2GiB（NFR-P4）双面 |
| 测试 P3-1 | pipe-gui-probe gui 相不注入工作区根：wsRoot 参数收而不用，MainWidget 构造读 QSettings 空键归一缺省 `~/VideoHandler-Workspace`，round-1 实况污染用户家目录（st.0b2bb088，31 版本目录） | **修**：gui 相构造前 QSettings 预写 kSettingsWorkspaceRoot=wsRoot + kSettingsModelRoot=wsRoot/models-lib（两级缺省解析恒走 appDir 随包，探针密闭化）；复跑前后缺省根计数不变实证（ws=2/models=1） |
| 测试 P3-2 | offscreen `QFileDialog::selectFile()` 延迟选在挂载 iso9660 卷的环境不落（Qt 6.10.2 文件模型停滞 → accept 空选不收尾）：PID 27570 gui 探针死泊 33 分钟（18:14–18:47，主线程 kill 收尸） | **修**：行编辑直填 + AcceptRole 确认键（测试代理验证过的 workaround）；pipe-gui-probe 与 mdl-gui-probe（:166 同型）双加固；**光盘仍挂载的红条件下** gui-pipe 复跑全绿 = 修复实证 |
| 测试 P3-3/P3-4 | 消息顺序自洽性（progress/jobDone 到序）两处 | **记档**：实测自洽，非缺陷；不改 |
| 测试 P3-5 | truncated 覆盖缺口：管线链中 truncated 标记的传递面无 I/O 错误注入用例 | **挂账**：需故障注入基建（磁盘满/写中断），超本轮范围；记入 report.md 遗留 |
| 实现 3 处 spec 措辞偏差（E_MODEL/E_ENGINE 按失败形态分派、output 走 allowsOutput=false 白名单、模型不存在 → E_MODEL） | 双代理独立复核均「实现更对」 | 追认（round-1.md 已记，spec 不回头维护） |
| 复审轮 P2-1 | run-pipe 新增带模型 MEM 块缺 `else skip` 惯例（同文件 :117/:142 两处既有惯例）：缺模型强制跑（CI fetch 失败场景）下 MEM-003/004 静默消失仍报全绿——门禁完整性缺口 | **修**：一行 else-skip 对齐惯例（SKIPANY→PARITY_REQUIRED=1 升级机制自动生效）；bash -n 过；本次 ci-smoke 已过 run-pipe 段后落盘，改动为纯 else 分支追加、模型在位路径字节不变 |
| 复审轮 P3-1/P3-2/P3-3 | 单动词基线无 ok 守卫（失败时 004 变红但缺精准定位，与 round-1 口径一致）；用途映射与 ffextractor verb→disp 两份平行知识；missing-text 探针 semi=2.5 注释论证与真实触发序不符 | **记档**（report.md）：前两项非必改；P3-3 待测试代理报告后一并处置 |

环境事件记录：18:01 用户挂载两个 iso9660 卷（sr0/sr1，至今未卸）→ gui-pipe 4 例 FAIL 均系探针
kill 的输出级联，7 PASS 证据（15:02–15:31）先行成立；探针加固后同环境复跑 7/7 全绿，事件闭环。

## 四修复内容

1. **run-pipe.sh**：PIPE-MEM-003（带模型链 ok=true 前置防空转——链失败提前退出会峰值虚低，
   同量级断言假绿）+ PIPE-MEM-004（120s aiDenoise→separate vs max(ai,sep) 同量级 + NFR-P4 2GiB）。
2. **mainwidget.{h,cpp}**：`onModelMissing(const QString& forVerb)`（mainwidget.cpp:220 附近 PMF
   连接自动传参零改）；用途映射与七槽/预设文案同词，未登记动词退通用（未来注册表新引擎的防御分支）。
3. **pipe-gui-probe.cpp**：gui 相 QSettings 受控根注入 + 应答器文件对话框行编辑直填 + AcceptRole。
4. **mdl-gui-probe.cpp**：删 `fd->selectFile(g_importPath)`（行编辑直填+AcceptRole 本就在位，FR-8
   round-4 的 142 PASS 证据不受影响，本轮全量复跑仍 142 PASS）。
5. **run-pipe.sh**（复审轮追修）：带模型 MEM 块补 `else skip`（P2-1，惯例对齐，见处置表）。

## 门禁复跑（round-2 全量）

| 门禁 | 结果 | 备注 |
|---|---|---|
| run-pipe.sh | **77 PASS 全绿**（75+新增 2） | MEM 双实证：免模型 60,880 ≤ 60,056+32MB 且 <256MB；带模型 1,517,592 ≤ 1,515,180+32MB 且 <2GiB |
| gui-pipe.sh | **7 PASS 全绿** | 光盘挂载红条件下；缺省根零新污染 |
| run-models.sh | **142 PASS 全绿** | mdl 探针加固 + Fix 2 文案兼容（GML-114 族过，应答器 contains("模型不可用") 对新文案仍命中） |
| gui-slot-eqv.sh（fc69173） | **18 PASS 全绿** | Fix 2 对七槽转录零漂移 |
| thinshell-eqv.sh（fc69173） | **16 PASS 全绿** | CLI 薄壳三方一致 |
| fake-engine-e2e.sh | **25 PASS 全绿** | REG-012/012r/013/013r 假引擎免改入链 |
| 构建 | 零警告（ninja 5 步） | |
| ci-smoke.sh | **361 PASS 全绿**（rc=0；唯一 SKIP=PAR-MD 缺 audio-separator，本地降级口径，CI 以 PARITY_REQUIRED=1 强制） | 含 run-pipe 77 + gui-pipe 7 接线段；光盘挂载环境下 gui-pipe 亦全绿 |
| 双代理复审 | 审查：四修复 **3 过 + 1 有保留（P2-1 已修）**；测试：MIST **11/11 全绿**（主线程亲手复跑同绿） | 审查另带两处本机实证（fdprobe/qsprobe）；测试补 missing-text 探针族 |
| missing-text.sh | **11 PASS 全绿**（MIST-001..011） | Fix 2 用途文案三态逐字锁 + Yes 引导链回归锁；主线程复跑实证 |

## Fix 2 文案断言缺口（round-2 过程发现）

新用途文案初始无任何断言锁定——GML-114 只锁问询次数、mdl 应答器 contains 匹配新旧都命中，
映射改坏将静默退化。测试代理补 missing-text-probe（T1/T2 管线链中 E_MODEL 双动词真实路径 +
T3 未登记动词信号直发——Qt6 信号公可直接 emit 实证可行）+ missing-text.sh 断言族
**MIST-001..011 全绿**（主线程亲手复跑同绿）；红绿论证含旧形态样本必红实证（丢参退化被
MIST-003/006 抓、交叉映射同抓、未登记误入用途分支被 008/009 抓）。已接线 ci-smoke。

## 测试代理范围外 P2 的复现定性（配方 #7：未复现不采纳入债表）

代理首轮调试曾见 `QTimer::singleShot(ms,&loop,quit)` 谓词早退后悬挂单发致 qFatal abort（gdb 栈
实录），归因为「receiver 形 singleShot 不受析构保护」并称 mdl/pipe 两探针同款配方「侥幸不崩」。
主线程 30 行复现探针（/tmp/vh-repro/hang-repro.cpp，谓词立即真 + singleShot(3000) 悬挂 +
app.exec 存活观察）**证伪该归因**：receiver 形 singleShot 受 QObject 析构断连保护，悬挂单发
到期无副作用、进程存活（REPRO_RC=0）。处置：mdl/pipe 探针现形态安全**不改**（工作原则 7）；
代理首轮 abort 系其未入仓的首轮实现（细节不可考）——missing-text-probe 已改用栈上 deadline
QTimer 形态保留（防御性更优，非「修复」）。
