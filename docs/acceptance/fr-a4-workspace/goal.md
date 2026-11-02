# FR-A4 工作区版本化 总目标

## 目标

落地工作区版本化核心（`src/service/workspace.{h,cpp}`）并接通 `--json` 命令面：带 `workspace`
请求字段的任务不再落盘到源目录，而是进集中式工作区根（`<stem>.<hash8>/vNNN_<cmd>/`），每次任务
登记一条可追溯的版本记录（来源链：filled 参数 / 输入哈希 / 模型摘要 / 父版本 / 输出真值），
产物目录 append-only、原件只读红线由机制保证（两段提交：产物落盘 → QSaveFile 原子登记，登记失败
= 任务失败且删产物）。direct 模式（不带 workspace）行为零漂移（thinshell/run-json 门禁）。
规格契约：`docs/spec/fr-a4-workspace.md`（D1–D8 + §7.1 主线程拍板 OQ-1..6）。

## sub goal matrix

| sub goal | 内容 | 状态 |
|---|---|---|
| SG-1 片 A：工作区核心 + CLI（--json）面 + 验收基建 | `workspace.{h,cpp}`（planVersion/commitVersion/discardVersion/queryInfo：身份解析[缓存三键→根目录哈希扫]→目录规划[跳孤儿]→两段提交）+ `commandlayer.cpp` 接缝（workspace/output 互斥、probe·wsInfo 拒收、runInWorkspace 外壳、wsInfo 入内置表、extract 族 outDir）+ `fftranscoder` OutInfo.truncated（OQ-5）+ `ffextractor` outDir 参数 + `data.h` 三常量 + CMake 两行 + `scripts/run-ws.sh`（WS-001..049 十族）+ 门禁全量回归（run-json 36 / thinshell-eqv 16 / fake-engine-e2e / ci-smoke 新接线 run-ws） | **round-1 全绿**：WS-001..049 全 PASS（0 SKIP，含 WS-030b onnx 注入）+ run-json 36 零漂移 + thinshell-eqv 16/16（基线 fa15432）+ fake-engine-e2e 全绿（基线清单同步后重跑）+ ci-smoke 全量（run-ws 已接线）+ 构建零警告 + 改动面自证（`git diff --stat fa15432 -- src/ CMakeLists.txt` 恰 7 改 + 2 新）；round 内发现并修复产品 bug 1 处（同内容跨 stem 身份断裂，见 round-1 §3）；不可构造面 3 处以代码级映射收口（round-1 §6）；**WS-WIN PENDING**（Windows 真机随 WP-1.0 round-4）；**审查轮已过（round-1 §9）**：P0=0，P1×1（索引类型损坏→版本史静默清空，主线程复现后修 loadWsIndex 三键类型校验 + WS-036b/c/d 回归门禁）+ P2-1 goal 口径矛盾（SG-2 改齐 OQ-2 无开关硬切）已修，P2-2 direct result 键集门禁挂片 B 收尾轮，P3×8 三修两记档三维持；修复后 run-ws 58 PASS 全绿；**测试轮已过（round-1 §10）**：PASS 有条件——双二进制锚定全门禁双跑（run-ws 58×6/run-json 36×2/thinshell 16:16×2）、对抗 8 族未击穿、P1 三路独立 + 读路径同洞一并收敛；新挂账 D-2（direct 模式 symlink 绕过 output==input 防线，pre-existing、主线程已复现，与 fr24 P3-3 同域，随 direct 语义域统一拍板） |
| SG-2 片 B：GUI 工作区接线 | GUI 七槽**缺省走工作区、无开关硬切**（spec D5/OQ-2 拍板：机制不是选项，不加「关闭工作区」开关，QSettings 只记根路径不记开关）、继续按钮（OQ-3：对旧产物继续加工 = 以产物为输入开新任务）、任务完成文案带工作区路径 | **round-2 全绿**：GWS-001..007（含 001b/005b 子断言共 39 PASS）新探针 gui-ws-probe.cpp + 编排 gui-ws.sh 绝对断言全绿（七槽+继续链路径结构/双工作区 workspace.json 结构与 parentVersion 链/wsInfo 深度一致+继续回拼在盘/原件 sha256 不变/取消索引逐字节不变无残留/probe 不受注入影响/direct 落源目录与 CLI 产物 cmp 一致）；硬门禁复跑全绿（run-json 36 / thinshell-eqv 0c9fc6c 16 / gui-slot-eqv 默认基线 fd7badb 两相转录逐字一致+产物 cmp / run-ws 含 WS-049）+ 构建零警告 + 改动面自证恰 7 文件改 + 2 新（CMake 零改）；round 内仅脚本侧修复（gui-ws.sh 缺 mkdir 致空洞 PASS → 补前置完整性闸，产品代码零缺陷）。**round-3 审查+测试轮过（round-3.md）**：审查 P0=0，P1-1（wsInfo 迟到弹窗击穿 busy 拍板）主线程修（taskRunning 成员 + openFile/onWsInfoDone busy 感知）、P2-2（gui-ws.sh 陈旧库闸=增量构建）+P3-3/4/6 修、P3-5/7 记档 Windows round-4；测试代理两度被 429 限杀、残余对抗面主线程接手为 adv 相常驻（GWS-008..011：换根只影响后续任务/中文路径直通/wsInfo 失败载荷/置空回 direct）；片 A 挂账 P2-2（direct result 键集门禁）落地 json_check `keys:` 断言 + CMD-002 七键恰等（正反例自检非恒真）；修复后全部门禁主线程复跑绿（gui-ws 44 PASS/gui-slot-eqv/run-ws/run-json/ci-smoke）；**GUI-WIN PENDING**（对话框渲染/QSettings·QFileDialog 行为/转发层运行时随 WP-1.0 round-4） |

