# FR-9 管线串联 round-1（CLI 面 + GUI 壳全绿）

日期：2026-11-03。环境：Linux x64（Qt6 apt + FFmpeg/SoundTouch pkg-config + ORT 1.30 `third_party/ort/linux`）、
构建 `cmake --build --preset linux-test`（`build-linux/`）。工作区临时目录一律磁盘卷（/tmp 配额 tmpfs
不跑批，fr8 round-2 教训）。

## 一、实现面（SG-1..3，零新文件零 CMake 改动）

| 文件 | 对应决策 |
|---|---|
| `src/service/commandlayer.h/.cpp` | D2/D3/D4/D9/D10：staticCheck 从 runEngine 抽取（校验顺序 validateParams→semanticCheck→模型存在性、消息文案单一来源零漂移）；cmdPipeline（steps schema 五连检 + 整链静态预校验不调 create + 循环组合 execute + 进度等分 `(i*100+pct)/n` + stepError 归属前缀/E_CANCELLED「已取消」例外）；内置表条目 `{"pipeline", …, allowsOutput=false, allowsWorkspace=true, extraKeys={"steps"}}`；Ctx 增 `pipelineIndex/pipelineTotal`（标记传递缝，见 §四） |
| `src/service/workspace.h/.cpp` | D5/附裁 4：VersionPlan 增可选 `pipeline{index,total}`，commitVersion **按存在性插键**（非管线任务不含 → workspace.json 逐字节零漂移） |
| `src/service/mediaservice.h/.cpp` | SG-3：`pipeline(inPath, stepsJson)` 一对信号槽（QString 传 JSON，wsInfo/modelList 先例）；占 busy 一次一任务 |
| `src/service/ffextractor.h/.cpp` | SG-3/附裁 1：`doPipeline` 槽（解析 steps → requiresModel 步逐个 resolveDefaultModel 两级缺省解析 → 组 pipeline 请求走 runRequest）；runRequest `E_MODEL → modelMissing(forVerb)` 取 `error.stepCmd`（照搬请求 cmd 会得 "pipeline" 引导错用途模型）；成功文案「管线完成：<终产物路径>」（附裁 5） |
| `src/view/mainwidget.h/.cpp` | OQ-3(a)：三条静态预设（AI 降噪→人声分离 / DSP 降噪→人声分离 / 人声分离→变调）+ 下拉 + 一键按钮 + busy 面板纳入（btnPipeline+cmbPipeline）；denoise 步 humBase/wind、pitch 步 semi 提交时取面板当前值；参数非法不在 GUI 重复判（薄壳，预校验给步骤级错误） |
| `CLAUDE.md` | FR-24 动词表 + FR-9 条目 + 架构行同步 |

## 二、round-1 验收结果（全部真跑）

### run-pipe.sh（CLI 面 + GUI worker 壳）——75 PASS 全绿

`TMPDIR=/home/psh/vh-tmp bash docs/acceptance/fr9-pipeline/scripts/run-pipe.sh build-linux`
→ rc=0，75 PASS / 0 FAIL / 0 SKIP（本机 third_party 双模型 + python3-onnx 齐备，无降级）。
完整日志 `/home/psh/vh-tmp/run-pipe-r2.log`。分族：

- **PIPE-OK-001..010（10）**：两步/三步/带模型链 e2e——result 键集恰等
  `{output,steps,truncated,workspace}`、steps 摘要含 cmd/version/outputSpec/engineResampled、
  版本目录 `v001_denoise/+v002_pitch/` 落位、48k 源跟随源免重采样（engineResampled=false）、
  带模型链锁兜底 44.1k/2ch（engineResampled=true）。
- **PIPE-EQV-001..009（9）核心护栏**：同参数「管线跑」vs「逐步手动跑」（同字节输入副本）——
  **终产物与中间产物 cmp 逐字节一致**（无模型链 + 带模型链 aiDenoise→separate 双份）：
  管线 = 纯编排、无数值漂移（D1 组合非复制的直接证据）。
- **PIPE-CHAIN-001..006（6）**：pipeline 标记逐条断言（含「手动任务无标记」的存在性对照）、
  parentVersion 链（v5.parent=4）、链中插手动任务后续号交错仍正确、params 纯度
  （链级键不泄入步骤记录）、rm index.json 后同工作区续定位 version=7。
