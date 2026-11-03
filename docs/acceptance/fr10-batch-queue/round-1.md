# FR-10 批处理队列 round-1（片 A CLI + 片 B GUI 同轮落地）

日期：2026-11-04。实现代理串行单轮；本文件记 round-1 全部门禁证据（用例逐条状态以
`matrix.csv` 为准）。改动面 = spec §4：commandlayer（validateSteps 抽取 + cmdBatch +
内置表 + 批标记）/ workspace（VersionPlan.batch）/ mediaservice（batch/batchFinished）/
ffextractor（resolveStepModels 抽取 + doBatch）/ mainwidget（批处理对话框 + onBatchDone）/
CLAUDE.md + fr24 §5.4 记档 / run-batch.sh 进 ci-smoke。

## 门禁 1：构建零警告

```
$ cmake --build --preset linux-test --clean-first
[27/27] Linking CXX executable video_handler        # 全量重编 27/27
$ grep -icE 'warn|error' 构建输出 → 0
```

## 门禁 2：run-batch.sh 全绿（本 feature 主验收）

```
$ TMPDIR=/home/psh/VideoHandler/build-linux/.batch-tmp \
    bash docs/acceptance/fr10-batch-queue/scripts/run-batch.sh build-linux
== run-batch 全绿 ==           # 89 PASS / 0 FAIL / 0 SKIP（模型在位，含模型族全跑）
```

关键实测值（断言均在脚本内机器判定）：

- **BATCH-EQV（编排零数值漂移，核心护栏）**：3 件 × denoise(60,wind)→pitch(2.0) 批跑 vs
  逐件手动 pipeline——各件终产物与中间产物 `cmp` 逐字节一致（6 组）；带模型
  aiDenoise→separate 批 2 件终产物逐字节一致（2 组）。
- **BATCH-FAIL（失败不中断）**：件 2 不存在 → `ok:true` + okCount=2 + 件 1/3 产物在盘 +
  `items[1].error.message` = 「打不开文件：<路径>」原文（无「第 N 件」前缀，附裁 3）；
  video-only mp4 → E_FILE「文件里没有音频流」同型续跑；垃圾字节模型全败批 → `ok:true`
  okCount=0 且退出码 0（ok=编排层成功，OQ-1(a)）；维度错位模型多步件 → 件内
  `步骤 2（aiDenoise）：` 前缀 + step/stepCmd 归属键（pipeline 归属透传）+ 步 1 版本保留。
- **BATCH-NEG（30 条）**：inputs schema 六态（含 ./ 归一重复拒收）/ steps schema 七态 /
  内置动词与 pipeline/batch 嵌套拒收 / 模板步链级键（input/output/workspace/v）与未知字段
  拒收 / semi=15 与模型不存在在开工前步骤级拦截 / 缺 workspace / 信封 output 拒收 /
  顶层 input 未知字段（requiresInput=false）/ schema 错误无归属键 / **零副作用实证**
  （negws 根零目录零产物 + 全部源 sha256 前后不变）。
- **BATCH-WS**：3 件三工作区独立；多步件版本记录 `pipeline:{index,total}` +
  `batch:{index,total}` **双键同记**（OQ-3(a)，风险 4 的 cmdPipeline subCtx 显式拷贝拦截面）；
  单步件只带 batch 键无 pipeline 键；params 纯度（链级键不泄入）；同内容不同路径 →
  同工作区 v001/v002 顺次；rm index.json 后同工作区续号 v003/v004；非批任务记录无
  batch/pipeline 键。
- **BATCH-CANCEL/PROG（batch-cancel-probe 直调 execute 的 Ctx 面）**：cancel-pre →
  `error{code:E_CANCELLED, message:"已取消"（逐字）, completed:0, total:2}` 且无 items/result、
  连工作区根都不建；cancel-mid → completed=0、版本目录无残迹；cancel-between →
  completed=1（被取消件不计，OQ-2(b)）、件 1 v001 保留、进度止于映射 50；progress 3 件 →
  件界 33/66 可观测 + 单调 + 终值 100；progress2 两件两步 → 复合边界 25/50/75/100 全中。