## 重大决策

- **OQ-1..6 主线程拍板转录（spec §7.1）**：OQ-1 工作区根缺省 `~/VideoHandler-Workspace`（请求字段显式给根，片 A 不落死缺省）；OQ-2 无全局开关（per-request 字段）；OQ-3 片 B 继续=以产物为输入的新任务；OQ-4 workspace 模式不接受显式 output（互斥违例 E_PARAMS，文案「output 与 workspace 不能同给」对齐「output 不能与 input 相同（原件只读）」句式）；OQ-5 truncated 进本包（result 与版本记录各带一份）；OQ-6 v1 无锁（D7 单写者假设）。
- **身份解析双层（D1 落实补强，round-1 首跑实证必需）**：缓存命中（path+size+mtimeMs 三键一致且目录仍在）免哈希；miss 先算 sha256 再**扫根目录找 `*.<hash8>` 既有工作区**——同内容换路径/换 stem 不另立门户（stem 只是助读，先登记者留名），无命中才以输入 stem 铸新名。缺扫描则 WS-006 红灯（同内容经 alias.wav 进入会开出第二个工作区，身份断裂）。queryInfo 同路复用。
- **版本号语义：目录在即不复用，目录删则号可回收**：nextFreeVersion 取 max(head, 清单最大号) 后递增、跳过已占目录名（kill -9 孤儿不阻后续、号不复用防新旧混淆）；被 discard 的占位目录已整目录删除且索引从未登记，其号回收无歧义（WS-030b 补删 v005 → WS-033 复用 v005；预置 v007 → WS-037 跳到 v008，两头都有实证）。
- **两段提交不变式（D2）**：plan（定位/建工作区/占位版本目录）→ 任务（产物落盘进版本目录）→ commit（重载索引→追加记录→QSaveFile 原子覆盖）；任一环失败 → discardVersion 补删版本目录、索引不动。失败清理三个落点全部 e2e 实证：plan 段（只读根 WS-029）、任务段（维度错位模型 E_ENGINE WS-030b）、登记前校验段（缺模型 E_MODEL WS-030——runEngine 在 plan 前就拒）。
- **缓存是加速层不是真相源**：根级 `index.json`（路径缓存）损坏按空处理不拦任务（WS-035）、写失败静默；权威 `workspace.json` 损坏 = E_FILE 拒任务（WS-036，真相源坏不可静默重建）。
- **模型摘要 path 记绝对路径**（`QFileInfo::absoluteFilePath`）：请求里 model 是相对还是绝对路径不该影响记录的可追溯性。
- **fake-engine-e2e 基线清单滚动更新**：脚本断言「拷贝清单 == 主树 vs HEAD 实际漂移」防静默漏测；NFR-M1 片 A/B 已在 HEAD，本轮把清单收敛为 FR-A4 片 A 的 9 文件并复跑全绿（这正是该断言的设计目的）。

片 B round-2 实现中拍板：

