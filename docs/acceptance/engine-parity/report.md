# WP-1.1 engine-parity 收官报告（NFR-Q1 引擎侧落地）

- 周期：2026-10-18（round-1 基建+DSP/变调）→ 2026-10-30（round-2 MDX 首轮对照）→ 2026-11-02（round-3 GTCRN/MDX 双收敛 + 独立验收）→ 2026-11-02（round-4 并入 ci-smoke）
- 目标回顾：五引擎对照官方/独立参考实现，−60dB RMS 判据（NFR-Q1），进 CI 门禁后保持全绿。

## 最终状态（matrix.csv 全 PASS/VOID）

| 族 | 判据 | 最终数字 | 轮次 |
|---|---|---|---|
| PAR-DS（DSP vs scipy） | −60dB；hum50 分级 vs float32 同构参考 −45dB | −71.8 / −75.6 / −75.2；hum50 −50.9 | round-1 |
| PAR-GT（GTCRN vs sherpa-onnx） | −60dB（近静音小分母改残差/RMS输入） | 主用例 −123.9（输入口径 −156.0）；边界 −88.9~−120.8 | round-3 |
| PAR-MD（MDX vs audio-separator） | −60dB（同工作点判据 round-3） | 8/8 机器精度 −107.6~−145.4，a=1.0000 | round-3 |
| PAR-PT（变调性质） | 谱判据（无独立参考） | 0.00 音分 / 59.0dB 杂散抑制 / RMS +0.00dB | round-1（round-4 自动化） |
| 分块不变性 / FFT-PR | 逐字节 / WP-1.3 五项套 | 五引擎全逐字节；FFT 全绿 | WP-1.2/1.3（交叉引用） |

## 证据链（判别实验驱动，先论后证）

1. **round-1**：DSP 全绿（hum50 float32 量化归因：同构参考同崩、理论上限 −26.7dB → 分级判据）；GTCRN 两债实锤（帧网格半窗偏移、首尾窗分母）；变调谱判据决策（WSOLA 往返相位漂移非缺陷）。
2. **round-2**：audio-separator 0.47.0 落地（py3.14 绕 diffq 配方）；MDX 首轮全 FAIL → 幅度约定两实锤（normalize 工作点 / ×peak 混合域）。
3. **round-3**：三重归因闭合 → 判据同工作点（金样本旁路+代数域重建，自洽 3e-8）→ 引擎块级化复刻官方 demix 分块（PAR-MD −5dB→机器精度）；GTCRN 参考语义锁定（离线复刻 −153.0dB float64 机器精度）→ 两债修复（反射帧网格 + WOLA÷Σw² + 反射尾帧；债②机理修正：主因缺反射尾帧而非分母欠补偿）→ 边界组全过；对抗式审查工作流 11 代理 0 confirmed；**独立验收子代理全绿 18/18**（含 120s RSS 1.48GiB，NFR-P4）。
4. **round-4**：自动化收口——run_parity.sh 四族编排 + 金样本哈希复用/反漂移 + pitch_check 固化 + ci-smoke/CI workflow 接入（PARITY_REQUIRED=1 强制点）+ 依赖版本矩阵；正例全绿 + 负例三连（必判 FAIL / 裸机降级 / 篡改重生成）。

## 机制沉淀

- 判据即契约：对等比较须同工作点（参考实现的幅度策略是产品语义不是引擎语义，旁路+域重建剥离）；输出长度约定以参考为准（GTCRN (N//hop)·hop）。
- 引擎实现语义锚点：torch.stft/istft 的 center+reflect+WOLA÷Σw²；MDX 官方 demix 分块（C/step/trim/块级 hanning OLA/divider）。
- CI 强制点：`PARITY_REQUIRED=1`（github linux-engine job）；本地缺依赖降级 SKIP 不误红。换参考版本 → `gen_golden.py --force`。

## 遗留（移交后续工作包/用户决策）

- CI 真机首跑回填（push 后）；Windows round-4 真机复验（既有闸门，含本轮引擎改动）。
- 真实语音素材（许可核定，PAR-GT 终判升级）；48k 源跨采样率用例（canonical 48k 决策 NFR-Q3 后）。
- GTCRN 输出等长补尾的产品决策（FFTranscoder 层，FR-24/实时化前拍板）。
