# WP-1.4 round-1：分离器查表取参 + plane-major + 输出契约 + RSS（2026-10-18）

环境：Linux（本仓库 linux-test preset 构建 `build-linux/`），真模型 UVR-MDX-NET-Voc_FT.onnx（66,776,249 字节，末 10000KiB MD5 `77d07b2667ddf05b9e3175941b4454a0`）。素材 `/tmp/wp14/st.f32`＝5s 440Hz 正弦 44.1k 立体声（ffmpeg lavfi `sine` 默认幅度，peak 0.0884）。

## 实现内容（对 spec `separator-output-validation.md` §2 全条目）

1. **查表取参**：`kMdxModelData` 内嵌 TRvlvr `model_data_new.json` 的 MDX 子集 44 条（MIT）；`tailMd5()`＝文件末 **10000 KiB**（audio-separator `BYTES_TO_HASH = 10000 * 1024`，≈9.77MiB，非严格 10MiB）的 MD5；查不到→「不认识的模型」拒绝。`nFft=dimF*2` 推导废除。
2. **plane-major**：`analyzeFrame` 按 `((plane·dimF)+bin)·dimT+slot` 写入、`ifftChannel` 同布局读出；前 3 频点置零（对齐 `mdx_separator.py:438`）。
3. **输出契约**：`runModel()` 断言输出个数/名字/dtype==float/rank4==[1,4,dimF,dimT]，违例→中文 errMsg（带实际值）→失败路径无残留。
4. **compensate/primary 语义**：副 stem＝`mix − compensate×主 stem`；`wantSub() = (instrumental == primaryVocals)`。
5. **AiDenoiser 补强**：`n_fft==window_length` 断言；输入/输出槽位按名字映射（`conv_cache`↔`conv_cache_out` 等，谱/增强槽为剩余者）。
6. **RSS**：`opts.DisableCpuMemArena()`（round-3 实锤 arena 稳态驻留为 2.6GB 主因）。

## 根因记录：wantSub 公式取反（本轮最重要发现）

初版写成 `instrumental != primaryVocals`，真值表反了：sep-voc 走了相减、sep-inst 走了直出。诊断路径值得留档——纯音被 MDX 掩模到 −74dB（直出≈0），于是「voc≈mix 增益 1.00003、inst≈−73.9dB」，表象是「compensate 失效（实测 1.0 而非 1.021）」。探针证明 setup 解析 1.021 无误后，直接对比 `|inst−voc|` 与两 stem 角色才定位：compensate 从头到尾在生效，只是作用侧反了。修复为 `==` 后：

```
mix=-24.08 dB  voc(直出人声)=-73.91 dB  inst(mix−c×voc)=-24.08 dB
compensate 实测 = 中位 1.02100  均值 1.02100  std 1.05e-06
若 c=1.0 相对残差 = -33.6 dB   ← 反证：系数确为 1.021 而非 1.0
```

## PASS 证据（命令→输出摘录）

- **PAR-MD-001/003/004**（上节输出；`engine_test sep-voc|sep-inst st.f32 out 1000000000 模型`）
- **PAR-MD-006 分块不变性**：整块 vs 4096 vs 977，两动词各 3 组，python 逐样本比较：`最大 |d| 0.00e+00`（相对差 −inf dB）。
- **PAR-MD-008 RSS**（`/usr/bin/time -v … 120s 素材 …/long120.f32`， DisableCpuMemArena 后）：
  - sep-voc：`Maximum resident set size: 1,553,560 kB`（≈1.48GB）
  - sep-inst：`1,551,424 kB`（≈1.48GB）
  - 对照 round-3 基线（arena 开）：120s＝2.58GB → **降 1.1GB，过 NFR-P4 ≤2GB 门**。压线实验②（dimT 分块）不需要。
- **PAR-MD-009~011**（手术模型，`scripts/make_bad_model.py make` 产出 bad-layout/bad-dtype，未注入时）：
  ```
  FAIL: 不认识的模型：末 10000KiB MD5=b3867157… 不在内置 UVR 参数表…  exit=1  无残留
  ```
  注入哈希重编译后（`make_bad_model.py inject`，测毕 `restore`）：
  ```
  FAIL: 模型输出布局不符：需要 [1,4,3072,256] plane-major，实际 [1,4,256,3072]  exit=1  无残留
  FAIL: 模型输出 dtype 非 float32（type=11）  exit=1  无残留
  ```
- **PAR-MD-012/013**（GTCRN 手术：产出节点+ValueInfo 同步改名 / metadata n_fft→999）：
  ```
  FAIL: 模型输入/输出名与 GTCRN 约定不符（缓存张量应名 conv_cache/_out）  exit=1  无残留
  FAIL: 模型规格不支持：n_fft=999 与 window_length=512 不等  exit=1  无残留
  ```
- **PAR-MD-014**：注入构建与还原构建对同输入 sep-voc 输出 `cmp` 逐字节一致；还原后表 JSON 44 条合法。
- **REG-001**：`docs/acceptance/wp-1.2-linux-build/scripts/ci-smoke.sh` → `== ci-smoke 全绿 ==`（FFT-PR + dsp/ai/pitch/sep-voc/sep-inst 分块不变性 + CLI 7 动词 + ffprobe 体检）。
- 另：GTCRN 喂 sep（合法 ONNX 形状不符）→ `FAIL: 模型输入规格不符，需要 [1,4,dimF,dimT] 的 MDX-Net 模型` exit=1 无残留。

## 过程坑（供复现者）

1. **「10MiB」是行文近似**：官方哈希范围实为末 10000KiB（10,240,000 字节）。脚本最初按 10MiB 算 → 手术模型哈希与 C++ 运行时报值对不上；用真模型两范围独立算 MD5 判定（10000KiB 命中官方表）。
2. **JSON 表不能插 C 注释**：临时注入条目首版带 `/*…*/` 标记 → `QJsonDocument::fromJson` 整表解析失败 → 全部模型（含真的）查表 miss。改用冗余键值对做标记。
3. 提取嵌入 JSON 验证合法性时，切片起点应含 `{`（`index('R"mdx({')+6` 而非 +7），否则首 key 被当独立值报 Extra data（文件本身没坏）。

## 盲点声明（未收敛项，随 WP-1.1/round-4 处理）

- **数值 parity 未做**：输出只证了「自身分块不变 + 契约自洽 + compensate 代数关系」，与 audio-separator 官方输出的 −60dB 对照是 WP-1.1 金样本范围（含前 3 频点置零、反射填充边界差异）。
- **Windows MinGW 未编译未跑**：本轮全部证据为 Linux；Windows 随 wp-1.0 round-4 一并（`_GLIBCXX_ASSERTIONS` 缺失时越界为静默破坏，更需要真机回归）。
- AiDenoiser「名字映射」负例只测了 cache 输出改名；mix/enh 槽位扫描分支无独立负例（GTCRN 正路名字即约定名，回归绿覆盖恒等情形）。
- 非 V/I primary_stem 模型（Bass/Drums/Other/Reverb/No Crowd）：行为=主 stem 直出 / mix−c×主 stem，「人声/伴奏」标签仅对 V/I 主 stem 严格成立；未用真模型实测（手头只有 Voc_FT）。
