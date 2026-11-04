# FR-4 质量档 round-1：全链落地首测（2026-11-04）

基线 3066010（FR-10 收口）。本包按 spec `docs/spec/fr4-quality-tier.md` §7.1 拍板落地 SG-1..SG-6
全链：DFN3 引擎 + `aiDenoiseHq` 注册表条目 + 模型管理第三条目 + GUI 档位三通路 + PAR-DF parity 族
+ FR4/GFR4 验收族 + ci-smoke 接线。状态：**FR4/GFR4 全绿 + ci-smoke 全量全绿**（本文件 §6）。

## 1. 改动清单

| 件 | 内容 |
|---|---|
| `src/service/dfn3denoiser.{h,cpp}`（新增 744 行） | DFN3 引擎：vorbis 窗 STFT/WOLA、32 带 ERB、指数均值/单位归一、lsnr −15/35/35 三档门控、DF 滚动复数滤波、post_filter、atten_lim、静音跳过复刻；enc/erb_dec/df_dec 三会话 PIMPL；块 30s + K=100 warmup 前缀重放（附裁 C）；config.ini 伴读 + shape 断言；48k/1ch、输出跟随源 |
| `src/service/engineregistry.cpp` | `aiDenoiseHq` 条目（params={}/requiresModel=true/setupErrorClass=Model/lockCanonicalOutput=false/后缀 `_aihq`）——命令层/GUI/服务层零改，pipeline/batch 经引擎表自动入链 |
| `src/service/modelmanager.{h,cpp}` | 目录表 dfn3 条目（tar sha256=c94d91f7…）；下载→解包→目录级幂等（关键文件集在位即信）；models.json 目录条目形态；listModels 磁盘视角识别目录条目；by-id 指纹（先比对目录表 sha 再解包）/自由导入试开（合法 tar 解包失败→E_FILE、内容不对→试开 E_MODEL）；gtcrn 条目 hfSource 门控（见 §7 修复 3） |
| `src/service/ffextractor.{h,cpp}` | 质量档缺省模型两级解析接入（`kDfn3ModelRelPath`）；`doDenoiseAiWav(in, quality)` 档位随行（缺省 false=快速，旧探针单参调用面零漂移）；缺模型文案分档「找不到 AI 降噪（质量档）模型：%1」 |
| `src/view/mainwidget.{h,cpp}` | denRow 档位下拉（快速（GTCRN）/质量（DeepFilterNet3），QSettings `aiDenoiseTier` 记忆缺省快速）；`aiVerb()` 单源映射——七槽/管线步/批动作三面组包同取；modelMissing 用途文案/导入组合框/过滤器三处映射 |
| `src/service/mediaservice.{h,cpp}` | `aiDenoiseRequested(in, quality)` 档位透传（缺省 false） |
| `src/model/data.h` | `kSuffixAiHq`/`kDfn3ModelRelPath`/`kSettingsAiTier` 三常量 |
| `src/test/engine_test.cpp` | `dfn3` 动词（`dfn3 <in> <out> [块帧数] [模型目录]`，块长 0=整段） |
| `CMakeLists.txt` | dfn3denoiser 两行入 engine 库 |
| `scripts/fetch-deps.sh` | dfn3 tar（sha 钉死，tar 保留供导入测试/golden CLI `-m`）+ deep-filter CLI v0.5.1 musl 预编译（sha 钉死）双 ensure |
| parity 四件 | `gen_inputs.py` dfn3_speech 配方+三素材；`gen_golden.py` dfn3 子命令（一文件一进程）；`compare.py --gate-abs`+零除护栏；`run_parity.sh` PAR-DF 族（依赖分级 SKIP/PARITY_REQUIRED 升 FAIL） |
| 本目录 | `run-fr4.sh`（FR4 族 40 判定）+ `fr4-gui-probe.cpp`（GFR4 族 engine/gui 两相）+ `matrix.csv` |
| `engine-parity/` | matrix.csv round-7 列 + `round-7.md`（PAR-DF 证据）+ `dfn3_feat_check.py`（图级夹具回放） |
| ci-smoke | dfn3 依赖闸/mo48 素材/`invariance dfn3` + run-fr4.sh 接线（磁盘卷 FR4TMP） |

## 2. parity 结果（判据与证据 `engine-parity/round-7.md`）

| 用例 | 实测 | 门 |
|---|---|---|
| PAR-DF-001（65s 合成 noisy 语音整段单 Run） | −75.3dB | ≤−60 PASS |
| PAR-DF-002（480×7+377 边界） | −78.9dB | ≤−60 PASS |
| PAR-DF-003（最短 3 帧，近零 golden） | rms(d)=1.65e-05 | ≤2e-4 绝对 PASS |
| PAR-DF-FEAT（图级夹具 9 输出） | 全 0.000e+00 | ≤1e-5 PASS |
| PAR-DF-SR（生产链 vs 引擎直调） | max\|d\|=0 bit-exact | ≤−60 PASS（附 meta/head：engineResampled=false、48000/1ch） |
| PAR-DF-SEAM | −3.4dB | ≤−2.5 回归护栏 PASS（§3） |

