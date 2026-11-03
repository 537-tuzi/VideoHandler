# FR-9 管线串联验收 report（全绿复盘，2026-11-03）

总目标见 `goal.md`；需求/方案/改动见 `plan.md` + `docs/spec/fr9-pipeline.md`（D1–D10 + §7.1）；
逐轮证据 `round-1.md`（实现轮）+ `round-2.md`（审查+测试收口轮）。

## 结论：全绿合入

- **用例规模**：matrix.csv 97 行全 PASS——PIPE-OK 10 / EQV 9 / CHAIN 6 / PREVAL 7 / FAIL 8 /
  NEG 19 / CAN 6 / PROG 1 / GUIE 7 / MEM 4（round-2 增 003/004 带模型链）/ GUI 7 /
  MIST 11（round-2 补面）/ REG 2（fake-engine-e2e 金标准延伸）。
- **最终门禁**（round-2 全量复跑）：run-pipe 77 PASS、gui-pipe 7 PASS（iso9660 挂载红条件下）、
  missing-text 11 PASS（主线程亲手复跑）、run-models 142 PASS、gui-slot-eqv 18 PASS（基线
  fc69173）、thinshell-eqv 16 PASS、fake-engine-e2e 25 PASS、ci-smoke 361 PASS（唯一 SKIP=
  PAR-MD 本地缺 audio-separator，CI 以 PARITY_REQUIRED=1 强制）、构建零警告。
- **核心论断实证**：
  - D1 组合非复制：管线 vs 手动逐步跑终产物逐字节 cmp 一致（免模型 + 带模型双链）。
  - D1 峰值=逐步最大值而非和：免模型 60,880 ≤ 60,056+32MB 且 <256MB；带模型 1,517,592 ≤
    1,515,180+32MB 且 <2GiB（GTCRN 会话随遍释放零叠加；与审查代理独立补测吻合，差 2.4–2.7MB）。
  - 整链静态预校验零副作用（参数错/模型缺开工前拦截 + 工作区根零目录 + 源 sha 不变）。
  - 零漂移三触碰面：staticCheck 抽取逐字等价、workspace 按存在性插键（非管线逐字节一致）、
    信封零改——gui-slot-eqv/thinshell-eqv/run-json/run-ws/run-models 全绿承载。
  - 注册表开放性：假引擎 gain 免改入链（REG-012/013），M3 RVC 注册即自动入链。
- **双轮过程**：round-1 实现（src 六文件零新文件零 CMake）+ round-2 双代理收口（审查 10 维度 +
  测试 10 组 → 四修复 + 复审轮 P2-1 else-skip + MIST 文案锁）；设计轮双代理交叉对照补缝
  （附裁 1：forVerb 取 error.stepCmd）与 D1 复审补面（带模型链 MEM）是「对抗性独立报告」价值的
  两个实证。

## 遗留与挂账

- **Windows round-4 真机**：全 FR-9 面（GUI 片 B 运行时行为、带模型链、光盘挂载类环境脆弱性）
  随既有 round-4 挂账（runbook `docs/acceptance/wp-1.0-hotfix/round-4-windows.md`）。
- **P3-5 truncated 覆盖缺口**（测试轮挂账）：管线链中 truncated 标记的传递面无 I/O 错误注入
  用例（需磁盘满/写中断故障注入基建）；单动词面 truncated 已有既有覆盖。
- **记档不修**：单动词 MEM 基线无 ok 守卫（失败可被 004 变红捕获，缺精准定位，与 round-1 口径
  一致）；用途映射与 ffextractor verb→disp 是两份平行知识（MIST 逐字断言已锁死漂移面，新引擎
  需同步两处+断言）；QTimer 悬挂定性复现证伪（round-2.md 记档，两探针现形态安全）。
- **环境注记**：本机 iso9660 双卷挂载期间 selectFile 类探针应答死泊——探针已加固为行编辑直填
  +AcceptRole（pipe/mdl/missing-text 三探针统一），该形态为今后探针配方。

## 附：ci-smoke 接线

run-pipe + gui-pipe + missing-text（MIST 族，round-2 增）三段均走 `$BUILD/.pipe-tmp` 磁盘卷
TMPDIR（/tmp 配额教训，fr8 round-2）；PIPE 族缺模型时 else-skip + PARITY_REQUIRED=1 升级
FAIL 的门禁语义三处齐整。
