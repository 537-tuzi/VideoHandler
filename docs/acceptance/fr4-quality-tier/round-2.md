# FR-4 round-2：独立测试轮（门禁复跑 + 红绿论证 + SEAM 补测 + 补面 + 缺陷记档）

- 轮次定位：独立测试代理（与实现轮不同实例），不复用 round-1 自报结论，全部独立复跑。
  基线 3066010 + 工作区未提交改动（实现轮产物）；本轮 **src/ 零改动**（终态 `git status` 核对 +
  `dfn3denoiser.cpp` sha256 前后 413e43a1… 恒等，K 扫描临时改值后逐字节还原）。
- 测试侧改动（仅 `docs/acceptance/` 与探针/脚本，均经套件自身匹配式复跑验证，见 §5）：
  run-fr4.sh 扩面 +10 判定、fr4-gui-probe.cpp 补 fast-lib 相与问询文案捕获、
  新增 dfn3-cli-isolation.sh（ISO 守卫）并接线 ci-smoke。

## 1. 门禁独立复跑（任务 1，全串行）

| 门禁 | 结果 | 与 round-1 对照 |
|---|---|---|
| 构建：增量（no work）→ touch dfn3denoiser.cpp 单元重编 → find src 全量重编 26 步 18 单元 | **0 警告** | 同 |
| run-fr4.sh 全族（TMPDIR=build-linux/.fr4-tmp 磁盘卷） | **40 判定全 PASS，exit=0**；RTF=0.0306（round-1 0.0287，同量级波动）；2h 峰值 RSS=829MiB；wall 3:04.23；MDL-005/006 真网在线通过 | 数字一致（RTF +6.6% 属机器态波动，门 ≤0.5 余量大） |
| run_parity.sh PAR-DF 段（常规） | **全族 PASS**：−75.3dB / −78.9dB / abs 1.65e-05 / SEAM −3.4dB / FEAT 9×0.000e+00 / SR bit-exact（max\|d\|=0） | 六组数字与 round-1/engine-parity round-7 逐项一致 |
| run_parity.sh PARITY_REQUIRED=1 口径演示 | **exit=1**（SKIP 升 FAIL 机制有效；PAR-DF 依赖在位不受影响，PAR-MD 缺 audio-separator 本地维持 SKIP——round-1 同口径） | 机制复核 |
| engine_test dfn3 分块不变性（1e9/4096/977） | 三块长输出 sha 唯一（240000 样本逐字节一致）**PASS** | 同 |
| 零漂移回归网 11 项 | run-json ✓ run-ws ✓ gui-ws ✓ run-models ✓（MDL-010 按 id 定位，FR-8 行为零漂）run-pipe ✓ gui-pipe ✓ missing-text ✓ run-batch ✓ gui-batch ✓ gui-slot-eqv ✓ thinshell-eqv ✓（16/16）**全绿** | FR-8/9/10/A4/24 五面零漂移独立证实 |
| ci-smoke 全量 | **exit=0** 全绿（接线前全量一次 + ISO 守卫接线后全量复跑一次，均 exit=0；后次尾部 `== ci-smoke 全绿 ==` + ISO-001/002 PASS，日志中 18 处「FAIL」字样经核全为 WS-FAIL/PIPE-FAIL/BATCH-FAIL 族段名与用例名） | — |

一次性环境时序（非缺陷）：gui-slot-eqv 首跑报「FAIL: worktree 建不了」，与后台 gui-batch 并发的
git 时序冲突；手动 `git worktree add` 验证正常，串行复跑全绿（fd7badb）。本轮其后未再复现。

## 2. 红绿论证：断言非空洞（任务 2）

五处字节级翻转（改期望值/期望文案/比较对象），每处预期**恰好目标用例转红、其余绿**：

| # | 翻转点 | 转红输出（实测摘录） | 其余用例 |
|---|---|---|---|
| 1 | PAR-DF-001 门 −60 → **−76** | `PAR-DF-001: FAIL parity=-75.3 dB (gate -76.0)  max\|d\|=4.74e-05`，尾部 `== parity FAIL（见上）==` exit≠0 | PAR-DF-002/003/SEAM/FEAT×9/SR×3 全 PASS |
| 2 | PAR-DF-SEAM 护栏 −2.5 → **−4.0** | `PAR-DF-SEAM: FAIL parity=-3.4 dB (gate -4.0)  max\|d\|=5.69e-01` | 001/002/003/SR 全 PASS |
| 3 | FR4-OK-001 期望 sampleRate 48000 → **44100** | `FAIL: result.outputSpec.sampleRate 期望数字 44100，实为 48000` | OK-001z/001d/002/002z/002f/003、NEG×6、EQV×3 全 PASS |
| 4 | FR4-EQV-001 比较对象 eq1b → **p48m.wav**（16bit 源 vs f32 产物必不同字节） | `FAIL FR4-EQV-001: 附加断言失败`（cmp 非零） | EQV-002/002f/003 及全段其余 PASS |
| 5 | FR4-NEG-002 期望文案 df_dec.onnx → **enc.onnx** | `FAIL: error.message 不含期望片段：'模型目录不完整（缺 df_dec.onnx）：<路径>'`（引号内为 json_check 打印的**实值**——实值正确报 df_dec，期望已被翻转故判不过） | NEG-001/003/004/005/noart 全 PASS |

