# FR-A4 片 B round-2 验证记录（GUI 工作区接线）

- 基线：HEAD=4a43dba（片 A 合入），工作树不提交，全部门禁在未提交形态跑。
- 构建：`cmake --build --preset linux-test` 零警告（增量 + 收尾全量 grep warn/error 零命中）。
- 环境：Linux（GCC/Qt6/FFmpeg apt 栈 + ORT 1.30 third_party），与 ci-smoke 同栈。
- 改动面：恰 `src/model/data.h`、`src/service/ffextractor.{h,cpp}`、`src/service/mediaservice.{h,cpp}`、
  `src/view/mainwidget.{h,cpp}` 七文件 + 本目录 `scripts/gui-ws-probe.cpp`、`scripts/gui-ws.sh` 两新文件；
  CMake 零改（三组源文件均已在构建），`git status --short` 自证无其他漂移。

## 1. 新增 GWS 用例族全绿（判据总表 matrix.csv，gui-ws.sh）

```
bash docs/acceptance/fr-a4-workspace/scripts/gui-ws.sh build-linux
→ == gui-ws 全绿 ==（exit 0；39 个 PASS 行，0 FAIL）
```

编排形态：直调 FFExtractor 槽的探针（gui-ws-probe.cpp，fr24 gui-slot-probe 同型：QCoreApplication +
connect jobDone/progress/probeDone/wsInfoDone + run() 转录）offscreen 三相跑，断言全部为**绝对断言**
（基线无此功能，不与基线对照）：

- **GWS-001/001b（ws 相七槽+继续链）**：`setWorkspaceRoot(root)` 后 extractAudio/extractVideo/denoise/
  aiDenoise/pitch/sepVocals(vocals)/sepVocals(instrumental) 七槽全部 `ok=1`，转录逐行正则断言
  「`TASK <槽> -> ok=1 msg=<XX完成文案>：<root>/[^/]+/vNNN_<cmd>/<产物名>$`」——文案格式逐字不变
  （OQ-2 拍板的「前缀+结构」双段式）+ 八个版本目录与产物在盘（含继续链 v006_denoise/）。实测转录：
  ```
  TASK denoise -> ok=1 msg=降噪完成：<wsroot>/st.0b2bb088/v001_denoise/st_denoised.wav
  TASK pitch -> ok=1 msg=变调完成（2 半音）：<wsroot>/st.0b2bb088/v003_pitch/st_pitch.wav
  TASK sep-inst -> ok=1 msg=伴奏提取完成：<wsroot>/st.0b2bb088/v005_separate/st_instrumental.wav
  ```
- **GWS-002（结构）**：python 断言 st 工作区 head=6/versions=6/动词序列
  `[denoise,aiDenoise,pitch,separate,separate,denoise]`/前 5 版 parentVersion=null 且 v006←v001
  （继续链）；tiny 工作区 head=2/versions=2（extract 族入工作区）。
- **GWS-003（原件只读）**：三相全程结束后 st.wav/tiny.mp4 sha256 与素材生成后基线一致。
- **GWS-004（查询面）**：探针 `doWsInfo` → `WSINFO wsInfo2 -> ok=1 <Compact JSON>` 转录，python 比对
  dir/head/versions/source 与 workspace.json **深度相等**；全部版本产物（含「用此版本继续」回拼路径
  `result.dir + '/' + output.path`）在盘。`CHAIN product=<wsroot>/st.<hash8>/v001_denoise/st_denoised.wav`
  行断言回拼正确。
- **GWS-005/005b（direct 零漂移）**：探针 direct 相**不调 setWorkspaceRoot**（worker 成员空串）→
  tiny_audio.m4a/tiny_mute.mp4/st_denoised.wav 落**源目录**（fr24 GUI-PRD 同形状）；GUI 槽 direct 产物与
  `video_handler --denoise-wav` CLI 直跑产物 `cmp` **逐字节一致**（跨面互证：runRequest 未插键）。
- **GWS-006（取消收口，spec §6-5）**：cancel 相预置 cancelFlag → `TASK cancel-denoise -> ok=0 msg=已取消`、
  `cancel-extractAudio` 同（mainwidget 特判字符串逐字命中）；两工作区 workspace.json **逐字节不变**、
  版本目录数不变（st 恰 v001..v006、tiny 恰 v001..v002，无 v007 残留）。
- **GWS-007（probe 不受注入影响）**：root 注入后 `TASK probe -> ok=1 msg=容器：…` 照常（probe 拒收
  workspace，若被无脑插键即 E_PARAMS 全挂）；direct 相同证。

## 2. 硬门禁复跑（direct 零漂移 + 片 A 不回归）

