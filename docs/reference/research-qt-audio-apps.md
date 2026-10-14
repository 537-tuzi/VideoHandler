# Qt 大型音频桌面应用的服务层-UI 层架构实践调研（Mixxx / LMMS / Hydrogen / Audacity 3.x / Qt 官方）

## 0. 元信息

- 调研日期：2026-10-14
- 任务：VideoHandler 架构审视的外部调研——对照业界 Qt 音频桌面应用，评估本项目现有链路「UI→信号槽→MediaService→QThread worker→FFExtractor/FFTranscoder」哪些是惯用、哪些有隐患，并给出分级改进建议。
- 服务落点：`docs/spec/architecture-review.md`（架构审视主文档，本调研为其外部输入）+ REQUIREMENTS **FR-24 无头命令层**（GUI 与未来 Agent 共用的命令抽象）；建议项同时关联 NFR-M1（引擎注册表）与离线闭环里程碑（批处理）。
- 信源原则：一手信源优先——应用侧读 GitHub 官方仓库源码原文（Mixxx `main`、LMMS `master`、Hydrogen `master`、Audacity `audacity3` 分支），Qt 结论以 doc.qt.io 官方文档逐字核验；每条标【已确认】（源码/文档逐字读到）/【倾向】（基于一手证据的合理推断）/【未确认】。行号均为 2026-10-14 快照，会随后续提交漂移。
- 方法：本仓库 4 组源码精读（`main.cpp`、`view/mainwidget.{h,cpp}`、`service/mediaservice.{h,cpp}`、`service/ffextractor.{h,cpp}`，另读 `fftranscoder.{h,cpp}`、`streamengine.h`、`model/data.h` 补全链路）+ 3 路并行外部源码调研（Mixxx；LMMS+Hydrogen；Audacity）+ Qt 官方文档 17 页核验。
- 分支勘误（影响取证路径的诚实声明，均为各路代理实测）：
  - Mixxx `main` 已无 `src/mixxx.cpp`（职责拆为 `src/coreservices.{h,cpp}` + `src/mixxxmainwindow.cpp` + `main.cpp`）、无 AnalyserQueue（被 `TrackAnalysisScheduler` + `AnalyzerThread` 取代）、`src/library/sources/*` 已移至 `src/sources/*`、无独立 EngineChain 基类文件【已确认】。
  - LMMS `master` 的音频引擎已由旧 `Mixer` 拆分改名：引擎 = `AudioEngine`（`include/AudioEngine.h`），`Mixer` 仅剩调音台；`MixerWorkerThread` 已改名 `AudioEngineWorkerThread`【已确认】。
  - Hydrogen 的 AudioEngine 在 `src/core/AudioEngine/` 子目录；`src/core/EventQueue.{h,cpp}` 存在，`QueuedEventQueue` 不存在【已确认】。
  - Audacity 官方仓库默认 `master` 已是 Audacity 4 全重构（README 明示应从 `audacity3` 分支开工），本调研取证全部取自官方维护的 **`audacity3`** 分支【已确认】。

---

## 1. 本仓库现状基线（引用带行号）

链路全貌：

```
main.cpp:110-122（组合根）
  ├─ argc>=3 → run_cli()：免 QApplication，直接调 FFExtractor 静态核心 / FFTranscoder（main.cpp:111-113）
  └─ GUI：QApplication → unique_ptr<MediaService>（先构造，:115）→ MainWidget w（后构造，:116）
          → w.setMediaService(mediaService.get())（setter 注入，:118）→ a.exec()
UI 层  MainWidget（src/view/mainwidget.{h,cpp}，view+controller 双角色）
  ├─ init_connect()：8 个按钮 clicked → 本窗口 doXxx 槽（mainwidget.cpp:96-106）
  ├─ setMediaService()：service 3 信号 → 本窗口槽（mainwidget.cpp:108-115）
  ├─ doXxx：校验 → set_busy(true)（锁按钮+显进度条）→ media->xxx() 提交（mainwidget.cpp:141-197）
  └─ onJobDone：set_busy(false) + 状态文案 + 成功弹窗/失败警告（mainwidget.cpp:207-213）
服务层 MediaService（src/service/mediaservice.{h,cpp}，住主线程）
  ├─ 构造：thread = new QThread(this)；worker = new FFExtractor（无父）→ setCancelFlag(&cancelFlag)
  │        → worker->moveToThread(thread) → thread->start()（mediaservice.cpp:6-14）
  ├─ 提交槽：if (busy) return;（静默拒绝）→ busy=true → cancelFlag=false → emit xxxRequested(...)（mediaservice.cpp:47-87）
  ├─ cancelJob()：cancelFlag = true（mediaservice.cpp:39-41）
  ├─ init_connect：7 对「请求信号→worker 槽」；worker progress 直转发；jobDone lambda 里复位 busy/cancelFlag（mediaservice.cpp:22-37）
  └─ 析构：thread->quit(); thread->wait(); delete worker;（mediaservice.cpp:16-20）
执行层 FFExtractor（src/service/ffextractor.{h,cpp}，住后台线程）
  ├─ doXxx 槽：per-job 构造引擎对象（DspDenoiser/AiDenoiser/...）→ runJob() → FFTranscoder（ffextractor.cpp:213-283）
  ├─ 静态核心 probe/extractAudio/extractVideo：纯函数，CLI 复用（ffextractor.cpp:123-205）
  └─ copy_stream()：流复制搬运 + 每包进度（ffextractor.cpp:70-98，无取消检查点）
转码层 FFTranscoder（src/service/fftranscoder.cpp）
  └─ 解码→swr→引擎→swr→WAV；按秒分块（chunk = engine.inRate()，:154）；取消检查点在每块边界（:204-207）；进度每块一次（:198-199）
```

对本调研关键的事实清单（后文对照用，全部本仓库实测【已确认】）：

| # | 事实 | 出处 |
|---|---|---|
| F1 | worker-object 模式（moveToThread，未继承 QThread 重写 run） | mediaservice.cpp:8-13 |
| F2 | 一次一任务 + `bool busy` 静默拒绝（`if (busy) return;`） | mediaservice.cpp:47-52 等 7 处 |
| F3 | `probe()` 无 busy 守卫，与任务共用同一 worker 事件队列 | mediaservice.cpp:43-45、25 |
| F4 | 取消 = `std::atomic<bool> cancelFlag`，仅转码路径检查；流复制 copy_stream 无检查点 → extractAudio/extractVideo 不可取消 | mediaservice.cpp:39-41；ffextractor.cpp:70-98、217；fftranscoder.cpp:204-207 |
| F5 | 关窗 → exec 返回 → ~MediaService `wait()` 阻塞至当前任务自然跑完；关停无人置 cancelFlag | main.cpp:114-122；mediaservice.cpp:16-20 |
| F6 | 进度粒度：转码每 1 秒音频块 1 次；流复制每包 1 次；上限钉在 99 | fftranscoder.cpp:154、198-199；ffextractor.cpp:89-92 |
| F7 | 结果协议：`jobFinished(bool ok, QString)` 全局广播，无任务 id | mediaservice.h:33 |
| F8 | 每操作一对请求信号 + 一对槽（现为 7 对） | mediaservice.h:25-31、35-42 |
| F9 | UI 双份状态：service `busy` + 窗口 `set_busy`（按钮态），靠约定同步 | mediaservice.cpp:47-52；mainwidget.cpp:215-227 |
| F10 | 组合根在 main()；服务先于窗口构造，但经 setter 注入（构造注入本可行）；`media` 可空 → 各槽防御 `if (!media)` | main.cpp:115-118；mainwidget.h:20、25；mainwidget.cpp:141 等 |
| F11 | 引擎对象 per-job 构造、任务间零共享可变状态 | ffextractor.cpp:223-269 |
| F12 | FFExtractor 双轨：QObject 薄壳（槽）+ 静态纯函数核心（CLI 复用） | ffextractor.h:10-19 |
| F13 | setMediaService 重复调用会重复 connect（无 UniqueConnection/断开旧连接） | mainwidget.cpp:108-115 |
| F14 | 信号载荷均为 Qt 内建类型（int/bool/QString），跨线程 queued 无需 qRegisterMetaType | mediaservice.h:32-34 |

