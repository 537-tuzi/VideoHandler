# round-3 — Linux 带真实模型冒烟（2026-10-17）

背景：round-1/2 因 Linux 无模型权重，`--ai-denoise`/`--sep-*` 三动词从未跑过真实推理。本轮按 README 两源下载模型补测（模型不入库，落 `/home/psh/vh-test/models/`）：

- `gtcrn_simple.onnx` 523KB（sherpa-onnx speech-enhancement-models release）
- `UVR-MDX-NET-Voc_FT.onnx` 63.7MB（hf-mirror Politrees/UVR_resources）

模型事实（onnx 库直读）：`input ['batch_size', 4, 3072, 256]`，`output` 同形，**零 metadata、动态 batch**——与 P0#3「须查 UVR 参数表」一致。

## 结果

| 动词 | 结果 | RSS / 耗时（30s 素材） |
|---|---|---|
| `--ai-denoise`（真 GTCRN） | **PASS** OK + ffprobe f32/44.1k/2ch/30s | 83MB / 3.7s（≈8× 实时，达 NFR-P2 ≥5×） |
| `--sep-vocals`（真 UVR） | 首跑 **SIGABRT** → 修复 → 复跑 **PASS**（机制面） | 2.59GB / 37s |
| `--sep-instrumental` | 同上 | 2.59GB / 37s |

输出响度记录：`out-ai` mean −75.5dB（GTCRN 对纯正弦强抑制，属预期；语音内容正确性归 WP-1.1 parity）；`out-voc` mean −62.3dB（非静音废音，与 P0#3 已知结论一致，内容正确性归 WP-1.4，本轮不判）。

## 发现 1（已修）：avail 截断除法假帧 → inQ 越界崩溃

- 症状：gdb 栈 `VocalSeparator::Impl::analyzeFrame` → `std::deque::operator[]` 断言 `__n < size()`，signal 6。
- 根因：`process()`/`flush()` 两处同款公式 `(pEnd - win - nextFrame*kHop)/kHop + 1`——分子 ∈ (−kHop, 0) 时 C++ 向零截断使商为 0 → avail=1 → 对数据不足的帧调 analyzeFrame → inQ 越界。Linux 构建（`_GLIBCXX_ASSERTIONS`）断言崩溃；**Windows MinGW 无该断言时为静默内存破坏**。
- 修复：先判 `reach < 0` 挡截断除法（`vocalseparator.cpp` process/flush 两处，reach 非负后再除）。
- 复验：两动词 OK、格式正确、限时无挂死；5s/120s 素材亦通过（见发现 2 的对照组）。

## 发现 2（未修，归 WP-1.4）：分离路径稳态 RSS ~2.6GB 破 NFR-P4

| 素材 | 峰值 RSS |
|---|---|
| 5s | 1.40GB |
| 30s | 2.59GB |
| 120s | 2.58GB |

随时长**不增长**（30s→120s 持平）→ 非 A1 类缓冲泄漏，是 ORT 侧稳态开销（arena/MDX 激活，我们自己的 tensor 仅 12.6MB）；但 2.6GB 本身 > NFR-P4 红线 2GB。随 WP-1.4 一并处理：首选实验 `SessionOptions::EnableCpuMemArena(false)`，次选 chunk（dimT）分档。

## matrix 对应

`cli-with-models` round-3 = PASS（含崩溃修复复跑）；`sep-rss-nfr-p4` 新行 = FAIL（记录在案，WP-1.4）。
