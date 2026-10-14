# 架构审视：VideoHandler 现状架构的重审（2026-10-14）

- 性质：**开工前快照**（spec 桶）——基于当日代码全量精读 + 外部调研 + 本地测试取证的三源审视；落地动作另立工作包。
- 方法：①代码精读 21 文件 / 2106 行（无遗漏）；②4 路外部调研（一手信源，`docs/reference/research-{streaming-pipelines,realtime-threading,qt-audio-apps,cpp-engine-patterns}.md`）；③本地测试取证（`docs/tmp/arch-review/`，g++ 15.2 / numpy 仿真双路交叉验证）。
- 服务对象：NFR-M1（引擎注册表）、FR-24（无头命令层）、NFR-L2（processInto 零分配）、M4（实时化）、P0 债清单复核。

---

## 1. 现状架构剖析

### 1.1 分层与依赖（实际代码，非设计意图）

```
main.cpp（组合根 + CLI 分支）
  ├─ GUI 路径：QApplication → MediaService(构造注入) → MainWidget::setMediaService
  └─ CLI 路径：run_cli（argc≥3）直接 new 引擎 → FFTranscoder / FFExtractor 静态核心

view/mainwidget    纯 UI：按钮 → MediaService 方法调用；3 个结果信号槽落地
service/mediaservice  调度：busy 标记 + QThread(worker=FFExtractor, moveToThread) + 7 组请求信号
service/ffextractor   任务执行：7 个 do* 槽；静态核心 probe/extractAudio/extractVideo 供 CLI 复用
service/fftranscoder  唯一转码通路：open_decoder → swr(→引擎规格) → engine.process/flush → swr(→44.1k) → WAV(f32)
service/ 引擎×4       IStreamEngine 实现（dspdenoiser / aidenoiser / pitchshifter / vocalseparator）
util/fft.h、ffutil.h、model/data.h  叶子工具与常量
```

依赖方向严格单向（view → service → util/model），无环。**问题不在纵向分层，在横向重复**（见 §3 A4）。

### 1.2 数据流（离线任务全链路）

解码循环（fftranscoder.cpp:204-226，每 packet 一轮，取消检查点在循环头 :205）→ `feedAi`：帧 → `swr_convert` 进 `convIn` → 攒入 `aiIn`（chunk = inRate 即 1 秒）→ `engine.process()` → 输出插入 `enhanced` →（**缺陷：只在收尾 drain，见 A1**）→ `drainEnhanced`：按 8192 帧切片 → bypassOut 直写 / `swr_convert` 回 44.1k → `write_samples`（每片新 AVPacket）。收尾顺序：冲刷解码器 → 冲刷 toAi → 尾块 process → `engine.flush()` → drain → 冲刷 toOut → trailer。

### 1.3 线程模型

- 主线程：UI + MediaService（busy/cancelFlag 都只在主线程读写；worker 通过 queued connection 回传）。
- worker 线程：QThread 事件循环跑 FFExtractor 槽（一次一任务，任务间串行）；引擎在任务栈上构造/析构（无跨任务状态残留 ✓）。
- 取消：`std::atomic<bool>` 由 UI 置位、worker 在转码 packet 粒度检查——协作式取消，语义正确。
- `~MediaService` 顺序 quit→wait→delete worker ✓ 无泄漏。

### 1.4 错误处理路径

- FFmpeg 胶水：`do{}while(false)` + 集中释放 + 失败删半成品（fftranscoder.cpp:286-290、ffextractor.cpp:101-113），纪律全线一致 ✓。
- 引擎层：`setup()` 返回 bool+QString err；运行期错误走 `lastError()` 轮询（fftranscoder.cpp:197/255 检查）。
- **缺口**：`lastError()` 仅 vocalseparator 实现（:240-242）；AiDenoiser 的 `process/flush` 无异常屏障（`sess->Run` 可抛 `Ort::Exception`），异常将穿透转码器与 worker 槽直达 QThread 事件循环 → `std::terminate`（见 A2）。

---

## 2. 测试取证（本地，2026-10-14）

