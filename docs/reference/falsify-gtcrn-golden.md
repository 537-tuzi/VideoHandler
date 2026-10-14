# GTCRN golden 结论反证审查

- 性质：**反证审查**（假设既有结论是错的，专找推翻证据；非复查性查证）
- 日期：2026-10-14
- 审查对象：
  - `docs/reference/research-gtcrn-sherpa-onnx.md`（2026-10-14）的主要结论
  - `docs/acceptance/engine-parity/goal.md` 决策 D-06 及「重要信息」节的对齐约定
- 信源（全部一手，2026-10-14 当日获取）：
  - sherpa-onnx master 源码快照 `/tmp/sherpa-onnx-src`（k2-fsa/sherpa-onnx，GitHub tarball；`CMakeLists.txt:31` 版本 1.13.8）
  - kaldi-native-fbank **v1.22.3** 源码（GitHub tag tarball，即 sherpa `cmake/kaldi-native-fbank.cmake:4` 锁定版）
  - 上游 GTCRN 仓库文件（Xiaobin-Rong/gtcrn：`model.py`、`stream/gtcrn_stream.py` 等，`/tmp/gtcrn_*.py`）
  - GitHub API：releases 列表、逐文件 commit 历史、issues 全文检索（gtcrn 共 96 条命中、denoiser 5 条、gtcrn in:title 18 条）
  - PyPI 官方 JSON 索引（当日新拉取）；arXiv 官方 abs 页与既有的 arXiv API 检索结果
- 判级口径：【站得住】= 反证失败且原证据复核成立；【需修正】= 结论方向成立但表述/范围需改；【被推翻】/【原证据不足】按字义。未找到反证处只写「暂未找到反证」。

---

## 反证 1：sherpa online 路径的分帧/对齐/滞后/flush 约定

**原结论**（research §2「流式分帧/延迟约定」+ goal.md「重要信息」）：online 逐帧推理（含前置补零第 0 帧）、输出与输入样本级对齐、滞后恰 1 hop=16ms、flush = 补零成整 hop + 一个全零 hop + 按总输入长截断。

**反证尝试与证据**（`sherpa-onnx/csrc/online-speech-denoiser-stft-impl.h`，全部当日逐行复核）：

1. 「凑满 hop 才处理」：`ProcessPending`（265–281 行）`while (pending - consumed >= hop_length)` → `ProcessHop`。**暂未找到反证。**
2. 「滑窗右对齐、等效 center=True、首帧前置 256 零」：`ProcessHop`（286–289 行）左移一 hop、新 hop 补尾部；`analysis_buffer_` 初始全零（构造函数 129 行 `std::vector<float>(n)` 值初始化）。推演：第 t 次 ProcessHop 的窗覆盖输入 `[t·hop−256, t·hop+256)`，窗中心对齐样本 `t·hop`，与 `torch.stft(center=True)` 帧 t 逐点相同。**暂未找到反证。**
3. 「模型每帧都推理、仅首 hop 不发射」（滞后 vs 吞帧的源码级判据）：`ProcessHop` 中 STFT→`process_frame`（模型推理）→iSTFT→WOLA 累加（295–307 行）**无条件执行**；`if (!started_) { started_ = true; return; }`（309–312 行）只拦掉输出发射（314–315 行）。判据：**`started_` 只 gate 发射，不 gate 推理与状态回填**。
   - 补充澄清 research/goal 的表述「这两者外部观测可能相同」：实际上**外部可观测不同**——若某实现少推理第 0 帧，conv/tra/inter 三缓存的状态递推从第 1 帧起整体错位一帧，且首 hop 的 WOLA 少一半帧贡献，后续**所有**输出都会不同。两者只在「喂 1 hop 时都出 0 样本」这一退化情形下观测相同。
   - 顺手闭环 SG4 首查项：我们的 `src/service/aidenoiser.cpp:90-124` 与 sherpa 同构——模型调用（104 行 `runFrame`）与 WOLA 累加（116–119 行）都在 `!started` 判断（120–123 行）**之前**，注释「首帧吞掉一个 hop」实义为「延迟发射首 hop」，非「少推理一帧」。**两侧语义一致。**