钉死结论（实现口径，round-7.md §0/§2 同述）：**输出长度 = 输入长度**（不足 hop 尾帧零填，与官方
CLI break-on-short-chunk 在整素材上等长）；**df_dec 未命名第二输出**官方导出即有、引擎按名绑定
`coefs` 不消费；分块不变性（ci-smoke `invariance dfn3` 1000000000/4096/977 逐字节一致）。

金样本主链校准：spec 附裁 B 定 cargo 构建，实现轮实测 Releases **有** v0.5.1 musl 预编译 CLI
（sha256 ea9c8ce4…，fetch-deps 钉死备齐）——免 Rust 工具链依赖且出处可 byte 级钉死，改用之；
cargo 路径保留 runbook 备选（plan.md 勘误条目同步）。

## 3. SEAM 量化与 OQ-2 升级数据（块 30s vs 整段单 Run）

**现象（素材依赖强烈）**：

| 素材 | 全程 parity |
|---|---|
| 平稳合成（sine1k/wn3/step 探针） | 近 bit-exact（max\|D\| 3–7e-5 绝对） |
| dfn3noisy65（合成 noisy 语音，登记素材） | **−3.4dB**（PAR-DF-SEAM 门基线） |
| 真语音开发素材（/tmp/noisy65.f32 65s） | **−12.8dB**（K=100 现厂配置） |

**本质（dfn3denoiser.cpp 头注钉死）**：enc/erb_dec/df_dec 三图因果且对 Run 长度逐位稳定（同起点
Run 前缀逐位一致，已实测）——接缝不在图行为，在**注意力记忆有效无限**：重放起点 ≠ 0 时后续行与
整段连续态的偏差不随重放长度收敛（K=100→K=5000 实测掩码偏差仅 0.31→0.10，不归零）。

**K 扫描（真语音素材，全程/接缝段 t≥30s/max|D|）**：

| K | 全程 | 接缝段 | max\|D\| |
|---|---|---|---|
| 100（现厂） | −12.8dB | −10.9dB | 2.34e-01 |
| 300 | −14.8dB | −13.0dB | 2.17e-01 |
| 1000 | −17.1dB | −15.2dB | 1.88e-01 |
| 3000 | −24.9dB | −23.0dB | 1.05e-01 |

K 增大单调改善但不归零（O(K) 每块重放成本线性涨）；spec §7.1 已拍 K=100/块 30s，**阈值归属
OQ-2 主线程裁**：若真语音 −12.8dB（≈23% 相对误差）判不可接受，候选 = 调大 K（换算成本表见上）/
短素材单 Run/维持现状（感知面：输出仍是有效降噪语音，仅与整段参考有幅度级差）。本包判据分立：
单 Run −60dB 硬门禁保证实现正确，SEAM −2.5dB（基线 −3.4 + 0.9dB 余量）只作**回归护栏**防实现
退化，不作质量门槛。

## 4. FR4/GFR4 族关键证据（`run-fr4.sh`，matrix.csv 40 行全 PASS）

- **OK**：direct 48k/1ch `engineResampled=false` + outputSpec 跟随源（AI 引擎首个）；工作区
  `v001_aiDenoiseHq/<stem>_aihq.wav` + 版本记录键面恰 8 键（无 pipeline/batch 标记，非管线/批
  任务键面零漂移）；44.1k/2ch 源 `engineResampled=true` 输出回源规格（模型工作点 48k/1ch 锁定）。
- **NEG**：未知字段 E_PARAMS / 目录缺件 E_MODEL「模型目录不完整（缺 df_dec.onnx）」/互斥 E_PARAMS/
  原件只读 E_PARAMS/目录不存在 E_MODEL「找不到模型」五连 + 负例零产物。
- **EQV（零漂移锚）**：旧 aiDenoise 两次运行 cmp 逐字节一致；`v001_aiDenoise/<stem>_ai.wav` 命名
  面不变；缺模型报文 str 逐字锁定「找不到模型 <路径>」。
- **REG**：pipeline 步 0=aiDenoiseHq 双版本链成功（pipeline{index:1/2,total:2} 标记，人读 1 起计
  数）；batch 单步模板 2/2（batch 标记在位、无 pipeline 键——单步降级单动词子请求）；词表负例
  「steps[0] 的 cmd 不可链式」+ 整链预校验零目录。
- **MDL**：目录表 dfn3 条目（for=aiDenoiseHq/available/7983136/c94d91f7…）+ gtcrn 条目零漂移；
  by-id 导入官方 tar 四关键文件落位 + modelList 视角翻 installed；自由导入 id=myhq + license
  unknown（用户自核）；负例两分支（非法 tar→E_FILE 解包失败 / 指纹不符→E_MODEL）均零残留零落位；
  真网下载 0.99s 落地一致 + 幂等秒回（离线自动 SKIP，本轮在线实测）。
