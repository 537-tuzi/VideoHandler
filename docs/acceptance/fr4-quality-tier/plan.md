# FR-4 质量档选型 plan

## 需求

`docs/REQUIREMENTS.md:89` FR-4 离线 AI 降噪：快速档 GTCRN（已有）+ 质量档；v1.4 注：FastEnhancer
并列候选 M2 评估。选型结论（spec §7.1）：**DFN3 落地集成，FastEnhancer 评估记档不集成**（网络
死路由无一手数据不盲拍，留缝附裁 D）。NFR-Q1（parity ≤−60dB 不过不上线）、NFR-P2（实测 68× 达标）、
NFR-P4（严格流式）、NFR-Q3（48k 源免重采样，AI 引擎首个跟随源）。

## 方案

见 spec `docs/spec/fr4-quality-tier.md` §2–§4（D1–D7 + §3 数据流 + §4 改动面）+ §7.1 拍板。要点：

- 引擎 `dfn3denoiser`：IStreamEngine + PIMPL，前端 DSP 按 `DFN3-official/libDF/src/{lib.rs,tract.rs}`
  C++ 复刻（vorbis 窗 960/480、32 带 ERB、指数均值/单位归一、lsnr −15/35/35 三档门控、DF 滚动复数
  滤波（coefs 末维 10=5×2 实虚）、post_filter、atten_lim、静音跳过）；三会话 ORT；块式 Run 30s +
  K=100 warmup 前缀重放 + L=2 前瞻后缀（附裁 C）；config.ini 伴读 + shape 断言；48k/1ch、
  lockCanonicalOutput=false（输出跟随源）。
- 命令面：engineregistry 新条目 `aiDenoiseHq`（params={}/requiresModel/E_Model/不锁兜底/`_aihq`），
  词表=引擎表枚举 → pipeline/batch 自动入链（FR4-REG 守门）。
- 模型管理：目录表第三条目 dfn3（forVerb=aiDenoiseHq 独占；tar.gz 整包 sha256=c94d91f7…；下载→
  解包 `denoiser/dfn3/` 目录→关键文件集在位即信幂等；失败删产物不留孤儿；models.json 目录条目）。
- GUI：denRow 档位下拉（缺省快速零漂移，QSettings aiDenoiseTier）+ 三通路（单步/管线/批）按档位
  映射动词 + MIST 新文案「AI 降噪（质量档）」族。
- parity：golden 主链 = cargo 构建官方 deep-filter CLI（本地克隆 f1d19bff；apt cargo 1.93.1 +
  crates.io 可达已探明）；两层判据 = 整段单 Run −60dB 硬门禁 + SEAM 专项（附裁 A 输出长度约定
  对 golden 实测钉死）；图级夹具 7 件作回归梯子。

## 改动面

spec §4 表（dfn3denoiser 新增 ~700 行为主体；engineregistry/data.h/modelmanager/ffextractor/
mainwidget/engine_test/CMake/fetch-deps/fr24+CLAUDE.md 增量）。零触碰：commandlayer 信封校验序、
dspdenoiser/pitchshifter/vocalseparator、workspace、FR-8 下载主链（仅追加解包段）、旧 7 薄壳、
fr9/fr10 编排器、旧 aiDenoise 全部行为。

## 验证

- FR4 族（run-fr4.sh + run_parity.sh PAR-DF）：PAR（整段 −60dB/SEAM/FEAT 图级梯子/SR bit-exact）、
  OK/NEG（命令面含 E_MODEL 目录缺文件）、EQV（旧 aiDenoise 全族零漂移 + GUI-EQV 逐字）、REG
  （pipeline/batch 自动入链）、MDL（下载幂等/目录形态/导入/modelMissing 引导）、GUI（档位三通路
  + 档位=快速零漂移）、RES（RTF ≤0.5 可用性线 + 2h RSS ≤2GB + 登记基线）。
- 零漂移回归网全量复跑（run-json/run-ws/run-models/run-pipe/run-batch/gui-slot-eqv/thinshell-eqv）
  + 构建零警告 + ci-smoke 全绿（串行、磁盘卷 TMPDIR）。
- Windows round-4 挂账同前六包惯例。

## 研究档勘误记档（附裁 F；round-1 再校准）

`docs/reference/research-deepfilternet3.md` 两处与实测（2026-11-04）不符：①「npz 夹具随包产出可
直接当 golden」（:84/:214）——官方 tar 仅 4 文件，npz 是 export.py 导出期 torch.randn 现场生成从不
入库；②golden 语境的「CLI Releases 预编译二进制免 Rust 工具链」（:76）——Releases 无 DeepFilterNet3
模型 tar 对应发布物（**对模型 tar 成立；对 CLI 二进制不成立**）。reference 档不回改，以本条为准。

round-1 再校准（实现轮实测）：Releases **有** deep-filter CLI 预编译产物（v0.5.1 musl，
sha256 ea9c8ce4…，`fetch-deps.sh` 钉死备齐）——金样本主链据此从「cargo 构建」（附裁 B 原案）改用
Releases 预编译：免 Rust 工具链依赖且出处可 byte 级钉死；cargo 路径保留 runbook 备选（本地克隆
f1d19bff 在位可考）。另实证：官方 CLI 单次调用多文件**不重置模型状态**——golden 生成必须一文件
一进程（`gen_golden.py dfn3` 固化，round-1.md §5）。