4. 「滞后恰 1 hop、样本级对齐」：第 t 次（t≥1）ProcessHop 发射的 `overlap_add_buffer_[0:256)` = 帧(t−1) 后半 + 帧 t 前半的 WOLA，对应输入区间 `[t·hop−256, t·hop)`。喂 T 个 hop 累计输出 `(T−1)·hop`。**暂未找到反证。**旁证：上游 `model.py` 自带 causality check 用 `y1[:16000-256*2]` 截断（作者按 n_fft 保守取 512），数学前瞻恰 1 hop（center=True 时样本 i 依赖最晚中心在 `i+256` 处的帧）——与 sherpa 的 1 hop 滞后互为印证。
5. 「flush 语义」：`Flush`（183–223 行）逐段复核——重采样器 flush 追加 tail 并计入 `total_input_samples_`（189–195）→ `ProcessPending` 处理剩余整 hop（197）→ 余量补零成整 hop 再跑一次（199–204，余量恰为 0 时跳过）→ `started_` 则再补全零 hop（206–208）→ 按 `total_input_samples_ − total_output_samples_` 截断（210–217，`total_input_samples_` 计的是**重采样后**样本，172 行）→ `Reset()` + 模型状态清零（220–221；`online-speech-denoiser-gtcrn-impl.h:49` 回填 `GetInitStates()` 全零）。**暂未找到反证。**
6. 「1.13.8 之后行为无变化」（锁版本策略安全性）：
   - 逐文件 commit 历史（GitHub API）：`online-speech-denoiser-stft-impl.h`、`online-speech-denoiser-gtcrn-impl.h` 自 2026-03-17 #3324 加入后**零改动**；CLI `sherpa-onnx-online-denoiser.cc` 最后改动 2026-08-10（#3824，DPDFNet 相关）与 2026-04-08（session 初始化），均早于 v1.13.8 发布（2026-09-10）；`resample.cc` 最后行为改动在 2023 年。
   - CHANGELOG 1.13.8 一节无 denoiser/gtcrn 条目；master 至 2026-10-14 无 1.13.9。
   - issues 检索（见反证 2 附带）：无任何 GTCRN/denoiser 行为 bug 报告或修复。
   - **暂未找到反证**：锁 1.13.8 不会因「锁的版本之后修了 bug」而失效。

**判级：【站得住】**（全部子项源码级复核成立；附带把「外部观测可能相同」的表述纠正为「可观测不同」，并把 SG4 首查项直接闭环）。

---

## 反证 2：「sherpa-onnx 1.13.8 这个版本存在」

**反证证据**（三独立信源，全部当日重拉，非沿用 research 旧档）：

- PyPI 官方索引 `https://pypi.org/pypi/sherpa-onnx/json`：`info.version == "1.13.8"`（当日实时拉取）。
- GitHub releases：tag `v1.13.8`，published 2026-09-10T14:00:23Z，非 prerelease；其后仅有 `tauri`/`flutter`/`xcframework`/模型类 tag，无更新版本号。
- master 快照 `CMakeLists.txt:31` `set(SHERPA_ONNX_VERSION "1.13.8")`，`CHANGELOG.md` 首节 `## 1.13.8`。

**判级：【站得住】**（版本号记忆无误，依赖锁定写法正确；且 1.13.8 即当前最新，无「锁旧版」问题）。

附带：issues 全文检索 `repo:k2-fsa/sherpa-onnx gtcrn` 共 96 条命中，除去 Release PR 外的真实 issue 仅 3 条问答（#2289 iOS 流式、#2600 Android 集成、#2670 speech enhancement 提问），`denoiser in:title` 5 条全是功能 PR——**不存在已知的 online denoiser 行为 bug 或其修复**。

---