工具：`docs/tmp/arch-review/`——`fft_probe.cpp`（g++ 15.2.0 -O2 编译真跑）+ `fft_sim.py`（numpy float32 逐舍入点复刻 fft.h 算法）+ `results.txt` / `sim_results.txt`。**双路交叉验证一致**（例：n=8192 Bluestein 往返误差 C++ 3.25e-4 vs 仿真 3.28e-4），两条证据链互证。

### 2.1 FFT 精度与耗时（P0 债 #2 的量化）

| 证据 | 数值 | 结论 |
|---|---|---|
| 基-2 直通 vs 双精度真值 | n=64 误差 4.8e-6 → n=8192 **7.0e-3**（相对谱峰 3.3e-5） | float twiddle 递推漂移随 n 单调放大；−60dB parity 判据（1e-3 量级）在 n≥8192 已被 FFT 自身噪声侵蚀 |
| FftPlan(Bluestein) 往返 | 6144/7680/8192 ≈ 3e-4；12288 劣化至 1.4e-3（Parseval 3.5e-4） | 全 float Bluestein 在 MDX 工作点（6144/7680）往返 ~3e-4，12288（48k 场景）明显变差 |
| 2 的幂走 Bluestein（同输入对撞） | 与基-2 直通互差 7.6e-3@4096、8.8e-3@8192，且自身对真值误差更大（1.28e-2 vs 7.3e-3@8192） | **「慢且亏精度」双双坐实**：现结构 pow-2 帧应直通基-2 |
| 耗时（g++ 15.2 Linux，指示性；产品为 MinGW 13.1/Win） | n=8192 完整往返：基-2 0.29ms vs FftPlan 0.71ms = **慢 2.5×**；12288 plan 1.6ms | 直通 radix-2 是白捡的 2.5× |
| 分配 | `FftPlan::run` 稳态 **0 分配** ✓；`process()` 返回 vector = 每调用 1 次堆分配（语言级保证，libstdc++ vector 无 SBO） | plan 的缓冲复用是好底子；按值返回是 P0 #7 实体 |

> 探针备注：合成循环里 GCC 15 -O2 会把「拷贝返回」整体堆消除（无真实消费者时分配被证明无副作用），故分配对照不靠实证、靠语言级事实；真实转码循环中 vector 被 memcpy 消费不可消除。

### 2.2 A1 内存推算（见 §3）

`enhanced` 峰值 = 时长 × inRate × inChannels × 4B：**2h @44.1k/2ch = 2.37GiB > 2GB（NFR-P4 破线）**；1h=1.27GB；连 GTCRN 链（16k 单声道，2h=460MB）也属「随时长无界增长」。

---

## 3. 发现清单（A1 起新记；含定级与证据）