- **GFR4**：engine 相 hq-missing 文案+forVerb=aiDenoiseHq 逐字 / hq-lib 两级解析取库（版本记录
  model.path 真值）/ fast-missing 旧文案零漂移锚；gui 相 `TIERDEFAULT idx=0 texts=快速（GTCRN）|
  质量（DeepFilterNet3）`、`TIERPERSIST settings=quality reloadIdx=1`、三路组包
  `PATH7SLOT quality=1` / `PATHPIPE steps=[{"cmd":"aiDenoiseHq"},{"cmd":"separate"}]` /
  `PATHBATCH inputs=2 steps=[{"cmd":"aiDenoiseHq"}]`，切回快速档三面回落
  `cmd=aiDenoise`（FAST 行）。
- **RES**：生产 CLI 65s **RTF=0.0287**（门 ≤0.5；设计轮裸引擎 0.0147 同量级）；2h 48k 单声道
  **峰值 RSS=829 MiB**（门 ≤2048，NFR-P4 严格流式）+ wall 3:08（RTF≈0.043）+ 产物时长 7200s
  恒等长。
- **终闸**：素材原件 sha256 不变（原件只读红线）+ third_party DFN3 tar sha 不变（不动 third_party）。

## 5. 重要发现：官方 CLI 跨文件状态携带（golden 工程坑）

官方 deep-filter CLI **单次调用多文件不重置模型状态**（libDF 滚动谱缓冲/归一化状态跨文件携带）：
三文件同次调用产出的第 2/3 件金样本吃到前件尾巴，parity 假 FAIL 到 −0.0dB——一度误判为引擎 bug，
经 tract.rs 溯源 + CLI `-vvvv` 轨迹 + lsnr 逐帧对照（门控值与引擎逐位一致、图级夹具全零）定位为
**测试基建污染**。`gen_golden.py dfn3` 固化一文件一进程并记档（round-7.md §0）。同型素材口径：
官方 CLI 对不足 hop 尾帧 break 不输出，引擎零填——整素材等长对齐（PAR-DF-002/003 即边界卷）。

## 6. 零漂移回归网（ci-smoke 全量，串行）

`ci-smoke.sh build-linux` 全绿（含本轮新增 dfn3 依赖闸/分块不变性/PAR-DF 族/run-fr4 段）：
FFT-PR 自检 + 分块不变性六引擎（新增 dfn3）+ parity 全族（PAR-DS/GT/MD/PT/SR + DF）+ CLI 冒烟
7 动词 + ffprobe 体检 + CMD（run-json）+ WS/GWS（run-ws/gui-ws）+ MDL/GML（run-models）+
PIPE（run-pipe/gui-pipe/missing-text）+ BATCH（run-batch/gui-batch）+ FR4/GFR4/RES。构建零警告
（linux-test 全量重编 0 warning）。Windows round-4 真机挂账（同前七包惯例）。

两处跨包兼容修正（回归网抓出，属测试面/接口面而非行为漂移）：

1. `doDenoiseAiWav`/`denoiseAiWav` 档位参数给缺省 `= false`——fr8/fr-a4 探针单参直调面零漂移
   （否则 mdl-gui-probe/gui-ws-probe 编译断）。
2. `run-models.sh` MDL-010 由固定下标 `models.2` 改按 id 定位——FR-4 目录表增 dfn3 条目后列表
   基数 3→4，位置断言随目录表演进漂移（FR-8 行为本体零变化，全族复跑全绿）。

## 7. 其他修复与挂账

- **修复 3（modelmanager，SG-3 真网实测发现）**：ETag↔sha256 对照原对全部下载源生效，但该契约
  只对 HF resolve 源成立（LFS/Xet 文件 ETag 即 sha256，FR-8 D3 步骤 2）——GitHub 直连源（gtcrn
  release / DFN3 raw）的 ETag 是主机侧不透明值，真网下载必假报 E_NET「远端文件指纹与目录不符」。
  gtcrn 即直连源，此为其**潜伏真网断路**（FR-8 测试走本地 range_server 未暴露）。修复 = 对照
  收缩到 `hfSource` 条目（直连源收尾整包 sha256 兜底不变）；dfn3 按同口径直连落表。MDL-005
  真网实测 0.99s 一次落位佐证。
- **挂账**：①OQ-2 SEAM 阈值（§3 数据齐备待主线程裁）；②Windows round-4 真机（编译 + parity +
  GUI 三面）；③法务复核（DFN3 权重无独立 LICENSE，与 GTCRN/sherpa 同批挂 NFR-C2）；④FastEnhancer
  附裁 D 留缝（评估记档不集成，触发条件见 spec）。另注：不加 `--ai-denoise-hq` 旧式 CLI 薄壳是
  spec §4 零触碰清单的明文决定（「旧 7 薄壳」不动）——aiDenoiseHq 的契约面 = `--json`/GUI，
  与旧 7 动词薄壳共存不冲突。