五处均「恰好目标转红、其余绿」，断言非空洞成立。翻转副本跑完即删（`run-fr4-flip*.sh`、
`run_parity-flip*.sh`，终态 scripts/ 目录核对无残留）。

## 3. SEAM 独立复测与补面（任务 3，OQ-2 裁定供数）

真语音素材 65s 48k/1ch（run_parity SEAM 配方独立重跑），三口径：

| 口径 | round-2 实测 | round-1 对照 |
|---|---|---|
| 整段单 Run vs 官方 golden（CLI 重产） | **−74.1dB**（max\|d\|=5.33e-05，corr=1.000000） | 未登记（**新数据点**：−60dB 硬门禁在真语音上同样过） |
| 生产块 30s（K=100 现厂）vs 我方整段单 Run | **−12.8dB**（max\|d\|=2.34e-01，corr=0.976594） | 自报 −12.8dB，**逐位复现** |
| 生产块 30s vs 官方 golden 直差 | **−12.8dB**（max\|d\|=2.34e-01，corr=0.976592） | 未登记（**新口径**：生产模式对官方参考的总偏差 ≈ 接缝主导，−74.1 的引擎本征差深埋其下） |

- golden 链独立复现：官方 CLI 重跑 dfn3noisy65.wav → 解 f32 → 与登记 golden `max|d|=0`
  （compare.py --case CLI-REPRO）——CLI 确定性 + 登记 golden 可复现。
- K 扫描复核（kWarmupFrames 临时 sed 改值 + 独立 build-k300 构建目录，跑完 cp 还原源码，
  sha256 前后均 `413e43a1c4553a0b4891c395d1d40daeca62d80e5b7244efc21b22ea47c94fa0`，构建目录已删）：

| K | round-2 实测 | round-1 自报 | 一致性 |
|---|---|---|---|
| 100（现厂，未改源） | −12.8dB / 2.34e-01 | −12.8dB / 2.34e-01 | 逐位一致 |
| 300 | **−14.8dB / 2.17e-01**（corr=0.988828） | −14.8dB / 2.17e-01 | 逐位一致 |
| 3000 | **−24.9dB / 1.05e-01**（corr=0.998483） | −24.9dB / 1.05e-01 | 逐位一致 |

单调改善不归零三点全复现（K=1000 档未重跑：两端 + 中点已证曲线可信）。结论维持 round-1：
注意力记忆有效无限，−2.5dB 门只作回归护栏，**阈值裁定归 OQ-2 主线程**（本节即为供数）。

## 4. 补面用例（任务 4，对照 spec §6 族面 + 实现面找洞）

全部并入 run-fr4.sh（+10 判定）或独立探针，扩面后全族复跑 **61 PASS，EXIT=0**：

