# FR-4 质量档选型验收 goal

## 总目标

按 spec `docs/spec/fr4-quality-tier.md`（D1–D7 + §7.1 主线程拍板）落地 FR-4 质量档：DFN3 引擎
（`dfn3denoiser.{h,cpp}`：前端 DSP 复刻 + 三会话 ORT + warmup/lookahead 块式 Run + config.ini 伴读）
+ 注册表新动词 `aiDenoiseHq`（表条目，命令层/GUI/服务层零改承诺第二次实战验证）+ 模型管理第三条目
（tar.gz 整包下载/解包/目录级幂等）+ GUI 档位下拉三通路同达 + 官方 CLI cargo 构建 golden 的
PAR-DF parity 族 + FR4-\* 验收族。FastEnhancer 评估记档不集成（附裁 D 留缝）。

## sub goal matrix

| sub goal | 内容 | 状态 |
|---|---|---|
| SG-1 | 引擎本体：dfn3denoiser（vorbis 窗 STFT/ERB/特征归一/lsnr 门控/DF 滚动滤波/post_filter/atten_lim/静音跳过复刻；enc/erb_dec/df_dec 三会话 PIMPL；块 30s+K=100 warmup+L=2 前瞻（附裁 C）；config.ini 伴读+shape 断言；分块不变性） | 完成（round-1 全绿） |
| SG-2 | 命令面：engineregistry aiDenoiseHq 条目 + data.h 三常量（kSuffixAiHq `_aihq`/kDfn3ModelRelPath/kSettingsAiTier）+ CMake 两行 + engine_test dfn3 动词（48k/1ch，块长参数 0=整段）+ fr24 契约表行 | 完成（round-1 全绿） |
| SG-3 | 模型管理：目录表 dfn3 条目（tar sha256=c94d91f7…登记）+ 下载解包目录级幂等 + models.json 目录条目形态 + listModels 磁盘视角识别目录 + 自由导入试开 + GUI 三处映射（kForText/onModelMissing/导入组合框） | 完成（round-1 全绿） |
| SG-4 | GUI 三通路：denRow 档位下拉（缺省快速=零漂移，QSettings aiDenoiseTier）+ doDenoiseAiWav/doPipeline/doBatchStart 组包按档位映射动词 + resolveStepModels 新分支 + MIST 新文案（「AI 降噪（质量档）」族逐字锁定） | 完成（round-1 全绿） |
| SG-5 | parity 与验收基建：官方 CLI cargo 构建 golden 主链（附裁 B）+ run_parity.sh PAR-DF 族（依赖分级 SKIP/PARITY_REQUIRED 升 FAIL）+ 图级夹具回归梯子（7 件已登记）+ run-fr4.sh（OK/NEG/EQV/REG/MDL/GUI/RES 族）+ ci-smoke 接线 | 完成（round-1 全绿） |
| SG-6 | 门禁全绿：零漂移回归（run-json/run-ws/run-models/run-pipe/run-batch/gui-slot-eqv/thinshell-eqv）+ 构建零警告 + FR4 全族 PASS + 文档（CLAUDE.md/fr24）+ 中文提交 | 完成（round-1 全绿） |

## 重大决策与关键信息

- 全部设计决策见 spec §2（D1–D7）+ §7.1 拍板（OQ 六条全裁 + 附裁 A–F）。设计轮三报告交叉对照：
  spec 作者 + 对抗约束枚举（八承重锚点主线程核实）+ 实测取证（`docs/reference/fr4-dfn3-measurement.md`）。
- 实测关键数字（i5-14400F/8vCPU，产线 ORT 1.30.0 直链）：DFN3 RTF 0.0147（S=100 块 1T，≈68× 实时）
  /0.0402（S=1 调用模式）；RSS +20.8MB（加载后）；GTCRN 基线 RTF 0.030、+11.6MB。NFR-P2 ≥5× 大幅
  达标不修需求（OQ-5）。
- 流式适配判定：DFN3 三图无状态（5 GRU 内化）——块式 Run + 附裁 C 接缝设计（warmup 前缀重放 +
  2 前瞻后缀）为离线正路；状态化手术留 M4 实时化（spec §5）。
- FastEnhancer：契约完美（官方驱动逐字取证，GTCRN 同构）但 ONNX 因网络死路由未获取——留缝附裁 D
  （runbook + 触发条件），不盲拍。
- parity golden 主链 = 官方仓克隆（`build-linux/.fr4-tmp/DFN3-official/`，f1d19bff）cargo 构建官方
  CLI（apt rustc/cargo 1.93.1 + crates.io CDN 已探明可达）——**round-1 再校准**：Releases 有 v0.5.1
  musl 预编译 CLI（sha 钉死 fetch-deps），主链改用之，cargo 留 runbook 备选（见进展 round-1 ②）；
  官方 npz 不存在（export.py 导出期随机生成）——图级夹具 7 件（合成×官方图，sha256 登记）只作图
  行为回归梯子。
- 残余法务（附裁 E）：DFN3 权重无独立 LICENSE + FastEnhancer MIT 转录级——挂 NFR-C2 与 GTCRN/sherpa
  同批复核。

## 进展

