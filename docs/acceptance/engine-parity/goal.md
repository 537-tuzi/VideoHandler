# engine-parity 总目标与进展

- 建立：2026-10-13
- 上位计划：`docs/spec/v1-development-plan.md`（M1）；技术方案：`docs/spec/parity-test-plan.md`

## 总目标

NFR-Q1 落地：GTCRN/MDX/DSP 三引擎对照官方参考实现 parity ≤ −60dB RMS，全用例绿并进 CI 门禁；顺带覆盖 NFR-Q2（DSP 通带波纹）、分块不变性（流式正确性照妖镜）与 FFT 性质测试。**未全绿前不动 M2 及以后的引擎行为。**

## sub goal matrix（唯一进展事实源）

| SG | 目标 | 验收 | 状态 | 备注 |
|---|---|---|---|---|
| SG1 | 测试基建：gen_inputs / gen_golden / compare 三脚本 + /test/ 布局 + fixtures.json | 脚本可重跑产全部素材与金样本，哈希一致 | 未开始 | 依赖：无 |
| SG2 | Linux DUT 构建（WP-1.2a） | linux-test preset 构建 libengine+engine_test，跑通一个合成用例 | 未开始 | 依赖：无；spec ci-build-strategy §2/§6 |
| SG3 | FFT 重写（WP-1.3） | FFT-PR-001..005 全绿 | 未开始 | 依赖：SG1（脚本宿主）；spec fft-rewrite |
| SG4 | GTCRN parity（WP-1.1） | PAR-GT 组全绿 | 未开始 | 依赖：SG1+SG2；对齐约定是最大风险（spec §5） |
| SG5 | MDX parity + 输出契约（WP-1.1+1.4） | PAR-MD 组全绿（含负例 009） | 未开始 | 依赖：SG1+SG2+SG3 |
| SG6 | DSP/变调/采样率组（WP-1.5+1.6） | PAR-DS/PT/SR 组全绿 | 未开始 | 依赖：SG1+SG2 |
| SG7 | CI 接线（WP-1.2b） | linux-engine job 必绿；PR 门禁生效 | 未开始 | 依赖：SG4/SG5/SG6 |

## 重大决策记录

| # | 日期 | 决策 |
|---|---|---|
| D-01 | 2026-10-13 | 指标 = 20·log10(RMS(dut−ref)/RMS(ref)) ≤ −60dB，样本级对齐，长度不等即 FAIL |
| D-02 | 2026-10-13 | 分块不变性目标 bit-exact，确证浮点顺序差异时放宽 −120dB 并记录 |
| D-03 | 2026-10-13 | 跨采样率用例双级判据：参考侧先用同参数 swr 预降采样再推理，只比引擎段 |
| D-04 | 2026-10-13 | 素材/金样本一律不入库（/test/ 已 ignore），SHA256 进 fixtures.json 锁定 |
| D-05 | 2026-10-13 | 主线走 Linux engine_test 全自动；Windows 手跑仅作 SG2 前的过渡 |

## 重要信息

- 参考实现版本与 wheel SHA256：待 SG1 锁定后登记于此（并同步依赖矩阵）。
- 真实语音/音乐素材的来源与许可：待选定后登记（避开 ContentVec 类 NC 陷阱）。
- UVR-MDX-NET-Voc_FT 实测输出 shape：待 SG5 首日实测后登记（决定断言与索引方式）。
