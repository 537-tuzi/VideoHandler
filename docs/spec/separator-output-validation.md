# 分离器输出契约校验与引擎 I/O 断言补强

- 日期：2026-10-13
- 关联：P0 债 #3、NFR-R4（坏模型不产废音）

## 1. 现状与证据

- `vocalseparator.cpp:198-205` 只验输入 shape（rank=4、shape[1]==4）；**输出零校验**：`out[0].GetTensorData<float>()`（`vocalseparator.cpp:148`）不查 dtype、不查 shape、不查布局，随后按「每帧 dimF*4 连续」假设读谱（`vocalseparator.cpp:149` → `synthesizeFrame`）。
- 换一个输出布局不同的 MDX 模型 → 静默产出废音频（P0 债原话）。同类隐患：
  - `aidenoiser.cpp:95` `fft_forward(fftIn, imBuf, nFft)` 但缓冲按 winLen 分配（`aidenoiser.cpp:167/170`）——隐含假设 nFft==winLen，GTCRN 恰好成立（512/512），元数据驱动的其他模型会越界。
  - 两引擎取输出都按索引 `out[0]`，不按名字。

## 2. 设计

**输出契约（VocalSeparator）**：`runModel()` 内 `Run` 返回后立即断言：

1. 输出个数 ≥1；按**名字**选中目标输出（setup 时记录，不靠位置）。
2. dtype == float（`GetTensorTypeAndShapeInfo`）。
3. rank/dims 与「帧连续布局」假设一致。
4. 任一不满足 → 置 `errMsg`（中文、带实际 shape）、走失败路径（FFTranscoder 已有失败删半成品机制，`fftranscoder.cpp:286-290` 兜底）。

**待实测确认（开工第一步）**：UVR-MDX-NET-Voc_FT 的真实输出 shape。现有读法「每帧 4 平面 × dimF 连续」对应 [1, dimT, 4, dimF] 类布局，而 MDX 导出常见 [1, 4, dimF, dimT]（帧间步进 dimT，不连续）。实现时用 Python ORT 会话 load 模型查输出签名（Linux 可做），以实测为准写断言；**两种布局都可能出现时按 shape 分派索引，不猜**。

**引擎 I/O 断言补强（AiDenoiser）**：

- setup 末尾断言 `n_fft == window_length`，不等则报「模型规格不支持」（当前实现隐式依赖相等）。
- 输出按名字取（GTCRN 输出 4 个：增强谱 + 3 状态，`aidenoiser.cpp:84-86` 现按位置拿 `out[k≥1]`，改按名字映射，防导出顺序变化）。

**负例测试**：PAR-MD-009——用一个 shape/dtype/布局不符的假模型（小脚本改 ONNX 图输出或直接构造错误签名 mock）喂 setup/process，断言失败路径返回错误信息且不写输出文件。

## 3. 边界与不做

- 不做「任意模型自动适配」（过度设计）：只支持契约相符的 MDX-Net，不符就明确报错引导换模型（NFR-R4 精神：不崩溃、不静默）。
- `kHop=1024` 硬编码（`vocalseparator.cpp:12`）维持现状：UVR 全系恒定，无元数据可读；在代码注释里已说明，不加配置项。
