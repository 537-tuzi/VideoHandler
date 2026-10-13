# parity 测试计划（NFR-Q1 落地）

- 日期：2026-10-13
- 关联：P0 债 #1、NFR-Q1/Q2、NFR-M2；验收骨架 `docs/acceptance/engine-parity/`
- 现状证据：无任何 parity/golden 测试；GTCRN 前端（STFT/WOLA/状态管理 `aidenoiser.cpp:90-125`）与 MDX 前端（反射填充/WOLA/窗²归一 `vocalseparator.cpp:45-186`）均为手推实现，正确性未对照官方实现验证

## 1. 指标定义（精确化 NFR-Q1）

对齐后逐样本比较：

```
parity_dB = 20·log10( RMS(x_dut − x_ref) / max(RMS(x_ref), ε) )
判据：parity_dB ≤ −60 dB（ε 防 silence 除零，取 1e-12）
```

- **样本级对齐**：dut 与 ref 输出必须等长且逐样本对应；长度不等直接判 FAIL（暴露首尾边界 bug，不是截断凑数）。
- 辅助指标（诊断用，不作判据）：max |x_dut − x_ref|、相关系数。
- 全部输出声道参与（立体声两声道都算进 RMS）。

## 2. 被测通路（DUT）与参考实现

| 引擎 | DUT（现有 CLI） | 参考实现 | 备注 |
|---|---|---|---|
| GTCRN AI 降噪 | `--ai-denoise` | sherpa-onnx 同模型 speech-enhancement 推理（Python） | 参考侧需复现我们的通路：swr 到 16k 单声道 → 逐 hop 推理 → 回 44.1k。对齐点定义见 §5 |
| MDX 分离 | `--sep-vocals` / `--sep-instrumental` | audio-separator（Python，UVR-MDX-NET-Voc_FT 同模型） | 官方管线即 44.1k 立体声，通路最短，对齐预期最干净 |
| DSP 降噪 | `--denoise-wav` | scipy：同参数 RBJ 系数（double）+ `scipy.signal.lfilter` | 验证 biquad 实现与双精度参考的差异量级 |
| 变调 | `--pitch-wav` | （SoundTouch 自身即规范） | 无独立参考；用性质测试替代：pitch(+n)∘pitch(−n) 往返近似恒等 + 长度一致 + 分块不变性 |
| FFT | 引擎内部 | numpy.fft（double） | 性质测试：往返恒等、Parseval、线性、冲激响应、随机信号 vs numpy（幅/相误差限） |

参考实现版本在 WP-1.1 执行时锁定（pip 版本号 + wheel SHA256 记入依赖矩阵），避免「今天绿明天红」。

## 3. 执行环境策略（关键决策）

**问题**：DUT（C++/Qt/MinGW）只能在 Windows 构建；本开发环境是 Linux。

- **主线（WP-1.2 落地后）**：CMake 分层出「引擎 + CLI」可移植目标，Linux 上直接构建 headless 测试可执行（Qt6 Core + 系统 FFmpeg + ORT Linux 发行包，见 `ci-build-strategy.md`）。参考实现（Python）与比较器都在 Linux 跑 → **单机全自动**，进 CI。REQUIREMENTS §6「Linux 保持可编译，CI 双跑」本就是约束。
- **过渡（WP-1.2 完成前）**：Windows 机器手跑 CLI 产 DUT WAV 带回 Linux 比较；金样本生成与比较器先行开发。此阶段不追求全自动，只求把用例与脚本磨定。

## 4. 素材与金样本管理（红线：大 binary 不入库）

- 一切音频素材与金样本落 `/test/`（`.gitignore` 已忽略），**入库的只有再生配方**：
  - `scripts/gen_inputs.py`：合成信号全部确定性生成（固定 seed 的 numpy）；真实语音素材用「下载 URL + SHA256」配方（镜像可配，对齐 NFR-C4 思路，素材选许可宽松的公开语音样本，实现时核定来源与许可）。
  - `scripts/gen_golden.py`：调参考实现产金样本，并把输入/金样本 SHA256 写进 `fixtures.json`（防素材漂移：比较前先验哈希）。