- **workspace 键单点插入 = runRequest 顶部**（非七槽各插）：`workspaceRoot` 非空才组键，七个任务槽共用一处、不可能漏插；probe（doProbe 直调静态 probe 不经 runRequest）与 wsInfo（doWsInfo 自组请求）**结构性排除**——「probe 槽永不插键」由代码结构保证而非约定。收尾与文案重建逻辑零改动。
- **wsInfo 的 root 取 worker 注入成员**（GUI 只传 input）：查询与任务请求同根，历史查的正是任务落的；mediaservice 不存 root 成员（无人消费，最小面）。回传 `wsInfoDone(ok, QString)`——ok 时 payload 为 result 的 Compact JSON、失败为 error message（免自定义 QMetaType，spec D5 同款约定）。
- **「处理历史」「修改」busy 期间禁用**：wsInfo 不占 busy 锁（probe 同型只读先例），但 worker 单线程——任务期间查询排队会迟到弹出，干脆 GUI 侧锁住（无 curFile 时禁用是 spec D5 原文，busy 禁用是实现补充拍板）。
- **「用此版本继续」遇产物已被用户清理**：提示「版本产物已不在盘上：<路径>」不调 openFile（对话框数据可能陈旧，历史记录如实保留不等于产物在盘）；正常路径 = 回拼绝对路径 `result.dir + '/' + output.path` → openFile（与 doOpen 共用核心，OQ-3）。
- **mediaservice 四条槽注释随行为更正**：「输出到源目录」→「输出到工作区版本目录（根未注入时落源目录）」——注释跟行为（工作原则 10 的代码内面），七对信号槽**签名零改**不变。
- **gui-ws.sh 前置完整性闸**：素材存在且哈希非空 + ws 相建成两工作区索引，否则硬退——首跑自暴露「缺 mkdir → 空串==空串 空洞 PASS」假绿面，绝对断言脚本必须自带前置检查（防假绿比防红灯重要）。

round-3 审查+测试轮拍板：

- **P1-1 修方案 a（busy 感知而非缓存延迟弹出）**：`taskRunning` 成员（set_busy 维护）+ `openFile` busy 分支（不解禁按钮/不覆盖状态行，probe 照常排队任务后执行）+ `onWsInfoDone` 状态行 busy 感知。迟到弹窗本身保留（如实呈现迟到结果），「继续」在 busy 中行为确定正确；拒绝缓存 payload 延迟弹出方案（任务完成后突然弹历史对话框，行为更怪）与对话框内再设按钮态机（过度设计）。
- **测试代理被 API 限额（周/月档 429）两度中断**：其已交付 run-json/thinshell 复跑转录；残余对抗面由主线程接手落地为探针 adv 相 + gui-ws.sh 常驻断言（GWS-008..011），不阻塞合入；mainwidget 渲染层冒烟无自动化手段，挂 GUI-WIN。
- **陈旧库闸用增量构建而非 mtime 比对**：`cmake --build "$BUILD"` 是「产物相对依赖新鲜」的权威判定（无变化亚秒）；手搓 mtime 闸对不在 libengine.a 的 mainwidget.cpp 会误报（编辑 view 层触发全 src 闸但库无需重链）。
- **direct result 键集门禁（片 A P2-2 挂账落地）**：json_check.py 加 `keys:<path>=<逗号列表>` 断言（sorted 恒等），CMD-002 direct transcode result 七键恰等（durationSec/engineResampled/frames/output/outputSpec/sourceSpec/truncated）——防 workspace 族键未来意外泄漏 direct 面；断言器正反例自检非恒真。

## 关键信息

- 工作区请求形态：`{"v":1,"cmd":"denoise","input":"...","workspace":"/path/to/root"}`；成功响应 result 增量带 `workspace:{root,dir,version}`（direct 模式无此键）；查询动词 `wsInfo`（root+input，只读不落盘）。
- extract 族在工作区模式只给目录不给文件名（`outDir` 参数），缺省命名规则仍是底座单一来源（依赖源流编解码器）；转码族产物名 = 输入 stem + 注册条目 `outputSuffix(args)` + `.wav`（后缀单一来源）。
- 转码族 direct 模式 `--json` result 新增 `truncated` 键（OQ-5 落点，纯增量；旧 7 动词 stdout/退出码/产物零漂移）。
- 内存有界：哈希 64KiB 分块流式（NFR-P4 同口径），WS-049 实测 30min 素材首任务（含全量哈希）峰值 RSS 与 direct 同量级（Δ=1.0MiB）。
- Windows 真机（WS-WIN）：文件原子性（QSaveFile rename 语义）、只读根负例、路径分隔符行为随 WP-1.0 round-4 补验。
