# WP-1.1 engine-parity 总目标

## 目标

NFR-Q1 落地：五引擎对照官方/独立参考实现的数值 parity 测试（判据 −60dB RMS，见 `docs/spec/parity-test-plan.md` §1），进 ci-smoke 回归网，此后引擎改动保持全绿。

## sub goal matrix

| sub goal | 内容 | 状态 |
|---|---|---|
| SG-1 基建 | 素材生成 gen_inputs.py + 比较器 compare.py + fixtures.json 哈希防漂移 | round-1 完成 |
| SG-2 DSP parity | scipy 双精度 RBJ+lfilter 参考 vs dspdenoiser（PAR-DS） | round-1 全绿（hum50 走 float32 同构参考分级判据，理由 round-1 §2） |
| SG-3 GTCRN parity | sherpa-onnx 同模型推理参考 vs aidenoiser（PAR-GT，对齐点=首 hop 吞吐约定） | round-1 未收敛：判据修订（残差/RMS输入，−59.4dB）+ 两引擎债实锤（帧网格半窗偏移、首尾窗分母补偿缺），round-2 修后复测 |
| SG-4 MDX parity | audio-separator 官方管线 vs vocalseparator（PAR-MD，边界=反射填充） | round-2 落地并首轮对照：未收敛——幅度约定实锤（判据已修 --fit-scale）+ 纯音泄漏差 23dB 引擎前端谱构嫌疑（主）+ 块级 OLA vs 帧级 WOLA 架构差异（次），round-3 判别归因 |
| SG-5 变调性质 | pitch(+n)∘pitch(−n) 往返恒等 + 长度一致（PAR-PT，无独立参考） | round-1 全绿（谱判据） |
| SG-6 接入 CI | parity 套件并入 ci-smoke.sh（依赖 venv 存在时跑）+ 依赖版本锁依赖矩阵 | 待做（依赖 SG-3/4 收敛） |

FFT 性质五件套（FFT-PR）已在 WP-1.3 落地并入 ci-smoke，不重复建。分块不变性照妖镜已在 WP-1.2 并入 ci-smoke（五引擎），不重复。

## 重大决策

- **DUT 用 engine_test（裸 f32 直调引擎）而非 CLI**：parity 纯比引擎段，绕开 swr 重采样核差异（spec §5 对策的更彻底形态）；MDX 喂 44.1k 立体声裸流、GTCRN 喂 16k 单声道裸流。参考实现侧喂同内容的 pcm_f32le wav（无 16bit 量化垫噪）。CLI 级 e2e 行为由 ci-smoke 冒烟覆盖。
- **PAR-PT 变调往返用谱判据而非逐样本**：SoundTouch +2∘−2 逐样本残差 −12.9dB 是 WSOLA 往返相位漂移（正弦相位敏感），非缺陷。判据 = 长度 0 差 + 峰频误差 <10 音分（实测 0.00）+ 峰外杂散抑制 ≥40dB（实测 58.8dB）+ RMS 守恒 ±0.1dB（实测 +0.00）。
- **PAR-DS 判据分级（round-1）**：通带用例（white/sine1k/odd）vs double 参考 −60dB 硬线全过；陷波深度用例（hum50）vs double 只剩 −15.9dB，判别实验证明主因是 float32 系数量化（同构 scipy-float32 参考同样崩，理论深度上限 −26.7dB）——引擎固有精度而非实现 bug，改用 vs float32 同构参考 ≤−45dB 线（实测 −50.9）。精度债随 WP-1.6 double 化消灭后收回该分级。
- **venv 不可用（python3.14 无 ensurepip）→ pip3 --user --break-system-packages**：PEP 668 环境装 ~/.local，不动系统目录；CI 侧用工作流 pip 装。
- 素材与金样本落 `/test/`（gitignore），入库只留再生配方脚本。
- 参考实现版本锁定：sherpa-onnx 1.13.8、audio-separator[cpu] 0.47.0、scipy（装后把精确版本+wheel SHA256 记 ci-build-strategy.md §4）。

## 关键信息

- 判据与对齐细则见 spec；GTCRN 最大不确定性 = 首 hop 吞吐延迟约定（spec §5）——round-1 已实锤为**帧网格半窗偏移**（knf center=true/reflect 默认 vs 我们无 pad 网格），详 round-1 §3。
- **GTCRN 判据修订**（round-1）：白噪声下参考输出近静音，spec §1「残差/RMS(ref)」小分母失效；改残差/RMS(输入)。
- **两笔 GTCRN 引擎债**（round-1 实锤，处置待拍板）：① 前端缺 center-reflect 半窗 pad（帧网格错半窗，对齐后残差 −33.8→−57.5dB）；② 逆变换缺窗-OLA 分母补偿（首尾段最差 −51.6dB）。改动落 aidenoiser.cpp 前端。
- **DSP 精度债**（round-1 新增建议）：float32 系数量化把 Q30 陷波理论深度吃到 −26.7dB（double −203.7dB），建议随 WP-1.6 动 dspdenoiser 时 double 化；WP-1.1 不改引擎，hum50 判据分级处理。
- sine16mo 用例废除（非模型工作点），PAR-GT 终判改用真实语音素材（许可核定后引入）。
- sherpa-onnx 版本 1.13.8、scipy 1.18.1 已锁定；audio-separator 0.47.0 round-2 落地（绕 diffq：py3.14 无 wheel 且 MDX 路径不用它；环境配方固化 scripts/requirements.txt）。
- **MDX 参考幅度约定（round-2 实锤）**：参考输出=模型输出×s×peak（s=0.9/peak 当 peak>0.9 否则 1），三素材拟合增益与理论全吻合；parity 判据须去增益（compare.py --fit-scale）。
- **MDX 纯音泄漏差 23dB（round-2 新引擎嫌疑）**：sine1k 下 DUT voc 泄漏 0.233 vs 参考 0.017，宽带却 corr 0.97——指向引擎前端谱构（dimF 截取/高频重建/前 3 bin 置零），round-3 判别实验归因后决定是否立债（与 GTCRN 两债一并处置）。
- odd 素材（0.165s）触发引擎最短 ~0.17s 契约，MDX 边界用例换 odd1s（≈1s 非整 hop）。

