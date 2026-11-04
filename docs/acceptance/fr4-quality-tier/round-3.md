# FR-4 round-3：审查+测试收口与主线程修复轮（2026-11-04）

轮次定位：round-2 审查（十维 + 判别实验）与测试（独立复跑/红绿/补面）**并行**回传 → 主线程
配方 #7 逐项亲手复核 → 修复（P1×1 + P2×1 + P3×7）→ 全门禁复跑 → OQ-2 落裁。
基线 = round-1 工作区（未提交）+ round-2 测试侧扩面；本轮 src 改动仅 dfn3denoiser.{h,cpp}
（P1 修复 + 两断言 + 头注纠正）。

## 1. 审查发现与主线程复核（9/9 采纳）

| 级别 | 发现 | 主线程独立复核证据 | 处置 |
|---|---|---|---|
| **P1** | 零掩码帧解码器游标错位 + 堆越界读（dfn3denoiser.cpp:420-423 对 :351-354；头注 :17-19 把不变量写反） | 三腿：① regIdx 构造已排除零掩码帧（`!(l<lsnrMin)`）；② 官方 tract.rs `apply_stages` 零掩码档 `(false,true,false)`——m=零张量直返、erb_dec/df_dec 均不跑、不占解码器行；③ 判别实验独立复算：阈值搬移双侧同语义（引擎 config.ini lsnr_min=10 vs CLI `--min-db-thresh 10`）parity=**0.00dB**、max\|d\|=4.03e-01，对照 −73.74dB | 修（§2） |
| P2 | PAR-DF-FEAT 净环境 FAIL 非 SKIP（夹具为设计轮手工就位，fetch-deps 不备；`dfn3_feat_check.py:33` sys.exit(1)） | grep fetch-deps 零 candidates 引用；run_parity 只闸 onnxruntime import | 修（run_parity.sh FEAT 段补夹具目录 SKIP 闸） |
| P3-1 | DF 回溯 `xi=p-4+j` 硬编码 dfOrder=5（官方一般式依赖 max(L,D)；自由导入 D≠5 模型静默错位） | tract.rs:322/601 滚动窗容量与索引一般式直读；−75.3dB 证明 D=5/L=2 特例正确 | 修（setup 断言 dfOrder==5，锁支持域不投机泛化） |
| P3-2 | 帧分析/合成 mem 推进仅 fft==2hop 等价（:206 隐含假设，:556 却放行 fft>2hop） | 两处代码直读 | 修（setup 断言 fft==2*hop） |
| P3-3 | .h/.cpp 头注矛盾（附裁 C「扣留后缀」vs 实现「即发」）+ round-1 偏离清单漏记第三处 | 两注释直读；cpp 侧为实现真义（−75.3dB 背书） | 修（.h 注释改实现口径 + 本轮 §6 补记） |
| P3-4 | CLAUDE.md/spec 复刻清单列 post_filter/atten_lim 失实（实现不启用=CLI golden 缺省关同口径，行为无错纯文档失实） | grep 两处措辞 + dfn3denoiser.cpp:458 注释直读 | 修（CLAUDE.md 措辞；spec 为开工前快照不改，以本表与 §7.1 修订为准） |
| P3-5 | fetch-deps 下钻循环空目录死循环（`ls -A` 空串路径自叠） | sed 直读 :93-95 | 修（空判护栏 + 明确报错） |
| P3-6 | 目录条目取消删 .part vs 单文件保留（OQ-6 续传口径）有意分叉未记档 | modelmanager 两路径对比（审查 :589/:434） | 记档（CLAUDE.md FR-8 条补注） |
| P3-7 | ci-smoke `invariance dfn3` 素材 5s=500 帧 < 3000 帧内部块——全落首块，块界记账零覆盖（平凡通过） | DUR=5 直读 + 算术 | 修（素材改 70s 直采 48k，跨 ≥2 内部块） |

审查对实现轮两处 §7.1 记档偏离的判定（主线程维持）：golden 主链改 Releases 预编译=**合规**
（附裁 B 前提被实测推翻，sha 钉死可复现性不降）；SEAM −2.5dB 护栏=**实质偏离但处置可接受，
裁决权正确移交**——本轮 §5 终裁。

## 2. P1 修复与红绿链（本轮核心）

**修复**：删 `++*rPos`（零掩码分支不推进游标——官方该档不跑解码器不占行，行↔常规帧保 1:1，
越界读随错位同灭）；头注 :17-19 重写为正确不变量并记 round-8 修复溯源。

**测试盲区根因与补面**：dfn3noisy65 配方 10dB SNR，lsnr 恒 >−15，零掩码路径（连带 gatherS 压缩
路径）从未被任何用例触发——审查代理以**阈值搬移法**（双侧同语义抬门强制触发）实锤。补面固化为
**PAR-DF-004**（engine-parity round-8）：gen_golden 增 `dfn3min10` 变体（`--min-db-thresh 10`，
后缀 dfmin10 独立登记）+ run_parity 现配引擎侧模型目录副本（config.ini lsnr_min=10）。

**红绿链**：

| 阶段 | PAR-DF-004 | 其余 PAR-DF |
|---|---|---|
| 修复前（含 P1 树） | **段错误 SIGSEGV（核心已转储）**——65s 素材零掩码帧远超 washup 富余，rPos 大幅越界 | 001/002/003/SEAM/FEAT/SR 全 PASS（不触发路径零漂） |
| 修复后 | **PASS −78.8dB**（max\|d\|=4.74e-05，与常规路径同深） | 001 −75.3 / 002 −78.9 / 003 abs 1.65e-05 / SEAM −3.4 / FEAT 9×0 / SR bit-exact——**数字与修复前逐位一致** |

