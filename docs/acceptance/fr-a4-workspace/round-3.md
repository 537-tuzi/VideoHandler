# FR-A4 片 B round-3 验证记录（审查轮 + 主线程接手测试轮）

- 基线：HEAD=4a43dba（片 A 合入），片 B 改动未提交形态跑（含 round-3 修复增量）。
- 环境：Linux（GCC/Qt6/FFmpeg apt 栈 + ORT 1.30 third_party），与 ci-smoke 同栈。
- 背景说明：round-2 全绿后开审查 + 测试双代理；审查代理完稿（10 维度 9 PASS / 1 FAIL=P1-1）；
  测试代理两度被 API 限额（429）杀中断（5 小时档复位后复活一次、周/月档 20:51 才复位），
  其未竟的对抗增补面由**主线程接手**（配方兜底：代理角色已履行，残余不阻塞合入）。

## 1. 审查轮（对抗审查代理报告 → 主线程甄别 → 处置）

P0=0。逐条处置（修复均主线程亲手改 + 门禁复跑实证）：

| 级别 | 发现 | 处置 |
|---|---|---|
| P1-1 | **wsInfo 迟到弹窗击穿 busy 拍板**：查询不占 busy 入队后用户启动任务，`set_busy(true)` 期间查询结果到达 → `onWsInfoDone` 无 busy 感知照常 `dlg.exec()`；弹窗里「用此版本继续」→ `openFile` 无条件解禁七槽+历史按钮 → 用户二次提交被 MediaService busy 检查静默吞（mediaservice.cpp:64）、且按钮态与真实任务态错位；`onWsInfoDone` 首行「就绪」还会抹掉运行中状态行 | **修复**（采纳审查倾向方案 a）：`taskRunning` 成员（`set_busy` 维护）+ `openFile` busy 分支（运行中不解禁、不覆盖状态行/流信息区；curFile/lblFile/probe 排队照常——任务完成后 `set_busy(false)` 统一恢复，probe 在任务后 FIFO 执行）+ `onWsInfoDone` 状态行 busy 感知。对话框在任务中迟到弹开本身保留（如实呈现迟到结果），「继续」经修复后行为确定正确：换文件 + probe 排队 + 按钮保持锁定；任务完成框叠加弹出为 Qt 模态叠加确定行为（消息已传达，不再加状态机——不过度设计）。代码级自证（三环均主线程预审亲读：onWsInfoDone 无 busy 检查 / openFile 八连无条件 setEnabled / busy 静默吞），GUI 运行时表现随 Windows round-4 |
| P2-2 | gui-ws.sh 探针链 libengine.a 无陈旧库闸（编辑后忘构建 → 静默测旧码假绿） | **修复**：编排头部加增量构建闸（`cmake --build "$BUILD"`——构建系统是「产物相对依赖新鲜」的权威判定，无变化亚秒级；比手搓 mtime 比对语义准确：mainwidget.cpp 不在 libengine.a，全 src mtime 闸会误报） |
| P3-3 | gui-ws.sh FAIL 分支文案「留 $W」与 EXIT trap 删 $W 矛盾（误导排查） | **修复**：FAIL 分支 `trap - EXIT` 保留现场，其余路径照常清理 |
| P3-4 | QSettings 存过空串（手改等）→ 任务静默 direct 而历史查询报「root 不能为空」+ 标签「工作区：」两副面孔 | **修复**：读侧归一（空串 → 缺省根，一处 if；写侧 UI 永不写空——`getExistingDirectory` 空即 return） |
| P3-6 | GWS-007b 脚本打印断言但 matrix.csv 无行（账面不齐） | **修复**：matrix 补行 |
| P3-5 | doWsInfo 无取消语义（大文件首查哈希数十秒不可中断） | **记档**：查询路径与 busy 不互斥的设计使然，入口在 P1-1 修复后仍受按钮态保护；真机体验随 Windows round-4 评估是否加取消 |
| P3-7 | MediaService 两对转发信号槽（root 注入/wsInfo）无运行时用例 | **记档**：转发对形态与既有九对同型（probe 先例），gui-ws 探针直调 worker 槽绕过转发层——转发层运行时验证随 Windows round-4 GUI 真机 |

