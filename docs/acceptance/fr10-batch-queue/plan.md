# FR-10 批处理队列 plan

## 需求

FR-10（REQUIREMENTS.md:95）：多文件排队、逐个执行、失败不中断后续。场景锚点：US8「把这批
文件都降噪然后提伴奏」（:50）、P4 批量清理（:37）；GUI 与 Agent 是同一命令层的两个客户端、
能力完全对等（:73）——批不能做成 GUI 专属队列。

## 方案

见 `docs/spec/fr10-batch-queue.md`（权威契约，D1–D10 + §7.1 拍板）：

- `batch` = 命令层编排动词：`{cmd:"batch", workspace, inputs[≥2], steps[≥1]}`（同模板多文件），
  执行器逐件组合既有 execute（1 步 → 单动词子请求、≥2 步 → pipeline 子请求），模板静态预校验
  一次全批共享（含模型存在性，不调 create）。
- 结果信封：`ok` = 编排层成功（成败在 `items[].ok` + `okCount/total`；件错误原样透传不缀前缀，
  归属在 index/input 结构键）；`truncated` = 各件之或；退出码表「批是否跑完」。
- 取消：整批停（message 逐字「已取消」+ `completed/total` 增量键，已完成件保留 append-only）。
- 工作区：强制（缺即 E_PARAMS）；件间隔离 = 内容哈希身份天然性质；版本记录
  `batch:{index,total}` 标记按存在性插键（非批任务逐字节不变）。
- GUI 片 B：批处理对话框（清单/动作组合 8 条 = 5 引擎槽 + 3 预设/开始/结果逐件渲染）；
  busy 全批一锁；modelMissing 仅开工前（附裁 1）；「已取消」静默特判同型（附裁 5）。

## 改动

spec §4 改动面表：commandlayer.{h,cpp}（validateSteps 抽取 + cmdBatch + 内置表 + 批标记）/
workspace.{h,cpp}（VersionPlan.batch）/ mediaservice.{h,cpp}（batch/batchFinished）/
ffextractor.{h,cpp}（resolveStepModels 抽取 + doBatch）/ mainwidget.{h,cpp}（对话框 + onBatchDone）/
CLAUDE.md（方向节 + --json 面 + ok 口径）/ fr24 spec §5.4 记档（附裁 4）/ ci-smoke.sh（接线）。
源码零新文件零 CMake，引擎/底座/注册表/工作区内核零触碰。

## 验证

spec §6：BATCH-\* 用例族（OK/EQV/FAIL/NEG/CANCEL/WS/PROG/ZERO/GUIE+GUI/REG/MEM）+
run-batch.sh 进 ci-smoke（磁盘卷 TMPDIR=$BUILD/.batch-tmp，else-skip + PARITY_REQUIRED 惯例）。
核心断言：BATCH-EQV 批跑 vs 逐件手动跑逐字节一致（编排零数值漂移）、BATCH-FAIL 失败不中断
三配方、BATCH-WS 件间隔离 + 双键标记 + 非批任务逐字节不变、BATCH-ZERO 八触碰点回归网、
BATCH-MEM 峰值 = 全部件·步最大值。Windows 真机随 WP-1.0 round-4 挂账（六包同款）。