| # | 级 | 发现 | 证据 | 修法方向 |
|---|---|---|---|---|
| **A1** | **P0** | **转码层内存无界，违反 NFR-P4**：`drainEnhanced` 全文件仅 1 处调用（fftranscoder.cpp:256，收尾阶段）；主循环里 `feedAi` 只 insert（:195）不消费，引擎全量输出常驻内存至文件结束 | §2.2 推算；grep 调用点唯一 | `feedAi` 的 chunk 分支 process 后即调 `drainEnhanced()`（一行级）；缓冲随即有界（~1s 块 + 引擎内部 + 8192 帧写出缓冲）。**建议立即修，不等 v2 接口** |
| **A2** | **P1** | **引擎异常屏障缺失 → 可 std::terminate**：AiDenoiser::process/flush 无 try/catch（aidenoiser.cpp:190/204），`runFrame→sess->Run` 抛 `Ort::Exception` 将穿透 FFTranscoder、runJob 槽、QThread 事件循环；接口的 `lastError()` 通道被绕过 | 对照 vocalseparator.cpp:150-152（有 catch→errMsg）——同库同错误两种处理 | 接口契约补「process/flush 不得抛出」；引擎内 catch 归口 lastError；runJob 外围兜底 catch 兜住一切 |
| A3 | P1 | **接口契约缺口**：①输出规格=输入规格（inRate/inChannels）这一硬约束只存在于 fftranscoder 消费方式（:157 步进、:121 bypass 判定）里，接口注释未写；②「flush 后不得再 process」与 AiDenoiser 实现矛盾（flush 重置状态可复用，aidenoiser.cpp:218-221）；③FftPlan::run 复用内部缓冲非线程安全（单实例单线程假设未文档化） | streamengine.h:6-16 注释 | v2 接口文档化/类型化（§4.1 R 系列） |
| A4 | P1 | **横向重复：一个功能动 4–5 处**。4 引擎 `setup()` 签名异构（rate/ch/flags、modelPath、modelPath、rate/ch/semi）→ 装配逻辑不可泛化；任务枚举在三层各写一遍（mainwidget 10 槽 / mediaservice 7 信号+7 槽 / ffextractor 7 槽）+ main.cpp CLI 7 分支 + data.h 后缀常量 | main.cpp:18-108；mediaservice.h:24-42；ffextractor.h:27-34 | NFR-M1 注册表（键+能力元数据+统一 setup 参数集）+ FR-24 命令层，一次消解三层重复（§4.4） |
| A5 | P2 | **引擎骨架不一致 + O(n²) 搬运**：aidenoiser 每 hop 从 vector 头 erase（aidenoiser.cpp:198；1s 块@16k/hop256 = 62 次 erase ≈ 每秒音频 0.5M float 搬运）；vocalseparator 同类问题用 deque 两种解法 | 两文件对照 | 读游标替代 erase；v2 时抽公共「待分析帧缓冲」骨架 |
| A6 | P2 | **模型路径双真相源**：GUI = `applicationDirPath()+kAiModelRelPath`（ffextractor.cpp:230/255），CLI = 硬编码 `third_party/models/...`（main.cpp:48/86） | 三处对照 | FR-8 模型管理器统一解析（参数 > 管理器目录 > exe 相对） |
| A7 | P2 | **规格协商无统一归属**：canonical 常量直拼进各调用点（data.h:7 + main.cpp/ffextractor 各 setup），NFR-Q3（48k 可配）落地前需收敛协商点 | P0 债 #4 的架构面 | v2 的 prepare/协商阶段（§4.1 R3）承接，常量降级为默认值 |
| A8 | P2 | **直通也整块拷贝**：DspDenoiser 无滤波时仍 vector 拷贝整块（dspdenoiser.cpp:59-63），`--transcode-wav` 冒烟路径每次 process 白拷 352KB | 代码直读 | processInto + `filters.empty()` 短路 |
| A9 | P2 | **WIN32 子系统 + CLI 同体的输出机制脆弱**：GUI 子系统下 stdout 依赖父进程句柄继承，无控制台会话可能无输出（现冒烟能用不等于稳） | add_executable(WIN32)（CMakeLists.txt:13）+ main.cpp:101-107 | 待 Windows 实测；必要时 CLI 分离或 AttachConsole（仅标注，未定论） |
| A10 | P3 | CMake 规范性：`CMAKE_CXX_EXTENSIONS ON`（gnu++20 而非严格 c++20）、Release flags 放 preset 全局而非 per-target、D:/qt6 硬编码（已记 NFR-M4）、静态 .a 与 DLL 直连混链（已记 P0 #6）；另 presets version 6 要求 CMake ≥3.25 但 cmake_minimum_required(3.21) 不一致 | CMakeLists.txt:5-7/100-101；CMakePresets.json | §4.4 CMake 修法（UserPresets 迁移并入 NFR-M4） |
| **A11** | **P1** | **关窗假死**：`~MediaService` 直接 `quit(); wait();`（mediaservice.cpp:17-18），未先置 cancelFlag——AI 长任务运行中关窗 = 进程无 UI 挂起直至任务自然跑完 | mediaservice.cpp:17-18 直读；qt-audio-apps H1（未真机复现，机制推导成立） | 析构先 `cancelFlag = true` 再 quit/wait（一行级）；外围为关停序协议 S1 |
| A12 | P2 | **流复制路径完全不可取消**：copy_stream（--extract-audio/--extract-video 及 GUI 提取）循环无取消检查点，转码路径有 packet 检查点而流复制没有 | ffextractor copy_stream 对照 fftranscoder.cpp:205 | copy_stream 循环头加同一原子量检查点 |
| A13 | P3 | **busy 静默拒绝**：`if (busy) return;` 无反馈通道，正确性靠 UI 按钮禁用兜底——约定而非机制 | mediaservice.cpp 各请求方法 | 三态结果（Cancelled/Stopped 区分）+ 提交协议显性化（qt-audio-apps S4/A 系列） |
| A14 | P3 | 卫生项：vocalseparator.h:17 `lastError()` 缺 `override` 关键字（同签名隐式虚覆写，**运行时正确**——代理「非虚遮蔽」定性经复核推翻；加 override 防将来签名漂移变真遮蔽） | streamengine.h:16 与 vocalseparator.h:17 签名比对 | 加 `override`；全接口实现统一补关键字 |

