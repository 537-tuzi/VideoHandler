# WP-1.1 round-6：SG-8 落地——hum 陷波 50/60Hz 参数化（2026-11-03）

P0 债 #5 / FR-3（spec `docs/spec/hum-notch-60hz.md` §2 拍板：`enum class HumBase { Off, Hz50, Hz60 }` 替代 removeHum 布尔）。
改动前：`makeNotch(50.0 * h, 30.0)` 基频硬编码，60Hz 电网地区（美日等）完全无效；改动后：CLI `[50|60]`、GUI 制式下拉（记住上次选择）、引擎按制式取基频，缺省 50Hz 向后兼容。基线 dcb588b（P0 #4 输出跟随源），`probeSourceSpec` 探测行全部保留。

## 1. 改动清单

| 件 | 内容 |
|---|---|
| `src/service/dspdenoiser.h:7-9,13` | 新增 `enum class HumBase { Off, Hz50, Hz60 }`（定义在引擎头，参数类型随引擎）；`setup` 第 3 参 `bool removeHum` → `HumBase humBase` |
| `src/service/dspdenoiser.cpp:14,38-42` | 陷波链按制式取基频：`base = Hz60 ? 60.0 : 50.0`，`makeNotch(base*h, 30.0)` h=1..3；系数计算（15-25 行采样率感知段）与高通支路不动 |
| `src/main.cpp:32,35-53,125-126` | `--transcode-wav` 直通改 `HumBase::Off`；`--denoise-wav` 加可选第 4 参 `[50|60]`（缺省 50，非法值 FAIL 用法提示，**校验先于转码故零半成品**）；GUI 分支补 `setOrganizationName/setApplicationName`（QSettings 定位配置必需） |
| `src/test/engine_test.cpp:7,48-50,72-86` | dsp 动词加可选 `[50|60]`（argv[5]，argv[4] 为块帧数）：`engine_test dsp <in> <out> [块帧数] [50|60]`，缺省 50，非法值 exit 2；头注释/用法文案同步 |
| `src/service/mediaservice.h:9,29,41` | `denoiseRequested`/`denoiseWav` 第 2 参 `bool removeHum` → `HumBase humBase`；头内前向声明 `enum class HumBase;`（不拖引擎头） |
| `src/service/mediaservice.cpp:2,10,14,83-88` | 构造首行 `qRegisterMetaType<HumBase>()`（队列投递注册，先于 init_connect/首次 emit）；denoiseWav 透传 |
| `src/service/ffextractor.h:9,35` / `.cpp:241-246` | `doDenoiseWav` 槽签名同步；probe 行保留，`setup(rate, channels, humBase, removeWind)` |
| `src/view/mainwidget.h:8,37` / `.cpp` | chkHum 文案「除电流声(50Hz)」→「除电流声」；新增 `cmbHumBase`（QComboBox：50Hz/60Hz，缺省 50，QSettings 记住上次选择，随 chkHum 勾选联动使能）；doDenoiseWav 组装 `HumBase`（不勾=Off） |
| `scripts/gen_inputs.py:56` | hum60 注释「（WP-1.6 后）」→「（PAR-DS-005）」（素材自 round-1 已生成，本轮起用；注释列不参与生成，素材字节/哈希不变） |
| `scripts/gen_golden.py` | dsp 分支解基频硬编码：`hum*` 素材名尾数即基频（hum50→50、hum60→60），其余素材对 DUT 缺省 50Hz 成参考；50Hz 族金样本逐字节复现（哈希不变）；docstring 同步 |
| `scripts/run_parity.sh` | PAR-DS 块加 hum60 DUT（显式传 60）+ PAR-DS-005 判定行（vs dspf 金样本 ≤−45）；块头注释补 hum60 |
| `matrix.csv` / `goal.md` | round-6 列 + PAR-DS-005 行（既有行补「—」不动）；SG-8 行 + 4 条决策（见 goal.md 重大决策） |

**设计取舍**：
- **enum + qRegisterMetaType vs 全链 int**：取 enum。`denoiseRequested` 是队列投递到后台线程的信号，enum class 跨队列连接须 `qRegisterMetaType<HumBase>()`（MediaService 构造注册一次即可）；int 免注册但 50/60 魔数穿透 GUI→服务→worker→引擎四层且失类型防护。枚举定义在 dspdenoiser.h，服务头仅前向声明（`enum class HumBase;` 不透明声明合法，underlying int），不把引擎头拖进服务头依赖。
- **GUI 控件选型 QComboBox 而非单选组**：两态制式一行放下即够，combo 与相邻 QDoubleSpinBox 视觉一致且省一行；勾选联动（chkHum toggled → setEnabled）代替三态控件（勾选框+「关」选项）避免与「不勾=不滤」语义打架。
- **QSettings 键存 50/60 数值而非索引**：条目重排不漂移；非 60 值一律回落 50（脏配置免疫）。
- **GUI 记忆时机=切换即写盘**（currentIndexChanged → setValue），启动只读一次；不落「提交时写」的第二条路径。

## 2. 验证（round-6 实测，全部本机跑出）

