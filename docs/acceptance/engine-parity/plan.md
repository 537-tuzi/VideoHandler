# engine-parity 实施计划

- 建立：2026-10-13；随轮次维护「改动」与「验证」两节

## 需求

- NFR-Q1：GTCRN↔sherpa-onnx、MDX↔audio-separator parity ≤ −60dB RMS（不过不上线）。
- NFR-Q2：DSP 降噪 200Hz–20kHz 通带 |H| 波动 ≤ ±0.1dB。
- NFR-Q3 相关回归：采样率策略改动不破坏现有 44.1k 行为（PAR-SR 组）。
- P0 债 #1/#2/#3 的验收载体。

## 方案

见 `docs/spec/parity-test-plan.md`（指标/参考实现/素材管理/对齐细节）与各分项 spec：
`fft-rewrite.md`、`separator-output-validation.md`、`canonical-rate-48k.md`、`hum-notch-60hz.md`、`ci-build-strategy.md`。此处不重复。

## 改动（计划中的落点，随实现更新）

| 层 | 内容 |
|---|---|
| 构建 | CMake 分层 libengine/engine_test + linux-test preset + fetch-deps（ci-build-strategy §2/§6） |
| util | `fft.h` 重写（twiddle 表、2 的幂直通、Bluestein 加固） |
| service/vocalseparator | 输出契约断言 + 按名取输出 |
| service/aidenoiser | n_fft==window_length 断言 + 按名取输出 |
| service/fftranscoder + model/data.h | 输出规格跟随源 |
| service/dspdenoiser + view + main.cpp | hum 50/60 参数化 + CLI/GUI 入口 |
| 测试 | acceptance/engine-parity/scripts/{gen_inputs,gen_golden,compare}.py；/test/ 布局 |
| 文档 | CLAUDE.md 关键约定两处同步（输出规格、CLI 清单）；README 构建节 |

## 验证

- 用例与判据：`matrix.csv`（case × round 总表，状态唯一事实源）。
- 每轮证据：`round-N.md`（环境、版本锁定、逐用例指标值、失败分析）；fix-rerun 新开 round。
- 全绿出口：`report.md` + goal.md 的 SG 状态全部翻完成 + NFR-Q1/Q2 在 REQUIREMENTS 现状列翻「已达标」。