**注**：P0 债 #1（parity）/#2（FFT）/#3（MDX 双错）/#4（44.1k）/#5（50Hz）/#6（LGPL）/#7（按值返回）本次全部复核在位，且 #2、#7 获得量化数据（§2.1）；A 系列是**新增**清单，不与旧编号重叠。A1–A10 出自代码精读，A11–A13 出自 Qt 实践对照（H1–H3），A14 出自语言特性调研并经主会话复核修正。

---

## 4. 业界对照（外部调研结论，证据链在各 research 文档）

### 4.1 流式管线框架 × IStreamEngine（research-streaming-pipelines.md）

七框架取证（SoX / FFmpeg avfilter / GStreamer / JUCE / Web Audio / miniaudio / speexdsp）核心结论：

- **调用方持缓冲 + 双计数是绝对主流**（5/7 家），**无一支持本项目「process 返回 vector」形态**（反证记录在文档 §11-5）。
- v2 建议八条（R1–R8，含接口全景草案）：`prepare()` 显式协商 + `preferredBlockFrames`（对标 JUCE prepareToPlay / SoX imin）、`processInto` 双计数（对标 SoX flow / miniaudio onProcess，miniaudio 明文支持消费≠产出）、`drainInto` 循环到零取代 flush 返回 vector（对标 SoX drain，修掉无界尾段）、`latencyFrames` 申报（对标 JUCE setLatencySamples）、`canProcessInPlace` 就地申报（DspDenoiser 直接受益）、注册表「类型与实例分离」（对标 SoX 效果表 / miniaudio vtable）。
- **独立复核确认 A1**（其 R5 同判：调用时机错位，修复不依赖 v2）。

### 4.2 实时音频线程规范（research-realtime-threading.md）

- **RT 铁律全部一手原文取证**：Bencina 七条 Don'ts（含最坏情况执行时间论述）、PortAudio portaudio.h:770-835（回调 RT 优先级、70% CPU 口径、除 Pa_GetStreamCpuLoad 外禁调 API）、JUCE WASAPI 设备线程完整先例（avrt.dll 动态加载 + `AvSetMmThreadCharacteristicsW("Pro Audio")` + 预分配 bufferSize+32）。Timur Doumler 一手文本未取得（标【未确认】，以上三家已覆盖）。
- **MMCSS 定论**：High 类 23–26 "designed for Pro Audio tasks"——本项目自建处理线程挂 "Pro Audio"（JUCE 同款）。
- **队列选型**：首选自研 SPSC（rigtorp MIT 模式：acquire/release + 索引缓存 + 缓存行对齐；pa_ringbuffer MIT 双重锚定）；现成件 rigtorp（MIT）/ readerwriterqueue（BSD-2）。辨析：ConcurrentQueue 是 MPMC 非 SPSC；**JUCE AbstractFifo 因 AGPLv3 禁用（红线 D1 相关，新条目）**。
- **`std::atomic::wait` 关键发现**（libstdc++ 源码一手）：无平台原语时回退 mutex+condvar 16 桶池，_M_notify 也持锁 → 推论【倾向】MinGW 目标上 RT 线程连 notify 都不能调（与 §4.4 代理独立同判，双重确认）。RT 侧同步白名单：**atomic load + SPSC push/pop + 写预分配内存，仅此而已**。
- **双线程模型先例**：PortAudio pablio（回调只碰环形缓冲 + 欠载补零，worker 不需 RT 侧唤醒）；Web Audio 规范「控制线程+渲染线程 MUST」+ 128 帧渲染量子 + (2×128)/44100=5.805ms 缓冲换算；GStreamer buffer_time/latency_time 参数化深度。
- **processInto 八条约束**（与 §4.1 R 系列互补）：缓冲归属调用方、返回写入帧数、禁扩容、滞后上界静态可声明（maxOutPerCall / algorithmicDelayFrames）、flush 语义保留、**取消不入签名**、规格声明制不变、旧 process 保留为离线薄封装。
- **预算分解**：软件预算 = 150 − 捕获 40~90 = **60~110ms**；GTCRN 链 ~55ms 勉强可行；RVC chunk 96ms 单项超支，须砍到 1024 帧级或接受分级——归 O3 实测裁决。
- **取消/背压**：单写多读原子位（现状 mediaservice.cpp:39-41 即正确）；欠载补零 + xrun 计数（pablio 先例），不阻塞不 notify，异常不跨界。
- **注入法**：无官方可照抄实现（web-audio-samples >50MB 未核验）；可借规范级口径 = baseLatency/outputLatency 分解 + 在途帧数/fs 换算；O3 工具自建（回采互相关法）。