- **BATCH-GUIE（batch-gui-probe 直调 doBatch）**：ok-batch → `BATCHDONE 1 order=B`（整批
  恰一次）+ 载荷为批 result JSON + 进度 100 + 零 modelMissing；missing-pre →
  `BATCHDONE 1 order=BM`（**batchFinished 先发放 busy，modelMissing 后发引导**，附裁 1 顺序）
  + forVerb=aiDenoise + 零工作区目录（整批速断零件启动）；emid-item（appDir 垃圾模型，
  pre-check 放行件内 setup 失败）→ ok=1 + okCount=0 + **零 modelMissing**（件循环内禁发，
  Y5 毒缝封堵）；cancel → message 逐字「已取消」+ 件 1 版本保留。
- **BATCH-MEM**：2 件 × 30min 批（denoise+pitch）峰值 61396KB ≤ 单动词最大 59604KB + 32MB
  且 < 256MB（件串行×步串行零叠加，NFR-P4）；带模型批（2 件 × 120s aiDenoise→separate）
  峰值 1525484KB ≤ separate 单跑 1514784KB + 32MB 且 < 2GiB（模型会话跨件零叠加）。

## 门禁 3：零漂移回归网（BATCH-ZERO，全量复跑）

| 脚本 | 结果 | 护栏对象 |
|---|---|---|
| run-json.sh（fr24） | 全绿（CMD-001..035 + 021r 全文逐字节） | 信封/双查表/旧动词零漂移（内置表加行） |
| run-ws.sh（fr-a4） | 全绿 58 PASS（TMPDIR=build-linux/.ws-tmp） | workspace.json 键面与两段提交零漂移（batch 键存在性插键） |
| run-models.sh（fr-8） | 全绿 142 PASS（TMPDIR=build-linux/.models-tmp） | 模型族零触碰 |
| run-pipe.sh（fr-9） | 全绿 77 PASS（TMPDIR=build-linux/.pipe-tmp） | **PIPE-NEG 19 条逐字断言 = validateSteps 抽取零漂移的硬护栏** |
| gui-slot-eqv.sh（fr24） | 全绿 18 PASS（基线 fd7badb） | 七槽转录/产物逐字节等价 = resolveStepModels 抽取零漂移 |
| thinshell-eqv.sh（fr24） | 全绿（基线 b6a5f21） | 旧 7 动词 CLI stdout/退出码/产物三方一致 |

注：run-ws 首跑曾 FAIL——原因是本机调用侧 TMPDIR 目录未建导致 mktemp 落 /tmp 配额区
（fr8 round-2 同款假象），mkdir 后复跑全绿；与本包改动无关，已复跑取绿为准。

## 实现要点对照（spec §7.1 拍板逐条落位）

- OQ-1(a) ok=编排层成功：BATCH-FAIL-001/004 断言（全败批退出码 0）。
- OQ-2(b) 取消带 completed/total：batchCancelled 组 error 增量键；「已取消」逐字（GUI
  静默特判零漂移）；被取消件不计完成度。
- OQ-3(a) batch:{index,total}：Ctx.batchIndex/batchTotal（缺省 0 不插键）→
  runInWorkspace 存在性插键 → VersionPlan.batch → workspace.json；cmdPipeline 组步骤
  subCtx 显式拷贝批字段（风险 4，BATCH-WS-001 双键断言拦截）。
- OQ-5(a) inputs ≥2；OQ-6(a) GUI 动作 8 条（5 引擎单步 + 3 预设，无 transcode）。
- 附裁 1 modelMissing 仅开工前：doBatch 解析失败 → batchFinished 先发（放 busy）+
  modelMissing(动词) 整批速断；件循环内 E_MODEL 不发（BATCH-GUIE-004 断言零触发）。
- 附裁 2 无件数上限：schema 无 upper bound（BATCH-NEG 仅断 ≥2 下限）。
- 附裁 3 件错误原文透传：无前缀（BATCH-FAIL-002 python 断言），归属在 index/input。
- 附裁 4 fr24 §5.4 记档：`docs/spec/fr24-command-layer.md` 该行已注明批语义由 FR-10 承载。
- 附裁 5 GUI「已取消」静默 + 汇总 information「批处理完成：k/N 件成功」：onBatchDone
  实现；widget 面自动化（BATCH-GUI）round-2 承接（见 matrix 待跑行）。

## 遗留

- BATCH-GUI（widget 面探针：对话框组包深度比对/多选应答/busy 全批覆盖/逐件渲染）按
  spec §6.9 排 round-2；GUIE 已在 round-1 全跑（壳行为面全覆盖）。
- BATCH-REG（注册表零改前瞻）分支证据制，不进 ci-smoke（spec §6.10）。
- Windows 真机复验随 WP-1.0 round-4 挂账（六包同款）。
