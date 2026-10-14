# 实时音频线程工程规范与零分配接口设计调研

- 日期：2026-10-14
- 任务：架构审视外部调研之「实时音频线程工程规范与零分配接口设计」——从现状（QThread worker + busy 锁 + `process()` 堆分配返回 vector）到可实时化，梳理铁律、先例与接口约束。
- 服务落点：`docs/spec/architecture-review.md`（架构审视主文档）、NFR-L1/L2/L3（`docs/REQUIREMENTS.md` 156-158 行）、M4 实时化里程碑与 O3 延迟实测闸门（`docs/spec/v1-development-plan.md`）。
- 结论分级：【已确认】= 一手信源原文/源码直接证实；【倾向】= 一手信源的结构推断，缺最后一环直接验证；【未确认】= 本次未取得一手证据，附排查记录。

## 0. 本环境网络限制（诚实声明）

本 Linux 环境抓取外部信源受以下限制，已用替代通道逐一绕过，未能绕过的在对应条目标注：

- `raw.githubusercontent.com` SSL 握手失败 → 改用 `cdn.jsdelivr.net/gh/<owner>/<repo>@<ref>/<path>`（支持 tag/commit 精确引用，单文件直取对大仓库也有效）。
- GitHub 匿名 API 限流 → 部分改用 ungh.cc 代理（同样间歇 429）。
- `docs.juce.com` curl 直连失败、WebFetch 报 Socket is closed → 经 webReader 服务端抓取成功。
- `timur.audio` curl 返回 HTTP 200 但响应体 0 字节，www 与裸域均然；WebFetch 返回 HTTP 500 → **Timur Doumler 一手信源未取得**（见 2.4）。
- `en.cppreference.com` 被 Cloudflare 拦截（"Just a moment..."）→ 改用 libstdc++ 源码直接取证（更强的一手证据）。
- `learn.microsoft.com` 可达；`gcc.gnu.org/git` 触发 sourceware 429 → GCC 源码改经 jsdelivr 取 `gcc-mirror` 的 `releases/gcc-13.2.0` tag（与本项目 MinGW GCC 13.1 同大版本）。

## 1. 现状锚点（仓库源码，2026-10-14 检视）

| 位置 | 现状 | 与实时化的冲突 |
| --- | --- | --- |
| `src/service/streamengine.h:13-14` | `virtual std::vector<float> process(const float* in, int64_t frames) = 0;` 与 `flush()` 同 | 每次调用堆分配返回 vector（P0 债 #7 / NFR-L2 明文要求扩展） |
| `src/service/fftranscoder.cpp:154` | `const int chunk = engine.inRate();` 按秒分块喂引擎 | 离线无妨；实时下 chunk 粒度=延迟下界 |
| `src/service/fftranscoder.cpp:194,249` | `auto aiOut = engine.process(...)` | 分配点即调用点 |
| `src/service/fftranscoder.cpp:205` | `if (cancel && cancel->load())` | 协作式取消检查点，形状正确（见第 7 节） |
| `src/service/fftranscoder.cpp:253` | `engine.flush()` 收尾 | 「引擎缓冲自持」约定，v2 必须保留 |
| `src/service/mediaservice.h:20-21` | `bool busy` + `std::atomic<bool> cancelFlag` | busy 非 atomic，但仅在 GUI 线程读写，现状正确 |
| `src/service/mediaservice.cpp:39-41` | `cancelJob() { cancelFlag = true; }` | 非 RT 侧置位、worker 侧只读——正是业界同构做法（第 7 节） |
| `docs/REQUIREMENTS.md:156-158` | NFR-L1 round-trip ≤150ms（打拍注入法口径）；NFR-L2 音频线程零堆分配 + 抖动 ≤5ms；L3 至多一次重采样 | 本调研的直接服务对象 |
| `docs/REQUIREMENTS.md:138-155`（§5.1 延迟预算） | OS 捕获/回放按 OEM 现实 20-45ms/向；降噪链软件内合计 125-150ms 压线；变声链最坏 181ms 超支 | 第 9.2 节设计空间的预算输入 |

## 2. 实时音频铁律（一手原文）

### 2.1 Ross Bencina《Real-time audio programming 101: time waits for nothing》【已确认】

最常被引用的清单原文（bencina.txt 236-248 行，抓取自 rossbencina.com）：

> Don't allocate or deallocate memory
> Don't lock a mutex
> Don't read or write to the filesystem or otherwise perform i/o. (In case there's any doubt, this includes things like calling printf or NSLog, or GUI APIs.)
> Don't call OS functions that may block waiting for something
> Don't execute any code that has unpredictable or poor worst-case timing behavior
> Don't call any code that does or may do any of the above
> Don't call any code that you don't trust to follow these rules