---

## 2. Mixxx（Qt 音频应用旗舰参照，`main` 分支）

### 2.1 Engine/Effects 链：双对象镜像 + 无锁消息管道【已确认】

- 架构：主线程侧 `EffectChain`（QObject，UI 可触达）与音频线程侧 `EngineEffectChain` 一一配对，只经 `EffectsRequest/EffectsResponse` 管道通信。头文件直接把线程契约写进接口注释：`src/engine/effects/engineeffectchain.h:16-24`（"EngineEffectChain is the audio thread counterpart of EffectChain... processes a list of EngineEffects in series"），构造/析构标注 `called from main thread`、`process` 标注 `called from audio thread`。
  https://github.com/mixxxdj/mixxx/blob/main/src/engine/effects/engineeffectchain.h
- 链内严格串行 + 双缓冲乒乓：`src/engine/effects/engineeffectchain.cpp:236-243、279-280`（for 循环 + buffer1/buffer2 交替）。
  https://github.com/mixxxdj/mixxx/blob/main/src/engine/effects/engineeffectchain.cpp
- 全部处理发生在音频回调线程（PortAudio 回调，线程被命名 "Engine"）：`src/soundio/soundmanager.cpp:559-563` → `src/engine/enginemixer.cpp:362-382`；效果链处理一条不漏地在该线程内完成。音频回调里的慢活（如波形预处理）显式外移到「回调间隙」调度线程：`src/engine/engineworker.h:1-6`（注释原文）+ `src/engine/engineworkerscheduler.h:11`（`EngineWorkerScheduler : public QThread`）。
  https://github.com/mixxxdj/mixxx/blob/main/src/engine/enginemixer.cpp
- **启示（对普适桌面音频应用）**：跨线程边界用「镜像对象 + 消息/快照」替代共享可变状态；线程契约写进接口注释并全库一致执行；回调线程内不进事件循环、不做慢活。

### 2.2 SoundSource 解码抽象：声明式注册表 + open 三态【已确认】

- 三层解耦：`SoundSource`（`src/sources/soundsource.h:10-14`，继承 AudioSource：RAII 隐式 open/close、拉取式分帧读取）→ `SoundSourceProvider`（`src/sources/soundsourceprovider.h:13-31`：声明支持扩展名 + 5 档优先级，注释明确「实现必须线程安全」）→ `SoundSourceProviderRegistry`（`src/sources/soundsourceproviderregistry.h:86-88`：扩展名 → 按优先级排序的 provider 列表）。`SoundSourceProxy` 短生命周期门面按序尝试，`src/sources/soundsourceproxy.cpp:884-948` 两轮回退（Strict→Permissive）。
- 关键细节：open 结果三态 `Succeeded/Failed/Aborted`（`src/sources/audiosource.h:165-187`）——Failed=文件本身坏（别再试别的 provider）、Aborted=此 provider 打不开（给低优先级让路），这是优雅回退不被滥用为无谓重试的语义基石。注册在启动时一次性完成（`SoundSourceProxy::registerProviders()`，注释 "must be called only once upon startup"）。
  https://github.com/mixxxdj/mixxx/blob/main/src/sources/soundsourceproviderregistry.h
- **启示**：多解码器/多引擎共存的关键是「自描述（扩展名/能力 + 优先级）+ 集中注册表 + open 三态」——新增格式零侵入。直接对标本项目 NFR-M1 引擎注册表。

### 2.3 WorkerThread / 分析任务池：无事件循环 worker + 调度器两级限频【已确认】

- `src/util/workerthread.h:12-27` 设计注释直译要点：「无事件循环的 QThread 模板；宿主→worker 方向用无锁类型（避免优先级反转锁住宿主线程）；worker→宿主方向用 queued signal」。`run()` 被 `final` 封死（:83），子类实现 `doRun()` 与非阻塞 `tryFetchWorkItems()`；同步 = `std::mutex + std::condition_variable + std::atomic suspend/stop`（:142-146）。
  https://github.com/mixxxdj/mixxx/blob/main/src/util/workerthread.h
- 现行分析任务模型 = 「GUI 线程 QObject 调度器 + N 个常驻 worker」：`src/analyzer/trackanalysisscheduler.h:38-168`（构造收 `numWorkerThreads`；`std::vector<Worker>` + `std::deque<AnalyzerScheduledTrack>` FIFO 队列 + `std::set<TrackId>` 在途集合；worker 低优先级、启动即挂起，空闲才被派活（拉模式）；suspend/resume/stop 覆盖全 worker；并发上限 = worker 数组大小，队列纯 FIFO 无任务级优先级）。
  https://github.com/mixxxdj/mixxx/blob/main/src/analyzer/trackanalysisscheduler.cpp
- 进度协议两个关键设计【已确认】：
  1. **单一 progress 信号保序**：`src/analyzer/analyzerthread.h:82-89` 注释——"Different signals would otherwise end up in different queued connections which are processed in an undefined order!"（不同信号会落进不同 queued connection，处理顺序未定义）。
  2. **两级限频**：worker 侧发前限频（同文件 39-46 行注释）+ 调度器侧 100ms 抑制窗（`trackanalysisscheduler.cpp:16` `kProgressInhibitDuration(100)`，`finished()` 不受限流约束）。
- **启示**：后台线程可不用 Qt 事件循环（显式 run 循环 + SPSC 队列 + 单一信号回报，时序完全可控）；进度限频是任务池最常被忽略的坑；固定 worker 数 + FIFO 队列 + 空闲拉取比通用线程池简单一个量级。

### 2.4 ControlObject/ControlProxy：控制平面与 UI 解耦【已确认】

- 三层：`ControlDoublePrivate`（真值载体：wait-free 环形缓冲（`src/control/controlvalue.h:76-80` 注释 "sacrifice perfect consistency for the benefit of wait-free access"）+ 全局 ConfigKey 注册表 + `valueChanged(value, pSender)` 信号带写者身份）→ `ControlObject`（引擎侧包装）→ `ControlProxy`（任意线程轻量按值句柄，`src/control/controlproxy.h:111-153` 每个 get/set 标 "Thread safe, non-blocking"；`slotValueChanged` 统一 `if (pSetter != this)` 防回环）。
  https://github.com/mixxxdj/mixxx/blob/main/src/control/controlproxy.h
- UI 部件不 include 任何 engine 头：`src/widget/wpushbutton.cpp:378-389` 鼠标事件只改控制参数；桥梁是皮肤解析时创建的 `ControlParameterWidgetConnection`（`src/widget/controlwidgetconnection.h:39-42`，`parented_ptr<ControlProxy>` 挂对象树，同线程创建/销毁防 pending queued signal 悬垂——头注释引用 Issue #7773）。
  https://github.com/mixxxdj/mixxx/blob/main/src/widget/controlwidgetconnection.h
- **启示**：「key 注册表 + 原子真值 + 信号」中间件让 UI/皮肤/脚本/控制器/引擎五方互不 include、互不持锁地读写同一控制量——这是「无头命令层（FR-24）」的成熟参照：命令层只发 Request/控制量，不触碰引擎对象。

### 2.5 组合根：CoreServices 两段式 + 构造注入【已确认】

- `src/main.cpp:60-129`：`auto pCoreServices = std::make_shared<mixxx::CoreServices>(...)` → 作用域注释强制「窗口先死于服务」（92-95 行："This scope ensures that MixxxMainWindow is destroyed *before* CoreServices is shut down"）→ `mainWindow(pCoreServices)` **构造注入** → `pCoreServices->initialize(pApp)` 重装配 → 同一组合根可喂 Widgets/QML 两套 UI（83 行）。
  https://github.com/mixxxdj/mixxx/blob/main/src/main.cpp
