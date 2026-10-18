# WP-1.1 round-1：parity 基建 + DSP/变调全绿 + GTCRN 判据与引擎债实锤（2026-10-18）

环境：Linux `build-linux/` DUT（engine_test）+ python3.14（pip --user --break-system-packages；scipy 1.18.1、sherpa-onnx 1.13.8、soundfile 0.13.x；audio-separator 0.47.0 安装受阻见 §4）。素材 `test/parity/`（gen_inputs.py 确定性生成，SEED=20261018，fixtures.json 哈希登记）。

## 1. 基建（SG-1）

- `scripts/gen_inputs.py`：9 组素材（sine1k/white/hum50/hum60 @44.1k 立体声；white16mo/sine16mo @16k 单声道；odd=1024·7+123/short=300/multi12s 边界组），每组成对 .f32（喂 DUT）+.wav pcm_f32le（喂参考，免 16bit 量化垫噪）。
- `scripts/compare.py`：等长对齐 + parity_dB + 相关系数 + 退出码（--shift 支持约定平移）。
- `scripts/gen_golden.py`：dsp 子命令（scipy 双精度 RBJ 级联，与 DspDenoiser 同参）。
- 决策：DUT=engine_test 裸 f32 直调（绕开 swr 核差异），CLI 级行为由 ci-smoke 覆盖。

## 2. PAR-DS（SG-2）——3 PASS + 判据分级 + 新精度债

| 用例 | 结果 |
|---|---|
| PAR-DS-white | PASS −71.8 dB |
| PAR-DS-sine1k | PASS −75.6 dB |
| PAR-DS-odd | PASS −75.2 dB（biquad 无内部缓冲，边界天然一致） |
| PAR-DS-hum50 | 分级判据 PASS：vs float32 同构参考 −50.9 dB（≤−45 线） |

**hum50 深挖（判据分级理由，全部数值实证）**：DUT vs double 参考 −15.9 dB 一眼 FAIL，但判别实验显示 scipy-float32 vs double 同样崩——float32 系数量化后 Q30 陷波**理论深度只剩 −26.7 dB**（50Hz；数值评估频响，double −203.7 dB），DUT 实测 50Hz 压制 −12.2 dB vs double −33.1 dB。即差异主体=系数量化+单精度（引擎固有精度），非实现 bug（DUT 与同构 float32 参考结构等价 −50.9 dB）。
→ **新引擎债：DspDenoiser 系数/状态 float 化吃掉陷波深度 ~20dB，建议随 WP-1.6 动该文件时顺手 double 化**（已记 CLAUDE.md P0 清单建议，本轮未改引擎）。

## 3. PAR-GT（SG-3）——判据修订 + 两笔引擎债实锤，未收敛

- **判据修订**：GTCRN 对白噪声输出近静音（−46 dB vs 输入 −14 dB），spec §1「残差/RMS(ref)」小分母失效（−25 dB 假 FAIL）。改用**残差/RMS(输入)**：物理意义=残余降噪误差相对原信号。
- **帧网格债（实锤）**：sherpa 前端 knf::StftConfig **center=true、pad_mode="reflect" 默认**（kaldi-native-fbank 1.22.3 stft.h:34），帧 i 覆盖 [i·hop−256, i·hop+256)（= torch.stft center=True 训练网格）；我们无 pad 网格错半窗。判别：给 DUT 喂 np.pad(x,256,'reflect') 后对齐，残差/输入 −33.8→−57.5 dB 骤降、corr 0.9987。
- **首尾窗分母债（次要）**：knf IStft 除窗-OLA 分母（首尾欠覆盖段补偿），我们无 → 段0（头部）残差最差 −51.6 dB。
- pad 对齐后稳态段残差/输入 −55.6~−63.2 dB 波动（掐 4096 全段 −59.4 dB）：接近但未稳过 −60 dB，疑数值噪声经模型非线性放大（白噪声敏感面）+ 上述两债残余。**未收敛**。
- **用例修订**：sine16mo 废除（正弦非模型工作点，两实现对近零输出分歧 −2.1 dB 无意义）；真实语音素材缺（spec §4 本就要求，许可待核定）——两债修复后用语音素材复测才是终判。
- sherpa 输出 79872=帧数 floor 约定（312×256），长度差 128 属正常边界约定，对齐后比较。

## 4. PAR-MD（SG-4）——参考实现安装受阻（进行中）

audio-separator 0.47.0 依赖链含 torch（554 MB wheel）：
- pip --user：~/.local 配额 Errno 122（清 2.4G pip 缓存后 scipy/sherpa/soundfile 装入成功，torch 撞墙）；
- --target test/pydeps + TMPDIR/cache 重定向 / 分区：后台进行中，结果 round-2 回填。
备选（若仍失败）：pip download 到 / 分区手动解包；MDX 用例整体顺延。

## 5. PAR-PT（SG-5）——谱判据全过

SoundTouch +2∘−2（st.f32 5s 440Hz）：长度 0 差、RMS 守恒 +0.00 dB、峰频误差 0.00 音分、峰外杂散抑制 58.8 dB（判据：<10 音分 / ≥40 dB / ±0.1 dB）。逐样本残差 −12.9 dB 系 WSOLA 往返相位漂移，非缺陷（判据决策记 goal.md）。

## 6. 下轮（round-2）清单

1. audio-separator 落地 → PAR-MD 全组（含 multi12s 多 batch、odd 反射填充边界、sep-inst compensate 支路）。
2. 引擎债处置决策（需用户拍板）：AiDenoiser center-reflect pad + 窗分母补偿（对齐训练网格，改动 aidenoiser.cpp 前端）；DspDenoiser double 化（建议随 WP-1.6）。
3. 真实语音素材获取（许可核定）+ GTCRN 复测。
4. 版本锁定入 ci-build-strategy.md §4（scipy 1.18.1 / sherpa-onnx 1.13.8 / audio-separator 0.47.0 + wheel SHA256）。
5. SG-6：gen_golden/compare 并入 ci-smoke（依赖在位时）。