两条容易被忽略的引申，同样是原文（bencina.txt 118、120 行）：

- 平均情况优化 vs 最坏情况："real-time waits for nothing, certainly not for that 1000-times-normal processing spike that happens every now and then. For this reason, you should always consider the worst-case execution time of your code." —— 对本项目的含义：GTCRN/MDX/RVC 的单次推理最坏耗时（ORT CPU 抖动、缓存污染）才是要测的对象，不是均值。
- 隐藏的量级陷阱：memset 清大延迟线这种"看起来快"的调用也能 glitch（bencina.txt 120 行）——引擎重置/状态清零必须分摊到多个块。

原文还点名：许多 STL 容器方法是平均情况优化算法、通用分配器与 GC 行为不可预测（bencina.txt 122 行）——直接支撑 NFR-L2 的 processInto 方向。

链接：<https://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing>

### 2.2 PortAudio 官方回调契约【已确认】

`include/portaudio.h` 770-835 行（master，jsdelivr 取证）：

> The PortAudio stream callback runs at very high or real-time priority. It is required to consistently meet its time deadlines. Do not allocate memory, access the file system, call library functions or call other functions from the stream callback that may block or take an unpredictable amount of time to complete.

预算口径原文（788 行附近）：

> It is reasonable to expect to be able to utilise 70% or more of the available CPU time in the PortAudio callback.

API 边界原文（835 行）：

> @note With the exception of Pa_GetStreamCpuLoad() it is not permissible to call PortAudio API functions from within the stream callback.

要点：PortAudio 把「回调运行在 RT 优先级线程」作为平台契约写死，且 CPU 预算按 70% 规划而非 100%——NFR-L2 的「抖动 ≤5ms」应按此口径留 30% 余量设计。

链接：<https://github.com/PortAudio/portaudio/blob/master/include/portaudio.h>

### 2.3 JUCE：工业级实现怎么做（AGPL 排除代码，保留证据）【已确认】

**许可证定论**：JUCE `LICENSE.md` 明文 "dual-licensed under the AGPLv3 and the commercial JUCE licence"。AGPLv3 触本项目红线（CLAUDE.md：不引入 GPL 依赖），**JUCE 任何源码不可复制进本仓库**；以下引用仅作先例证据。

三个可核验的先例：

1. WASAPI 设备线程（`juce_WASAPI_windows.cpp` master，jsdelivr 取证）：
   - `run()` 第一件事 `setMMThreadPriority()`：动态加载 `avrt.dll`，`AvSetMmThreadCharacteristicsW(L"Pro Audio", &dummy)` + `AvSetMmThreadPriority(h, AVRT_PRIORITY_NORMAL)`（1541-1556 行区域）。
   - 进入循环**前**预分配输入输出缓冲 `AudioBuffer<float> ins(jmax(1, numInputBuffers), bufferSize + 32)`——多留 32 样本余量（1560-1566 行区域）。
   - 主循环 `while (! threadShouldExit())`，启停用 `flags.fetch_or / fetch_and` 原子位（1568-1590 行区域）。
2. `Thread::RealtimeOptions`（`juce_Thread.h` 88-135 行区域）：优先级 0-10 注释明写 "Currently only used by Posix platforms"；processingTimeMs "Only used by macOS/iOS"——即 **JUCE 在 Windows 上的实时线程方案就是 MMCSS（第 3 节），不做 SetThreadPriority 直调**。
3. `AbstractFifo`（官方文档，webReader 取证）：定位原文 "Encapsulates the logic required to implement a lock-free FIFO... This class handles the logic needed when building a single-reader, single-writer FIFO. It doesn't actually hold any data itself"——索引管理器与数据分离的 SPSC 设计（许可证排除，模式可鉴，等价物见第 4 节）。

另外：`juce_Thread.h:691` `std::atomic<bool> shouldExit { false };`——协作式取消就是一个原子布尔（第 7 节）。

链接：<https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_devices/native/juce_WASAPI_windows.cpp>、<https://docs.juce.com/master/classAbstractFifo.html>、<https://github.com/juce-framework/JUCE/blob/master/LICENSE.md>

### 2.4 Timur Doumler 的实时 C++ 规则【未确认——一手信源未取得】

排查记录：`timur.audio` 与 `www.timur.audio` curl 均返回 200 + 0 字节响应体（Apache/PHP 7.4，疑似按客户端过滤）；WebFetch 返回 HTTP 500；无 GitHub 上的官方 slides 镜像可定位。其演讲（CppCon 2015/2016 "Real-time programming with the C++ standard library" 等）核心规则（无锁、无分配、无边界不确定的库调用、无异常穿越 RT 边界）与 2.1-2.3 一手文本高度重叠，**本文以 2.1-2.3 替代覆盖，不再转述无法溯源的二手总结**。待有正常网络环境时补一手 slides 链接。