## 反证 3：「online 谱变换用 double 直算 DFT（而非 float FFT）」

**反证证据**（`online-speech-denoiser-stft-impl.h:50-117`，逐行）：

- 旋转因子表 `cos_f_/sin_f_/cos_i_/sin_i_` 均为 `std::vector<double>`（113–116 行），构造时 `double angle`、`std::cos/std::sin`（59–72 行）。
- `Forward`（75–89 行）：每 bin 一趟 O(N) 乘加，累加器 `double real/imag`（77–78 行）——**直接 DFT，非 FFT**；`Inverse`（91–108 行）同构，用实信号共轭对称省半计算（Nyquist 项 `(n & 1) ? -1 : 1`）。
- 对照：offline 路径走 knf `Rfft`（kissfft 后端，float）——两路径数值实现确实不同。

**需精确化的一处**（不构成推翻）：sherpa online 并非全 double 流水线——输入 `fft_input_`/输出均为 float，**窗乘在 float 域**（291–293 行 `analysis_buffer_[i] * window_[i]`），DFT 内部才升 double、出口 cast float（86–87、106 行）。故参考侧自带 ~1e-7 量级的端点舍入；我们 float 基-2 FFT 的核心误差同为 ~1e-7·log₂(512) 量级。对 −60dB（1e-3 相对）判据余量仍约 4 个数量级，「我们 float FFT 是主要数值变数」的判断**维持成立**。

**判级：【站得住】**（补充精确化：参考实现是「float 端点 + double 核直算 DFT」）。

---

## 反证 4：「GTCRN 论文官方实现与 sherpa 行为一致（online 无 lookahead、因果）」

**反证尝试与证据**：

1. 论文编号质疑：任务线索给出的 arXiv 2403.17012 经 abs 页核实为 *Evolution and Efficiency in Neural Architecture Search*（NAS 论文），**与 GTCRN 无关**。arXiv API 全文检索 "GTCRN" 仍只有 4 篇引用/衍生论文（H-GTCRN 2505.19597 等）——research「无 arXiv 预印本」未被推翻，反获佐证。论文正文依旧只能经 IEEE（付费墙），**论文级 lookahead 表述维持【原证据不足】**（与 research 的【未确认】标注一致，非新发现）。
2. 官方实现的因果性（源码级，一手）：`model.py`（上游仓库）`__main__` 段自带 causality check——`x1=cat([a,b])` vs `x2=cat([a,c])`（各 16000 样本），打印 `y1[:16000-256*2]` 与 `y2[:16000-256*2]` 差的最大值（应为 0）。这是作者自己证明「前段输出不依赖未来段」的代码。**暂未找到反证。**
3. 帧对齐约定：上游离线/流式/导出全部用 `torch.stft(x, 512, 256, 512, torch.hann_window(512).pow(0.5))`（`stream/gtcrn_stream.py:371`、`model.py` causality check、`infer.py`）——center=True 默认，与 sherpa online 滑窗逐点等效（反证 1 第 2 条已推演）。**暂未找到反证。**
4. sherpa 分发模型与官方同源：`scripts/gtcrn/run.sh` 第 5–7 行直接 `wget https://github.com/Xiaobin-Rong/gtcrn/raw/refs/heads/main/stream/onnx_models/gtcrn_simple.onnx`；`add_meta_data.py` 只注入/替换 metadata（先清空再写，不改权重）。上游该文件的 commit 历史：最后改动 **2024-03-25**（"Add files via upload"），此后未变——不存在「上游后来重导出、sherpa 拿到不同权重」的时间窗问题。**暂未找到反证。**
5. **顺带发现（上游 demo 自身瑕疵，不推翻结论）**：上游 `stream/gtcrn_stream.py` ONNX demo 的重建端用 `librosa.istft(..., window=np.hanning(512)**0.5)`（456 行）——`np.hanning` 是 **symmetric** hann，而其分析端 `torch.hann_window(512)` 是 **periodic**。上游 demo 分析/重建窗不一致（靠 librosa 内部归一化掩盖，误差小但非零）。sherpa 取 periodic √hann + COLA 是自洽且与分析端一致的正确做法。含义：**勿将上游 demo 的输出当参考值**（我们已锁 sherpa online，无实际影响）。

