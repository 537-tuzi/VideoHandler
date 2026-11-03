# FR-8 模型管理验收 goal

## 总目标

落地 FR-8（spec `docs/spec/fr8-model-management.md`，§7.1 八项 OQ 已拍板）：片 A = 模型管理核心
（内置目录表 + models.json 清单 + modelList 三视角合并 + QNAM 下载器 + 两形态导入）+ 命令面三动词
（modelList/modelDownload/modelImport）+ 验收基建（range_server + run-models + 取消探针）；
片 B = GUI 面（D1 两级缺省解析接线 + D8 结构化 modelMissing 引导 + 模型管理对话框 + QSettings 新键）。

## sub goal matrix

| sub goal | 内容 | 状态 |
|---|---|---|
| SG-A1 | 基建：data.h 三常量（kModelsIndexFile/kModelsDefaultRootName/kDefaultModelEndpoint）+ errinfo 七类码（Net/E_NET，OQ-1） | round-1 PASS |
| SG-A2 | ModelManager 核心：目录表（2 条目，sha/URL 与 fetch-deps.sh 同源）/清单读写（QSaveFile，损坏按空）/三视角合并/下载器（HEAD 解析→ETag 对照→Range 续传→64KiB 增量写→sha256 收尾→同卷 rename→登记）/导入（by-id 指纹 + 自由导入 setup 试开） | round-1 PASS |
| SG-A3 | 命令面：三动词处理器 + BuiltinVerb.requiresInput 信封分叉（旧动词零漂移）+ 错误映射（E_PARAMS/E_FILE/E_MODEL/E_NET/E_CANCELLED） | round-1 PASS |
| SG-A4 | 壳与构建：main.cpp --json/CLI 分支 QCoreApplication（QNAM 事件循环前提）；CMake Network 组件 + modelmanager 两行 + Windows 部署补 Qt6Network.dll | round-1 PASS |
| SG-A5 | 验收基建：range_server.py（Range 206/注入/请求日志）+ run-models.sh（MDL 族）+ mdl-cancel-probe.cpp（取消/bundled） | round-1 PASS |
| SG-A6 | 门禁全绿：run-models / run-json / run-ws / thinshell-eqv / gui-slot-eqv / fake-engine-e2e（基线滚动）/ ci-smoke 全量 / 构建零警告 | round-1 PASS |
| SG-B1 | ffextractor：resolveDefaultModel 两级缺省解析（D1：modelList 查 for==verb 且 installed → appDir bundled；根空串不查库 = 旧行为零漂移）+ runRequest E_MODEL → 结构化 modelMissing + 四新槽（setModelRoot/doModelList/doModelDownload/doModelImport，全经 CommandLayer::execute） | round-3 PASS |
| SG-B2 | mediaservice：四请求信号槽 + 三完成信号转发 + modelMissing 转发；modelList 不占 busy（wsInfo 先例）、下载/导入占 busy + cancelFlag（D3） | round-3 PASS |
| SG-B3 | mainwidget：「模型管理…」按钮 + 对话框（QTreeWidget id/用途/状态/许可证/大小、下载/取消/进度条、导入两段含 §161 提示、endpoint 组合框、换根、打开目录）+ modelMissing 问询引导（含 F-2 解药附注）+ QSettings 两键（modelRoot/modelEndpoint，D7 只在主线程读写） | round-3 PASS |
| SG-B4 | 验收基建：mdl-gui-probe.cpp（engine 槽直调 / gui offscreen 全链两模式）+ range_server --chunk-sleep-ms 节流 + run-models.sh GML 段（36 用例） | round-3 PASS |
| SG-B5 | 门禁：run-models 133 PASS（95 MDL 含 024t + 36 GML + 2 前置闸）/ gui-slot-eqv / thinshell-eqv / fake-engine-e2e（基线滚动至片 B 7 文件）/ gui-ws / 触碰式重编译零警告全绿；ci-smoke 在本 worktree CI-RC=1——唯一 FAIL = WS-019 为 third_party 软链环境伪差（realpath vs cwd 绝对化；同一二进制主树 cwd 通过，登记代码零 diff，根因链 round-3.md），主树复跑应全绿 | round-3 PASS（WS-019 伪差除外） |

## 重大决策与关键信息

- **Qt 6.10.2 移除 `QNetworkRequest::setRedirectPolicy`**：改经 `RedirectPolicyAttribute`
  （int 取 RedirectPolicy 枚举）承载；缺省本即 NoLessSafeRedirectPolicy，显式钉住语义（NFR-C1）。
- **QNAM 掐断实测行为（round-1 关键发现）**：服务器 premature close 时 Qt 6.10.2 的
  `HttpStatusCodeAttribute` 残留 200 且 `finished` 正常触发——若按「status==200 → 成功」判定会把
  截断传输当完成（首版实现实踩，靠收尾 sha256 兜底拦截后修正）。判定树改为：netError 干净为准；
  HTTP 错误只认 `status >= 400`；416 特判（.part 已全量的残局）。详见 round-1.md。
