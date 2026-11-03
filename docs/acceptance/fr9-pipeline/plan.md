# FR-9 管线串联验收 plan

## 需求

REQUIREMENTS.md:94「处理管线串联：降噪→分离→变声链式一键执行（核心差异化）」。变声=FR-7 在
M3——v1.0 词表=引擎表五动词，变声槽位由注册表机制自带（M3 RVC 注册即自动入链，管线零改，
PIPE-REG 金标准验证）。

## 方案

全量见 `docs/spec/fr9-pipeline.md`（开工前快照，含 D1–D10 论证与 §7.1 拍板）。要点：

- 多遍执行：cmdPipeline 循环组合既有 execute（单动词代码零复制）；
- steps=自包含数据模板（无 input 绑定，FR-10 换 input 循环即批处理单件）；
- 强制 workspace；每步一版本 parentVersion 链 + pipeline:{index,total} 可选标记；
- 错误归属 error.step/stepCmd + 消息前缀「步骤 N（cmd）：」（E_CANCELLED 例外逐字透传）；
- GUI 三条预设一键（D8），E_MODEL→modelMissing 取 error.stepCmd（附裁 1）。

## 改动

spec §4 改动面表：commandlayer.cpp（staticCheck 抽取 + cmdPipeline + 内置表条目 ~160 行）、
workspace（pipeline 标记 ~12 行 + 透传 ~4 行）、mediaservice（一对信号槽 ~14 行）、
ffextractor（doPipeline ~45 行）、mainwidget（预设下拉+按钮 ~60 行）、CLAUDE.md 同步 ~3 行、
ci-smoke 接线 1 行。**零新文件、零 CMake、零引擎/底座触碰**。

## 验证

PIPE-\* 用例族（spec §6 十一族）：正例链/编排等价（逐字节）/版本链与标记/整链预校验（零副作用
断言）/部分失败/schema 负例/取消/零漂移硬门禁/GUI 面/注册表零改（假引擎注入）/内存抽测。
门禁 = run-pipe 全绿 + 既有全门禁复跑（PIPE-ZERO）+ ci-smoke 接线。round-1 CLI 面、round-2 GUI 面。
