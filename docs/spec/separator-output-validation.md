# 分离器输出契约校验与引擎 I/O 断言补强

- 日期：2026-10-13；2026-10-14 依 `docs/reference/research-mdx-uvr-audio-separator.md` 实测证据修订（原「待实测确认」项已关闭，且发现两处实现 bug）
- 关联：P0 债 #3、NFR-R4（坏模型不产废音）

## 1. 现状与证据（含 2026-10-14 实测新证据）

- `vocalseparator.cpp:198-205` 只验输入 shape（rank=4、shape[1]==4）；**输出零校验**：`out[0].GetTensorData<float>()`（`vocalseparator.cpp:148`）不查 dtype、不查 shape、不查布局。
- **【实测证据·bug 其一·布局读反】** 对 Voc_FT 模型文件用 onnx 实测图签名：输入名 `input`、输出名 `output`，均为 `[batch,4,3072,256]`，**plane-major**（4 个平面各 dimF×dimT 连续，元素 (p,f,t) 在 `(p·dimF+f)·dimT+t`）。audio-separator 源码 `mdx_separator.py:460` 把输出原样喂回 `stft.inverse` 佐证输出与输入同布局。**我们按「每帧 dimF×4 连续」（frame-major，`vocalseparator.cpp:149`）读输出是布局错误——当前实现对 Voc_FT 产出必然是废音。**
- **【实测证据·bug 其二·n_fft 取错】** MDX 模型 ONNX 无 metadata（metadata_props 为空）；audio-separator 的取参机制 = **模型文件末尾 10MiB 的 MD5** 查 UVR 官方参数表（`separator.py:526/537-541`）。按此算法 Voc_FT 哈希 `77d07b26…` 命中官方表：**n_fft=7680**（非我们 dimF×2=6144）、dim_f=3072、dim_t=2^8=256（静态）、primary_stem=Vocals、**compensate=1.021**。谱 bin 宽 5.86Hz、dim_f 覆盖至 17.94kHz（丢 769 个高频 bin）。参数必须逐模型查表（生态确有 n_fft=6144 的其他模型）。
- 附带差异（parity 对齐项，见对照清单）：官方推理前 `spek[:, :, :3, :] *= 0`（前 3 个频点置零，`mdx_separator.py:438`）；伴奏支路 = mix − **1.021**×vocals（compensate 系数，我们缺）；分块 demix 的 chunk=hop×(dim_t−1)、两端 n_fft//2 零填充、step=0.75×chunk（我们为整流反射填充，边界行为不同，见 parity 计划 §5）。
- 同类隐患：`aidenoiser.cpp:95` `fft_forward(fftIn, imBuf, nFft)` 但缓冲按 winLen 分配——隐含假设 nFft==winLen，GTCRN 恰好成立（512/512），其他模型会越界；两引擎取输出都按索引 `out[0]`，不按名字。

## 2. 设计

**模型参数表（前置新增）**：内置 UVR 官方参数表（模型哈希 → n_fft/dim_f/dim_t/compensate/primary_stem，MIT 数据），setup 时按「末尾 10MiB MD5」查表取参；查不到 → 明确报「不认识的模型」拒绝运行（NFR-R4）。`nFft = dimF*2` 的推导废除。**2026-10-14 反证补强（`falsify-mdx.md`）**：① 断言与缓冲一律用查表值——TRvlvr 表存在 dim_t=128/512 条目（**非全系 256**），n_fft∈{4096..16384}、compensate∈[1.019,1.75] 逐模型；② UVR5 GUI 与 audio-separator 共用同一张表（命中不构成独立交叉；另有 UVR 5.4 硬编码 7680/3072 与 kuielab configs 佐证，结论不变）；③ parity 参考参数集钉死 **audio-separator 默认**（overlap 0.25 / segment 256 / batch 1 / denoise off；UVR5 GUI 默认 `step=chunk−n_fft` 与之不同，不采）。

**输出契约（VocalSeparator）**：`runModel()` 内 `Run` 返回后立即断言：

1. 输出个数 ≥1；按**名字**（`output`，setup 时记录，不靠位置）选中目标输出。
2. dtype == float（`GetTensorTypeAndShapeInfo`）。
3. rank=4 且 dims == [batch,4,dimF,dimT]（与输入同构、plane-major）。
4. 任一不满足 → 置 `errMsg`（中文、带实际 shape）、走失败路径（FFTranscoder 已有失败删半成品机制，`fftranscoder.cpp:286-290` 兜底）。

**读谱改 plane-major**：`synthesizeFrame` 从输出张量按 `(p·dimF+f)·dimT+t` 步进取帧 t 的四个平面（或一次转置到帧连续缓冲再走现有 WOLA，以实测性能定，不预设）。

**前端对齐**：分析谱写入前执行前 3 频点置零（对齐 `mdx_separator.py:438`）；伴奏模式引入 compensate 系数（查表所得）。

**引擎 I/O 断言补强（AiDenoiser）**：

- setup 末尾断言 `n_fft == window_length`，不等则报「模型规格不支持」（当前实现隐式依赖相等）。
- 输出按名字取（GTCRN 输出 4 个：增强谱 + 3 状态，`aidenoiser.cpp:84-86` 现按位置拿 `out[k≥1]`，改按名字映射，防导出顺序变化）。

**负例测试**：PAR-MD-009——用一个 shape/dtype/布局不符的假模型（小脚本改 ONNX 图输出或直接构造错误签名 mock）喂 setup/process，断言失败路径返回错误信息且不写输出文件。

## 3. 边界与不做

- 不做「任意模型自动适配」（过度设计）：只支持契约相符的 MDX-Net，不符就明确报错引导换模型（NFR-R4 精神：不崩溃、不静默）。
- `kHop=1024` 硬编码（`vocalseparator.cpp:12`）维持现状：UVR 全系恒定，无元数据可读；在代码注释里已说明，不加配置项。（2026-10-14 反证坐实：kuielab 侧证明 MDX-Net 全系 hop=1024；bs_roformer hop=441 属另一架构，不在本引擎范围。）