| 门禁 | 命令 | 结果尾行 |
|---|---|---|
| fr24 run-json 36 | `bash docs/acceptance/fr24-command-layer/scripts/run-json.sh` | `== run-json 全绿 ==` |
| fr24 thinshell-eqv | `bash docs/acceptance/fr24-command-layer/scripts/thinshell-eqv.sh 0c9fc6c build-linux` | `== 薄壳等价全绿：基线 0c9fc6c vs 新薄壳，stdout/退出码/产物三方一致 ==` |
| fr24 gui-slot-eqv | `bash docs/acceptance/fr24-command-layer/scripts/gui-slot-eqv.sh`（默认基线 fd7badb） | `== gui-slot-eqv 全绿 ==`（A/B 两相转录逐字一致 + 8 产物 cmp + 取消无残留） |
| 片 A run-ws | `bash docs/acceptance/fr-a4-workspace/scripts/run-ws.sh build-linux` | `== run-ws 全绿 ==`（含 WS-049 ΔRSS=1.0MiB） |

fr24 既有脚本与 probe 语义**零改动**（`git status` 自证 fr24 目录无漂移）。gui-slot-eqv 全绿即
spec D5「jobDone 文案重建零改动」的运行时实证：同一探针源双树双跑转录逐字一致（默认无 root 的
direct 行为不变），gui-ws.sh 再补绝对断言盖住有 root 的新行为面。

## 3. 实现落法要点（与 spec D5/D7 对照）

- **线程边界照抄 spec 形状**：mainwidget 读 QSettings（`workspaceRoot` 键，缺省
  `~/VideoHandler-Workspace`，常量 `kSettingsWorkspaceRoot`/`kWsDefaultRootName` 单一来源 data.h）→
  `MediaService::setWorkspaceRoot` → 队列信号 `workspaceRootRequested` → worker 槽
  `FFExtractor::setWorkspaceRoot`（装配期 setCancelFlag 同型）；mediaservice 只加两对信号槽
  （root 注入 + wsInfo 查询），**七对既有业务信号槽签名零改**（仅四条「输出到源目录」注释改为
  「输出到工作区版本目录（根未注入时落源目录）」——注释跟行为，签名未动）。
- **workspace 键单点插入**：`FFExtractor::runRequest` 顶部 `workspaceRoot` 非空才插键（收尾/文案重建
  零改动）——七个任务槽共用一处、结构性排除 probe（doProbe 直调静态 probe 不经 runRequest）与
  wsInfo（doWsInfo 自组请求）；空串不插键 = CLI/默认构造零漂移（GWS-005 双证）。
- **wsInfo 查询对**：`wsInfoRequested(input)` → `doWsInfo(input)`（root 取注入成员，与任务请求同根，
  历史查的正是任务落的）→ execute → result 序列化 Compact JSON 经 `wsInfoDone(ok, QString)` 回传
  （失败回传 error message），mediaservice 转发 `wsInfoFinished`——**不占 busy**（probe 同型先例）。
- **GUI 面**：顶部工作区行（`工作区：<root>` + 「修改」（QFileDialog + QSettings 持久化 + 下传）+
  「处理历史」按钮（无 curFile 禁用；busy 期间禁用——查询排 worker 队会迟到，干脆锁住））；
  doOpen 核心提取为私有 `openFile(path)` 与「用此版本继续」共用（OQ-3）；历史对话框 QTableWidget
  五列（序号/时间/动词/参数摘要/输出路径）+「打开目录」（QDesktopServices 开版本目录）+「用此版本
  继续」（回拼产物绝对路径，盘上不存在时提示「版本产物已不在盘上」不误开）；`onJobDone` 的
  「已取消」特判一行未动。
- **无开关硬切**（OQ-2）：无任何「关闭工作区」选项；QSettings 只记根路径。D7 边界：mediaservice/
  ffextractor 不 include QSettings（命令层/工作区模块零配置读取）。

## 4. round 内发现并修复（脚本侧，不改判据）

gui-ws.sh 首跑红灯自暴露：素材循环缺 `mkdir -p` → ffmpeg 全败 → 空哈希/空路径使多个断言**空洞
PASS**（`[ "" = "" ]` 恒真）。修复：补 mkdir + 两道前置闸（素材存在且哈希非空、ws 相建成两工作区
索引，否则硬退）——这正是该脚本断言「绝对断言」必须自带的前置完整性检查，防假绿比防红灯更重要。
产品代码本轮零缺陷（ws 相首跑即全绿）。

## 5. 弱点与未验证面（诚实面）

- **历史对话框渲染未经 e2e**：探针覆盖到 wsInfoDone 信号的 QString 载荷（含深度一致断言），但
  QTableWidget/按钮/`openFile` 续链的**渲染层**只有代码级正确性，无自动化（对话框需人眼/Windows
  真机）；「继续」链路的槽级行为由 GWS-001 chain-denoise + GWS-004 回拼在盘双侧证。
- **btnWsChange/btnHistory busy 联动、换根后旧工作区不可见**：换根只影响后续任务（spec 未定义
  迁移），历史查询按当前根——行为符合 D1 集中式根语义，但无专门用例（round-2 GUI 面未开）。
- **Windows 面全挂账**：QFileDialog/QSettings 路径、QDesktopServices、中文路径（已全用
  QStringLiteral）、对话框布局，随 WP-1.0 round-4 真机。
- fake-engine-e2e 的基线清单断言（主树 vs HEAD 漂移）在本片合入后需滚动更新（SG-1 同款惯例），
  随主线程提交时处理。