- 两段式初始化：构造函数只做轻量初始化（`src/coreservices.cpp:361-369` 注释 "Defer long initializations to CoreServices::initialize() which is called after the GUI is initialized"），重装配按依赖顺序进行（registerProviders → DB → EffectsManager → EngineMixer → SoundManager → PlayerManager → Library），边装配边发 `initializationProgressUpdate(0..50)` 驱动启动画面。
  https://github.com/mixxxdj/mixxx/blob/main/src/coreservices.cpp
- 诚实面对混合态：新架构以注入为主，但仍残留 `PlayerInfo::create()` / `CoverArtCache::createInstance()` 等历史单例（`coreservices.cpp:575、614-615`）——增量重构不必推倒重来。
- **启示**：构造注入 + 唯一装配根 = 可测试、可换 UI；「构造轻、initialize 重」缩短感知启动时间；销毁顺序用作用域强制并写注释。

---

## 3. LMMS 与 Hydrogen（Qt 音乐应用：线程模型与单例/注入取舍）

### 3.1 LMMS：Engine 单例枢纽 + 回调拉动的引擎 + job 化并行【已确认】

- **Engine = 静态指针仓库**：`include/Engine.h:59-94、135-139` 全静态 getter（audioEngine/mixer/song/...），所有权是 Engine 的静态裸指针；init 顺序手写（`src/core/Engine.cpp:69-89`），销毁手写逆序且 deleteHelper 先置空再 delete 防悬垂（:104-119）。销毁顺序是真实坑：`src/gui/MainWindow.cpp:253-259` 注释直书依赖编辑器先死于 Song（引 GitHub issue #2015）。
  https://github.com/LMMS/lmms/blob/master/include/Engine.h
- **引擎不是线程**：`AudioEngine` 本体是 QObject（非 QThread），渲染函数在音频设备回调线程被执行（`src/core/audio/AudioPortAudio.cpp:191-204`：PortAudio 回调内直接调 `renderNextBuffer`）。离线渲染不复制引擎，只换「拉它的线程」：`src/core/ProjectRenderer.cpp:80、142-179`（QThread::run 循环拉同一 `renderNextPeriod()` + 写文件）。
  https://github.com/LMMS/lmms/blob/master/src/core/ProjectRenderer.cpp
- **UI→引擎边界**：结构性修改走大递归锁 `requestChangeInModel()`（`src/core/AudioEngine.cpp:567-577`；渲染线程整周期持锁 :318-320 + `thread_local s_renderingThread` 自免锁）；高频路径走无锁链表 `LocklessList<PlayHandle*>`；引擎→UI 走 Qt 信号（音频线程 emit `nextAudioBuffer`，跨线程自动 queued）。铁律：UI 可阻塞等音频周期，音频线程永不等待 UI。
  https://github.com/LMMS/lmms/blob/master/src/core/AudioEngine.cpp
- **job 化并行**：`include/AudioEngineWorkerThread.h:45-117` 全局静态 `JobQueue`（原子数组 + writeIndex/itemsDone，JOB_QUEUE_SIZE 8192）+ N 个常驻 QThread（`AudioEngine.cpp:82、105-113`：`idealThreadCount()-1` 个、TimeCriticalPriority）；job 用原子状态机保证恰好执行一次（`include/ThreadableJob.h:72-80` compare_exchange）；最后一个 worker 不启动、由音频线程内联执行以降延迟（`AudioEngineWorkerThread.cpp:146-154` 注释）。
  https://github.com/LMMS/lmms/blob/master/include/AudioEngineWorkerThread.h
- **无头分支**：`src/core/main.cpp:368-370` `coreOnly ? new QCoreApplication : new gui::MainApplication`；`:710-758` renderOnly 模式 `Engine::init(true)` + RenderManager，完全不建 GUI。
  https://github.com/LMMS/lmms/blob/master/src/core/main.cpp

### 3.2 Hydrogen：分层单例 + timed_mutex + 引擎→UI 事件队列轮询【已确认】

- `Hydrogen::get_instance()` 总单例（`src/core/Hydrogen.h:132-136`），裸指针持有 AudioEngine（`Hydrogen.cpp:128`）；分层 create_instance 手写顺序 + 防双重创建抛异常（:115-118、171-187）。core 对 GUI 的感知收敛为一个状态枚举 `GUIState { startup, headless, ready, shutdown }`（`Hydrogen.h:113-125`）——core 不依赖 GUI 但知道其状态。
  https://github.com/hydrogen-music/hydrogen/blob/master/src/core/Hydrogen.h
- **实时线程与锁**：引擎本体非线程，`audioEngine_process` 跑在驱动回调线程（JACK `JackDriver.cpp:1108-1109` / PortAudio `PortAudioDriver.cpp:49`）；锁 = 一把 `std::timed_mutex` 大锁 + 文档化锁序；回调侧按时间预算 `tryLockFor`，拿不到记 ERROR 丢 buffer（`src/core/AudioEngine/AudioEngine.cpp:1763-1798`，为修 Bug #164 死锁引入）；UI 动作无 command 队列——UI 线程直接抢同一把锁，或置意图标志位下个回调周期生效（`AudioEngine.h:353-368` play/stop 注释）。
  https://github.com/hydrogen-music/hydrogen/blob/master/src/core/AudioEngine/AudioEngine.cpp
- **引擎→UI 解耦 = 事件队列 + 20Hz 轮询**：音频线程向 `EventQueue`（互斥 deque 环，满 1024 丢最旧；GUI 未就绪时静默丢弃）push 事件；UI 侧 `HydrogenApp` 用 50ms QTimer（=20 次/秒，`src/gui/src/HydrogenApp.h:46` `QUEUE_TIMER_PERIOD = 50` + 类文档注释明写频率）轮询 `popEvent` 分发给注册的 EventListener。队列只承载引擎→UI 单向通知。
  https://github.com/hydrogen-music/hydrogen/blob/master/src/core/EventQueue.h
- **CoreActionController = 无头命令层现成范本**：全静态方法的「核心动作门面」（`src/core/CoreActionController.h:45`），GUI/OSC/MIDI/NSM 共用同一动作层，需要一致性的动作内部显式拿引擎锁——多前端复用 core 而不重复实现动作。
  https://github.com/hydrogen-music/hydrogen/blob/master/src/core/CoreActionController.h

### 3.3 两家的普适教训

1. **引擎不是线程，是被拉动的状态机**：把处理核心写成「给定输入帧→产出输出帧」的纯流式函数（本项目 `IStreamEngine` 正是此形态，`src/service/streamengine.h:7-16`），实时与离线自然同源；不要把引擎生命周期绑在某条线程上。
2. **音频线程永不等待 UI**：宁可丢一个 buffer/丢最旧事件。离线批处理无实时约束，此纪律弱相关，但「宁可晚到不可阻塞」的队列设计同样适用于 UI 进度回报。
3. **中央 singleton 是「低成本高顺序债」**：LMMS Engine 让任何代码可达一切，代价是 init/destroy 顺序全靠手写且已出过 bug（issue #2015）；本项目现有 MediaService 显式注入的组合根优于引入全局单例，不必学它们。

---

## 4. Audacity 3.x（`audacity3` 分支）：任务/进度/取消的模式

### 4.1 无通用线程池：点状线程 + future【已确认】

`libraries/lib-utility/` 全目录与全仓 8193 文件树中无 ThreadPool/任务队列命名的抽象（文件名级确认）；后台工作 = 各处按需一次性 `std::thread`/`std::future`（如云同步哈希 `libraries/lib-cloud-audiocom/sync/BlockHasher.cpp:34-81`：`hardware_concurrency()/2` 条带分片 + `std::async` + future 收集）。专用小件：`CancellationContext`（atomic bool + weak_ptr 广播 Cancel，`libraries/lib-concurrency/concurrency/CancellationContext.h:21-47`）、SPSC `LockFreeQueue`、双槽丢旧 `MessageBuffer`。
https://github.com/audacity/audacity/blob/audacity3/libraries/lib-cloud-audiocom/sync/BlockHasher.cpp

