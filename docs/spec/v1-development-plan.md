# v1 开发计划与工作包分解（WBS）

- 日期：2026-10-13
- 性质：开工前快照（spec，不回头维护；进展追踪见 `docs/acceptance/<feature>/goal.md`）
- 输入基线：`docs/REQUIREMENTS.md` v1.1（决策 D1–D7、FR/NFR、风险 R1–R11、执行顺序）
- 现状代码：离线核心已完成（FR-2/5/6/12、FR-3/4 各一半），带五项 P0 质量债

## 1. 里程碑总览

| 里程碑 | 主题 | 出口条件（DoD） | 阻塞的开放问题 |
|---|---|---|---|
| M1 | 正确性兜底（P0 债 1–5 + 测试/CI 基建） | `acceptance/engine-parity/matrix.csv` 全绿；CI Linux+Windows 双跑绿；NFR-Q1/Q2/Q3 有量化证据 | 无（O1 已于 2026-10-14 关闭） |
| M2 | 离线闭环（命令层/注册表/工作区/管线/批处理/模型管理/DFN3） | FR-1/4/8/9/10/13-前置、FR-24、FR-A4、NFR-M1 验收通过；GUI 全操作走命令层 | 无 |
| M3 | RVC 变声（FR-7） | FR-7 验收（REQUIREMENTS §7）；O2 已关闭（2026-10-14，ContentVec 为 MIT、R1 降级），无前置闸门 | 无（O2 已关闭） |
| M4 | 实时化（FR-11） | **O3 实测达标后方可开工**；round-trip ≤150ms 实测存证；NFR-L1/L2/L3 | O3（延迟实测） |
| 发版 | v1.0 tag | O1/O4 已关闭（2026-10-14），残余法务复核项随 NFR-C2 落实（sherpa 书面确认、ContentVec 权重传递链）；NFR-C2/C3/C5；全局门禁：parity + eval 全绿（REQUIREMENTS §7） | 无（O1/O4 已关闭） |

依据：执行顺序「正确性兜底 → 离线闭环 → 变声 → 实时化」（REQUIREMENTS §9）。

## 2. M1 工作包（本轮，全部有 spec）

| # | 工作包 | spec | 关联 | 规模 | 依赖 |
|---|---|---|---|---|---|
| WP-1.1 | parity 测试基建（金样本/指标/用例/比较器） | `parity-test-plan.md` | P0#1、NFR-Q1/Q2、NFR-M2 | M | WP-1.2（Linux 跑 DUT）或 Windows 手跑 |
| WP-1.2 | Linux 引擎测试构建 + CMake 分层 + CI 双跑 | `ci-build-strategy.md` | NFR-M2/M4、P0 债「D:/qt6 硬编码」 | M | 无（先行） |
| WP-1.3 | FFT 重写（twiddle 表 + 2 的幂直通 + Bluestein 精度） | `fft-rewrite.md` | P0#2 | M | 无 |
| WP-1.4 | 分离器输出契约校验（+引擎 I/O 断言补强） | `separator-output-validation.md` | P0#3、NFR-R4 | S | 无 |
| WP-1.5 | 采样率策略：跟随源，消灭 44.1k 往返 | `canonical-rate-48k.md` | P0#4、NFR-Q3 | M | WP-1.1（回归保护） |
| WP-1.6 | hum 陷波 50/60Hz 参数化 | `hum-notch-60hz.md` | P0#5、FR-3 | S | 无 |
| WP-1.7 | FFmpeg 构建统一 BtbN（弃 gyan+BtbN 混用） | `ci-build-strategy.md` §5 | NFR-C3、R6 | S | 无 |

建议顺序：WP-1.2 → WP-1.3 → WP-1.1 → （1.4 / 1.5 / 1.6 / 1.7 可并行收尾）。
理由：没有 Linux 侧 DUT 构建与回归网，先动 FFT/采样率这类全局改动等于裸奔（先兜底再改）。

## 3. M2 工作包（概览，开工前再出 spec）

按依赖序：FR-24 无头命令层（JSON 进/JSON 出，CLI 与 GUI 共用）→ NFR-M1 引擎注册表（FR-24 载体）→ FR-A4 工作区版本化（原件只读 + 输入哈希 + 版本化输出）→ FR-8 模型管理器（HF/镜像可配，NFR-C4）→ FR-9 管线串联 → FR-10 批处理队列 → FR-4 DFN3 质量档（parity 对照官方 Rust runtime）→ FR-1 多音轨选择。
红线提醒：FR-A4 落地后一切新功能必须走工作区，不得改写源文件。

## 4. M3/M4 前置闸门

- ~~M3 开工前必须关闭 **O2**~~ **O2 已于 2026-10-14 关闭**（ContentVec 实为 MIT，R1 降级为低×中；REQUIREMENTS v1.1）——M3 无前置闸门，残余法务复核移至发版期随 NFR-C2 落实。
- M4 开工前必须完成 **O3**（RVC chunk 推理延迟最小 benchmark，R2，真机脉冲法）；实测数据决定实时架构是否成立。
- 均为「先实测/先核验，后动工」闸门，不允许边写边赌。

## 5. 横切约定

- 每个工作包实现期在对应 `acceptance/<feature>/` 留证据链（matrix.csv + round-N.md），全绿才写 report.md。
- 行为或接口变化同步更新 `CLAUDE.md` 关键约定与 `README.md`（如 WP-1.5 会改「输出统一 44.1k」约定）。
- 依赖版本与来源统一登记进 `ci-build-strategy.md` §4 依赖矩阵（NFR-M3）。
- 本计划不新增/修改任何需求；需求变更走 REQUIREMENTS.md 修订记录。

## 6. 本轮明确不做

- 不实现 FR-7/11/13（M3/M4 范围）；不做 Agent 本体（v1.x，D5）。
- 不引入单测框架以外的重型基础设施；测试基建以「脚本 + CLI + 金样本」为核，框架选型在 WP-1.2 内最小化决策。
- 不动 GUI 交互层（M2 命令层接入时才动）。