审查未收敛面（诚实转录）：P1-1 为代码级推演（Qt 嵌套事件循环语义）无 GUI 自动化复现手段；零警告未独立复核；
doWsInfo 空根负例未 e2e（GWS-011 置空面已盖「空串=不插键」，GUI 层空串被读侧归一消灭，面收敛）。

## 2. 测试轮残余（主线程接手对抗增补：GWS-008..011）

测试代理 429 前已完成：run-json 36 / thinshell-eqv 双跑复验（其转录在任务输出文件）。残余对抗面主线程
以探针第四相 `adv` 落地并进 gui-ws.sh 常驻（gui-ws-probe.cpp 加相 + 编排断言）：

- **GWS-008 换根只影响后续任务**：同输入 st.wav 先后落 root1/root2 → 两根各自独立工作区、
  版本号都从 v001 起、各恰 1 版本（索引 head=1 锚「互不可见」）。
- **GWS-009 中文路径**：中文根（`<root>_中文根`）+ 中文素材（`中文素材.wav`）→ 工作区名
  `中文素材.<hash8>`、产物名 `中文素材_denoised.wav` 全程直通（正则双锚路径结构）。
- **GWS-010 wsInfo 失败载荷**：输入不在盘 → `ok=0` + 非空错误说明（`onWsInfoDone` 警告分支的数据面，
  round-2 无覆盖）。
- **GWS-011 注入后置空回 direct**：`setWorkspaceRoot("")` → 不插键、产物落源目录、rootcn 仍 head=1
  （若误插键会出 v002）——与 GWS-005 默认空串对称，换根 UI「只换不清空」的兜底语义闭环。

未接手项说明：mainwidget offscreen 冒烟（渲染层无自动化手段，挂 Windows round-4 GUI-WIN）；
symlink 类比（direct 模式 pre-existing，片 A round-1 §10 已挂 D-2 与 fr24 P3-3 同域记档）。

## 3. 修复后门禁复跑（全部主线程亲手）

| 门禁 | 结果 |
|---|---|
| `cmake --build --preset linux-test` | 零警告（grep warn/error 零命中） |
| gui-ws.sh（含新陈旧库闸 + adv 相） | 全绿（原 39 PASS + adv 相 5 PASS）；两轮复跑均绿 |
| gui-slot-eqv.sh（默认基线 fd7badb） | 全绿（转录逐字一致 + 8 产物 cmp + 取消无残留） |
| thinshell-eqv.sh（基线 0c9fc6c） | 全绿（stdout/退出码/产物三方一致） |
| run-ws.sh | 全绿（含 WS-049 ΔRSS=1.2MiB） |
| run-json.sh（含新 keys: 键集断言） | 全绿（CMD-002 direct result 七键恰等；断言器正反例自检非恒真） |
| fake-engine-e2e.sh（基线=片 B 7 文件） | 全绿（金标准：新增引擎三处文件，命令层/GUI/服务层零改） |
| ci-smoke 全量 | 总闸（见提交前记录；本轮已把 gui-ws.sh 接进 ci-smoke，GWS 族随总闸常驻） |

## 4. 改动面（round-3 增量，均在片 B 未提交形态上）

- `src/view/mainwidget.h/.cpp`：P1-1（taskRunning + openFile/onWsInfoDone busy 感知）+ P3-4（读侧归一）。
- `scripts/gui-ws.sh`：P2-2 增量构建闸 + P3-3 trap 保留现场 + adv 相编排（GWS-008..011 断言）。
- `scripts/gui-ws-probe.cpp`：第四相 adv（换根/置空/失败载荷/中文路径转录）。
- `matrix.csv`：GWS-007b 补行 + GWS-008..011 四行。