- **下载器失败分类**（OQ-6 拍板执行）：取消/断连 → .part 保留续传；sha256 不符 → 删 .part；
  200 不支持 Range → 弃 .part 从头重写；登记失败 → 删最终产物。全部由 MDL 用例实证。
- **真网红线**：全部下载用例走本地 range_server.py（127.0.0.1）；GTCRN（GitHub 直连，非 hfSource）
  无法本地重定向，下载 e2e 以 UVR（hfSource，endpoint 可换前缀）承载，代码级论证见 round-1.md。
- **连带基线滚动**：engine 链入 Qt6Network 后，链 libengine.a 的探针编译（gui-slot-eqv.sh、
  gui-ws.sh）PKGS 补 Qt6Network；fake-engine-e2e.sh BASELINE_FILES 滚动到本片 7 文件。

## round-2 处置拍板（审查轮，2026-11-03）

- **P1-1/P2-1（自由导入路径/id 命名空间，同根）**：采纳最小修——占用三规则（dest 撞目录条目
  relPath / 同 id 异 sha / dest 撞其他 id 登记 path → E_PARAMS 拒收；同 id 同 sha 翻新幂等放行），
  结构修（dest 按净化 id 命名）超收口轮范围不采。红绿证据链与同库不可达推论见 round-2.md。
- **并发导入脏终态（A7b，过程发现未入正式分级）**：进程间并发非契约面（--json 单发、GUI busy
  锁、D1–D9 无跨进程锁要求），记档不修；双开场景若成真实反馈再议 flock。主线程未独立复现，
  仅边界记档不作 scope-out 依据。
- **P3-3 记档**：D3 步骤 1「取最终 URL」未实现（GET 重试轮重发原始 URL、重定向重走，语义等效
  多一跳）；真机首次下载人工过目。
- **P3-4 记档**：models.json 损坏可重建仅对目录条目成立（MDL-023/009b 收编路径）；自由导入条目
  损坏需重导入覆盖，不建磁盘扫描兜底（超片 A 范围）。
- **P3-6 记档**：MDL-060i 源不变性由 MDL-999 聚合哈希间接覆盖（matrix 说明列已注明）；
  modelImport id+for 同给时 id 优先（D6 未定义，行为确定）。
- **环境教训（入记忆配方 #9）**：/tmp 为带用户配额的 tmpfs，EDQUOT 下 fwrite 静默失败 →
  MDL-092 三轮「rc=1 + 0 字节响应」假象（根因 = 复现/跑批工作区遗留叠加逼近配额）。对策：
  jr stderr 保留回放、VH_KEEP 保尸、大体积跑批走磁盘卷 TMPDIR、复现脚本 cleanup 必删工作区。
  **F-6 复发**：VH_KEEP 行自身 `"$W"#` 紧贴又漏 324MB/轮（ci-smoke 修过同型坑、run-models 漏改），
  测试代理抓出后修复——「# 紧贴引号」是系统性坑，写脚本注释必须空格开头。
- **F-2 拍板（测试轮新发现，主线程已修）**：幂等命中加「登记 sizeBytes==stat 实值」（与 D1
  bundled「存在+size」同精度），截断/替换型损坏重下治愈（MDL-024t）；**等长翻字节为已知
  边界**——size 不变仍命中，代价 = D1「不逐次算 sha」拍板的固有精度，引擎 setup E_MODEL
  兜底 + 片 B 引导文案给「删除条目重下」解药；与 A7b 并发边界同挂「非契约面已知边界」档。

## round-3 处置拍板（片 B 实现轮，2026-11-03）

- **modelMissing 双触发面**：pre-check（两级皆缺，锁定文案同点位）与 runRequest E_MODEL（引擎
  setup 失败）都发结构化信号——D8 契约文字点名 runRequest，但 spec §3 首用流程（打开对话框引导）
  要求缺文件即引导，双面齐备；无文案前缀匹配（P3-1 教训）。
- **E_MODEL 注入方法（验收）**：appDir 降噪模型写损坏垃圾字节——文件在盘 → 两级解析 pre-check
  放行 → E_MODEL 必出自 runRequest 引擎 setup 失败；AiDenoiser 异常屏障是片 A 已实证路径，
  不赌 vocalseparator 的抛出面。
- **锁定文案 %1 = bundled 路径**（「找不到 AI 降噪模型：%1」/「找不到人声分离模型：%1」）：
  gui-slot-eqv 对缺模型分支锁字面，bundled 是缺省安装形态；库路径不进锁定文案。
- **offscreen QFileDialog 确定性**：`AA_DontUseNativeDialogs` 必须在 QApplication 构造**前**
  setAttribute（portal native helper 竞态会让 exec 挂死，构造后补设无效）——探针曾 124 超时的
  根因。