**判级：【站得住】**（实现层面：官方源码因果 + 帧对齐一致 + 模型同源闭环；论文 PDF 未直读的部分维持 research 原有的「未确认」标注，不升级也不降级）。

---

## 反证 5：「offline 与 online 边界行为不同、禁作 golden（D-06）」

**原结论**：offline 走 knf（center=true、reflect 填充、窗²归一化），online 隐式零填充 + COLA 无分母，两者「在首尾 n_fft/2 内不严格一致」→ golden 只用 online。

**反证方向**：若两路径在**中段**（远离边界）完全一致，D-06 可放宽（golden 互替）；若差异更深，D-06 要收紧。

**反证找到的东西——差异比 research 描述的更深，需收紧**：

1. 已核实的基础事实（knf v1.22.3 源码）：
   - `stft.h:34-35` 默认 `center=true, pad_mode="reflect"`，sherpa offline impl 未改这两项（`offline-speech-denoiser-gtcrn-impl.h:60-74`）；`stft.cc:108-127` `Pad()` 做 reflect（两侧各 n_fft/2）。
   - `istft.cc:54-60` + `116-142`：输出除以**窗平方和分母**；`istft.cc:62-65` center 截除两端 n_fft/2。
   - offline FFT 为 knf `Rfft`（kissfft，float），与 online double 直算 DFT 不同。
2. **中段一致性拆解**（关键）：
   - 中段帧（第 1..T−1 帧）两路径的**输入谱逐点相同**（同样的输入样本 × periodic √hann；offline 帧数 = 1 + n/hop，与 online「T 个 hop → 帧 0..T−1 + flush 全零帧 = T+1 帧」帧数一致、帧中心一致）。
   - 但 offline 帧 0 的窗前半是 `reflect(x[1..257])`，online 是**零**；末帧（帧 T）的后半同理。这两侧共约 n_fft 个样本的谱差异进入**同一个因果模型的状态递推**（conv/tra/inter cache），而 GTCRN 的 GRU/深度卷积状态会把帧 0 的差异**传播到全部后续帧**的 mask——影响幅度未知（经验上衰减，但无解析界）。
   - 且两路径的数值实现全线不同（kissfft float + 分母除法 vs double DFT + 无分母；中段分母 = hann 的 COLA 和，数学恒 1、浮点 ≈1±1e-7）。
   - **结论：中段「帧输入谱相同」≠「输出相同」。offline 与 online 的差异不是只落在首尾 n_fft/2 输出样本内，而是经状态递推 + 数值路径差异作用于整条输出。**
3. 因此：
   - 「中段完全一致 → D-06 可放宽」的反证路径**失败**：不存在可互替的段。
   - D-06 的决策（golden 一律 online）**维持**，但 research/goal 中「在首尾 n_fft/2 内也不严格一致」的表述**低估差异范围**，应收紧为：边界帧谱差异（reflect vs 零填充）经因果状态递推传播全线 + 两路径全程数值实现不同，offline 输出与 online **无任何逐段 bit-exact 保证**；offline 至多作容差 sanity check（独立容差，不可挂 −60dB 同一判据）。

**判级：【需修正】**——D-06 决策方向正确且不可放宽；但「差异仅在首尾 n_fft/2 内」的差异范围描述被反证修正为「全线受影响」，D-06 的说明文字需收紧。

---

## 顺带发现的新风险（不属五条反证，建议登记）