段错误级红证 = 生产定性升级：任何低信噪比真实输入（环境噪声/风噪/增益不足的纯噪声段）曾会使
质量档**直接崩溃**，非仅音质劣化。

## 3. SEAM 数据有效性判定（P1 与 OQ-2 的交叉）

round-2 实测真语音整段单 Run vs golden = **−74.1dB**——若该素材触发过零掩码帧，P1 错位会把此
口径打成 0dB 级（§2 判别实验同型）。−74.1dB 证明**登记素材在缺省阈值下不触发零掩码路径**，
故 round-1/round-2 全部 SEAM 数字（−12.8dB 三口径、K 扫描三点）为**纯接缝偏差、未被 P1 污染**，
可径直用于裁定；§2 修复后复跑数字逐位一致为最终佐证。

## 4. OQ-2 终裁（主线程）

**维持 K=100/块 30s；SEAM −2.5dB（合成素材口径）定格为回归护栏，不作质量门槛。** 理由：
①−60dB 在无状态图块式架构下物理不可达（K 扫描 100→3000 单调改善 −12.8→−24.9dB 不收敛，
K=5000 掩码偏差 0.10 不归零——注意力记忆有效无限）；②§7.1 梯子 K↑/块长↑实测排除，状态化
手术定性 M4 设计级变更非 FR-4 范围；③偏差两端均为合法 DFN3 输出、无感知劣化证据；④NFR-Q1
「不过不上线」由整段 −60dB 硬门守护（合成 −75.3dB / 真语音 −74.1dB vs golden；生产总偏差
= 接缝主导 −12.8dB 已量化记档）；⑤K=3000 翻倍推理成本仅换指标余量，无感知收益佐证，不成立。
裁定已回写 spec §7.1 修订段（留痕不改原拍板）+ CLAUDE.md FR-4 条。

## 5. 修复清单落点

| 项 | 文件 | 内容 |
|---|---|---|
| P1 | src/service/dfn3denoiser.cpp | 删零掩码 `++*rPos`；头注重写（官方三档解码器行占用语义） |
| P3-1/2 | src/service/dfn3denoiser.cpp | setup 支持域断言：`fft==2*hop`、`dfOrder==5`（与 convLookahead==2 同列，文案锁官方工作点） |
| P3-3 | src/service/dfn3denoiser.h | 头注改实现口径（图内化前瞻、即发无扣留 + 支持域） |
| P2 | docs/acceptance/engine-parity/scripts/run_parity.sh | FEAT 段补夹具目录存在性 SKIP 闸（净环境 SKIP 非 FAIL） |
| P1 补面 | 同上 + gen_golden.py | PAR-DF-004 接线 + `dfn3min10` 金样本变体（--min-db-thresh 10） |
| P3-5 | scripts/fetch-deps.sh | 下钻循环空目录断路护栏 |
| P3-7 | docs/acceptance/wp-1.2-linux-build/scripts/ci-smoke.sh | dfn3 不变性素材 5s→70s 直采 48k（跨 ≥2 内部块） |
| P3-4/P3-6 | CLAUDE.md | dfn3denoiser 架构行措辞（post_filter/atten_lim 不启用）+ FR-4 条（OQ-2 已裁/PAR-DF-004/round-8）+ FR-8 条（.part 取消分叉记档） |
| 记档 | docs/spec/fr4-quality-tier.md §7.1 | OQ-2 修订段 + 附裁 C 重解释留痕 |
| 归档 | docs/tmp/fr4-measurement.md → docs/reference/fr4-dfn3-measurement.md | tmp 规范转档（goal/spec 引用同步） |

## 6. round-1 偏离清单补记（第三处）

round-1 记档两处（golden 主链改 Releases CLI / SEAM 护栏口径）；审查发现第三处未记：附裁 C
「L=2 前瞻后缀扣留」实现期重解释为「图已内化 2 帧前瞻、行 p 随帧 p 就绪即发射、无需扣留」——
正确性由整段 parity（−75.3/−74.1dB）与行-帧对齐论证背书，spec §7.1 修订段已补留痕。

## 7. 修后全门禁复跑（磁盘卷 TMPDIR，串行）

| 门禁 | 结果 |
|---|---|
| run_parity PAR-DF（含新 004） | **全绿**（§2 表：004 −78.8dB，其余逐位一致） |
| run-fr4.sh 全族（61 判定，含 round-2 补面） | **全绿 exit=0** |
| ci-smoke 全量（含 PAR-DF-004 / 70s 跨块不变性 dfn3 / ISO 守卫） | **全绿 exit=0**（`== ci-smoke 全绿 ==`） |
| 资源面 | RTF=0.0293（门 ≤0.5）；**2h 峰值 RSS=734MiB**（门 ≤2048，round-1 829MiB 同量级机器波动）；wall 3:04 |
| 构建 | **0 警告**（cmake --build linux-test 全量重编；ci-smoke 日志 warning 计数 0） |

## 8. 收口

三轮闭环：round-0 设计（spec 作者 + 对抗枚举 + 实测取证三报告交叉）→ round-1 实现（全链落地
全绿自报）→ round-2 审查+测试**并行**（审查十维 + 判别实验抓出 P1；测试独立复跑/红绿五处/SEAM
三口径/补面 13 例/测试侧 3 缺陷自修）→ round-3 主线程配方 #7 复核（9/9 采纳）+ 修复（P1 段错误
级红绿链闭环）+ OQ-2 终裁 + 全门禁复跑全绿。挂账不变：Windows round-4 真机（编译+parity+GUI
三面）；NFR-C2 法务批次（DFN3 权重无独立 LICENSE）；FastEnhancer 附裁 D 留缝；M4 状态化手术
（SEAM 物理不可达的原则性修复位）。