| 新用例 | 断言 | 覆盖缺口 |
|---|---|---|
| FR4-NEG-002m | 目录缺 config.ini → E_MODEL「模型目录不完整（缺 config.ini）」 | 目录型模型关键文件集逐件文案（round-1 只测缺 df_dec） |
| FR4-NEG-002e | 目录缺 enc.onnx → E_MODEL「模型目录不完整（缺 enc.onnx）」 | 同上 |
| FR4-MDL-004c/c2/c3/c4 | free 导入 fakedfn3 tar.gz（文件名齐、内容垃圾）→ 试开败 E_MODEL 文案含 config.ini；c2 无产物目录 / c3 无 .part / c4 无 `.-import-stage` stage 残留 | 自由导入坏 tar 清理无孤儿（round-1 只测非法 tar 字节流与指纹不符） |
| GFR4-104/104b | engine 相 fast-lib：库内 gtcrn 两级解析 → 任务 ok + modelMissing 0 + 版本 model.path=库路径；104b CLI `--ai-denoise` 与 GUI 槽产物逐字节 cmp | 快速档两级解析 + GUI 槽产物直证（round-1 只测 hq 档） |
| GFR4-204 | gui 相 hq 档缺模型问询文案「AI 降噪（质量档）模型不可用，是否打开模型管理下载？」逐字 | modelMissing(aiDenoiseHq) 引导弹流文案（round-1 锁的是 engine 相 runJob 文案） |
| GFR4-205 | fast 档同位文案「AI 降噪模型不可用，是否打开模型管理下载？」逐字 | 零漂移锚（旧文案不因新增分支移位） |
| ISO-001/002（dfn3-cli-isolation.sh，接线 ci-smoke） | 官方 CLI 两文件同进程 vs 独立进程：首件 cmp **一致**（ISO-001）/ 次件 cmp **不一致**（ISO-002，状态串染实证） | golden 契约「一文件一进程」测试守卫——round-1 只在文档记档，若后人把 gen_golden 改批量或官方修好状态重置，ISO-002 转红报警（守卫即报警器，非恒真断言） |
| FR4-RES-002p（本轮实测记档） | 2h 管线 [aiDenoiseHq→pitch]：峰值 RSS=746040KB（**729MiB**，门 ≤2048）、wall 3:22.65、产物时长 7200.000000 恒等、v002_pitch/r2h_aihq_pitch.wav 落位 | 双引擎链 + 中间 f32 落盘的峰值内存（BATCH-MEM 方法论 /usr/bin/time -v；低于单动词 829MiB——峰值=最重步而非叠加，与 FR-9/10 结论同型） |

族面覆盖对照（不新增用例、记档映射）：管线 vNNN_aiDenoiseHq + pipeline 标记共存已由
FR4-REG-001/001f/001z 覆盖；档位三通路映射 + QSettings 已由 GFR4-201..203 覆盖；
快速档与基线组包对照由 GFR4-203 + thinshell eqv-ai-model + gui-slot-eqv st_ai 承载，
本轮 GFR4-104b 再补产物级直证；NFR-P5 按需加载由 BATCH-MEM（批峰值=单件峰值非叠加）承载。

## 5. 测试侧缺陷与修复（任务 5，修后均套件自身匹配式复跑转绿）

- **T-1（run-fr4.sh jr() 失败分支不打印用例 ID）**：翻转 3 复跑时实锤——失败只输出 json_check
  裸行（`FAIL: result.outputSpec.sampleRate 期望数字 44100，实为 48000`），无 `FAIL FR4-OK-001`
  行，失败难定位。修复=else 分支补 `echo "  FAIL $id"`。复验：①翻转 5 重放失败路径，输出
  `FAIL: …不含期望片段…` + **`FAIL FR4-NEG-002`** + exit 1；②正常路径扩面全族 61 PASS EXIT=0。
- **T-2（探针新码 firstOutputPath 读错层级）**：初版读 `output` 为字符串，但版本记录中
  `output` 是对象（{durationSec,frames,outputSpec,path,truncated}）→ MODELOUT 返回空。
  修复=读 `output.path` 并拼 `wsRoot/<hash 目录>/<rel>`。复验：手动跑 fast-lib 相，
  MODELOUT 返回绝对路径且 104b cmp 一致；扩面复跑 GFR4-104/104b PASS。
- **T-3（编排缺陷：fast-lib 与 hq-lib 共用工作区根）**：同输入 → 同内容哈希 → 同工作区目录，
  versions[0] 被另一相记录占据，firstModel/firstOutputPath 取错版本。修复=fast-lib 相独立
  `ws-eng-fast` 根。复验同 T-2（GFR4-104/104b PASS 即其上）。
- 附：json_check 的 `msg-contains` 失败行打印的是**实值**（`{v!r}`=实际 error.message，
  fr24/scripts/json_check.py:56），读红绿证据时勿把引号内容当期望值。

## 6. 未收敛项 / 挂账

- **OQ-2 SEAM 阈值**：本轮三口径 + K 扫描供数完毕（§3），阈值裁定归主线程，测试侧不改门。
- PAR-MD（MDX parity）本地缺 audio-separator 维持 SKIP（round-1 同口径）；PARITY_REQUIRED=1
  口径下会升 FAIL，CI 侧依赖在位。
- Windows 真机复验（round-4）挂账：FR-4 全族 GUI 面（GFR4-201..205 widget 面探针）与
  mingw 构建均未在 Windows 验证。
- gui-slot-eqv worktree 一次性时序（§1）——非缺陷，并发跑 git worktree 时需串行。
