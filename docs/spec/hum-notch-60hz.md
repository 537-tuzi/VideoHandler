# hum 陷波 50/60Hz 参数化

- 日期：2026-10-13
- 关联：P0 债 #5、FR-3（50/60Hz hum 参数化）

## 1. 现状与证据

- `dspdenoiser.cpp:39-40`：`makeNotch(50.0 * h, 30.0)`，h=1..3——基频 50Hz 硬编码，60Hz 电网地区（美日等）完全无效。
- 系数计算本身已是采样率感知（`dspdenoiser.cpp:15-25`），只有基频是常量；GUI 与 CLI 均无入口。

## 2. 设计

- `DspDenoiser::setup` 增加 hum 基频参数：`enum class HumBase { Off, Hz50, Hz60 }`（或 int 0/50/60，跟随现有风格定），替代现有 `removeHum` 布尔——`Off` 兼容原 false 语义，避免参数堆叠（工作原则 5：一条清晰路径）。
- 高通支路不动（`removeWind` 语义无关电网）。
- GUI：降噪区加一档选择（默认 50Hz，记住上次选择）；CLI `--denoise-wav` 加可选 `[50|60]` 参数，缺省 50（向后兼容）。
- 不做自动检测（过零点/频谱峰推断电网制式）：M1 保持最小改动，有真实需求再立项。

## 3. 验证（挂 engine-parity matrix）

- PAR-DS-003：60Hz profile——合成「60Hz+谐波+噪声」素材，陷波后 60/120/180Hz 处衰减 ≥ 20dB（对照 scipy 双精度参考实现计算实际频响），50Hz 信号不受额外损伤。
- PAR-DS-001/002 复跑：50Hz 路径行为不变（回归）。
