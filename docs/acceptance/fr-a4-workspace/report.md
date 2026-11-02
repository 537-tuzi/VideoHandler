# FR-A4 工作区版本化 验收报告（收官）

**状态：全绿收官**（片 A + 片 B 两 sub goal 全部达成；Linux 验收面闭环，Windows 真机面挂账随 WP-1.0 round-4）

## 1. 目标与达成

落个工作区版本化机制：带 `workspace` 字段的任务产物落 `<根>/<stem>.<hash8>/vNNN_<cmd>/` 集中式
版本目录（append-only、原件只读），每次任务登记可追溯来源链（filled 参数/输入哈希/模型摘要/
parentVersion/输出真值），direct 模式（不带 workspace）行为零漂移。

| sub goal | 达成 | 证据 |
|---|---|---|
| SG-1 片 A：核心 + CLI 面 | ✅ | round-1（58 用例含审查/测试轮补强 WS-018b/036b/c/d），commit 4a43dba |
| SG-2 片 B：GUI 接线 | ✅ | round-2（39 PASS）+ round-3（审查轮修复 + adv 相 5 PASS = 44 PASS），commit 6e467d4 |

## 2. 落地形态（三行摘要）

- **核心**（`src/service/workspace.{h,cpp}`）：planVersion/commitVersion/discardVersion/queryInfo 两段提交
  外壳；身份 = 内容哈希（根级缓存三键加速 + 根目录扫描复用既有工作区）；QSaveFile 原子登记，
  登记失败 = 任务失败删产物不留孤儿；64KiB 流式哈希（NFR-P4 同口径）。
- **CLI 面**（commandlayer 接缝）：`workspace` 与 `output` 互斥、probe/wsInfo 拒收、result 增量带
  `workspace:{root,dir,version}`、`wsInfo` 只读查询；七旧动词/GUI 七槽同为薄壳。
- **GUI 面**（片 B）：七槽缺省走工作区**无开关硬切**（OQ-2）；QSettings 只记根（D7 只在 GUI 层）；
  注入链 mainwidget→MediaService 队列→worker 槽；workspace 键单点插入 `runRequest`；历史对话框
  （五列 + 打开目录 + 用此版本继续，OQ-3 继续链）；busy 期间迟到交互不击穿任务锁（round-3 P1-1）。

## 3. 验收轨迹

| 轮 | 内容 | 结果 |
|---|---|---|
| round-1 | 片 A 58 用例（十族）+ 审查轮（P1 索引类型损坏静默清空 → 修 + WS-036b/c/d 回归门禁）+ 测试轮（双二进制锚定全门禁双跑、对抗 8 族未破、D-2 挂账） | 全绿 |
| round-2 | 片 B GWS-001..007b（探针直调槽三相：ws/cancel/direct）+ 硬门禁复跑 | 全绿 |
| round-3 | 审查轮（P0=0；P1-1 busy 击穿修复 + P2-2/P3×4 处置）+ 测试轮残余主线程接手（adv 相 GWS-008..011）+ 片A挂账 P2-2 键集门禁落地 | 全绿 |

最终门禁（全主线程复跑）：ci-smoke 138 PASS（parity 四族 + CMD 36 含键集断言 + WS 58 + GWS 44）+
thinshell-eqv + gui-slot-eqv + fake-engine 金标准全绿；构建零警告。

## 4. 关键拍板（全记录见 goal.md）

集中式根缺省 `~/VideoHandler-Workspace`（OQ-1）；无开关硬切（OQ-2）；继续 = 以产物为输入的新任务
（OQ-3）；workspace/output 互斥（OQ-4）；truncated 进 result 与版本记录各一份（OQ-5）；v1 无锁
单写者（OQ-6）；身份解析双层（缓存三键 → 根扫 `*.<hash8>` 复用）；版本号目录在即不复用；
权威索引损坏 E_FILE 拒任务不静默重建；模型摘要记绝对路径；busy 感知方案 a（taskRunning 成员，
拒绝缓存延迟弹出）。

## 5. 遗留面（挂账清单）

- **Windows 真机**（随 WP-1.0 round-4）：WS-WIN（QSaveFile rename 原子性/只读根/路径分隔符）、
  GUI-WIN（对话框渲染/QFileDialog·QDesktopServices·QSettings 行为/MediaService 转发层运行时/
  P1-1 运行时表现/wsInfo 取消体验 P3-5）。
- **D-2（pre-existing）**：direct 模式 symlink 绕过 output==input 防线——与 fr24 P3-3 同域，随
  direct 语义域统一拍板（canonicalFilePath 方向）；workspace 模式天然免疫。
- **P3-5 wsInfo 无取消语义**：大文件首查哈希数十秒不可中断，真机体验评估后定。
