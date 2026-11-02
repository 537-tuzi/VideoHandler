# WP-1.1 engine-parity 总目标

## 目标

NFR-Q1 落地：五引擎对照官方/独立参考实现的数值 parity 测试（判据 −60dB RMS，见 `docs/spec/parity-test-plan.md` §1），进 ci-smoke 回归网，此后引擎改动保持全绿。

## sub goal matrix

| sub goal | 内容 | 状态 |
|---|---|---|
| SG-1 基建 | 素材生成 gen_inputs.py + 比较器 compare.py + fixtures.json 哈希防漂移 | round-1 完成 |
| SG-2 DSP parity | scipy 双精度 RBJ+lfilter 参考 vs dspdenoiser（PAR-DS） | round-1 全绿（hum50 走 float32 同构参考分级判据，理由 round-1 §2） |
| SG-3 GTCRN parity | sherpa-onnx 同模型推理参考 vs aidenoiser（PAR-GT，对齐点=首 hop 吞吐约定） | **round-3 全绿**：主用例 −123.9dB（残差/RMS输入 −156.0）+ 边界组全过。两债修复 = 反射帧网格（center+双侧 reflect）+ WOLA÷Σw² + 反射尾帧（输出长度约定 (N//hop)·hop 与参考一致）；机理修正见 round-3 §6 |
| SG-4 MDX parity | audio-separator 官方管线 vs vocalseparator（PAR-MD，边界=反射填充） | **round-3 收敛全绿 8/8**：机器精度 −107.6~−145.4dB corr 1.000000。三重归因（normalize 工作点/×peak 域/块级 OLA+块尾 reflect）→ 判据同工作点（gen_golden mdx 旁路+域重建）→ 引擎块级化复刻官方分块（step=195840/块内 reflect/hanning OLA+divider，环形 3MB 有界） |
| SG-5 变调性质 | pitch(+n)∘pitch(−n) 往返恒等 + 长度一致（PAR-PT，无独立参考） | round-1 全绿（谱判据） |
| SG-6 接入 CI | parity 套件并入 ci-smoke.sh + 依赖版本锁依赖矩阵 | **round-4 完成**：run_parity.sh 四族编排（缺依赖族 SKIP / PARITY_REQUIRED=1 升级 FAIL，CI=强制点）+ 金样本哈希复用/反漂移（--force 换版本重生成）+ pitch_check 固化 + workflow venv 同 pin + 版本矩阵入 ci-build-strategy §4；正例全绿 + 负例三连（必判 FAIL/裸机降级/篡改重生成）。**WP-1.1 全 sub goal 收官，report.md 落档** |
| SG-7 输出跟随源 | 转码层输出规格跟随源，消灭 48k 源双重重采样（P0 债 #4 / NFR-Q3，spec canonical-rate-48k §2）：transcodeStreamWav 尾加 outRate/outChannels（0=跟随源）+ probeSourceSpec 轻量探测；DSP/变调引擎 setup 改用源率，AI/MDX 路径显式传兜底规格不动（PAR-SR，**CLI 级 DUT** 与 engine_test 族互补——此族考转码层） | **round-5 全绿**：48k 直通 bit-exact（240000/240000）+ DSP@48k −71.1dB + 变调 +2 谱判据（+200.41 音分/48.2dB/−0.00dB/长度 0 差）+ 44.1k 回归无回退（PAR-DS/PT 数值逐位同旧 + 体检仍 44100）；反证：模拟旧 44.1k 往返链 vs 48k 金样本 −10.2dB 必 FAIL |
| SG-8 hum 50/60 参数化 | P0 债 #5 / FR-3（spec hum-notch-60hz §2）：`HumBase` 枚举（Off/Hz50/Hz60）替代 removeHum 布尔贯通引擎/CLI/GUI，陷波基频 50/60Hz 双制式；PAR-DS-005（hum60 素材，DUT 传 60） | **round-6 全绿**：vs float32 同构参考 −51.4dB（≤−45，分级判据同 PAR-DS-001）；缺省 50 路径 cmp 改动前基线逐位一致；CLI/GUI 参数通路打通（GUI 以编译+走查验收，运行时行为列薄弱点） |

FFT 性质五件套（FFT-PR）已在 WP-1.3 落地并入 ci-smoke，不重复建。分块不变性照妖镜已在 WP-1.2 并入 ci-smoke（五引擎），不重复。

## 重大决策

- **DUT 用 engine_test（裸 f32 直调引擎）而非 CLI**：parity 纯比引擎段，绕开 swr 重采样核差异（spec §5 对策的更彻底形态）；MDX 喂 44.1k 立体声裸流、GTCRN 喂 16k 单声道裸流。参考实现侧喂同内容的 pcm_f32le wav（无 16bit 量化垫噪）。CLI 级 e2e 行为由 ci-smoke 冒烟覆盖。
- **SG-7 输出规格=跟随源而非「canonical 改 48k」（round-5，spec canonical-rate-48k §2 拍板）**：`transcodeStreamWav` 尾参 outRate/outChannels（0=跟随源，规整 8k–192k / min(源,2)，无效回落兜底）；DSP/变调引擎 setup 用 `probeSourceSpec` 探测的源规格（avformat 开读即关；失败不拦任务——后续 open_decoder 给正式报错，回落兜底规格）。AI/MDX 路径显式传 kCanonicalRate/kCanonicalChannels，模型工作点不动（spec 明示本工作包不改）。**PAR-SR 是唯一 CLI 级 DUT 族**：考的是转码层（WAV 头/bypass 判据/toOut 目标），与 engine_test 族正交互补。
- **PAR-SR-002b 单程变调用 --expect-cents 200（round-5）**：单程 +2 若沿用「|cents|≤10」口径，未变调的 bypass 也会过门——判据改为「|cents−200|≤10」，bypass 输出 0 音分即 FAIL。
- **PAR-PT 变调往返用谱判据而非逐样本**：SoundTouch +2∘−2 逐样本残差 −12.9dB 是 WSOLA 往返相位漂移（正弦相位敏感），非缺陷。判据 = 长度 0 差 + 峰频误差 <10 音分（实测 0.00）+ 峰外杂散抑制 ≥40dB（实测 58.8dB）+ RMS 守恒 ±0.1dB（实测 +0.00）。
- **HumBase 枚举 + qRegisterMetaType 而非全链传 int（round-6，spec §2 拍板 enum）**：`denoiseRequested` 队列投递到后台线程，enum class 跨队列连接须 `qRegisterMetaType<HumBase>()`（MediaService 构造注册一次，先于 init_connect/首次 emit），否则运行时静默投递失败；int 免注册但 50/60 魔数穿透 GUI→服务→worker→引擎四层且失类型防护，不值。枚举定义在 dspdenoiser.h（引擎参数类型随引擎），mediaservice.h/ffextractor.h 仅前向声明 `enum class HumBase;`，不把引擎头拖进服务头。
- **PAR-DS-005 编号避让（round-6）**：spec hum-notch-60hz §3 写的「PAR-DS-003」是 2026-10-13 拍的旧编号，matrix 里 PAR-DS-003 自 round-1 起即 sine1k sanity——60Hz 用例启用新号 PAR-DS-005，既有四行不动；spec 按归档规范不回改，以 matrix/round-6 为准。
- **PAR-DS-005 判据沿用 float32 同构分级（round-6）**：hum60 vs double 参考 −30.9dB 过不了 −60 硬线，与 round-1 hum50（−15.9dB）同因——Q30 陷波深度受 float32 系数量化限制，60Hz 处 f32 量化残差与 50Hz 不同纯属量化运气，非实现差异；门 = vs float32 同构参考 ≤−45dB（实测 −51.4）。spec §3 的频响口径（60/120/180 衰减 ≥20dB）作辅助证据：f32 级联实测 −49.2/−48.1/−51.8dB、50Hz −0.04dB 不受损，由同构 parity 隐含覆盖。
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
- **MDX 判据同工作点（round-3 决策）**：参考两处幅度约定（前置 normalize 峰值>0.9 工作点偏移、输出 ×peak 纯增益）是产品策略非引擎语义，金样本侧旁路+代数除回（gen_golden.py mdx，域重建自洽校验 3e-8）；--fit-scale 降级为纯校验（a 恒 1.0000）。
- **MDX 引擎块级化（round-3 决策）**：parity 达标须复刻官方 demix 分块语义（全局网格在块重叠区数学上不等价，判别实验：段A 机器精度/段B −0.1dB 分界在块1起点 x=192000）。输出延迟增至 ~2 块（块级 OLA 固有），流式/内存契约不变（环形 3MB、flush 补零 ≤2C）。
- 块级化实现备注：块内合成必须 padded 域整块 WOLA+裁 [trim,trim+C)（istft center 语义）——「每帧排 kHop」流式网格帧贡献差半窗且 dimT·kHop=C+kHop≠C（ASan 越界实锤后重写）。
- **GTCRN 输出长度约定变更（round-3）**：修复后引擎输出 = (N//hop)·hop（参考 sherpa OfflineSpeechDenoiser 同款，golden 逐字节对齐的前提），5s 素材少 8ms 尾段；ci-smoke w3 断言已按此改为区间断言（其余 w* 仍精确断言）。**开放产品决策**：离线产品若要输出与源等长（视频音轨回贴防漂移），应在 FFTranscoder 层补尾零——动 fftranscoder.cpp 全引擎共层，不随 parity 轮擅改，FR-24 无头命令层/实时化前拍板。
- **GTCRN 两债机理修正（round-3 子代理实锤）**：债②主因不是首尾窗分母欠补偿（发射区 Σw² 恒=1），而是旧引擎缺反射尾帧（参考对最后样本镜像补帧）；分母补偿保留（istft 语义正确且零成本）。参考语义锁定：center 网格 + 双侧 reflect + WOLA÷Σw² + 裁头，帧数 = N//hop+1，python 离线复刻 vs golden −153.0dB（float64 机器精度）。
- **子代理对抗式审查闭环（round-3）**：3 维度 × 验证共 11 代理，0 confirmed / 3 killed / 1 uncertain——该 P1（runModel 失败后 specCount 残留致越界写）针对 round-3a 旧代码，块级化已删 specCount 成员；主线程在最终代码上复核失败-重试路径幂等安全（失败发生在谱累计之前，环形仅零扩展），判定失效关闭。
- GTCRN 规格防护收紧（主线程审查发现）：setup 约束由 hop≥winLen/2 收紧为 hop==winLen/2——feedPending 的 need=winLen−half 与 slideAnalysis 每步吃 hop 仅在等式下一致，否则假想导出会越界读 pending；GTCRN 导出恒为该规格。
- **SG-7 素材/金样本口径（round-5）**：white48k/sine48k @48000/2ch（gen_inputs SR_48=48000）；`gen_golden.py dsp` 分支 sr 参数化——取素材在 fixtures.json 登记的 sr 元数据（44.1k 素材同旧路径，哈希不变，PAR-DS 数值逐位复现）；`pitch_check.py` 加 `--sr`（rfftfreq 用，48k 素材必传）与 `--expect-cents`（默认 44100/0，PAR-PT 口径不变）；新增 `sr_check.py`（CLI 输出 WAV 头断言 + bit-exact + --dump 裸流导出供 compare/pitch_check 复用）。
- **SG-7 probe 一致性边界（round-5 已知）**：引擎 setup 用 probe 的 codecpar->sample_rate，转码输出用 open_decoder 后的 dec->sample_rate——两者不一致的怪文件会多一次 toOut 重采样（行为仍正确，仅非零重采样）；WAV/常见容器下两者恒等。
- **SG-8 dsp 金样本基频随素材走（round-6）**：`gen_golden.py` dsp 分支在 sr 参数化（round-5）之上再解基频硬编码——`hum*` 素材名尾数即基频（hum50→50、hum60→60），其余素材对 DUT 缺省 50Hz 成参考；50Hz 族金样本逐字节复现（哈希不变），PAR-DS-001..004 数值逐位同旧。