- round-0（2026-11-04）：设计轮收口。三报告交叉对照 + 主线程锚点核实 + 环境探明（cargo/crates.io
  可达性、python3.14 无 torch）→ §7.1 落档（OQ 全裁 + 附裁 A–F）+ 验收骨架（goal/plan）。
  模型与夹具已就位 third_party（不入库）；spec/实测报告/克隆三件套为实现轮输入。
- round-1（2026-11-04）：实现轮收口，全链落地全绿。①SG-1..SG-6 全完成：引擎/注册表/模型管理/
  GUI 三通路/PAR-DF 族/FR4 族 + ci-smoke 接线；②**金样本主链校准**：spec 附裁 B 定 cargo 构建，
  实测 Releases 有 v0.5.1 musl 预编译 CLI（sha 钉死 fetch-deps）→ 改用之（免 Rust 工具链 + 出处
  byte 级钉死），cargo 留 runbook 备选；③**重要发现两条**：官方 CLI 单次调用多文件不重置模型
  状态（golden 须一文件一进程，曾致假 FAIL −0.0dB）；ETag↔sha256 对照对 GitHub 直连源必假报
  （gtcrn 潜伏真网断路，修复=对照收缩到 hfSource 条目）；④**SEAM 量化**（OQ-2 数据）：合成语音
  −3.4dB / 真语音 −12.8dB@K=100，K 扫描 100→3000 改善 −12.8→−24.9dB 不归零（注意力记忆有效
  无限），门 −2.5dB 只作回归护栏；⑤两处跨包兼容修正：denoiseAiWav 档位参数缺省 false（旧探针
  编译面零漂移）、run-models MDL-010 改按 id 定位（目录表基数 3→4）；⑥RES：RTF 0.0287、2h 峰值
  RSS 829MiB。证据 matrix.csv + round-1.md + engine-parity round-7.md；提交由主线程统一执行。
- round-2（2026-11-04）：独立测试轮收口（src 零改动）。①门禁全量独立复跑绿：构建 0 警告 /
  run-fr4 40 判定 / PAR-DF 六组数字逐项一致 / PARITY_REQUIRED=1 升级机制复核 / dfn3 分块不变性 /
  零漂移回归网 11 项（run-models MDL-010 按 id——FR-8 零漂证实）/ ci-smoke exit=0；②红绿论证
  五处翻转均「恰好目标转红其余绿」（PAR-DF-001 门/SEAM 护栏/OK-001 规格值/EQV-001 比较对象/
  NEG-002 文案）；③SEAM 三口径供数 OQ-2：真语音单 Run vs golden **−74.1dB**（新数据点，引擎
  本征差）、生产块 vs 单 Run −12.8dB（逐位复现）、生产块 vs golden 直差 −12.8dB（新口径，接缝
  主导）；K 扫描 100/300/3000 三点逐位复现（−12.8/−14.8/−24.9dB）；④补面 +13 用例：NEG-002m/e
  （缺件逐件文案）、MDL-004c 族（坏内容 tar 试开败清理无孤儿）、GFR4-104/104b（快速档两级解析
  +产物直证）、GFR4-204/205（问询文案两档逐字）、ISO-001/002（golden 一文件一进程守卫接线
  ci-smoke）、RES-002p（2h 管线 RSS 729MiB）；⑤测试侧缺陷 3 项修复并复验（jr 失败分支缺用例
  ID/探针 output 对象层级/工作区根共享）。证据 matrix.csv round-2 列 + round-2.md。
- round-3（2026-11-04）：审查+测试并行轮收口 + 主线程修复。①审查十维（+阈值搬移判别实验）与
  测试独立复跑并行回传，主线程配方 #7 逐项复核 **9/9 采纳**；②**P1 修复**：零掩码帧解码器游标
  错位 + 堆越界读（官方该档 `(false,true,false)` 不跑解码器不占行）——修复前段错误 SIGSEGV、
  修复后 PAR-DF-004 **−78.8dB**（阈值搬移双侧同语义新护栏用例 + gen_golden dfn3min10 变体），
  其余 PAR-DF 逐位一致（登记素材不触发该路径，round-1/2 SEAM 数据未污染）；③P2：FEAT 夹具
  SKIP 闸（净环境不再 FAIL）；P3×7 全处置（setup 支持域断言 dfOrder==5/fft==2hop、头注矛盾、
  post_filter/atten_lim 措辞、fetch-deps 护栏、.part 分叉记档、ci-smoke 不变性素材 5s→70s 跨
  内部块）；④**OQ-2 终裁**：维持 K=100/块 30s，SEAM −2.5dB 定格回归护栏不作质量门槛（梯子
  实测不收敛，状态手术留 M4；NFR-Q1 由整段 −60dB 硬门守护，真语音 −74.1dB 佐证）——spec
  §7.1 修订段留痕；⑤偏离记档补第三处（附裁 C 后缀重解释）；⑥修后全门禁：run-fr4 61 判定 +
  ci-smoke 全量 exit=0、RTF 0.0293、2h RSS 734MiB、0 警告；⑦实测报告转档 docs/reference/
  fr4-dfn3-measurement.md。证据 matrix.csv round-3 列 + round-3.md + engine-parity
  round-8.md。**全绿收口**，提交主线程统一执行。