## 3. Windows 线程优先级：MMCSS（本项目目标平台）【已确认】

Microsoft Learn《Multimedia Class Scheduler Service》原文取证：

- 默认任务名清单（OEM 可增）："Audio, Capture, Distribution, Games, Playback, Pro Audio, Window Manager"。
- 调度类别与优先级带（原文表）：

> High 23-26 — These threads run at a thread priority that is lower than only certain system-level tasks. This category is designed for **Pro Audio** tasks.
> Medium 16-22 — part of the application that is in the foreground.
> Low 8-15 — the remainder of the threads.

- 每任务注册表键：`HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Multimedia\SystemProfile\Tasks\<name>`（Affinity / Priority(1-8) / Scheduling Category(High|Medium|Low) 等）。

对本项目的映射：

- WASAPI 共享模式的回调线程由系统挂 MMCSS（"Pro Audio"），**我们不用也不能改它**；需要动手的是**自己创建的处理线程**——参照 JUCE 先例动态加载 avrt.dll 调 `AvSetMmThreadCharacteristicsW(L"Pro Audio")`，并注意配对 `AvRevertMmThreadCharacteristics`。
- "High 23-26" 意味着自建处理线程一旦挂上 Pro Audio，优先级仅次于系统级任务——这也是「worker 处理线程必须挂 MMCSS 否则被 GUI/渲染线程抢占」结论的依据。
- 注意 SystemResponsiveness 注册表值（默认 20，即给系统留 20%；Pro Audio 任务的推荐配置常配 10）——README/部署文档若给用户调优指引可引用。

链接：<https://learn.microsoft.com/en-us/windows/win32/procthread/multimedia-class-scheduler-service>

## 4. SPSC 无锁环形缓冲：实现与许可证核验

### 4.1 候选清单（许可证逐一核验）

