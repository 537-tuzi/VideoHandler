# round-3：片 B（GUI 面）实现与验收（2026-11-03）

片 B（spec `docs/spec/fr8-model-management.md` D1/D3/D4/D7/D8/D9）落地：两级缺省解析接线、
结构化 modelMissing 引导、模型管理对话框全链、QSettings 新键；验收 = GML 族 36 用例
（mdl-gui-probe 两模式）+ 片 A MDL 族全量回归 + 六门禁。判据总表 matrix.csv（round-3 列）。

## 实现面（7 个 src 文件，+510/−34；CMake 零改——无新源文件入目标）

- `src/model/data.h`：+`kSettingsModelRoot`/`kSettingsModelEndpoint` 两键名（跟随
  `kSettingsWorkspaceRoot` 排布）。
- `src/service/ffextractor.{h,cpp}`：`resolveDefaultModel(for, bundled)`（D1：modelList 查
  `for==verb && status=="installed"` 首条 → appDir bundled 兜底；**根为空串不查库** = 旧槽行为
  零漂移）挂入 doDenoiseAiWav/doSepVocals；runRequest 失败分支读 `error.code`，`E_MODEL` 时
  加发结构化 `modelMissing()`（无文案前缀匹配，NFR-M1 片 B P3-1 教训）；新槽
  setModelRoot/doModelList/doModelDownload/doModelImport 全部组 JSON 走 `CommandLayer::execute`
  （GUI 不直调 ModelManager，单一通路红线）；锁定文案「找不到 AI 降噪模型：%1」/
  「找不到人声分离模型：%1」逐字保持，%1 = bundled 路径（gui-slot-eqv 锁字面，bundled 是缺省
  安装形态）。
- `src/service/mediaservice.{h,cpp}`：四请求信号槽 + modelListFinished/modelOpFinished 转发 +
  modelMissing 跨线程转发；modelList 不占 busy（wsInfo 先例）、下载/导入占 busy + cancelFlag
  复用取消通道（D3）。
- `src/view/mainwidget.{h,cpp}`：工具行「模型管理…」按钮 + 对话框（QTreeWidget
  id/用途/状态/许可证/大小五列渲染、状态中文映射 已安装/可下载/已失效（文件缺失）、下载/取消/
  进度条、导入两段〔QFileDialog *.onnx → 用途对话框含 §161 提示 → 提交〕、endpoint 可编辑
  组合框（预设 huggingface.co + hf-mirror.com）、换根/打开目录）；modelMissing →
  QMessageBox::question「模型不可用，是否打开模型管理下载？」（informativeText 载 F-2 等长
  篡改解药「若下载后模型仍不可用，请在模型管理中删除该条目后重新下载。」）答 Yes 打开对话框；
  busy 期对话框操作键全锁/取消键开；QSettings 读写只在主线程（D7），值经队列信号下传 worker。

## 验收方法

- **engine 相**（GML-101..107m）：FFExtractor 槽直调（gui-ws-probe 同型），库 fixture 用 CLI
  modelImport 预装（by-id 形态 = 真实登记链）；**选用模型真值 = 工作区版本记录
  `versions[0].model.path`**（workspace.cpp 登记侧产物），不赌任务文案。
- **gui 相**（GML-110..119）：offscreen QApplication 驱动 MainWidget+MediaService 全链；
  XDG_CONFIG_HOME 隔离 QSettings；20ms 弹窗自动应答机（QMessageBox 按文案分流 / QFileDialog
  selectFile+accept / 用途对话框按 objectName 捕获 §161 原文）；range_server `--chunk-sleep-ms 8`
  节流把下载拉长到秒级，传输窗内做 busy/取消断言（避免竞态）；stderr 滤 offscreen 平台噪声后
  非空即 FAIL。
- **E_MODEL 注入**：appDir 降噪模型写损坏垃圾字节——文件在盘 → 两级解析 pre-check 放行 →
  E_MODEL 必出自 runRequest 引擎 setup 失败（AiDenoiser 异常屏障 = 片 A 已实证路径，不赌
  vocalseparator 抛出面）。
- **GUI 下载 e2e 载体 = uvr**（hfSource，endpoint 前缀可换本地 range_server）；gtcrn 为 GitHub
  直连非 hfSource（沿片 A round-1 论证），gtcrn 面由 engine 相槽直调覆盖。

## 关键发现（坑与修法）

1. **offscreen QFileDialog 确定性**：`AA_DontUseNativeDialogs` 必须在 QApplication 构造**前**
   setAttribute——portal native helper 竞态会让 exec 永不返回（探针曾 rc=124 超时），构造后补设
   无效。定位法：MODAL trace 打印 activeModalWidget 元对象名。
2. **endpoint 记忆双连**：editable QComboBox 的 `setCurrentText` 只调 setEditText 不发
   currentIndexChanged；且用户手填自定义后**重选同号预设**时索引不变、只有 `activated` 恒发。
   产品修 = currentIndexChanged + activated 双连（GML-117 曾红）；探针侧改用 setCurrentIndex
   模拟真实下拉。
3. **F-6 三次复发于本轮新增行**（`"$W"#注释` 变量吞注释 / ~30 处 line_is 实参 /
   `--chunk-sleep-ms 8#`）：与 round-2 记录同型的系统性坑，grep `"`# + sed 补空格修复。
4. **两级解析优先级可实证**：GML-103/105 用「库在盘确有 gtcrn」作反证——根注入时取库
   （GML-103）、根空串时若误查库必取到库路径而实际取随包（GML-105）→ 不查库行为坐实。

## 结果（全绿与证据）

