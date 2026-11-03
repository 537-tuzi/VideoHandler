# FR-10 批处理队列验收 goal

## 总目标

按 spec `docs/spec/fr10-batch-queue.md`（D1–D10 + §7.1 主线程拍板）落地 FR-10：`batch` 命令层
编排动词（inputs[≥2] × 共享 steps[≥1] 模板，逐件组合既有 execute，失败不中断在编排层机制化，
整批取消，batch:{index,total} 版本标记）+ GUI 批处理对话框（清单/动作组合/开始/逐件结果渲染）
+ BATCH-\* 验收族。

## sub goal matrix

| sub goal | 内容 | 状态 |
|---|---|---|
| SG-1 | 命令层：validateSteps 抽取（minSteps 参数化，PIPE-NEG 逐字零漂移）+ cmdBatch（inputs schema/重复拒收/模板预校验一次全批/件循环组合 execute/失败续跑/整批取消/两层进度等分）+ 内置表一条（requiresInput=false）+ 批标记（Ctx+VersionPlan+存在性插键+cmdPipeline subCtx 拷贝） | 完成（round-1） |
| SG-2 | GUI 壳：mediaservice batch/batchFinished 信号槽（busy 全批一锁）+ ffextractor resolveStepModels 抽取 + doBatch（开工前 modelMissing，附裁 1）+ mainwidget 批处理对话框 + onBatchDone（「已取消」静默特判同型，附裁 5） | 完成（round-1；widget 面 round-2 BATCH-GUI 23 用例全绿背书） |
| SG-3 | 验收基建：run-batch.sh + batch-cancel-probe.cpp + batch-gui-probe.cpp（BATCH-OK/EQV/FAIL/NEG/CANCEL/WS/PROG/GUIE/MEM 用例族；磁盘卷 TMPDIR + else-skip/PARITY_REQUIRED 惯例） | 完成（round-1 89 PASS + round-2 扩至 99 PASS 全绿；gui-batch.sh widget 面随 round-2 落地全绿） |
| SG-4 | 门禁全绿：BATCH-ZERO 全量复跑（run-json/run-ws/run-models/run-pipe/gui-slot-eqv/thinshell-eqv）+ 构建零警告 + run-batch 进 ci-smoke 接线 | 完成（round-1 全绿证据 round-1.md；round-2 独立复跑零漂移三件套全绿 + 主线程三修复后全量复跑见 round-2.md §8；run-models/gui-slot-eqv/thinshell-eqv round-1 全绿未随 round-2 重跑——零触碰面，round-4 前补） |

## 重大决策与关键信息

- 全部设计决策见 spec §2（D1–D10）+ §7.1 拍板（OQ-1..6 全裁 + 附裁五项）。
- 设计轮双代理（spec 作者 + 对抗约束枚举）交叉对照：对抗 U1–U8 与 spec OQ 六轴独立收敛
  （架构位置/请求形态/ok 聚合/单件下限/取消粒度/锁留缝）；分歧两处由 §7.1 裁定——
  件错误不缀「第 N 件」前缀（对抗 I3 vs spec D4，裁 D4）、批中段禁 modelMissing
  （对抗 Y5 毒缝 = busy 长持下引导下载被 `if (busy) return` 静默吞，附裁 1 封死）；
  U6 无件数上限（附裁 2）。对抗关键 file:line 锚点经主线程抽查核实（fr24 §5.4:227 /
  REQUIREMENTS.md:50/:37 / mediaservice busy 复位 :43-55 与 if(busy) return :79-91 /
  cmdPipeline fresh subCtx :338-341 / mainwidget「已取消」特判 :689）。
- fr24 §5.4「队列归服务层」旧缝由本包演进为命令层动词（D1）——落地时同步改记 fr24 spec
  该句（附裁 4）。

## 进展

- round-0（2026-11-03）：spec 落档（D1–D10 + OQ 六项）+ §7.1 拍板（OQ 全裁 + 附裁五项）+
  验收骨架（goal/plan）。双代理设计轮收口，关键锚点主线程核实完毕。
- round-1（2026-11-04）：片 A CLI + 片 B GUI 同轮落地（实现代理串行单轮）。命令层/GUI 壳/
  验收基建/零漂移回归全绿（run-batch 89 PASS + run-json/run-ws 58/run-models 142/
  run-pipe 77/gui-slot-eqv 18/thinshell-eqv 全绿 + --clean-first 零警告），run-batch 进
  ci-smoke 接线。证据 round-1.md + matrix.csv。BATCH-GUI（widget 面）与 BATCH-REG 留
  round-2/分支证据制；Windows 真机随 round-4 挂账。
- round-2（2026-11-04）：审查+测试双代理并行收口。审查十维全过**无 P1**（1 P2 + 4 P3）；
  测试独立复跑全绿（构建零警告 + run-batch 99 PASS 含补测 EQV-SR/WS-011..013 + 零漂移
  run-json/run-ws/run-pipe）+ 红绿论证 9 翻转全红（NEG 8 条改期望全转红 + NEG-030 修复后
  再翻红证非空）+ 补面 33 条（同根 EQV 7 / WS 字节不变 3 / widget 面 BATCH-GUI 23，
  gui-batch-probe.cpp + gui-batch.sh 新建，主路径零 QFileDialog 依赖）+ 测试侧 5 缺陷自修
  （T-1 NEG-030 恒真基线=审查 P2-1 双独立发现 / T-2 探针 waitUnlock 超时差 20 倍 /
  T-3..T-5 断言式——教训「验证断言须用套件自身匹配式复跑」）；src 零缺陷。主线程配方 #7
  复核后落三修复（P3-1 循环退出码遮蔽 / P3-2 mediaservice.h 注释矛盾 / P3-3 过滤器串
  同源化 kMediaFilter）+ gui-batch 进 ci-smoke 接线，全量门禁复跑后统一提交；P3-4 与
  doBatchStart busy 观察两项记档不修（两报告独立同判）。处置总表 round-2.md §8。
  挂账：run-models/gui-slot-eqv/thinshell-eqv 未随 round-2 重跑（round-1 全绿、零触碰面，
  round-4 前补）、BATCH-REG 分支证据制、Windows round-4。