1. **golden WAV 是 int16 量化**：sherpa CLI 固定写 16-bit PCM（`wave-writer.cc:88` `bits_per_sample = 16`，101–104 行 float→int16 舍入）。量化噪声 RMS ≈ (1/32768)/√12 ≈ 8.8e-6（约 −81dB 满幅），对 −60dB 判据占约 21dB 余量中的小头，但 parity 计划应显式记录「golden 含 int16 量化」，DUT 比较用读回值（误差进入 dut−ref 差值）。
2. **sherpa ReadWave 的输入格式边界**：支持 8/16/32-bit int 与 float32（format=3）；**不支持 24-bit、WAVE_FORMAT_EXTENSIBLE**（`wave-reader.cc:131-137, 183-191`，subchunk1_size 仅接受 16/18）。gen_inputs 产 WAV 须避开这两类；16-bit 输入时 DUT 读入换算须与 sherpa 一致（÷32768）。
3. **CLI `--chunk-duration-ms` 默认值是 10 不是 16**（`sherpa-onnx-online-denoiser.cc:70`；usage 示例才写 16）。16k 输入无重采样时分块大小不影响输出（ProcessPending 按 hop 聚合，帧序列与块边界无关），但 golden 命令应**显式固定**该参数以免复现歧义。
4. **`speech-enhancement-models` 是 rolling release tag**：`add_meta_data.py` 打包时先清空模型原有 metadata 再写入（`while len(model.metadata_props): pop()`），且 release 资产理论上可重传。golden 生成时登记 gtcrn_simple.onnx 的 SHA256（做法已被 D-04 覆盖，此处强调对模型文件同样适用）。
5. research 文档的一处行号笔误：§2 说谱布局出自「`ProcessFrame`，online-impl 84–94 行」，实际 `ProcessFrame` 在 `online-speech-denoiser-gtcrn-impl.h:75-95`（`online-speech-denoiser-impl.h` 里没有该函数）；内容属实，仅指引会误导。

---

## 判级总表

| # | 被反证的结论 | 判级 | 一句话依据 |
|---|---|---|---|
| 1 | online 逐帧推理/样本级对齐/滞后 1 hop/flush 双补零+截断 | 【站得住】 | stft-impl.h 全文复核 + 1.13.8 后零改动 + 无 bug issue；并纠正「滞后 vs 吞帧外部观测相同」的说法（实际可观测不同），SG4 首查项已源码级闭环 |
| 2 | sherpa-onnx 1.13.8 存在 | 【站得住】 | PyPI info.version + GitHub release v1.13.8(2026-09-10) + master CMakeLists/CHANGELOG，当日三源重拉 |
| 3 | online 谱变换 = double 直算 DFT | 【站得住】 | stft-impl.h:50-117 表与累加器均 double、O(N²)；精确化：端点 float、窗乘在 float 域，「float FFT 是主要变数」判断维持 |
| 4 | 官方实现与 sherpa 一致（因果、无 lookahead、同源） | 【站得住】 | 上游 model.py 自带 causality check + 全部 center=True + run.sh 直链上游 onnx（上游该文件 2024-03-25 后未变）；论文 PDF 仍未直读（维持原「未确认」标注）；arXiv 2403.17012 系 NAS 论文，与 GTCRN 无关 |
| 5 | offline 与 online 仅边界不同、禁作 golden（D-06） | 【需修正】 | D-06 决策维持且不可放宽；但差异机制更深：帧 0/末帧谱差（reflect vs 零）经因果状态递推传播**全线** + 数值路径全程不同，「仅首尾 n_fft/2」的表述需收紧 |

## 对既有文档的修订建议

- `goal.md` D-06/重要信息：① 补「offline 差异经状态递推传播全线，任何段不可互替」；② 登记「golden WAV 为 int16 量化」「CLI chunk-duration-ms 显式固定」「模型 SHA256 一并登记」三点；③ SG4 首查项可标注「已源码级闭环（本文反证 1 第 3 条 + aidenoiser.cpp:90-124）」。
- `research-gtcrn-sherpa-onnx.md`：§2 第 7 条（offline 差异范围）与 ProcessFrame 行号笔误按上表修正；§4 可补「1.13.8 后（至 2026-10-14）无 denoiser 相关改动」。