### 4.3 Qt 大型音频桌面应用实践（research-qt-audio-apps.md）

- **现状形态被业界同构确认**：worker-object+moveToThread（Qt 官方推荐）、**静态核心 + QObject 薄壳双轨 = FR-24 正确雏形**（对标 Audacity delegate+worker、Hydrogen CoreActionController、LMMS coreOnly）、引擎 per-job 零共享、jobDone lambda 以 this 为 context 复位无竞争。
- **隐患 8 项（H1–H8），最重三项入 A 系列**：H1 关窗假死（→A11）、H2 流复制不可取消（→A12）、H3 busy 静默拒绝（→A13）。
- **busy 模型判定**：本身无竞争、正确，但两态表达力弱。谱系：Mixxx FIFO+固定 worker+100ms 进度限频+单一信号保序 / Audacity 一次一任务同型但三态结果+块级取消谓词 / QThreadPool(maxThreadCount=1) 免手写串行队列。**反证：引入 QFuture 不解决取消**（QtConcurrent::run 的 future 官方明言不可 cancel）——防止将来走弯路。
- **退化机制**（推导非实测）：jobFinished/progress 广播无 jobId → 多窗口全收无法路由；解法谱系 = 任务对象化+每任务订阅（QFutureWatcher）/ Mixxx ControlProxy key 寻址 / LMMS 集中装配。
- **组合根六维评估**：唯一装配点 ✅、无头可运行 ✅、全局状态克制 ✅（优于 LMMS/Hydrogen 单例枢纽）、生命周期显式 ⚠（关停序缺口=A11）、依赖显式 ⚠（setter 注入，改构造注入零成本）、**可测性 ❌（直连具体类无接口）**。
- 建议：保守档 S1–S6 / 进取档 A1–A7（每条带先例），执行顺序 S1–S5→A1→A3→A2/A4→A5–A7；Mixxx/LMMS/Hydrogen 为 GPL 系——**只可参照思路、不可复制代码（红线）**。

### 4.4 C++ 语言特性与注册表/命令层模式（research-cpp-engine-patterns.md）

