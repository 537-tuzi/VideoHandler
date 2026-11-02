# WP-1.1 round-4：SG-6 落地——parity 套件并入 ci-smoke + 依赖版本锁矩阵（2026-11-02）

round-3 双收敛后的最后一项 sub goal。目标：此后任何引擎改动，parity 全绿成为 CI 门禁（spec §8）。

## 1. 改动清单

| 件 | 内容 |
|---|---|
| `scripts/run_parity.sh`（新） | 四族编排：素材保障（gen_inputs）→ 金样本保障（gen_golden，哈希登记+复用）→ engine_test 产 DUT → compare.py / pitch_check.py 判定。缺依赖族整族 SKIP（大声）；`PARITY_REQUIRED=1` 时 SKIP/缺依赖升级 FAIL——CI 是强制点，本地裸机降级跑（ci-smoke 保持绿，WP-1.2 契约不回归） |
| `scripts/gen_golden.py` | 新增 `gtcrn` 子命令（sherpa OfflineSpeechDenoiser，num_threads=1 与引擎一致，API 三层 config 逐字锁定并验证与既有 golden 逐字节一致）；`dsp` 增产 **float32 同构参考**（`dspf` 金样本，hum50 分级判据的自动化前提）；金样本**哈希复用**（`golden_ok` 先判后算，命中跳过推理与模型加载——首版先算后判浪费 5 分钟，修复后全套 7:46→2:20）；`--force` 换参考版本后强制重生成 |
| `scripts/gen_inputs.py` | 新增 GTCRN 边界素材 bound100/255/256/1025/4097（white16mo 确定性前缀，与 round-3 docs/tmp 边界素材逐字节同源）+ sine440（PAR-PT 靶，round-1 判据口径） |
| `scripts/pitch_check.py`（新） | PAR-PT 谱判据固化：长度 0 差 / 峰频 <10 音分 / 峰外杂散 ≥40dB / RMS ±0.1dB（实测 0.00 音分 / 59.0dB / +0.00dB，与 round-1 手工口径吻合） |
| `wp-1.2…/ci-smoke.sh` | 不变性组后接入 run_parity.sh |
| `.github/workflows/linux-engine.yml` | pip 缓存（torch 级包）+ venv 装 parity 参考实现（与本地 ~/.local+test/pydeps 双段式同 pin）+ `PARITY_REQUIRED=1` 跑 ci-smoke |
| `docs/spec/ci-build-strategy.md` §4 | parity 参考实现版本矩阵：numpy 2.5.4 / scipy 1.18.1 / sherpa-onnx 1.13.8 / audio-separator 0.47.0（wheel SHA256 指 requirements.txt）/ torch 2.14.1+cpu；配方唯一事实源 = requirements.txt + workflow yml |

## 2. 验证

**正例**：`run_parity.sh` 全绿 20 判定（DS 4 + GT 5 + MD 8 + PT 1 + 素材保障），数字与 round-3/matrix 逐项一致；全量 ci-smoke（FFT + 五引擎不变性 + parity + 7 CLI + ffprobe）**3:29 全绿**。

**负例三连**（失败路径实测）：
- A `PARITY_REQUIRED=1` + 缺依赖（PYTHONNOUSERSITE 屏蔽用户包）→ `exit 1`，FAIL 提示带配方指针；
- B 裸机缺 numpy/scipy（无 PARITY_REQUIRED）→ `exit 0` + 「parity 全 SKIP」大声横幅（引擎开发机 ci-smoke 不误红）;
- C 篡改金样本一字节 → 哈希失配自动重生成 → PASS 恢复且新哈希登记（反漂移闭环）。

**CI 侧**：workflow 未真机跑（push 后首跑回填，本地已验证 yml 语法与命令序列与本地 venv 同构）。

## 3. 结论与遗留

- SG-6 完成：WP-1.1 全部 sub goal（SG-1..6）收官，matrix 全 PASS/VOID → `report.md` 落档。
- 遗留（非 WP-1.1 范围）：① CI push 后真机首跑回填；② Windows round-4 真机（既有闸门）；③ 真实语音素材（许可核定后替换合成素材，PAR-GT 终判口径）；④ GTCRN 输出等长补尾的产品决策（round-3 §6 开放项）。