### 4.2 进度/取消约定：Poll 协议 + 三态结果【已确认】

- 抽象接口 `BasicUI::ProgressDialog::Poll(分子, 分母)` **只在主线程调用**、返回 `ProgressResult`（`libraries/lib-basic-ui/BasicUI.h:154-182`，注释 "Call only on the main thread"）。枚举语义三分是精髓：
  ```cpp
  enum class ProgressResult : unsigned {
     Cancelled = 0, // 用户说这事根本不该发生 → 回滚丢弃
     Success,       // 继续
     Failed,        // 出错，停止并撤销
     Stopped        // 没错，但用户说停下 → 保留已完成部分
  };
  ```
  https://github.com/audacity/audacity/blob/audacity3/libraries/lib-basic-ui/BasicUI.h
- 效果引擎在每个音频块边界轮询：`libraries/lib-effects/Effect.cpp:335-359` `TotalProgress/TrackProgress` 把 Poll 结果 != Success 统一折叠为 bool「要求停」；取消谓词逐层上传出循环（`libraries/lib-effects/PerTrackEffect.cpp:234-252` → `libraries/lib-audio-graph/AudioGraphTask.cpp:29-39` RunLoop）。完全协作式，粒度 = 每采样块。
- **无 UI 自动降级**：`mProgress` 是 RAII 作用域内临时注入的裸指针（`EffectBase.h:118` "Temporary pointer, NOT deleted"；`EffectBase.cpp:190-195` valueRestorer），`mProgress==nullptr` 时 Poll 短路成 Success——CLI/无头场景进度调用自动变 no-op。
  https://github.com/audacity/audacity/blob/audacity3/libraries/lib-effects/Effect.cpp

### 4.3 导入/导出的不对称线程模型【已确认】

- 导入：主线程同步执行，进度回调里直接 Poll，Cancelled→调 `ImportFileHandle::Cancel()` 置 bool 标志（`src/ProjectFileManager.cpp:1284-1348` + `libraries/lib-import-export/ImportPlugin.h:153-173`）。
- 导出：任务打包 `std::packaged_task<ExportResult(...)>` 丢裸 `std::thread`；**后台线程只写原子量**（`mProgress/mCancelled/mStopped`），UI 由主线程 50ms 轮询 future + `delegate.UpdateUI()` 驱动（`libraries/lib-import-export/ExportProgressUI.cpp:80-91、21-73`）；引擎侧只依赖纯虚 `ExportProcessorDelegate { IsCancelled()/IsStopped()/OnProgress(double) }`（`ExportPlugin.h:79-88`），异常经 packaged_task 存入 future、主线程统一翻译弹窗。
  https://github.com/audacity/audacity/blob/audacity3/libraries/lib-import-export/ExportProgressUI.cpp

### 4.4 后台线程 → UI：CallAfter + 弱引用防悬垂【已确认】

`BasicUI::CallAfter(Action)`（`BasicUI.h:252-257`，"This function may be called in other threads than the main"；Services 未装时进互斥回退队列不丢失，由 `Yield()` 冲刷）；惯用法是 lambda 捕 `weak_from_this()` 再 lock（`libraries/lib-project-file-io/ProjectFileIO.cpp:1637-1641`）。Qt 等价物 = `QMetaObject::invokeMethod(obj, lambda, Qt::QueuedConnection)` + `QPointer`。
https://github.com/audacity/audacity/blob/audacity3/libraries/lib-basic-ui/BasicUI.h

### 4.5 不定时长慢 IO：Pulse 忙指示 + 50ms 轮询【已确认】

大文件移动/重开数据库等无法估算进度的操作：临时 `std::thread` + `std::atomic_bool done`，主线程 50ms `GenericProgressDialog::Pulse()`（Qt 等价 `QProgressBar::setRange(0,0)`）（`ProjectFileIO.cpp:1304-1337、2280-2305`；源码注释自陈这是点状打线程而非通用模式）。

### 4.6 对本项目的启示

1. 进度接口收敛为「Poll(num,den) 返回枚举」且**区分 Cancelled（丢弃）/ Stopped（保留）**——批处理场景「停止但保留已处理文件」是真需求，单一 bool 表达不了。
2. 「引擎只认 delegate 接口 + 原子量，UI 定时轮询」是 Qt 下最省心的模型之一；与信号槽二选一即可，但**异常不要在 worker 线程弹窗**，回主线程统一翻译（本项目现状即如此：worker 只 emit jobDone，弹窗在主槽——正确【已确认】）。
3. 「接口注入 + nullptr 静默降级」让同一引擎核心天然兼容 GUI/CLI 双形态——与本项目 F12 双轨同构，可沿此方向固化 FR-24。

---

## 5. Qt 官方文档核验（doc.qt.io，Qt 6，逐字摘录）

