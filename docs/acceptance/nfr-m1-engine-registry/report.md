# NFR-M1 引擎注册表 验收报告

- 结论：**全绿（Linux）**。片 A（注册表 + 元数据驱动校验，832e49b）+ 片 B（ErrInfo 结构化错误通道 +
  前缀匹配退役，813d6af）双双合入；spec `docs/spec/nfr-m1-engine-registry.md`（D1–D7 + §7.1 拍板）全数兑现。
- **REG-WIN / WIN PENDING**：Windows 真机复验随 WP-1.0 round-4（fr24 同款挂账口径）。

## 兑现的目标

1. **新增引擎 = 新 .h/.cpp + engineTable() 追加条目 + CMake 两行，命令层/GUI/服务层零改**——
   金标准 REG-001 实证（detached worktree 假引擎注入，git status 白名单全等断言 + 构造性破坏复证；
   脚本含基线清单完整性断言防静默漏拷）。
2. **校验消息模板唯一权威 validateParams**：四 Kind schema + 缺省单源 defStr 回填（filled 出参），
   与旧处理器逐字兼容（run-json 36 零漂移 + REG-003..011 三态模板同构 + String 分支补覆盖）。
3. **结构化错误通道 ErrInfo**：错误码在产生点与中文文案同点诞生（errinfo.h 唯一 E_* 源），
   classify*/isFileError 前缀匹配硬切退役；OQ-4 承载面修正三差（送包解码/写包/写尾 → E_FILE，
   新旧二进制对照实证）；GUI 七槽文案逐字不变（槽级等价 + 产物 cmp）。

## 关键证据（详见 round-1/round-2 与 matrix.csv）

| 面 | 结果 |
|---|---|
| REG-001..011（假引擎 e2e + RMS 0.0000% + String/Number 三态） | 全 PASS |
| ERR 族 13 用例（OQ-4 δ 三差 e2e + 坏输出契约注入 + setup 失败 + 取消双路 + CMD-032 逐字节保真） | 全 PASS（1 条代码级：输出缺失不可构造，setup 不变量封死） |
| run-json 36 / thinshell 16:16 / gui-slot 全绿 / ci-smoke 全量（parity 四族） | 各轮基线（198493b / 832e49b）双跑全绿 |
| 构建 -Wall -Wextra 零警告 + 零改动范围自证（片 A 恰三文件 / 片 B 恰 14+1） | PASS |

## 质量制衡记录（多代理配方）

每片均过：实现（自证）→ 主线程逐行预审 + 六项独立复跑 → 审查代理（对抗）→ 测试代理（独立验收）
→ triage → 修复复证 → 中文提交。两轮审查合计 P0=0；片 A P2×2（setupErrorClass UB 防护 +
Kind::String 覆盖）、片 B P1×1（实现方「trailer 静默截断」误记经三路独立反证推翻，P3 债撤销）
均已裁决处置并复证。教训沉淀：验收文档每条「实证」须可复现；实现方自报缺陷用于移交决策前主线程必复现。

## 遗留与移交

- REG-WIN/WIN：Windows 真机随 WP-1.0 round-4。
- ERR 族 ad-hoc 配方在 round-2.md（未固化 .sh）——与片 A KIND 族同口径，是否入 ci-smoke 随 OQ 拍板。
- 后续受益方：FR-9 管线串联（注册表条目即管线节点候选）、FR-7 RVC/FR-23 插件（新增引擎零改命令层）、
  FR-A4 工作区（原件只读防线已在信封）、NFR-L2 processInto（IStreamEngine 接口演进）。