- **注册表形态定案**：LLVM「InitializePasses 式」显式组合根注册（EngineDesc 描述子 + catalog 一次装填）；**否决静态自注册**（LLVM LinkAllPasses.h 的 getenv 防死链 hack = 静态链接丢对象的一手铁证）与 CMake 生成 manifest（4 引擎过度设计）；CMake 与注册表零关系。
- **分层定案**：CommandTable（收敛 run_cli if/else 与 ffextractor do* 槽）→ EngineCatalog.resolve → FFTranscoder 现有通路不动；命令层出结构化结果（machineCode 映射退出码，预埋 Agent 复用）；顺带统一 GUI/CLI 两套模型路径解析（消 A6）。
- **processInto 形态定案**：bool + span 入参 + 复用 vector 出参 + 帧数出参；错误通道维持 bool+lastError()（现有约定）；expected 不进热路径、不跨 Qt 边界。
- **语言特性可用表（GCC 13.1/MinGW + -std=c++2b）**：expected(12.1)/monadic(13.1)/span(10.1)/jthread/format(13.1)/bit_cast 全就绪；ranges::to、std::print、flat_map = GCC 14/15 **不可用**。决定性证据：libstdc++ `<expected>` 整头包在 `__cplusplus > 202002L` 里——**用 expected 必须把 CXX_STANDARD 从 20 升 23**。MinGW 特有：atomic::wait 走 condvar 回退（与 §4.2 双重确认，离线无碍、实时化前实测）。
- **NFR-L2 定量**：process() 每块一次虚调用 + 单态调用点，开销占比 <1e-5，**CRTP 无必要**（Agner：误预测 ≥17 周期但预测命中≈免费）；逐样本内循环非虚（dspdenoiser.h:19）。std::function SBO 判据 = 平凡可复制+≤16B+对齐≤8：本仓库 [this] 捕获 8B 栈内零分配；捕获 QString 副本即堆分配。
- **CMake 修法**：D:/qt6 硬编码迁 gitignore 的 **CMakeUserPresets.json**（官方隐式 include 机制）；QT_BIN/MINGW_BIN 从目标推导；**新发现不一致：presets version 6 要求 CMake ≥3.25 但 cmake_minimum_required(3.21)**；file(GLOB) 官方明文不推荐——现有显式清单保持。

---

## 5. 现状做对了什么（保留清单——重构时不得丢）

1. **四层单向依赖、组合根干净**：main.cpp 构造注入，无全局单例、无循环依赖。
2. **FFTranscoder 单一通路**：规格适配集中两处 swr + `bypassOut` 直写优化；「引擎只声明规格」的思路正确（v2 是把它做完整，不是推翻）。
3. **失败路径零泄漏纪律**：do{}while(false) + 集中释放 + 失败删半成品，两文件全线一致。
4. **PIMPL 隔离 ORT/SoundTouch**：编译防火墙 + 可替换面。
5. **worker-object 线程模型正确**：moveToThread 而非继承 run；busy/cancelFlag 全程单线程（主）读写 + 原子跨线程，无数据竞争。
6. **协作式取消**：packet 粒度检查点，语义正确。
7. **引擎「缓冲自持 + flush 收尾」契约四引擎贯彻**——与 SoX flow/drain、JUCE processBlock 同构（§4.1 证实该契约即业界主流），当初设计直觉对。
8. 代码风格纪律一致（中文注释、kCamelCase、头文件职责注释）。
9. **静态核心 + QObject 薄壳双轨被业界同构确认**（Audacity delegate / Hydrogen CoreActionController / LMMS coreOnly，§4.3）——FR-24 不是补丁，是这条既有路线的延伸；busy/cancel 原子位用法也被 PortAudio 谱系确认为正确形态（§4.2）。

---

## 6. 架构演进建议（三档）

### 6.1 立即修（bug 级，不等任何重构；A11/A12 为一行到数行级）

1. **A1**：`feedAi` chunk 分支补 `drainEnhanced()`；验收 = 长音频（≥2h 素材或循环拼接）RSS 实测 ≤2GB（NFR-P4 复验）。
2. **A2**：引擎异常屏障（接口契约 + 引擎内 catch + runJob 兜底）；验收 = 构造 ORT 运行期错误注入，进程不崩、错误文案落 UI。
3. **A11**：`~MediaService` 先 `cancelFlag = true` 再 quit/wait。
4. **A12**：copy_stream 补取消检查点（验收：提取任务中取消，UI 即时恢复）。
5. **A14**：全接口实现补 `override` 关键字（编译器即查即验）。

### 6.2 近期（M1 窗口，与 parity/FFT 重写并轨）

