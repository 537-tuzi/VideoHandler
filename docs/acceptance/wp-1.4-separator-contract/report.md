# WP-1.4 验收报告：分离器输出契约（2026-10-18 全绿）

- 范围：`docs/spec/separator-output-validation.md` 全部设计条目 + dev plan 并入项（RSS 压回 ≤2GB）。
- 结果：**matrix.csv 15 用例 round-1 全 PASS**；证据 `round-1.md`，工具 `scripts/make_bad_model.py`。
- 关键数字：
  - Voc_FT 查表命中 n_fft=7680/dimF=3072/dimT=256/compensate=1.021/primary=Vocals；compensate 代数实测 1.02100（std 1.05e-06）。
  - 120s 素材峰值 RSS：sep-voc 1.48GB / sep-inst 1.48GB（NFR-P4 ≤2GB 达标；DisableCpuMemArena 前基线 2.58GB）。
  - 分块不变性（整块/4096/977 × 两动词）逐样本零差；ci-smoke 全量回归绿。
- 交付要点：UVR 官方参数表内嵌（44 条，末 10000KiB MD5 查表）、plane-major 读法、前 3 频点置零、输出契约校验（dtype/shape/布局，负例三分支全中、无残留）、AiDenoiser 名字映射 + n_fft==winLen 断言、wantSub 语义修复（初版公式取反，见 round-1 根因记录）。
- 遗留（非本 WP 出口条件）：数值 parity 归 WP-1.1；Windows 编译/冒烟随 wp-1.0 round-4；非 V/I 主 stem 模型未实测（行为语义已在代码注释与 round-1 声明）。