| 实现 | 许可证（取证） | 结论 | 备注 |
| --- | --- | --- | --- |
| `boost::lockfree::spsc_queue` | Boost 软件许可 1.0（boost/LICENSE） | 可用 | 头文件自述 "lock-free single-producer/single-consumer ringbuffer...pushing and popping is wait-free"（spsc_queue.hpp:1,39）；但引入 Boost 依赖与现工具链不符 |
| rigtorp/SPSCQueue | MIT（LICENSE，Copyright 2018 Erik Rigtorp） | 可用，模式教科书 | 单头文件；acquire/release + 索引缓存 + 缓存行对齐（见 4.2） |
| moodycamel/**readerwriterqueue** | BSD-2（rwq_LICENSE.md） | 可用 | README:25 "Completely 'wait-free' (no compare-and-swap loop)"、:45 try_enqueue "never allocates" |
| moodycamel/**ConcurrentQueue** | BSD-2（cq_LICENSE.md） | 可用但**是 MPMC** | README:11 "Use concurrently from any number of threads"——任务书"moodycamel（SPSC/MPMC 辨析)"的答案：SPSC 的是同作者 readerwriterqueue，勿混用 |
| PortAudio `pa_ringbuffer.c` | MIT（文件头） | 可用 | 源码注记 "safe only for a single-thread reader and a single-thread writer"；屏障位置见 4.2 |
| JUCE `AbstractFifo` | AGPLv3/商业双许可 | **禁用**（红线） | 仅作设计参考（2.3） |

moodycamel 附注：阻塞版（BlockingConcurrentQueue/BlockingReaderWriterQueue）内含 Jeff Preshing 的 zlib 许可信号量实现（rigtorp LICENSE 亦注明其第三方段）——本项目不用阻塞变体，不引入该负担。

### 4.2 公认写法（一手源码行号）【已确认】

SPSC 环形缓冲的三要素在两个独立实现中完全一致：

1. **索引/序号 + acquire/release 内存序**。rigtorp `SPSCQueue.h`：写侧 `writeIdx_.store(nextWriteIdx, std::memory_order_release)`（118 行），读侧 `readIdxCache_ = readIdx_.load(std::memory_order_acquire)`（115/132 行）；容量查询两侧各持对方索引的缓存副本（`readIdxCache_` / `writeIdxCache_`，171-190 行），避免每次都做原子读。
2. **2 的幂容量**。`pa_ringbuffer.c` `PaUtil_InitializeRingBuffer`："elementCount must be power of 2, returns -1 if not"（bigMask/smallMask 位与取模）。
3. **缓存行对齐防伪共享**。rigtorp `SPSCQueue.h:85-86` `static_assert(alignof(SPSCQueue<T>) == kCacheLineSize)`、`sizeof >= 3 * kCacheLineSize`（头尾索引各占一行，第三个缓存行起始放数据）。

PortAudio 的屏障放置（pa_ringbuffer.c，经 `pa_memorybarrier.h` 映射到平台屏障原语）：写索引推进前 `PaUtil_WriteMemoryBarrier()`（144 行）、读区域取址后 `PaUtil_ReadMemoryBarrier()`（181 行）、AdvanceWriteIndex 处 `PaUtil_FullMemoryBarrier()`（192 行）——比「纯 acquire/release」更保守（考虑了无 C++11 atomic 的 C 语境）。C++ 侧照 rigtorp 的 acquire/release 写即可。

链接：<https://github.com/rigtorp/SPSCQueue>、<https://github.com/PortAudio/portaudio/blob/master/src/common/pa_ringbuffer.c>、<https://github.com/cameron314/readerwriterqueue>、<https://github.com/cameron314/concurrentqueue>

## 5. std::atomic::wait/notify 与 C++20 并发原语的实时定位

### 5.1 libstdc++（GCC 13.2.0）实现形态【已确认——源码；MinGW 编译形态为【倾向】】

`libstdc++-v3/include/bits/atomic_wait.h`（gcc-mirror `releases/gcc-13.2.0`，jsdelivr 取证）：

- Linux：`#ifdef _GLIBCXX_HAVE_LINUX_FUTEX` 直接发 `syscall(SYS_futex, ...)`（`__platform_wait/__platform_notify`，59-127 行区域）——这是 configure 只在 Linux 目标定义的宏。
- **非平台等待平台**：注释原文 "define _GLIBCXX_HAVE_PLATFORM_WAIT and implement __platform_wait() ... if there is a more efficient primitive supported by the platform (e.g. __ulock_wait()/__ulock_wake()) which is better than **a mutex/condvar based wait**"——`__waiter_pool_base` 内嵌 `mutex _M_mtx; __condvar _M_cv;`（200-210 行区域），等待者按地址哈希进 16 桶静态池 `static __waiter_pool_base __w[16]`（250-258 行区域），`_M_notify` 持 `lock_guard<mutex>` 后 `notify_all()`（234-243 行区域）。**注意：源码中不存在 WaitOnAddress 路径**（全文件 grep 无命中）。

对本项目的推论（链条：宏由 configure 按目标平台定义 → MinGW 目标非 Linux → 走 mutex/condvar 回退）：

- 【已确认】GCC 13 libstdc++ 的 atomic wait 在 Linux = futex、在无更优原语的平台 = mutex+condvar 池。
- 【倾向】本项目 Windows/MinGW 目标上 `std::atomic::wait/notify_one/notify_all` 编译为 mutex+condvar 回退（configure 输出未直接核验，但 `atomic_wait.h` 中无第三条路径，排除法强）。**含义：notify 也持锁——RT 线程连 notify 都不能调**。wait 侧（worker 线程）用则完全没问题。

### 5.2 std::stop_token / jthread【已确认】

`include/std/stop_token`（GCC 13.2.0）：`_M_request_stop()` 用原子值中的 `_S_locked_bit` 位自旋抢锁再置停止位（156-233 行区域），注册回调还要二次抢锁；**检查侧 `stop_requested()` 无锁**。结论同上：请求停止的线程不能是 RT 线程；RT/worker 侧只读检查安全。

### 5.3 结论：本项目在 RT 边界只允许的同步原语

- RT 侧：`atomic.load(relaxed/acquire)`、SPSC 队列的 push/pop、写预分配内存。**不调 wait/notify、不碰 stop_token 请求侧、不锁、不分配**（Bencina 2.1 + libstdc++ 实证 5.1/5.2）。
- 唤醒方向的取舍：pablio 的答案是 **RT 侧根本不做唤醒**，worker 用短周期轮询（6.1）；Windows 原生无锁唤醒可用 WaitOnAddress/SetEvent（内核调用、有界），但 libstdc++ GCC 13 未用 WaitOnAddress，我们也不必引入——轮询周期 5-10ms 相对 150ms 预算可忽略。

链接：<https://github.com/gcc-mirror/gcc/blob/releases/gcc-13.2.0/libstdc++-v3/include/bits/atomic_wait.h>、<https://github.com/gcc-mirror/gcc/blob/releases/gcc-13.2.0/libstdc++-v3/include/std/stop_token>

## 6. 「回调搬运 + worker 处理」双线程模型先例

### 6.1 PortAudio pablio（官方先例，历史代码）【已确认】

pablio = Portable Audio Blocking I/O，Phil Burk/Ross Bencina 作，MIT。**master 已删除，存在于 19.7.0 及更早 tag**（363 文件的 master 列表零命中；19.7.0 列表含 `pablio/pablio.{c,h}` 等 8 文件）——引用须带 tag。

核心：音频回调只做环形缓冲搬运（pablio.c 61-96 行）：

```c
/* Called from PortAudio. Read and write data only if there is room in FIFOs. */
static int blockingIOCallback( void *inputBuffer, void *outputBuffer, ... )
{
    ...
    if( inputBuffer != NULL )
        PaUtil_WriteRingBuffer( &data->inFIFO, inputBuffer, numBytes );
    if( outputBuffer != NULL ) {
        int numRead = PaUtil_ReadRingBuffer( &data->outFIFO, outputBuffer, numBytes );
        /* Zero out remainder of buffer if we run out of data. */
        for( i=numRead; i<numBytes; i++ ) ((char *)outputBuffer)[i] = 0;
    }
    return 0;
}
```

要点全部命中本项目需要：回调内零分配零阻塞、输出欠载**补零**（不等待）、上层线程阻塞式 Read/Write（内部轮询 + Pa_Sleep）、示例参数 `FRAMES_PER_BUFFER 256`。

链接：<https://github.com/PortAudio/portaudio/blob/19.7.0/pablio/pablio.c>

### 6.2 Web Audio API 规范（W3C）：控制线程/渲染线程的规范性表述【已确认】

规范 §2.2（webaudio.github.io 2026-10 快照）：

> The Web Audio API **MUST** be implemented using a control thread, and a rendering thread.
> ... The rendering thread is the thread on which the actual audio output is computed... It can be a real-time, callback-based audio thread...
> Communication from the control thread to the rendering thread is done using control message passing. Communication in the other direction is done using regular event loop tasks.

- 渲染量子：`[[render quantum size]]` 默认 128 帧（规范 §2.5/上下文初始化算法）。@48k 即 2.67ms 一次回调——给「回调搬运粒度」提供业界基准：**搬运粒度由设备量子决定，与处理 chunk 解耦**，两者之间就是环形缓冲。
- 缓冲深度→延迟的规范级换算公式（baseLatency 定义原文）："if the audio context is running at 44.1 kHz with default render quantum size, and the AudioDestinationNode implements double buffering internally and can process and output audio each render quantum, then the processing latency is **(2·128)/44100 = 5.805 ms**"——即延迟 = 在途帧数 / 采样率，双缓冲就是 2×量子。
- `baseLatency`（到音频子系统的处理延迟）与 `outputLatency`（到硬件/声学的估计延迟）分离定义——O3 实测时应采用同样的分解口径（第 9.5 节）。

链接：<https://webaudio.github.io/web-audio-api/>

### 6.3 GStreamer GstAudioRingBuffer【已确认】

`gstaudioringbuffer.{h,c}`（gstreamer main，jsdelivr 取证）：分段（segment）环形缓冲 + `GCond cond`（"used to signal start/stop/pause/resume actions"）+ `segdone` 读指针；设备回调经 `GstAudioRingBufferCallback` 写段、`buffer_time/latency_time`（微秒）直接决定缓冲深度。先例价值：**用「缓冲时长」参数化队列深度**（total buffer time vs latency time），这正是 9.2 节设计空间表的参数化方式；cond 只在控制动作上使用，不在数据路径上。

链接：<https://gitlab.freedesktop.org/gstreamer/gstreamer/-/blob/main/subprojects/gst-plugins-base/gst-libs/gst/audio/gstaudioringbuffer.h>

### 6.4 JUCE WASAPI `run()` 循环

见 2.3 第 1 条——自建设备线程的完整样貌：MMCSS + 预分配 + `while(!threadShouldExit())` + 原子标志位。本项目 worker 线程（喂引擎、消费环形缓冲）即照此形状。

## 7. 协作式取消的实时安全表达

三个一手源同构，结论稳固【已确认】：

1. 本仓库现状已是正确形状：`mediaservice.cpp:39-41` 控制（GUI）线程 `cancelFlag = true`；`fftranscoder.cpp:205` 处理循环 `cancel->load()` 检查点；`ffextractor.h:22` 指针传递。
2. JUCE：`signalThreadShouldExit()` 就是 `shouldExit = true`（`std::atomic<bool> shouldExit`，juce_Thread.h:691），线程函数循环条件 `while (! threadShouldExit())`（juce_Thread.cpp:230-244；WASAPI run() 实际使用）。
3. std::stop_token：请求侧有锁（5.2），**检查侧无锁**——若用 C++20 词汇，RT/worker 侧只能当只读检查用，请求侧留在控制线程。

推论：取消是「单写多读的原子位 + 处理循环检查点」，没有任何要在 RT 侧做更多事情的空间。v2 接口不需要为取消增加参数（第 9.1 节第 6 条）。

## 8. 延迟测量：「打拍注入法 / loopback 脉冲法」先例

- 【已确认——口径先例】Web Audio 规范的 `baseLatency` / `outputLatency` 定义（6.2）：软件处理缓冲与输出通路分开度量、`outputLatency` "should be queried frequently"——分解口径可直接借用。
- 【已确认——公式先例】(2·128)/44100 = 5.805ms 的换算式（6.2）：任何"缓冲深度→延迟"的账都按在途帧数/采样率算。
- 【未确认——可照抄的官方注入法实现】Chromium 官方示例库 googlechrome/web-audio-samples 因仓库 >50MB 无法经 jsdelivr 列目录核验（403 Package size exceeded），GitHub 侧被限流；本轮未能定位到官方维护的 loopback 打拍样例。Microsoft Learn《Low Latency Audio》页本轮抓取返回 404（疑已搬迁），OS 层 20-45ms/向数字沿用 `docs/reference/research-realtime-audiostack.md` 已核验内容。**结论：注入法没有找到"官方可直接照抄"的实现，O3 需自建**：播放侧周期注入脉冲样本（或拍手/线路回采），回采侧互相关找峰，得 round-trip；再以 `baseLatency` 口径扣除软件缓冲账。此为 O3 的方法学基线，工具本身列入 M4 工作包。

## 9. 核心问题解答

### 9.1 a：IStreamEngine v2 / processInto 的硬性约束

从铁律逐条映射到接口（服务 NFR-L2）：

```cpp
// 零分配变体（示意，非最终实现）
// 返回实际写入 out 的帧数；outCapacity 不足属调用方编程错误（断言/返回 -1），绝不内部扩容
virtual int64_t processInto(const float* in, int64_t frames,
                            float* out, int64_t outCapacity) = 0;