- **endpoint 记忆双连**：`currentIndexChanged` + `activated` 都写 QSettings——editable 组合框
  `setCurrentText` 只改编辑文本不发前者；用户手填自定义后**重选同号预设**时索引不变，只有
  activated 恒发（产品与探针双修，GML-117 曾因此红过）。
- **F-6 三次复发于本轮新增行**（`"$W"#注释` / ~30 处 line_is / `--chunk-sleep-ms 8#`）：与
  round-2 记录同型，grep `"`# + sed 补空格修复；系统性坑再自证。
- **worktree 特例（合并注记）**：本 worktree 的 modelmanager.cpp = 主线程 7df4772 F-2 修复的
  未提交同步（非本片改动）；fake-engine-e2e 跑批前临时还原 HEAD、跑后恢复（其基线清单 = 片 B
  7 文件；主树合并后 modelmanager.cpp 与 7df4772 无 diff，清单即自然吻合）。
- **GUI 下载 e2e 载体 = uvr**：沿片 A 论证（gtcrn 为 GitHub 直连非 hfSource，无法本地 endpoint
  重定向；uvr 的 hfSource 前缀可换本地 range_server）。

## round-4 处置拍板（片 B 审查+测试轮，2026-11-03）

- **P1-1 两级解析跨 id 遮蔽（已修）**：resolveDefaultModel 加「installed 且非 bundled」过滤。
  bundled 标志语义恰 =「该条目当前解析到随包副本」，过滤后第一级=库内登记、第二级=appDir 兜底，
  与 D1 精确对齐；同 id 场景不受影响（登记分支先判）。红绿证据链 + GML-103b/bt 回归锚见
  round-4.md。**边界记档**：内容校验不在解析层——wrong-content 条目（假 for 导入真 UVR）由
  引擎 setup E_MODEL → 结构化引导兜底（测试代理 post-fix 复验证实契约稳定）。
- **P1-2 D4 镜像适用性注记（已修）**：对话框 endpoint 行下注记标签，GML-110n 锁字。
- **测试轮 D1 刷新选中重置（已修，P2）**：onModelListDone 按 id 恢复选中；后果链（下载键静默
  改目标 → 错击非镜像条目真外联 GitHub）与 GML-115r 锚见 round-4.md。
- **P2-1/P2-3 跨片修复（已修）**：解药文案改「打开库目录删文件」（对话框无删除功能 D5）；
  导入复制环补 onProgress（0–99+收尾 100）——服务/壳层零改（Ctx 进度通道片 B 已接），
  GML-116g 真值锚；MDL-093 兼作其红绿锚（修复前无回调 → 导入跑完必 FAIL）。
- **P3 修复一项记档四项**：downloadModel URL 校验前置于 mkpath（MDL-051d 零落盘断言）；
  记档 = E_MODEL 字面量双源（下次触碰共享常量）/ stale 自由导入行下载 E_PARAMS 边界 /
  GML-114e 文案域断言维持 / 垃圾相对 modelRoot 标签如实呈现。
- **覆盖空白补面**：导入复制环中段取消（MDL-093，首进度回调兼取消锚）；挂账 = Windows GUI
  真机面、openUrl 真实拉起、相对根端到端下载、多进程并发写清单（A7b 同族非契约面）。
- **gui-slot-eqv 盲区注记（审查提醒，防退化）**：modelMissing 新信号对共享探针不可见（基线树
  无此信号），其锁定全靠 GML-101m/107m/114 三钉——**勿删这三条断言**。
- **过程**：双代理 12:05–12:07 因 API 五小时限额中断，12:44 重置后原上下文续跑（已完成取证
  不重跑）；测试代理分段标注源态（pre-fix / post-fix 壳 / post-fix 全量）保证证据可归因。

## 进展

- round-1（2026-11-03）：全绿（MDL 族 84 项断言 + 8 门禁）。证据 round-1.md；用例矩阵 matrix.csv。
- round-2（2026-11-03）：审查轮收口——P1-1/P2-1 修复（红→绿）、+5 用例（023z/062b/c/u/r）
  后 94 PASS 全绿×2 + run-json 36/36；MDL-092 环境根因闭环。证据 round-2.md。
- round-3（2026-11-03）：片 B GUI 面落地 + GML 36 用例；run-models 133 PASS 全绿（含片 A 95 例
  回归）、gui-slot-eqv / thinshell-eqv / fake-engine-e2e / gui-ws 绿、触碰式重编译零警告；
  ci-smoke 在本 worktree 因 WS-019 软链伪差 CI-RC=1（根因链与主树复跑预期见 round-3.md）。
  证据 round-3.md。
- round-4（2026-11-03）：片 B 审查+测试双轮收口——P1-1/P1-2/D1/P2-1/P2-3/校验前置六修复 +
  9 新用例；run-models 142 PASS 全绿、七门禁全绿（含 ci-smoke 280 PASS）；fake-engine 基线
  滚动至 8 文件。证据 round-4.md。
