# FR-9 管线串联验收 goal

## 总目标

按 spec `docs/spec/fr9-pipeline.md`（D1–D10 + §7.1 主线程拍板）落地 FR-9：`pipeline` 动词
（steps 数组，步骤词表=引擎注册表枚举，多遍执行逐步组合 execute，强制 workspace，每步一版本
parentVersion 链 + pipeline:{index,total} 标记，error 增 step/stepCmd，E_CANCELLED 文案逐字
「已取消」）+ GUI 预设下拉一键（三条预设）+ PIPE-\* 验收族。

## sub goal matrix

| sub goal | 内容 | 状态 |
|---|---|---|
| SG-1 | 命令层：staticCheck 从 runEngine 抽取（顺序消息逐字零漂移）+ cmdPipeline（steps schema/整链静态预校验/循环组合 execute/进度等分/错误归属与 E_CANCELLED 例外）+ 内置表一条（allowsOutput=false, extraKeys={"steps"}） | 已实现（round-1 绿） |
| SG-2 | 工作区标记：VersionPlan 增可选 pipeline{index,total}；commitVersion 按存在性插键（非管线任务 workspace.json 逐字节零漂移）；runInWorkspace 透传 | 已实现（round-1 绿；标记传递缝=Ctx 字段，见 round-1.md） |
| SG-3 | GUI 壳：mediaservice pipeline 一对信号槽 + ffextractor doPipeline（模型步两级缺省解析；E_MODEL→modelMissing 取 error.stepCmd）+ mainwidget 三条预设下拉 + 一键按钮 + busy 面板纳入 | 已实现（round-1 绿：GUIE engine 面 + GUI widget 面都过） |
| SG-4 | 验收基建：run-pipe.sh（复用 run-json 断言器配方）+ PIPE 用例族（OK/EQV/CHAIN/PREVAL/FAIL/NEG/CANCEL/ZERO/GUI/REG/MEM）+ missing-text 断言族（round-2：Fix 2 用途文案锁定） | 已实现（round-1 绿 75 PASS + gui-pipe 7 例 + REG-012/013；round-2 +MEM-003/004+MIST） |
| SG-5 | 门禁全绿：run-pipe + run-json/run-ws/run-models/thinshell-eqv/gui-slot-eqv/fake-engine-e2e/ci-smoke 全量 + 构建零警告 + run-pipe 进 ci-smoke 接线 | round-1 全绿；round-2 修复后全量复跑全绿（77+7+142+18+16+25+ci-smoke 361，证据 round-2.md） |

## 重大决策与关键信息

- 全部设计决策见 spec §2（D1–D10）+ §7.1 拍板节（OQ-1..4 + 附裁 1..5 + 链中「源」语义确认）。
- **双报告交叉对照补的缝**：链中 E_MODEL 的 GUI modelMissing forVerb 取 `error.stepCmd` 而非请求 cmd（附裁 1）——单看任一报告都会漏。
- 架构主轴 D1 多遍执行：每步=一次完整单动词 execute（组合非复制），否决进程内单遍（级间 swr 新底座+全引擎同驻内存峰值=和+错误/进度/取消/版本化全重造）。
- 零漂移三触碰面：staticCheck 抽取 / workspace 增键 / 信封段——PIPE-ZERO 硬门禁。
- spec 轮过程：spec 设计代理 + 独立对抗约束代理并行；对抗清单 6 未收敛中 5 个被 spec 覆盖、1 个（哈希增量口）由附裁 3 关闭；对抗清单关键 file:line 断言经主线程抽查核实成立。

## 进展

- round-0（2026-11-03）：spec 落档 `docs/spec/fr9-pipeline.md`（D1–D10 + OQ 四项）+ §7.1 主线程拍板（OQ 全裁 + 附裁五项）。双代理并行产出（设计 + 对抗约束枚举），对抗清单经主线程抽查核实后作拍板对照面。
- round-1（2026-11-03）：SG-1..4 实现落地（src 六文件 + CLAUDE.md，零新文件零 CMake 改动）；run-pipe.sh 75 PASS 全绿（含 EQV 核心护栏逐字节一致、带模型链实跑）、gui-pipe.sh 7 例全绿（widget 面提前在 round-1 跑绿，非 round-2）、fake-engine-e2e 增 REG-012/013（假引擎免改入链）并全绿；零漂移门禁 run-json/run-ws/run-models/thinshell-eqv/gui-slot-eqv(fc69173) 全绿；ci-smoke 接线 run-pipe+gui-pipe 后全量复跑。三处 spec 措辞偏差（实测口径为准）记 round-1.md。PIPE-ZERO 以门禁复跑方式满足（无独立脚本用例）。
- round-2（2026-11-03）：审查+测试双轮收口——四修复落地（带模型链 MEM 例固化 PIPE-MEM-003/004、onModelMissing(forVerb) 文案带用途、pipe-gui 探针受控根注入、selectFile 行编辑加固含 mdl 同型）+ 复审轮 P2-1（else-skip 惯例对齐）；D1「峰值=逐步最大值而非和」免模型/带模型双实证（60,880 vs 60,056KB / 1,517,592 vs 1,515,180KB，差 0.8–2.4MB）；全门禁复跑全绿（77+7+142+18+16+25+ci-smoke 361 PASS）。Fix 2 用途文案断言缺口由测试代理 missing-text 探针补面（MIST 族，结果见 round-2.md/matrix）。环境事件闭环：iso9660 挂载致 selectFile 死泊（PID 27570，33 分钟）→ 加固后同环境全绿。
- 待办（主线程收口）：~~report.md（全绿复盘）~~ 已写；剩统一提交。