6. **IStreamEngine v2**（§4.1 R1–R8 + §4.2 八条约束，两组独立调研收敛）：processInto（bool + span 入参 + 复用 vector 出参 + 帧数出参，§4.4 定案）/drainInto 循环到零/prepare 显式协商/latencyFrames/maxOutPerCall；先加并行接口保留旧接口，引擎随 parity 工作逐个迁移；**取消不入签名**（离线由 FFTranscoder 检查点承担，实时由队列丢弃承担）。
7. **引擎注册表 + 无头命令层**（NFR-M1 + FR-24，形态定案见 §4.4）：CommandTable 收敛 run_cli 与 do* 槽 → EngineCatalog.resolve（LLVM 式显式组合根注册，否决静态自注册）；统一 setup 参数集与模型路径解析（顺带消 A4/A6）；CLI 率先改走命令层。Qt 侧按 qt-audio-apps 执行顺序 S1–S5→A1→A3→A2/A4 跟进（三态结果、jobId 单一信号、FIFO 串行）。
8. **FFT 重写**（WP-1.3，数据支撑已齐）：pow-2 直通 radix-2 + twiddle 预计算表 +（Bluestein 保留用于 6144/7680/12288）——§2.1 表即验收基线之一。
9. **CMake 规范化**（并入 NFR-M4）：硬编码路径迁 CMakeUserPresets.json、QT_BIN/MINGW_BIN 目标推导、presets/minimum_required 版本对齐；**C++ 标准决策**：v2 接口若用 expected 须升 CXX_STANDARD 23（§4.4 证据），随 v2 定稿一并定。

### 6.3 实时化前（M4 闸门联动，§4.2 已可定稿的约束）

10. 双线程模型按 pablio 先例：回调只碰 SPSC 环形缓冲 + 欠载补零 + xrun 计数；处理线程挂 MMCSS "Pro Audio"；队列深度 = 2×chunk 取 2 的幂。
11. RT 侧同步白名单：atomic load + SPSC push/pop + 写预分配内存——**禁** mutex/condvar/`atomic::wait`/notify（MinGW condvar 回退推论）；JUCE AbstractFifo（AGPL）禁用，SPSC 自研或 rigtorp（MIT）/readerwriterqueue（BSD-2）。
12. 预算分解执行：软件预算 60–110ms、级数×chunk 累加核算；RVC chunk 96ms 超支裁决与注入法工具（回采互相关）归 O3 实测。

---

## 7. 基线文档联动（待用户确认后落）

- REQUIREMENTS v1.3 建议：风险表新增 A1（已发生级：NFR-P4 当前不达标）与 A2/A11；§6 P0 债清单同步（CLAUDE.md 同）；§6 红线表注记「JUCE AbstractFifo 等 AGPL 组件禁用」（队列选型时易踩）。
- v1-development-plan：M1 工作包建议增补「A1/A2/A11/A12 热修」前置项（不阻塞 parity 主线，但 A1 应先于任何长音频验收）；NFR-M1 注册表与 FR-24 命令层的实现形态引用 §4.4 定案。

## 8. 盲点与未收敛项

- A9（WIN32 CLI 输出）待 Windows 实测；A11（关窗假死）为机制推导，未真机复现。
- MinGW `atomic::wait` condvar 回退为排除法推断（源码级，未 Windows 实测断言）；Timur Doumler 一手文本未取得（以 Bencina/PortAudio/JUCE 覆盖）；Audacity 4 重构版未调研（官方 master 已换架构）；ORT `Run` 预分配下是否零分配未取证（NFR-L2 首个验证点）；RVC chunk 下限取决于 ContentVec 感受野（归 O3/需求侧）。
- FFT 耗时为 g++15/Linux 指示值，MinGW 13.1/Win 数字待 Windows 工具链复核。
- 本机编译证据未覆盖 ORT/SoundTouch/FFmpeg 链路（third_party 不入 Linux 环境）——引擎运行时行为（含 A2 的真实触发概率）只能 Windows 实测或读码推定。
- **代理断言复核记录（工作原则 13/14）**：cpp-engine-patterns 代理断言 vocalseparator.h:17「非虚遮蔽基类虚函数」——主会话独立比对签名后**推翻**（同签名=隐式虚覆写，运行时正确），降级为 A14 卫生项。其余代理载荷性断言抽查（A11 的 mediaservice.cpp:17-18、A1 的 fftranscoder 调用点）均与源码一致。