**构建**：`cmake --build --preset linux-test` 全绿（含 GUI 目标 video_handler 编译通过）。

**PAR-DS-005（新用例，`run_parity.sh build-linux`）**：

| 项 | 实测 |
|---|---|
| 判据（门） | hum60 素材（5s@44.1k/2ch，60+120+180Hz+噪声）→ `engine_test dsp … 60` → vs scipy float32 同构参考（基频随素材取 60）**parity −51.4dB（≤−45）PASS**，corr 0.999996，max\|d\| 1.64e-04，n=441000 |
| vs double 参考 | −30.9dB——过不了 −60 硬线，与 PAR-DS-001 hum50（−15.9dB）同因：Q30 陷波深度受 float32 系数量化限制（round-1 §2 归因对 60Hz 同样成立，量化残差大小纯属 50/60Hz 处量化相位运气），非实现差异，故沿用分级判据 |
| 辅助口径（spec §3 频响说法） | f32 系数级联稳态响应（freqz 实测）：**60Hz −49.23dB / 120Hz −48.05dB / 180Hz −51.76dB（全部 ≥20dB）**；仅陷波链 @50Hz **−0.04dB（50Hz 信号不受额外损伤）**；全链 @440/1000Hz −0.01/−0.00dB 通带平。时域 DFT 单-bin 实测 DUT 压制 60/120/180Hz = −39.3/−37.4/−39.6dB（含 Q30 陷波建立期瞬态，5s 素材）。该口径由同构 parity 隐含覆盖：DUT 与同构 f32 参考差 ≤−45dB，参考自身的陷波深度即上表 |

**PAR-DS-001..004 回归（50Hz 路径行为不变）**：与改动前（dcb588b 构建）**cmp 逐字节一致**（hum50/white/sine1k/odd 四素材 DUT 输出全等，sha256 对照）；parity 数值 −50.9/−71.8/−75.6/−75.2dB 与 round-1/round-5 逐位一致；50Hz 族金样本哈希不变（gen_golden 复现同哈希）。CLI 级：`--denoise-wav` 缺省与显式 50 输出 cmp 全等且与改动前基线全等，显式 60 输出与之相异（差异符合预期）。

**PAR-SR 全族复跑**：PAR-SR-001 bit-exact（240000/240000）、002a −71.1dB、002b +200.41 音分/48.2dB/−0.00dB/长度 0 差——与 round-5 逐位一致，输出跟随源无回退。

**run_parity 全量**：24 判定全绿（PAR-DS 5 + PAR-GT 5 + PAR-MD 8 + PAR-PT 1 + PAR-SR 5），无 SKIP。

**ci-smoke 全量**：FFT 属性 + 五引擎分块不变性 + parity 全族 + 7 CLI 动词 + ffprobe 体检（w1–w6 仍 pcm_f32le/44100/2ch），**2:48 全绿**（round-5 为 3:08，时间差属运行噪声）。

**CLI 正负例**：

| 例 | 结果 |
|---|---|
| `--denoise-wav st.wav out.wav 60`（正例） | OK，输出头 **pcm_f32le/44100/2ch**（44.1k 源跟随） |
| `--denoise-wav st.wav out.wav 55`（负例·非法值） | FAIL「hum 基频参数只接受 50 或 60（用法…）」exit 1，**无半成品文件**（校验先于转码） |
| `engine_test dsp in out 4096 55`（负例） | FAIL exit 2，无输出文件 |
| 缺省 = 显式 50 | 两输出 cmp 全等（向后兼容） |

**GUI 冒烟（headless 降级）**：`QT_QPA_PLATFORM=offscreen` 启动 3s 存活（exit 124 超时杀），stderr 仅 offscreen 平台固有提示一条，无 QSettings 空组织名/元类型注册类告警；默认跑一遍不产生配置文件（符合「切换制式才落盘」设计）。

## 3. 结论与遗留

- SG-8 完成：P0 债 #5 关闭（Linux 侧）；60Hz 制式从引擎到 CLI/GUI 全链贯通，50Hz 路径逐字节不变。
- 遗留/薄弱点：
  1. **GUI 运行时行为未真机验证**（headless 无显示）：编译通过 + offscreen 构造冒烟 + 逻辑走查为验收口径——未验证项：下拉切换实际生效、QSettings 跨次启动还原、chkHum 联动使能、`denoiseRequested` 跨线程投递的运行时端到端（qRegisterMetaType 已按 Qt 文档要求注册且先于任何 emit，属走查级证据）。Windows/真机随 round-4 闸门补。
  2. spec `hum-notch-60hz.md` §3 的「PAR-DS-003」旧编号与本 matrix 冲突——已按 matrix 用 PAR-DS-005，spec 按归档规范（开工前快照不回改）留原样，以 matrix/round-6 为准。
  3. CLAUDE.md「已知 P0 债」#5 行与架构树「50Hz×3」描述、CLI 用法表 `[50|60]` 参数——**留主线程同步**（本轮约定不动 CLAUDE.md）。
  4. 旧 Windows 分支复验（round-4 真机）继续顺延累积本包改动。