run-models.sh（build-linux）：**rc=0，133 PASS / 0 FAIL** = 片 A MDL 95（含主线程 F-2 修复的
MDL-024t，modelmanager.cpp 已按 7df4772 同步进本 worktree）+ 片 B GML 36 + 前置闸 2。
证据摘录（完整日志 /home/psh/vh-tmp/rm.log）：

```
  PASS GML-101            # 锁定文案「找不到 AI 降噪模型：<bundled>」+ ok=0
  PASS GML-101m           # MODELMISSING 1（pre-check 路径结构化信号）
  PASS GML-102p           # MODELPATH <库路径>（版本记录真值）
  PASS GML-105            # 根未注入不查库（零漂移反证）
  PASS GML-110            # EPDEFAULT https://huggingface.co（D4）
  PASS GML-111            # DLBUSY 1（下载占 busy：锁/取消/进度条）
  PASS GML-112            # LISTDURINGBUSY 1（modelList 不占 busy）
  PASS GML-113            # CANCELMSG 已取消（锁字面）
  PASS GML-114            # MISSINGQ 1（E_MODEL→modelMissing→问询→Yes）
  PASS GML-115f           # 对话框下载产物 sha256 = 目录表基准
  PASS GML-116            # NOTICE 请确认您有权使用该模型（来源与许可证由您自行核验）（§161 逐字）
  PASS GML-117            # EPSAVED https://hf-mirror.com（QSettings 记忆）
  PASS MDL-999            # 完整性终闸：fixture 与 third_party 全程未被改动
== run-models 全绿 ==
```

其余门禁：

| 门禁 | 结果 | 备注 |
|---|---|---|
| gui-slot-eqv.sh | 全绿 | 七槽转录/产物逐字节/取消无残留，nomodel+withmodel 双相；缺模型分支锁定文案零漂移实证 |
| thinshell-eqv.sh fd7badb | 全绿 | stdout/退出码/产物三方一致（16 PASS） |
| fake-engine-e2e.sh | 全绿 | REG 族全过；BASELINE_FILES 滚动至片 B 7 文件（见下 worktree 特例） |
| gui-ws.sh | 全绿 | GWS 族含 adv 相（换根/中文路径/置空回 direct） |
| run-json.sh（ci-smoke 内） | 全绿 | 36/36 |
| parity/fft/不变性/CLI/ffprobe（ci-smoke 内） | 全绿 | parity 有 SKIP 族（本地降级跑，PARITY_REQUIRED 未设） |
| 构建零警告 | 0 warning | 8 个触碰 TU 全量重编（-Wall -Wextra，18/18 目标）rc=0 |
| **ci-smoke.sh 整体** | **CI-RC=1** | **唯一 FAIL = WS-019，worktree 环境伪差非产品回归**（根因链见下）；run-ws 失败后 set -e 截断，其后 gui-ws/run-models 段以独立跑补证全绿 |

### WS-019 环境伪差根因链（先给结论：本 worktree 的 third_party 软链所致，产品无回归）

- 现象：ci.log 仅 `FAIL WS-019（workspace.json 断言）`，断言期望
  `model.path == realpath(third_party/models/denoiser/gtcrn_simple.onnx)`。
- 本 worktree `third_party -> /home/psh/VideoHandler/third_party`（软链，红线不入库的共享），
  `realpath` 解析到**主树**路径；产品按请求所给路径（cwd 绝对化，不解析软链）登记
  **worktree** 路径 → 字符串不等。主树（真实目录）中两者恒等，故片 A round-1/2 主树全绿。
- 证据（同一 slice-B 二进制）：
  1. cwd=worktree：workspace.json 登记
     `/home/psh/VideoHandler/.claude/worktrees/agent-…/third_party/models/denoiser/gtcrn_simple.onnx`；
  2. cwd=主树根：登记 `/home/psh/VideoHandler/third_party/models/denoiser/gtcrn_simple.onnx`
     （= realpath，断言通过形态）。
- 登记代码零漂移：workspace.cpp / commandlayer.cpp 无 diff，ffextractor.cpp 的 runRequest 改动
  仅在失败分支读 error.code 发信号（git diff hunk 核对）。**主线程在主树复跑 ci-smoke 应全绿。**

## worktree 特例与合并注记（主线程必读）

- 本 worktree HEAD=1ef3964，主线程 7df4772（片 A 测试轮收口）已按未提交形态同步进来：仅
  `src/service/modelmanager.cpp`（9 行，F-2 修复）+ run-models.sh 的 MDL-024t 段/cleanup 修——
  合并时该文件与主树同内容即自然无 diff。本轮**未改 modelmanager.cpp**（只经 CommandLayer 调用）。
- fake-engine-e2e.sh BASELINE_FILES 已滚动为片 B 7 文件（data.h / ffextractor.×2 /
  mediaservice.×2 / mainwidget.×2）——合并后主树 `git diff --name-only HEAD -- src/` 恰为此
  7 文件（modelmanager.cpp 与 7df4772 同内容无 diff）。本 worktree 跑批前曾临时
  `git checkout -- modelmanager.cpp` 还原 HEAD、跑后恢复同步件（跑批时 DRIFT 与清单严格相等，
  REG-001 白名单断言过）。
- run-models.sh GML 段插在 MDL-CANCEL 之后、完整性终闸之前；主线程 031/032 之间的 MDL-024t
  段与 cleanup 空格修均保留未动。

## 挂账

- Windows round-4 真机复验随主线程统一安排（片 A 同款挂账，GUI 对话框人眼过目 + endpoint 真源首连）。
- GML 用例为 ci-smoke 间接覆盖（run-models 已在 ci-smoke 接线内）。