virtual int64_t flushInto(float* out, int64_t outCapacity) = 0;
// 声明单次调用最大输出帧数与算法滞后（供上层推导环形缓冲深度；见 9.2）
virtual int64_t maxOutPerCall(int64_t inFrames) const = 0;
virtual int64_t algorithmicDelayFrames() const = 0;
```

逐条约束（依据）：

1. **缓冲归属调用方**：in/out 均由调用方预分配，引擎不得在 `processInto/flushInto` 路径上 `new/delete/malloc/vector::push_back`（Bencina 2.1 第 1 条 + JUCE run() 预分配先例 2.3）。ORT 引擎的输出 tensor、STFT 重叠缓冲等内部状态必须在构造/prepare 阶段一次性建好（"重置清零要分摊"，bencina.txt:120）。
2. **禁止事项清单写进接口文档**：无锁、无文件/日志 I/O、无 printf、无可能阻塞的 OS 调用、无异常穿越（Bencina 2.1 全表 + PortAudio 2.2 + libstdc++ 实证 5.1——标准库原语也可能带锁，RT 侧调用白名单化）。
3. **签名返回写入帧数而非 vector**；容量不足不扩容、不静默截断（断言失败优于坏数据）。
4. **滞后上界可静态声明**：`maxOutPerCall`/`algorithmicDelayFrames` 是环形缓冲深度推导的输入——现状 FFTranscoder 用「处理返回多少收多少」的动态发现（fftranscoder.cpp:194-249），实时化必须有先验上界（Web Audio 量子分解 6.2 + GStreamer buffer_time/latency_time 参数化 6.3）。
5. **`flush()` 语义保留**（引擎缓冲自持约定，fftranscoder.cpp:253 依赖它收尾）；`flushInto` 同零分配。
6. **取消不入签名**：沿用外部 `std::atomic<bool>*` 只读检查点（第 7 节）；引擎若内部分段（MDX 分块），单次 `processInto` 内部循环每段查一次即可。
7. **规格声明不变**：`inRate/inChannels` 照旧，采样率/声道换算仍归转码层（swr 双向），L3「至多一次重采样」照 REQUIREMENTS。
8. 旧 `process()` 可保留为离线路径的薄封装（内部预分配 + 一次拷贝），避免 FFTranscoder 两套主循环；实时路径只用 `processInto`。

### 9.2 b：150ms 预算下 chunk / 队列深度设计空间

预算输入（REQUIREMENTS §5.1，已核验）：OS 捕获 20-45ms + OS 回放 20-45ms（共享模式 OEM 现实）→ **软件内可用预算 = 150 − 40~90 = 60~110ms**；变声链现状软件内 149-181ms 最坏已超支，须靠砍 chunk 解。

帧数↔毫秒基准表（延迟 = 在途帧数 / fs；公式先例 6.2）：

| 帧数 | @48k | @44.1k | @16k |
| --- | --- | --- | --- |
| 128（Web Audio 量子） | 2.67ms | 2.90ms | 8ms |
| 256（pablio 默认） | 5.33ms | 5.80ms | 16ms |
| 512（GTCRN chunk） | 10.67ms | 11.61ms | 32ms |
| 1024 | 21.33ms | 23.22ms | 64ms |
| 2048 | 42.67ms | 46.44ms | 128ms |
| 3072 | 64ms | 69.66ms | — |
| 4096 | 85.33ms | 92.88ms | — |

设计规则（各先例汇合）：

1. **搬运粒度 ≠ 处理粒度**：设备量子（WASAPI 共享模式通常 10ms 即 480 帧@48k）由系统给；处理 chunk 由引擎定；两者间 SPSC 环形缓冲吸收。Web Audio（128 帧量子 + 图内部更大块）与 pablio（256 帧回调 + 上层任意块）同构。
2. **每级队列深度 = 2×chunk + 抖动余量，取 2 的幂**（双缓冲即 2×量子 = 5.805ms 的规范公式；2 的幂是环形缓冲硬要求 4.2）。深度不是越大越好：深度 × 1/fs 直接进最坏延迟账。
3. **级数 × chunk 累加进预算**：捕获环形缓冲（≈2×chunk）+ 引擎内部滞后（GTCRN 恰 1 hop = 16ms，`docs/reference/research-gtcrn-sherpa-onnx.md`）+ RVC chunk（64-96ms）+ 回放环形缓冲（≈2×chunk）。例：GTCRN 链 chunk 512@16k → 缓冲账 10.67×2 + 16 + 等待 16 ≈ 55ms，落在 60ms 下限内勉强可行；RVC chunk 96ms 单项即吞掉预算——**变声链要么接受 >150ms 分级（REQUIREMENTS 开放项），要么把 chunk 砍到 1024 帧@48k（21.3ms）级**，这是 M4 前要实测的量（O3）。
4. **worker 轮询周期 5-10ms**（pablio 先例）：相对预算可忽略；不必引入 RT→worker 唤醒（5.3）。
5. 60% CPU 天花板规划（PortAudio 70% 口径再保守些，给系统留量）。

### 9.3 c：队列选型结论

- **首选自研 SPSC 环形缓冲**（≤200 行）：数据形态特殊（交错 float 帧 + chunk 边界 + 帧计数），通用件也要包一层；模式已被 rigtorp（MIT，索引缓存 + 缓存行对齐，4.2 行号）与 pa_ringbuffer（MIT，屏障位置 4.2）双重锚定，自研有明确参照且无许可纠缠；配 parity/压力测试即可。**另有一条硬理由**：本仓库红线禁 GPL/AGPL，而 JUCE AbstractFifo 这类"顺手抄"最危险的来源正是 AGPL——自研边界清晰。
- 若要现成件：rigtorp/SPSCQueue（MIT，单头文件）或 moodycamel/readerwriterqueue（BSD-2，try_enqueue 永不分配、无 CAS 循环）。**勿用** ConcurrentQueue（是 MPMC）、**勿用** JUCE（AGPL）。Boost 可用（Boost 软件许可）但为一个头文件引入 Boost 不值。
- 无论自研/引入：容量 2 的幂、写前 release/读后 acquire、单读单写纪律（pa_ringbuffer 注记）、伪共享对齐。

### 9.4 d：取消与背压在 RT 边界的正确做法

- **取消**：单写多读原子位，控制线程置位（mediaservice.cpp:39-41 现状即正确），RT/worker 循环检查点退出（fftranscoder.cpp:205；JUCE `while(!threadShouldExit())` 同构）。RT 侧永远不 join、不 wait、不 notify（5.1 的 mutex 回退证据使 notify 也不行）。
- **背压/欠载**：输出侧数据不足→**补零续播并计数 xrun**（pablio 61-96 行先例），绝不阻塞回调；输入侧环形缓冲满→覆盖最旧 + 计数（或丢新 + 计数，二选一写死，不做可配）；xrun 计数由非 RT 侧轮询上报 UI。错误不抛异常跨界（2.1），以返回码/原子错误位上抛。
- **降载次序**：先减队列深度，再升 chunk（延后处理），最后才丢帧——每一步都在预算账内权衡（9.2）。

### 9.5 e：注入法延迟测量先例

结论：**没有找到官方可直接照抄的注入法实现**【未确认——排查见第 8 节】；可照抄的是**口径**：baseLatency（软件缓冲账）/outputLatency（到硬件）分解 + 在途帧数/fs 换算。O3 自建方案基线：线路回采（播放出脉冲 → 线缆回采 → 互相关峰）测 round-trip，软件缓冲账按 9.2 表逐级扣除；工具与判据（如 P95 延迟 ≤150ms、xrun=0 持续 N 分钟）进 `docs/acceptance/` 规范落档。

## 10. 对 NFR 的直接映射（本调研的产出结论）

| NFR | 调研结论 |
| --- | --- |
| NFR-L1（≤150ms，注入法） | 口径用 baseLatency/outputLatency 分解（6.2）；软件预算 60-110ms（9.2 第 1 条）；变声链需砍 chunk 或分级，进 O3 实测 |
| NFR-L2（零堆分配，processInto） | 接口约束清单 9.1（八条）；RT 侧同步原语白名单 5.3；标准库原语带锁实证（5.1/5.2）使白名单化成为硬要求 |
| NFR-L3（至多一次重采样） | 引擎规格声明制不变（9.1 第 7 条）；实时化后 canonical 48k 决策（P0 债 #4）与本调研的帧数表衔接 |
| M4 / O3 | 注入法工具自建（9.5）；设备线程照 JUCE WASAPI 形状（2.3）、worker 照 pablio 形状（6.1）、优先级照 MMCSS（第 3 节） |

盲点声明（工作原则 14）：(a) MinGW 下 atomic_wait 走 mutex 回退为排除法【倾向】，上线前用一个小断言程序在 Windows 实测一次即可收敛；(b) ORT `Run()` 在预分配输出 tensor 下是否完全零额外分配未核验（本轮未取 ONNX Runtime 源证），列为 NFR-L2 落地时的首个验证点；(c) 变声链 chunk 下限取决于 RVC 模型结构（ContentVec 感受野），本轮未取证，归 O3/需求侧决策。

## 参考

- Ross Bencina, Real-time audio programming 101: <https://www.rossbencina.com/code/real-time-audio-programming-101-time-waits-for-nothing>
- PortAudio 回调契约（portaudio.h master）: <https://github.com/PortAudio/portaudio/blob/master/include/portaudio.h>
- PortAudio pa_ringbuffer.c（MIT）: <https://github.com/PortAudio/portaudio/blob/master/src/common/pa_ringbuffer.c>
- PortAudio pablio（19.7.0 tag，master 已删）: <https://github.com/PortAudio/portaudio/blob/19.7.0/pablio/pablio.c>
- Microsoft Learn, Multimedia Class Scheduler Service: <https://learn.microsoft.com/en-us/windows/win32/procthread/multimedia-class-scheduler-service>
- JUCE LICENSE（AGPLv3/商业双许可）: <https://github.com/juce-framework/JUCE/blob/master/LICENSE.md>
- JUCE WASAPI 设备线程: <https://github.com/juce-framework/JUCE/blob/master/modules/juce_audio_devices/native/juce_WASAPI_windows.cpp>
- JUCE Thread/RealtimeOptions: <https://github.com/juce-framework/JUCE/blob/master/modules/juce_core/threads/juce_Thread.h>
- JUCE AbstractFifo 文档: <https://docs.juce.com/master/classAbstractFifo.html>
- rigtorp/SPSCQueue（MIT）: <https://github.com/rigtorp/SPSCQueue>
- cameron314/readerwriterqueue（BSD-2，SPSC）: <https://github.com/cameron314/readerwriterqueue>
- cameron314/concurrentqueue（BSD-2，MPMC）: <https://github.com/cameron314/concurrentqueue>
- Boost.Lockfree spsc_queue: <https://github.com/boostorg/lockfree>
- libstdc++ GCC 13.2.0 atomic_wait.h: <https://github.com/gcc-mirror/gcc/blob/releases/gcc-13.2.0/libstdc++-v3/include/bits/atomic_wait.h>
- libstdc++ GCC 13.2.0 stop_token: <https://github.com/gcc-mirror/gcc/blob/releases/gcc-13.2.0/libstdc++-v3/include/std/stop_token>
- Web Audio API 规范: <https://webaudio.github.io/web-audio-api/>
- GStreamer gstaudioringbuffer: <https://gitlab.freedesktop.org/gstreamer/gstreamer/-/blob/main/subprojects/gst-plugins-base/gst-libs/gst/audio/gstaudioringbuffer.h>
- 仓库内关联：`docs/reference/research-realtime-audiostack.md`（OS 层延迟数字与 miniaudio/WASAPI 背景）、`docs/reference/research-gtcrn-sherpa-onnx.md`（GTCRN hop/滞后）、`docs/reference/falsify-realtime.md`（OEM 现实口径）