- **PIPE-PREVAL-001..007（7）**：步 2 semi 越界/semi=0/off+off 语义错/步 1 参数错/模型路径不存在
  → 开工前拦截（消息带「步骤 N（cmd）：」前缀 + step/stepCmd）；零执行实证（工作区根空、源 sha256 不变）。
- **PIPE-FAIL-001..008（8）**：垃圾字节模型链中 E_MODEL（step=2 前缀 + v001 保留 head=1 +
  失败步无残迹 + v001 产物可独立复用续 v002 + 重跑起新号 v003）；维度错位模型（craft-mismatch
  配方）→ E_ENGINE 同样带步骤归属（setup 全过、首推理抛错走异常屏障）。
- **PIPE-NEG-001..019（19）**：schema 电池——缺 workspace/缺 steps/非数组/空/单步/元素非对象/
  cmd 缺失/非字符串/内置动词（probe/extractAudio/modelList/pipeline 嵌套）不可链式/步骤含
  input/output/workspace/v/未知参数 → 逐条消息断言；schema 段错误不带 step/stepCmd。
- **PIPE-CAN-001..006 + PIPE-PROG-001（7）**：探针（pipe-cancel-probe.cpp）直调
  `CommandLayer::execute` 的 Ctx 注入面四相——cancel-pre（零进度连工作区目录都不建）/
  cancel-mid（步 1 块循环检查点、版本目录已 discard）/cancel-between（step=2、v001 保留、
  进度止于映射值 50=(0+100)/2）/progress（三步链终值 100、单调、步界值 33/66 可观测）；
  E_CANCELLED message 逐字「已取消」不缀前缀（GUI 静默特判依赖，D4）。
- **PIPE-GUIE-001..007（7）**：FFExtractor doPipeline 槽直调（无控件）——ok-chain 文案
  「管线完成：<路径>」+ 进度终值 100 + 产物在盘；missing-step1「找不到AI 降噪模型：」+
  modelMissing(aiDenoise)；**emid-chain 附裁 1 实证：响应 cmd=pipeline 而壳 modelMissing
  forVerb=error.stepCmd=aiDenoise**；emid-negative E_FILE 不触发 modelMissing（负例守卫）；
  cancel-between「已取消」逐字 + v001 保留。
- **PIPE-MEM-001..002（2）**：30min 素材两步链峰值 RSS **60544KB ≤ 单动词最大 61512KB + 32MB
  且 < 256MB**（整文件 regime ≈635MB 线上方即整载入，实测远低于线）——逐遍有界=全程有界
  （NFR-P4/D1）；同场终产物与手动逐步跑 cmp 逐字节一致。

### gui-pipe.sh（GUI widget 面）——7 例全绿（提前在 round-1 跑绿，未留到 round-2）

`TMPDIR=/home/psh/vh-tmp bash docs/acceptance/fr9-pipeline/scripts/gui-pipe.sh build-linux` → rc=0。
offscreen MainWidget+MediaService 全链（pipe-gui-probe.cpp gui 模式，QSettings 经 XDG_CONFIG_HOME
隔离）：文件载入后一键按钮解禁；四组面板态×预设的 stepsJson 深度比对（ai→sep 无动态参数、
denoise 的 humBase 取制式下拉/off、wind 取勾选、pitch 的 semi 取变调框 2.5）；busy 锁 4 case
提交即锁/完成即解；成功文案 4/4「管线完成：<终产物路径>」（附裁 5）。

### fake-engine-e2e.sh（PIPE-REG，分支证据制不进 ci-smoke）——全绿

`docs/acceptance/nfr-m1-engine-registry/scripts/fake-engine-e2e.sh`：基线文件清单滚动更新
（FR-9 六文件滚入 BASELINE_FILES 与 cp 清单；FR-8 片 B 已在 HEAD 不再漂移），新增
**REG-012 假引擎 gain 免改入链**（gain+gain 两步链 e2e：steps 摘要/版本目录 v001_gain/+v002_gain/
零额外接线）与 **REG-013 步骤级参数预校验**（步 2 gainDb 越界 →「步骤 2（gain）：gainDb 须在 …」
+ step/stepCmd + 零执行零落盘）。既有 REG-001..011 全数复绿（含构建零警告门禁与 worktree 清理复核）。