- 合成信号设计（覆盖各引擎敏感面）：
  - 白噪声（全频带，验频谱形状）
  - 50Hz 基波 + 2/3 次谐波 + 低电平噪声（DSP 陷波的靶子）
  - 60Hz 同上（WP-1.6 后的第二个 profile）
  - 单频正弦 1kHz（最简对齐 sanity）
  - 真实语音（GTCRN/MDX 的真实工作点；合成语音无意义）
- 边界素材：短于一个 hop 的输入、非整 hop 长度尾部、精确 1 帧、多批次（> dimT 帧，验证批间衔接）。

## 5. 对齐细节（易翻车点，先立规矩）

- **GTCRN**：我们的实现首帧吞一个 hop 补偿算法延迟（`aidenoiser.cpp:120-124`），flush 再补零 hop 后截到 totalIn（`aidenoiser.cpp:213-216`）。参考侧必须按「同一延迟约定」构造输出，或参考输出整体平移一个 hop 后比较——实现时以 sherpa-onnx 输出的实际长度/偏移为准，把约定写进 gen_golden.py 并注释。**此项是 GTCRN parity 最大的不确定性来源。**
- **MDX**：官方 audio-separator 对整文件跑（非流式），输出含与 torch 反射填充对应的边界帧。我们的 P 域裁剪窗口 `win()..win()+len`（`vocalseparator.cpp:158-186`）预期与其对齐；若金样本出现固定偏移，优先怀疑填充约定而非截断。
- **重采样链**：DUT 的 swr 参数（soxr/flt 组合）与参考侧 scipy/torchaudio resample 的核不同，48k 源全链路 parity 可能到不了 −60dB。**对策**：跨采样率用例的判定分两级——同率素材（44.1k 直接进 MDX）走 −60dB 硬线；含重采样链的用例（16k GTCRN、48k 源）把重采样差异与引擎差异分离：参考侧先用 FFmpeg 同参数 swr 预降采样再进参考实现，比较时即只比引擎段。

## 6. 用例清单

见 `docs/acceptance/engine-parity/matrix.csv`（case × round 总表，状态以它为准）。分组：
- PAR-GT-001..（GTCRN：合成/真实语音/边界/分块不变/48k 源/单声道源）
- PAR-MD-001..（MDX：镜像 GTCRN + 输出契约负例）
- PAR-DS-001..（DSP：双精度参考、通带波纹 NFR-Q2 ±0.1dB、60Hz profile）
- PAR-PT-001..（变调：往返恒等、分块不变）
- FFT-PR-001..（FFT 性质五件套）

## 7. 比较器与结果流

- `scripts/compare.py <dut.wav> <ref.wav> --case <id>`：验哈希 → 读 f32 → 对齐检查 → 指标计算 → 追加写 `round-N.md` 表格行，退出码非零即 FAIL（供 CI 判定）。
- 每轮验证记录：`acceptance/engine-parity/round-N.md`（记环境、版本锁定、每用例指标值）；fix-rerun 新开 round，不覆盖。
- **分块不变性**（不需要金样本的照妖镜）：同一输入分别按 chunk ∈ {64, 256, 4096, 整段} 喂 `process()`，输出应一致。目标 bit-exact；若发现浮点求和顺序差异，放宽线 −120dB 并记录原因。此组用例直接抓 WOLA/状态机边界 bug，优先做。

## 8. 验收与退出

- matrix.csv 全绿（全部用例 ≤ −60dB 或其分组判据）→ 写 `report.md`。
- NFR-Q1 从「待建」翻「已达标」，证据链归档；此后任何引擎改动必须保持 parity 绿（进 CI 门禁）。
