# FR-10 批处理队列验收 report（全绿复盘，2026-11-04）

总目标见 `goal.md`；需求/方案/改动见 `plan.md` + `docs/spec/fr10-batch-queue.md`
（D1–D10 + §7.1 拍板：OQ-1..6 全裁 + 附裁五项）；逐轮证据 `round-1.md`（实现轮）+
`round-2.md`（审查+测试收口轮，§8 主线程处置）。

## 结论：全绿合入

- **用例规模**：matrix.csv 130 行——BATCH-OK 10 / EQV 9+7（round-2 同根等价补面）/
  FAIL 5 / NEG 30 / CAN 6 / PROG 2 / GUIE 6 / MEM 4 / WS 13（round-2 增 011..013 字节不变）/
  GUI 23（round-2 widget 面）；BATCH-REG-\* 按设计未跑（分支证据制，spec §6.10）。
- **最终门禁**（round-2 主线程三修复后全量复跑）：构建零警告、run-batch 全绿 rc=0、
  gui-batch 全绿 rc=0、ci-smoke 全量全绿 rc=0（含 run-batch+gui-batch 接线段）；
  零漂移回归网（run-json / run-ws 58 / run-models 142 / run-pipe 77 / gui-slot-eqv 18 /
  thinshell-eqv）round-1+round-2 累计全绿（后三件 round-1 全绿、round-2 零触碰面未重跑，
  round-4 前补一次）。
- **核心论断实证**：
  - 编排零数值漂移：BATCH-EQV 批跑 vs 逐件手动（独立根 + 同根续号 v003+ 两形态）终产物与
    中间产物逐字节 `cmp` 一致（免模型 + 带模型双链）。
  - 失败不中断机制化：坏件三配方（不存在/无音频流/坏模型）ok=true + okCount 逐条断言 +
    件错误原文无「第 N 件」前缀（附裁 3）+ 好件产物完整落盘；全败批 ok=true okCount=0
    退出码 0（OQ-1(a) 口径，CLAUDE.md 明示判批看 okCount 不看退出码）。
  - 取消三相位（pre/mid/between）：message 逐字「已取消」+ completed 只计已落定件
    （OQ-2(b) 口径）、被取消件不计、已完成件保留 append-only。
  - 版本标记：batch:{index,total} 逐件断言 + 多步件 pipeline+batch 双键（subCtx 拷贝缝
    被 BATCH-WS-001 拦截验证）+ 非批任务 workspace.json 逐字节不变（WS-011..013）。
  - 内存（NFR-P4/D1）：件串行 × 步串行 × 模型会话随遍释放三层零叠加——免模型批 61116KB
    ≤ 单动词最大 59544KB+32MB <256MB；带模型批 1537120KB ≤ separate 1514836KB+32MB
    <2GiB（round-1/round-2/主线程复跑三组实测同量级）。
  - Y5 毒缝封堵（附裁 1）：modelMissing 仅 doBatch 开工前且 batchFinished 先发（GUIE-002
    order=BM + GUI-020 busy 已释放）；批中段 E_MODEL 零引导（GUIE-004）。
  - 零漂移八触碰点（spec D10）：validateSteps/resolveStepModels 两抽取纯搬家
    （PIPE-NEG 19 逐字 + gui-slot-eqv 18 护栏）、信封段零改、main.cpp/旧薄壳零触碰。
- **三轮过程**：round-0 设计轮双代理（spec 作者 + 对抗约束枚举）交叉对照——两处分歧由
  §7.1 裁定（件错误不缀前缀、批中段禁 modelMissing），后者被 round-2 GUIE-004 断言锁定，
  是「对抗性独立报告」价值又一实证；round-1 实现代理单轮全量（片 A+片 B）；round-2 审查
  （十维无 P1）+ 测试（独立复跑/红绿论证 9 翻转全红/补面 33 条/测试侧 5 缺陷自修）并行 +
  主线程配方 #7 复核三修复（循环遮蔽/注释矛盾/过滤器同源化）。

## 遗留与挂账

- **Windows round-4 真机**：批 GUI 完整体验（多选/清单/取消/结果渲染）+ run-batch/gui-batch/
  零漂移三件套同单，随既有 round-4 挂账（runbook `docs/acceptance/wp-1.0-hotfix/round-4-windows.md`）。
- **BATCH-REG-\***：注册表零改前瞻（M3 假想验证），spec §6.10 分支证据制不进 ci-smoke，未跑。
- **零漂移三件套未随 round-2 重跑**：run-models/gui-slot-eqv/thinshell-eqv（round-1 全绿、
  本包对它们零触碰），round-4 前补一次全量。
- **记档不修**：doBatchStart 先 set_busy 后 media->batch 的防御深度观察（现网 busy 期入口
  全禁用不可达、七槽同型，随交互式队列一并设计）；预设链 separate 步显式 stem=vocals vs
  doPipeline 注册表缺省（语义等价，GUI-008 锁形态）。
- **环境注记**：验收套件一律串行（round-2 首跑 run-json 1 败系并发假象，串行复跑 ×2 全绿）；
  验证断言须用套件自身匹配式复跑，近似手搓 grep 会误导（T-4/T-5 教训）。

## 附：ci-smoke 接线

run-batch.sh + gui-batch.sh 两段均走 `$BUILD/.batch-tmp` 磁盘卷 TMPDIR（/tmp 配额教训，
fr8 round-2 起）；gui-batch 自带缺模型整脚本 FAIL 口径（无 else-skip 面）。