## 三、零漂移硬门禁（PIPE-ZERO，spec §6.8）——全量复跑全绿

| 门禁 | 结果 |
|---|---|
| `run-json.sh`（CMD 族） | rc=0，36 PASS |
| `run-ws.sh`（WS 族） | rc=0，58 PASS |
| `run-models.sh`（MDL 族，磁盘卷 TMPDIR） | rc=0，142 PASS |
| `thinshell-eqv.sh fc69173 build-linux`（CLI 薄壳三方一致） | rc=0，16 PASS（stdout 逐字/退出码/产物 cmp） |
| `gui-slot-eqv.sh fc69173 build-linux`（七槽零漂移自检） | rc=0，18 PASS（转录归一化 + 产物 cmp + 取消无残留） |
| `fake-engine-e2e.sh` | rc=0 全绿（见上） |
| 构建零警告 | 增量 `ninja: no work to do`；fake-engine worktree 全新构建过 `warn|error` 门禁（FR-9 六文件全量重编） |
| `ci-smoke.sh` 全量（已接线 run-pipe+gui-pipe） | rc=0 全绿（engine/fft DUT + CLI 冒烟 + parity + CMD/WS/GWS/MDL/PIPE 全族；run-pipe 75 PASS + gui-pipe 7 例在其中磁盘卷 `.pipe-tmp` 下复跑全绿） |

## 四、标记传递缝说明（SG-2，主线程要求汇报）

未给 `execute`/`planVersion` 加公共参数，链内标记走 **Ctx 字段 + 尾默认参**两级最短通路：

```
cmdPipeline 组 subCtx{pipelineIndex=i+1, pipelineTotal=n}   // commandlayer.h Ctx 增两 int 字段（0=非管线）
→ runEngine 检 pipelineTotal>0 组 QJsonObject{index,total}  // 标记只在管线子调用语境诞生
→ runInWorkspace(..., pipelineMarker) 尾默认参 = {}          // 唯一签名变化，既有调用点零改
→ plan.pipeline = marker；commitVersion 按存在性插键         // 空对象不插键 → 非管线任务零漂移
```

选择理由：Ctx 本就是管线子调用的传递通道（cancel/onProgress 同路），标记随行零新机制；
runInWorkspace 尾默认参保住全部既有调用点不动；workspace 侧只认「plan 里有没有这个键」，
不感知管线存在。CLI 单发（`--json` 直读 stdin）不设 Ctx 字段 → 恒 0 → 恒零漂移。

## 五、spec 措辞偏差（实测口径为准，三处）

1. **§6.5 PIPE-FAIL「坏模型 → E_MODEL」**：spec 笼统写 E_MODEL，实测按产生点分流——
   垃圾字节模型 ORT 会话建立失败 = **E_MODEL**（PIPE-FAIL-001）；wp-1.0 round-2 维度错位配方
   setup 全过、首推理抛错 = **E_ENGINE**（PIPE-FAIL-008，异常屏障路径）。两形态都覆盖且都带
   step/stepCmd 归属——归属机制与错误码无关，spec 意图（步骤归属 + 部分失败语义）完整兑现。
2. **§6.6「output 同给 → 互斥违例（信封既有）」**：实测信封白名单先于互斥拒收——pipeline 的
   `allowsOutput=false` 使 `output` 落「未知字段：output」（PIPE-NEG-018）。同为 E_PARAMS、
   同为开工前拦截，仅消息文案与 spec 措辞不同（白名单拒收比互斥检查更早，语义更严）。
3. **§6.4 预校验「模型路径不存在 → E_PARAMS」**：实测为 **E_MODEL**（PIPE-PREVAL-005）——
   模型存在性检查在 staticCheck 静态段产 E_MODEL（与单动词同源），拦截时机不变（开工前）。

三处均为 spec 行文与既有错误码体系（产生点赋码）的对齐问题，非行为缺陷；断言一律按实测口径写死。

## 六、遗留与挂账

- Windows 真机随 WP-1.0 round-4 挂账口径（fr24/nfr-m1/fr-a4/fr8 同款）。
- `report.md` 待主线程收口后落（全绿复盘归档规范）。
- 跑批日志（`/home/psh/vh-tmp/gate-*.log`、`run-pipe-r2.log`）为会话产物，复现以脚本为准。