| # | 论点 | 官方原文摘录 | 出处 |
|---|---|---|---|
| Q1 | worker-object（moveToThread）是官方推荐的跨线程调槽方式；继承 QThread 只是「另一种方式」 | "You can use worker objects by moving them to the thread using QObject::moveToThread()."；"a developer who wishes to invoke slots in the new thread must use the worker-object approach"；"new slots should not be implemented directly into a subclassed QThread." | [QThread](https://doc.qt.io/qt-6/qthread.html)【已确认】 |
| Q2 | QThread 实例住创建它的线程，不是 run() 的线程 | "a QThread instance lives in the old thread that instantiated it, not in the new thread that calls run()" | 同上【已确认】 |
| Q3 | 默认 run() 起事件循环 | "By default, run() starts the event loop by calling exec()" | 同上【已确认】 |
| Q4 | 线程安全销毁 | "connect the finished() signal to QObject::deleteLater(), to free objects in that thread."；"Deleting a running QThread ... will result in a program crash."；"Wait for the finished() signal before deleting the QThread." | 同上【已确认】 |
| Q5 | 协作式中断原语（替代手写 atomic 的标准化选择） | requestInterruption: "That request is advisory and it is up to code running on the thread to decide if and how it should act upon such request"；isInterruptionRequested: "it is advisable do so regularly in long running functions"、"Take care not to call it too often, to keep the overhead low." | 同上【已确认】 |
| Q6 | AutoConnection 在发射时刻按接收者线程判定 | "If the receiver lives in the thread that emits the signal, Qt::DirectConnection is used. Otherwise, Qt::QueuedConnection is used. The connection type is determined when the signal is emitted." | [Qt::ConnectionType](https://doc.qt.io/qt-6/qt.html#ConnectionType-enum)【已确认】 |
| Q7 | queued 连接参数须是元类型系统认识的类型（引将来 JobRequest 的坑） | "the parameters must be of types that are known to Qt's meta-object system"；遇 "Cannot queue arguments of type 'MyType'" 时 "calling qRegisterMetaType() before establishing the connection" | 同上【已确认】 |
| Q8 | BlockingQueued 同线程死锁 | "This connection must not be used if the receiver lives in the signalling thread, or else the application will deadlock." | 同上【已确认】 |
| Q9 | 函数指针 connect 编译期检查 vs 字符串 SIGNAL/SLOT 运行期检查 | "the compiler can help us detect type mismatches when using the function pointer-based syntax"；"The string-based SIGNAL and SLOT syntax will detect type mismatches at runtime." | [Signals & Slots](https://doc.qt.io/qt-6/signalsandslots.html)【已确认】 |
| Q10 | context object 决定 lambda 执行的事件循环与生命周期 | 连接被放进 "a specific event loop of *context*"；"The connection will automatically disconnect if the sender or the context is destroyed." | [QObject::connect](https://doc.qt.io/qt-6/qobject.html)【已确认】 |
| Q11 | 线程亲和：queued 槽在接收者线程执行 | "A QObject instance is said to have a thread affinity"；"the slot or event handler will run in the thread that the object lives in"；无亲和（thread()==nullptr）"it cannot receive queued signals or posted events." | 同上【已确认】 |
| Q12 | QObject 非线程安全；子对象须创建于父所在线程 | "QObject and all of its subclasses are not thread-safe"；"The child of a QObject must always be created in the thread where the parent was created." | [Threads and QObjects](https://doc.qt.io/qt-6/threads-qobject.html)【已确认】 |
| Q13 | 线程内对象删除纪律 | "You must ensure that all objects created in a thread are deleted before you delete the QThread." | 同上【已确认】 |
| Q14 | 发射信号线程安全 | "you can safely emit signals from your QThread::run() implementation, because signal emission is thread-safe" | 同上【已确认】 |
| Q15 | QThreadPool = 自动入队的复用线程集合 | start() 超限时 "runnable is added to a run queue instead"；"The thread pool will always use at least 1 thread, even if maxThreadCount limit is zero or negative."；clear() 移除未启动任务；waitForDone(msecs) 等待；默认 expiryTimeout 30s；"All functions in this class are thread-safe." | [QThreadPool](https://doc.qt.io/qt-6/qthreadpool.html)【已确认】 |
| Q16 | QtConcurrent 定位与短板 | "high-level APIs that make it possible to write multi-threaded programs without using low-level threading primitives"；run() "uses one thread only, cannot be paused/resumed/canceled, and cannot report progress"（选型对照表原文）；"The thread is taken from the global QThreadPool." | [Multithreading Technologies](https://doc.qt.io/qt-6/threads-technologies.html)、[QtConcurrent](https://doc.qt.io/qt-6/qtconcurrent.html)【已确认】 |
| Q17 | QFuture::cancel 是异步、协作式；run() 的 future 不可取消 | "Note that the cancellation is asynchronous."；"Be aware that the computation may still be running even though this function returns true."（isCanceled）；"the future returned by QtConcurrent::run() cannot be canceled; but the future returned by QtConcurrent::mappedReduced() can" | [QFuture](https://doc.qt.io/qt-6/qfuture.html)【已确认】 |
| Q18 | QPromise 状态机与协作取消 | 取消需 worker 侧轮询："if (promise.isCanceled()) // support cancellation"；析构 "implicitly transitions to a canceled state unless finish() is called"；setProgressValue 只能递增、canceled/finished 态下 setProgressValueAndText 无效 | [QPromise](https://doc.qt.io/qt-6/qpromise.html)【已确认】 |
| Q19 | 官方进度限频先例 | "In order to avoid overloading the GUI event loop, QFutureWatcher limits the progress signal emission rate." | [QFutureWatcher](https://doc.qt.io/qt-6/qfuturewatcher.html)【已确认】 |
| Q20 | invokeMethod 免信号跨线程调用（信号-per-操作的替代） | "Invokes the function with arguments in the event loop of context."；"Since Qt 6.5, this function automatically registers the types being used"；"This function is thread-safe." | [QMetaObject](https://doc.qt.io/qt-6/qmetaobject.html)【已确认】 |
| Q21 | QPointer 防悬挂 | "guarded pointers to QObject"；"automatically cleared when the referenced object is destroyed"；"Use a QPointer when you are storing a pointer over time."（本仓库 UI 存 `media` 裸指针属「存指针过时间」场景） | [QPointer](https://doc.qt.io/qt-6/qpointer.html)【已确认】 |
| Q22 | QStringBuilder 全局启用 | 定义 `QT_USE_QSTRINGBUILDER` 后 "concatenating strings with '+' work the same way as QStringBuilder's '%'"；"multiple substring concatenations will be postponed until the final result is about to be assigned"（多次 '+' 否则 "may lead to multiple memory allocations"） | [QString](https://doc.qt.io/qt-6/qstring.html#more-efficient-string-construction)【已确认】 |
| Q23 | Q_ENUM：枚举进元对象系统 | "This macro registers an enum type with the meta-object system."；"Registered enumerations are automatically registered also to the Qt meta type system"；QDebug 打印名字、 QVariant 可转字符串、免 Q_DECLARE_METATYPE | [QObject](https://doc.qt.io/qt-6/qobject.html)（Q_ENUM 节）【已确认】 |
| Q24 | 属性系统 vs 手写 getter/setter | "a NOTIFY signal must be specified to allow QML property bindings"；经名字访问 "lets you access classes you don't know about at compile time"（Designer/QML/动态属性场景），直接 WRITE 访问器 "is ... faster"——纯 C++ 内部调用手写 getter/setter 即可，属性系统价值在运行期发现与工具链 | [Property System](https://doc.qt.io/qt-6/properties.html)【已确认】 |
| Q25 | 父子所有权树 | "When you create a QObject with another object as parent, it's added to the parent's children() list and is deleted when the parent is."；"No QObject is deleted twice, regardless of the order of destruction." | [Object Trees](https://doc.qt.io/qt-6/objecttrees.html)【已确认】 |
| Q26 | 选型边界一句话 | "The right solution for a given application depends on the purpose of the new thread and the thread's lifetime."（常驻多功能对象 → worker QObject + moveToThread；一次性调用 → run/QRunnable；容器批量运算 → map/filter） | [Multithreading Technologies](https://doc.qt.io/qt-6/threads-technologies.html)【已确认】 |

组合根/构造注入的通行做法（无 DI 框架）：Qt 生态无主流 DI 框架，通行做法即「纯手工 DI」——在 main() 唯一装配点构造服务、构造注入给窗口（composition root 概念见 [Seemann 的 canonical 解释](https://stackoverflow.com/questions/6277771/what-is-a-composition-root-in-the-context-of-dependency-injection)）【已确认（概念）+【倾向】（Qt 生态无主流 DI 框架为行业共识，未见官方框架）】。Mixxx `main.cpp`/`CoreServices`（§2.5）是该做法在大型 Qt 音频应用的活例；LMMS/Hydrogen 则是反面参照（单例枢纽，§3.3）。

---

## 6. 核心问题对照结论

### 6.a 本项目链路对照 Mixxx 等：哪些惯用、哪些非标准/有隐患

**惯用（与业界/官方一致，保持）**：

1. **worker-object + moveToThread**（F1）——Qt 官方推荐形态（Q1），Mixxx WorkerThread/Audacity 同样「常驻 worker」。本项目未踩「继承 QThread 加槽」的经典坑（Q1 末句）。
2. **请求经跨线程 queued 信号投递、主线程不碰 FFmpeg**（mediaservice.cpp:44 注释）——AutoConnection 自动判定（Q6），worker 槽在 worker 线程执行（Q11）。本项目 12 处 connect 全部用函数指针语法（Q9 编译期检查），零字符串 SIGNAL/SLOT——**优于多数存量 Qt 代码**【已确认】。
3. **静态纯函数核心 + QObject 薄壳双轨，CLI/GUI 复用**（F12）——与 LMMS `coreOnly ? QCoreApplication : QApplication`（§3.1）、Audacity「delegate 空指针静默降级」（§4.2）同构，是 FR-24 无头命令层的正确雏形。
4. **引擎 per-job 构造、零共享可变状态**（F11）——比 LMMS/Hydrogen 的大锁模型简单且安全（它们因实时共享才付出锁的复杂度）；离线批处理下这是优点，应保持。
5. **jobDone lambda 以 `this` 为 context**（mediaservice.cpp:31-35）——lambda 在主线程执行、自动断连（Q10），busy 复位无竞争【已确认：busy 仅主线程读写】。
6. **worker 发射信号线程安全**（Q14），`QThread` 挂父对象、析构先 wait 再删 worker（mediaservice.cpp:16-20）符合 Q4/Q13 的纪律。
7. **进度粒度**（F6）：转码 1 次/秒音频、流复制 1 次/包，量级远低于信号洪泛阈值；Qt 官方 QFutureWatcher 也内置限频（Q19）说明该量级无需再节流【倾向：当前载荷 int 单值，无积压风险】。

**非标准/有隐患（按严重度排序）**：

| # | 问题 | 证据 | 业界对照 |
|---|---|---|---|
| H1 | **关窗假死**：exec 返回后 ~MediaService `wait()` 阻塞至任务自然跑完，关停路径无人置 cancelFlag；AI 降噪分钟级任务 = 无 UI 的进程挂起 | F5（main.cpp:114-122; mediaservice.cpp:16-20） | Mixxx 用作用域+注释显式管理销毁序（§2.5）；LMMS issue #2015 为销毁序真实翻车例（§3.1） |
| H2 | **取消覆盖不全**：extractAudio/extractVideo（流复制路径）无取消检查点，用户点「取消任务」无效果 | F4（ffextractor.cpp:70-98、271-283） | Audacity 取消谓词贯穿每个采样块循环（§4.2）；官方建议长循环 "regularly" 检查（Q5） |
| H3 | **静默拒绝协议**：`if (busy) return;` 调用方无从得知任务被丢弃；当前靠 UI 按钮禁用兜底（set_busy），是「约定」而非「机制」 | F2、F9 | Audacity 一切结果走显式枚举（§4.2）；Mixxx scheduler 队列永不拒绝只排队（§2.3） |
| H4 | **probe 不受 busy 守卫**：任务中 probe 会排队在 worker 事件队列，probeDone 迟于 jobFinished 到达，UI 状态机可能被迟到结果覆写；当前被按钮禁用掩盖 | F3 | 机制事实【已确认】（同一接收线程事件 FIFO）；实际干扰程度【倾向：轻】 |
| H5 | **结果全局广播无任务 id**：窗口增多后 jobFinished/progress 无法路由，每窗口收到一切 | F7 | Mixxx progress 信号带 TrackId、调度器带在途集合（§2.3）；QFutureWatcher 每任务一 watcher（Q19） |
| H6 | **信号-per-操作样板**：每新增操作 = 请求信号+槽+connect ×2 + CLI 分支 ×1；NFR-M1 引擎注册表后线性膨胀 | F8 | Mixxx EffectsRequest 类型化消息（§2.1）；Audacity ExportTask 打包（§4.3）；Qt invokeMethod（Q20） |
| H7 | **setter 注入 + 空指针防御**：服务先于窗口构造（main.cpp:115-116），构造注入本可行；现状 `if (!media)` 防御散布各槽 | F10 | Mixxx 主窗口构造注入 shared_ptr（§2.5）；Q21：UI 存 service 裸指针过时间可用 QPointer（本项目窗口不拥有服务、生命周期由 main 保证，裸指针可接受但构造注入可消除整个空态） |
| H8 | setMediaService 重复调用重复连接 | F13 | Q（UniqueConnection 仅对 PMF 生效——本项目连接均为 PMF，可用） |

### 6.b 「一次一任务 + busy 锁 + 取消原子量」对照：排队、重入、进度粒度

- **现状正确性**：`bool busy` 只在主线程读写（提交槽在主线程被调、jobDone lambda 经 queued 回主线程）→ 无数据竞争【已确认】；`std::atomic<bool> cancelFlag` 跨线程正确。**正确但表达力弱**：状态只有 Idle/Running 两态，无 Queued/Canceling/Draining，排队、关停、取消过渡期全靠隐式约定。
- **排队 vs 立即拒绝的业界谱系**：
  1. Mixxx TrackAnalysisScheduler：FIFO deque + 固定 N worker + suspend/resume + 在途集合——**排队不拒绝**，进度 100ms 限频（§2.3）。适合批量（曲库分析 = 本项目离线闭环批处理同型负载）。
  2. Audacity：无队列——导入在主线程同步+模态进度、导出单任务 packaged_thread（§4.3）——**一次一任务**与本项目现状同型，但它有显式三态结果 + 块级取消谓词补协议短板。
  3. Qt 自带：`QThreadPool` + `setMaxThreadCount(1)` = 免手写的串行队列（Q15：超限自动入 run queue、clear() 撤未启动任务、waitForDone() 收尾）【已确认】。
  - 结论：一次一任务对当前单文件工具是合理产品选择（与 Audacity 导出同型），**真正的缺口不在「没有队列」而在协议**（H1–H4）；离线闭环里程碑需要批处理时，Mixxx 调度器形态（QObject 调度器 + FIFO + 空闲拉取）是首选参照，QThreadPool(1) 是低代码替代。
- **重入**：现状靠 busy 拒绝 + UI 按钮禁用双保险（F2/F9）——防线在 UI 而非服务，违反「机制优先于约定」；probe 是唯一可重入路径（H4）。业界做法是把「能否提交」做成服务端显式契约（返回值/拒绝信号/队列化三选一）。
- **取消语义**：
  - 手写 atomic 与官方 `QThread::requestInterruption` 等价（同为 advisory 协作式，Q5）——但 requestInterruption 只对「当前线程」有意义，本项目取消标志挂在 service 级、跨多个任务类型共享，形态合理【倾向】。
  - 引入 QFuture/QPromise **不解决取消**：QFuture::cancel 异步且协作（Q17），且 `QtConcurrent::run()` 的 future 干脆不可取消（Q16/Q17）——若未来迁 QtConcurrent 需自备取消通道。
  - 表达力缺口：bool 只能表达「丢弃式取消」；Audacity 三分 Cancelled/Stopped/Failed（§4.2）对批处理（停止但保留已产出文件）是实打实的功能语义。
- **进度粒度**：现状 1 次/秒音频（转码）/1 次每包（流复制）安全（Q19 对照）；AI 推理 chunk 墙钟时间长导致进度阶梯感（每 chunk 一跳）——可接受，若细化可学 Mixxx 在 chunk 内按帧插值上报（不增加真实信息，仅平滑观感）【倾向：非必要】。**真正的粒度问题是取消粒度**：取消检查点 = 1 秒音频块（fftranscoder.cpp:204-207），MDX 推理单块墙钟可达数秒——取消响应延迟可观，应在 UI 文案或检查点上处理（H2 同修）。

### 6.c 信号槽转发层（MainWidget）在窗口增多后的退化风险与业界解法

- **现状**：MainWidget 一个类承担 view（build_ui）+ controller（doXxx 槽 + 校验）+ 转发汇（setMediaService 连 3 信号），connect 共 11 处（8 按钮 + 3 服务）。单窗口下无冗余、可读性好。
- **退化路径**（窗口增多 = 工作区视图/批量队列/设置/Agent 面板，均为 REQUIREMENTS 规划内）【倾向：机制推导，非实测】：
  1. `jobFinished`/`progress` 广播无 id（F7）→ 每个新窗口都被迫收全部任务结果，无法过滤，只能靠「当前只有一个任务」的隐式全局假设——该假设恰是「一次一任务」给的，一旦引入排队（6.b）即碎。
  2. 每窗口重复 connect 粘合代码线性增长；窗口与服务的连接时点分散在各自 setXxx 里，装配知识从 main() 泄漏到各窗口。
  3. `set_busy` 全局锁按钮的模式无法表达「任务 A 锁工作区、任务 B 只锁自己」的局部性。
- **业界解法谱系**：
  - **每任务订阅句柄**（QFutureWatcher，Q19）：任务对象化后每任务一个 watcher，窗口只看自己的任务——Qt 原生、粒度最细。
  - **key 寻址控制平面**（Mixxx ControlProxy，§2.4）：窗口按 key 订阅控制量，天然多窗口多写者。
  - **集中装配**（LMMS GuiApplication，§3.1）：所有窗口在一处构造、连接与置空集中管理——反面教材是它靠单例取得指针；正面做法 = Mixxx CoreServices（§2.5）。
  - **事件队列轮询**（Hydrogen，§3.2）：20Hz QTimer 拉事件分发注册 listener——UI 侧合流，免跨线程信号扇出。
  - 对本项目最短路径：**任务对象化 + jobId**（进取档 A1）或过渡期「MainWidget 退化为纯视图 + 独立 mediator 调度器对象」（Mixxx TrackAnalysisScheduler 的 GUI 侧形态）。

### 6.d 组合根现状的评估标准（从先例提炼）

评估维度与现状打分（✅ 达标 / ⚠ 部分 / ❌ 缺失）：

| 维度 | 先例依据 | 本项目现状 |
|---|---|---|
| 唯一装配点 | Mixxx：main.cpp 装配 CoreServices（§2.5）；composition root 概念（§5 末） | ✅ main.cpp:110-122 唯一装配；CLI/GUI 双分支清晰 |
| 依赖显式（构造注入优先） | Mixxx 主窗口构造注入 shared_ptr（§2.5） | ⚠ setter 注入 + 空指针防御（F10）；服务先构造，构造注入零成本可改 |
| 生命周期/销毁顺序显式 | Mixxx 作用域注释（§2.5）；LMMS issue #2015 反例（§3.1） | ⚠ unique_ptr/栈序保证「窗口先死、服务后死」方向正确，但 worker 收尾是 H1 假死缺口 |
| 无头可运行 | LMMS coreOnly 分支（§3.1）；Audacity nullptr 降级（§4.2） | ✅ CLI 分支免 QApplication（main.cpp:111-113），静态核心复用 |
| UI 可替换/可测性 | Mixxx Widgets/QML 双前端共用 CoreServices（§2.5） | ❌ MainWidget 直连具体类 MediaService/FFExtractor（无接口）；可测性一般（好在静态核心可脱离 Qt 测） |
| 全局状态克制 | LMMS/Hydrogen 单例枢纽反例（§3.3） | ✅ 无全局单例；引擎 per-job 无共享（F11） |

总评：**小而正确的组合根**——在「克制全局状态」「无头复用」上优于 LMMS/Hydrogen；短板是「关停序」与「接口抽象」，而非装配形态本身。

---

## 7. 分级架构改进建议（问题 e，每条对标先例）

### 保守档（不推翻现有结构，修协议缺口；1–2 天量级/条）

| # | 建议 | 对标先例 | 关联 |
|---|---|---|---|
| S1 | **关停序修复**：~MediaService 在 `thread->quit()` 前置 `cancelFlag = true`，`wait()` 加超时（超时 qWarning 后仍 wait 或强制分离二选一，倾向前者保数据安全）；窗口 closeEvent 可选提示「任务进行中」。 | Mixxx 作用域销毁序注释（§2.5）；Q4 "Wait for the finished() signal"；LMMS #2015 反例（§3.1） | H1 |
| S2 | **流复制路径补取消检查点**：`copy_stream` 增 `const std::atomic<bool>*` 参数，`doExtractAudio/doExtractVideo` 传入 cancelFlag，取消时走既有「失败删半成品」收尾。 | Audacity 块边界取消谓词（§4.2）；Q5 "regularly in long running functions" | H2 |
| S3 | **提交协议显性化**：7 个提交槽返回 `bool`（或加 `rejected` 信号），UI 据此提示「任务进行中」；`probe` 同样纳入。 | Audacity 显式枚举结果（§4.2）；「机制优先于约定」 | H3、H4 |
| S4 | **取消三态**：取消原因区分 Cancelled（删半成品，现状）/ Stopped（保留已写部分）——改 `jobDone(bool, QString)` 之前可先在 err 约定上落地，批处理前必须显式化。 | Audacity ProgressResult 三分（§4.2） | 6.b |
| S5 | **setMediaService 防重复连接**：`Qt::UniqueConnection`（本项目连接均 PMF，适用）或先 disconnect。 | Q6 同页 UniqueConnection 说明 | H8 |
| S6 | （可选）**构造注入替换 setter 注入**：`MainWidget(MediaService*, QWidget*)`，删除各槽 `if (!media)` 防御；main.cpp 两行改动。 | Mixxx 构造注入（§2.5） | F10、H7 |

### 进取档（服务 NFR-M1 引擎注册表 + FR-24 无头命令层 + 离线闭环批处理）

| # | 建议 | 对标先例 | 关联 |
|---|---|---|---|
| A1 | **任务对象化 + jobId**：`JobRequest{ type, input, options }` 单一请求信号（或 `QMetaObject::invokeMethod` 直投 worker 槽）替代 7 对信号槽；`progress/jobFinished` 携带 jobId。注意 JobRequest 跨线程 queued 需 `qRegisterMetaType`（自定义类型）。 | Mixxx EffectsRequest 类型化消息（§2.1）；Q20 invokeMethod；Q7 元类型注册 | H5、H6、6.c |
| A2 | **串行 FIFO 队列支撑批处理**：MediaService 内加 `QQueue<JobRequest>` + worker 空闲拉取（保一次一任务的 UI 语义、新增「排队中」状态）；或直接 `QThreadPool` + `setMaxThreadCount(1)`。 | Mixxx TrackAnalysisScheduler（deque+在途集合+空闲拉取+100ms 限频，§2.3）；Q15 | 6.b、离线闭环 |
| A3 | **无头命令层固化（FR-24）**：FFExtractor 静态核心提取为 `CommandCore`（纯函数 + `ProgressDelegate{ onProgress(double), isCancelled() }` 接口，nullptr 静默降级），GUI 的 MediaService 与 CLI 的 run_cli 都成为它的薄壳；未来 Agent 直接调 CommandCore。 | Audacity ExportProcessorDelegate/ImportProgressListener + nullptr 降级（§4.2/4.3）；Hydrogen CoreActionController 多前端门面（§3.2）；LMMS coreOnly（§3.1） | FR-24、F12 |
| A4 | **引擎注册表（NFR-M1）对齐 SoundSourceProviderRegistry**：引擎工厂自描述（能力/规格/优先级）+ 集中注册表 + open/setup 三态（成功/文件问题/我不支持），新增引擎零侵入。 | Mixxx SoundSource 三层 + open 三态（§2.2） | NFR-M1 |
| A5 | **进度协议统一**：CommandCore 内进度归一为 `Poll(分子,分母)→枚举`（内部仍可发 Qt 信号到 GUI）；取消原因三分随 S4 落地。 | Audacity BasicUI::ProgressDialog（§4.2） | 6.b、A3 |
| A6 | **窗口扩展预案**：多窗口前先落地 jobId（A1），窗口按 jobId 订阅（QFutureWatcher 模式或 Mixxx ControlProxy 式 key 订阅二选一）；MainWidget 退化为纯视图。 | Q19；Mixxx ControlProxy（§2.4）；LMMS GuiApplication 集中装配（§3.1） | 6.c |
| A7 | （性能项，非本调研主题顺带记录）**模型会话缓存**：AiDenoiser/VocalSeparator per-job 重载 ONNX 模型，批处理下应做 session 缓存——属 NFR-P 批处理优化，随 A2 一并设计。 | Mixxx 分析器常驻 worker 复用（§2.3）形态类比【倾向】 | 离线闭环 |

执行顺序建议：S1–S5（安全网）→ A1（协议地基，其余进取项依赖它）→ A3（FR-24 骨架，CLI 已验证可行）→ A2/A4（批处理与注册表，按里程碑）→ A5–A7。

---

## 8. 未收敛盲点与反证声明

- **行号时效**：所有外部仓库行号为 2026-10-14 快照（main/master/audacity3 分支头），未 pin commit SHA，后续提交会漂移；关键结论均有 URL 可复核。
- Audacity 取 `audacity3` 分支而未调研 Audacity 4（master）——若 Audacity 4 引入新任务模型，本报告 4.x 节不覆盖【未确认】。
- Mixxx/LMMS 历史机制（旧 AnalyserQueue、旧 MixerWorkerThread 的 nextQueue/freeQueue）已被现行代码重构取代，本报告只采现行机制，历史细节未考证【未确认】。
- 「窗口增多后退化」（6.c）为机制推导：当前仓库仅单窗口，无实测退化案例【倾向】；H1 关窗假死为源码静态分析结论（构造/析构顺序 + wait 语义），未在 Windows 真机复现【已确认（代码路径）+ 未复现（运行时）】。
- Audacity「无通用线程池」为文件名级 + 抽样确认，未完成全仓内容级 grep（代理取证时 blob 按需加载超时）——不能绝对排除某 .cpp 内藏私有池类【未确认，影响面小】。
- Qt 文档版本为当前 doc.qt.io（Qt 6.12 系页面）；QMetaObject context 重载标注 "introduced in Qt 6.7"，本项目工具链 Qt 6.11——该特定重载不可用，invokeMethod 其余形态不受影响（引用时注意）【已确认（版本标注）】。
- 本报告未评估各应用许可证与本项目红线的关系（仅比架构模式）；Mixxx/GPL 系代码**只可参照思路、不可复制代码**。

## 9. 参考链接清单

**Qt 官方文档**

- QThread（worker-object/销毁/中断）：https://doc.qt.io/qt-6/qthread.html
- Threads and QObjects：https://doc.qt.io/qt-6/threads-qobject.html
- Qt::ConnectionType：https://doc.qt.io/qt-6/qt.html#ConnectionType-enum
- QObject（connect/线程亲和/Q_ENUM）：https://doc.qt.io/qt-6/qobject.html
- Signals & Slots：https://doc.qt.io/qt-6/signalsandslots.html
- Multithreading Technologies（选型对照）：https://doc.qt.io/qt-6/threads-technologies.html
- Threading overview：https://doc.qt.io/qt-6/threads.html
- QThreadPool：https://doc.qt.io/qt-6/qthreadpool.html
- QtConcurrent：https://doc.qt.io/qt-6/qtconcurrent.html
- QFuture：https://doc.qt.io/qt-6/qfuture.html
- QPromise：https://doc.qt.io/qt-6/qpromise.html
- QFutureWatcher：https://doc.qt.io/qt-6/qfuturewatcher.html
- QMetaObject（invokeMethod）：https://doc.qt.io/qt-6/qmetaobject.html
- QPointer：https://doc.qt.io/qt-6/qpointer.html
- QString（QStringBuilder 节）：https://doc.qt.io/qt-6/qstring.html#more-efficient-string-construction
- Property System：https://doc.qt.io/qt-6/properties.html
- Object Trees & Ownership：https://doc.qt.io/qt-6/objecttrees.html

**Mixxx（github.com/mixxxdj/mixxx，main）**

- 效果链双镜像：https://github.com/mixxxdj/mixxx/blob/main/src/engine/effects/engineeffectchain.h 、.../engineeffectchain.cpp
- 桥接与消息：https://github.com/mixxxdj/mixxx/blob/main/src/effects/effectchain.cpp 、.../src/effects/effectsmanager.h 、.../src/engine/effects/engineeffectsmanager.cpp
- 引擎线程：https://github.com/mixxxdj/mixxx/blob/main/src/engine/enginemixer.cpp 、.../src/soundio/soundmanager.cpp
- 回调间隙 worker：https://github.com/mixxxdj/mixxx/blob/main/src/engine/engineworker.h 、.../src/engine/engineworkerscheduler.h
- WorkerThread 模板：https://github.com/mixxxdj/mixxx/blob/main/src/util/workerthread.h 、.../src/util/workerthread.cpp
- 分析调度器：https://github.com/mixxxdj/mixxx/blob/main/src/analyzer/trackanalysisscheduler.h 、.../trackanalysisscheduler.cpp 、.../src/analyzer/analyzerthread.h
- SoundSource 三层：https://github.com/mixxxdj/mixxx/blob/main/src/sources/soundsource.h 、.../src/sources/audiosource.h 、.../src/sources/soundsourceprovider.h 、.../src/sources/soundsourceproviderregistry.h 、.../src/sources/soundsourceproxy.cpp
- 控制平面：https://github.com/mixxxdj/mixxx/blob/main/src/control/control.h 、.../src/control/controlvalue.h 、.../src/control/controlobject.h 、.../src/control/controlproxy.h 、.../src/widget/controlwidgetconnection.h
- 组合根：https://github.com/mixxxdj/mixxx/blob/main/src/main.cpp 、.../src/coreservices.h 、.../src/coreservices.cpp 、.../src/mixxxmainwindow.cpp

**LMMS（github.com/LMMS/lmms，master）**

- Engine 单例：https://github.com/LMMS/lmms/blob/master/include/Engine.h 、.../src/core/Engine.cpp
- 音频引擎：https://github.com/LMMS/lmms/blob/master/include/AudioEngine.h 、.../src/core/AudioEngine.cpp 、.../src/core/audio/AudioPortAudio.cpp
- 离线渲染：https://github.com/LMMS/lmms/blob/master/src/core/ProjectRenderer.cpp
- job 化并行：https://github.com/LMMS/lmms/blob/master/include/AudioEngineWorkerThread.h 、.../src/core/AudioEngineWorkerThread.cpp 、.../include/ThreadableJob.h 、.../include/Mixer.h
- GUI 装配：https://github.com/LMMS/lmms/blob/master/src/gui/GuiApplication.cpp 、.../src/gui/MainWindow.cpp
- 无头分支：https://github.com/LMMS/lmms/blob/master/src/core/main.cpp

**Hydrogen（github.com/hydrogen-music/hydrogen，master）**

- 单例与门面：https://github.com/hydrogen-music/hydrogen/blob/master/src/core/Hydrogen.h 、.../src/core/Hydrogen.cpp 、.../src/core/CoreActionController.h
- 引擎与锁：https://github.com/hydrogen-music/hydrogen/blob/master/src/core/AudioEngine/AudioEngine.h 、.../src/core/AudioEngine/AudioEngine.cpp 、.../src/core/IO/JackDriver.cpp 、.../src/core/IO/PortAudioDriver.cpp 、.../src/core/IO/DiskWriterDriver.cpp
- 事件队列：https://github.com/hydrogen-music/hydrogen/blob/master/src/core/EventQueue.h 、.../src/core/EventQueue.cpp
- UI 轮询：https://github.com/hydrogen-music/hydrogen/blob/master/src/gui/src/HydrogenApp.h 、.../src/gui/src/HydrogenApp.cpp

**Audacity（github.com/audacity/audacity，audacity3 分支）**

- 进度/取消抽象：https://github.com/audacity/audacity/blob/audacity3/libraries/lib-basic-ui/BasicUI.h 、.../libraries/lib-basic-ui/BasicUI.cpp
- 效果进度链：https://github.com/audacity/audacity/blob/audacity3/libraries/lib-effects/Effect.cpp 、.../libraries/lib-effects/EffectBase.h 、.../libraries/lib-effects/EffectBase.cpp 、.../libraries/lib-effects/PerTrackEffect.cpp 、.../libraries/lib-audio-graph/AudioGraphTask.cpp
- 导入/导出：https://github.com/audacity/audacity/blob/audacity3/libraries/lib-import-export/ImportProgressListener.h 、.../libraries/lib-import-export/ImportPlugin.h 、.../libraries/lib-import-export/ExportTypes.h 、.../libraries/lib-import-export/ExportPlugin.h 、.../libraries/lib-import-export/ExportProgressUI.cpp 、.../src/ProjectFileManager.cpp
- 主线程调度：https://github.com/audacity/audacity/blob/audacity3/libraries/lib-wx-init/wxWidgetsBasicUI.cpp
- 后台慢 IO：https://github.com/audacity/audacity/blob/audacity3/libraries/lib-project-file-io/ProjectFileIO.cpp
- 点状并行：https://github.com/audacity/audacity/blob/audacity3/libraries/lib-cloud-audiocom/sync/BlockHasher.cpp 、.../libraries/lib-concurrency/concurrency/CancellationContext.h

**组合根概念**

- Composition root（Seemann 的 canonical 解释）：https://stackoverflow.com/questions/6277771/what-is-a-composition-root-in-the-context-of-dependency-injection
