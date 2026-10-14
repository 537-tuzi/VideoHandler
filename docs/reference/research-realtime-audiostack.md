# 实时音频栈调研：Windows 捕获/回放、虚拟音频设备、延迟与时钟漂移

## 0. 元信息

- 调研日期：2026-10-13 ～ 2026-10-14
- 目的：为 VideoHandler 实时模式（麦克风 → 降噪/变声 → 输出设备/虚拟声卡，NFR-L1 ≤150ms 全链路、NFR-L2 音频线程零堆分配、NFR-L3 全链路至多一次重采样）提供一手信源依据。
- 信源原则：只采一手信源——官方文档（learn.microsoft.com / doc.qt.io / VB-Audio 官网 / support.discord.com）、库源码（GitHub raw/jsDelivr CDN 获取的仓库文件原文）、规范/官方发布说明。所有关键结论附来源 URL；查不到的明确标注【未确认】，不编造。
- 网络限制（影响取证方式的诚实声明）：本环境无法访问 chromium.googlesource.com / webrtc.googlesource.com（连接失败）；github.com 匿名 REST API 限流（限流窗口重置后部分请求成功，已用于核验 OBS 发布说明与提交日期）；superpowered.com/latency 与 oblique-audio.com 的测量工具页 404；software.muzychenko.net 返回 403/404。GitHub 文件内容主要经 raw.githubusercontent.com / cdn.jsdelivr.net / codeload.tar.gz 获取并核对文件路径；WebRTC 源码经由公开 GitHub 镜像获取并注明。
- 引文约定：英文/日文原文用引用块直录（法律条款必须原文），中文为笔者翻译；代码事实给出 `仓库文件路径:行号`（行号以本次抓取的 master/6.9.2 快照为准）。

---

## 1. 主要发现

### 1.1 Windows 低延迟捕获/回放：WASAPI 共享（事件驱动）vs 独占

来源：Microsoft Learn《Low Latency Audio》（Windows 10 音频栈官方说明）。

**官方延迟数字（渲染路径）**：

> "Before Windows 10, the latency of the audio engine was equal to ~12 ms for applications that use floating point data and ~6 ms for applications that use integer data. **In Windows 10 and later, the latency has been reduced to 1.3 ms for all applications**"

> "Before Windows 10, the buffer was always set to ~10 ms. Starting with Windows 10, the buffer size is defined by the audio driver (more details on the buffer are described later in this article)."

**捕获路径**：

> "Before Windows 10, this buffer was always set to 10 ms." （驱动→引擎缓冲）
> 引擎处理延迟：Win10 前 ~6ms（float）/ ~0ms（整数）；"In Windows 10 and later, the latency has been reduced to ~0ms for all applications."

**整体效果**：

> "All applications that use audio will see a 4.5-16 ms reduction in round-trip latency ... without any code changes or driver updates, compared to Windows 8.1."

**共享模式小缓冲（IAudioClient3）**：

> "The inbox HDAudio driver has been updated to support buffer sizes between **128 samples (2.66ms@48kHz) and 480 samples (10ms@48kHz)**."

即：Win10+ 共享模式下，配 IAudioClient3 小缓冲，OS 侧双向缓冲可压到每向 ~2.7–3ms；默认（应用不指定缓冲）仍为 10ms。

> **2026-10-14 反证修正（`falsify-realtime.md`）——上述「可压到 ~2.7–3ms」在普通 OEM 声卡上不保真**：MS《Low Latency Audio》FAQ 原文明确小缓冲须驱动逐机型适配、"It's up to the OEMs to decide"；**128–480 采样区间的主语是内置 HDAudio 驱动**（MS 自己的测量流程都要先手动换掉 OEM 驱动）。实测铁证 miniaudio#949（Realtek 笔记本）：调试日志 defaultPeriod=fundamental=min=max=**480**——IAudioClient3 被正常调用但小周期被驱动顶回 10ms；round-trip 实测 Win10 83ms / Win11 93ms，改进测量方法后共享 ~45ms（独占反而 125ms）。独占模式不能当兜底（mackron 自认实现 "fiddly"）。**工程口径：OEM 声卡捕获预算按 20–45ms/向计，小周期仅作上限优化而非承诺**；miniaudio 默认低延迟档申请的就是 480 帧（10ms×3），「自动启用 IAudioClient3 ≠ 拿到小周期」。

**独占模式 vs 共享小周期（官方建议）**，来源《Exclusive-Mode Streams》：

> "Starting with Windows 10, IAudioClient3 enables shared-mode streams with very low period sizes (as low as the value returned by GetSharedModeEnginePeriod). **This provides latency comparable to exclusive mode while preserving system sound mixing and interoperability.** Evaluate shared-mode low-period streams before committing to exclusive mode unless you have a hard requirement for bit-exact output or custom sample rates."

> "Exclusive-mode streams seize the audio endpoint—all system sounds, notifications, and audio from other applications are silenced."

事件驱动与线程优先级：同一文档明确 `AUDCLNT_STREAMFLAGS_EVENTCALLBACK` + MMCSS 是低延迟无爆音的正规做法；驱动侧 WaveRT DDI 性能最好。

**Loopback（采集系统输出）**，来源《Loopback Recording》：

> "A client can enable loopback mode only for a shared-mode stream (AUDCLNT_SHAREMODE_SHARED). Exclusive-mode streams cannot operate in loopback mode."

> "In versions of Windows prior to Windows 10 1703, pull-mode capture client does not receive any events when a stream is initialized with event-driven buffering and is loopback-enabled. ... In Windows 10 versions 1703 and higher, event-driven loopback clients are supported"

**按进程采集（Application Loopback）**：`AUDIOCLIENT_ACTIVATION_PARAMS`（audioclientactivationparams.h）经 `ActivateAudioInterfaceAsync` 以 `AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK` 激活"虚拟音频设备"端点，可只采集指定进程的播放音频（OBS 的"应用程序音频捕获"即此 API，引入于 2021-09 的提交 769a29bd1a，见 1.4.4）。

**量级结论**：共享模式事件驱动 + 默认 10ms 缓冲 ⇒ OS 侧往返 ~20ms 量级；共享小周期（~3ms/向）⇒ ~7–10ms；独占模式数值更低但官方已不推荐作为常规手段（独占 + loopback 不兼容，且霸占端点）。

### 1.2 miniaudio 的 Windows 后端能力与延迟特性

来源：miniaudio 官方 README 与 `miniaudio.h` 文件头文档（README 明言 "Documentation ... found at the top of miniaudio.h ... always the most up-to-date and authoritative source"）。

- Windows 后端：**WASAPI、DirectSound、WinMM**（README "Backends" 列表）；许可证 "public domain or MIT No Attribution"；单头文件、零依赖。
- 默认缓冲（`miniaudio.h` 源码宏）：
  - `MA_DEFAULT_PERIODS = 3`
  - `MA_DEFAULT_PERIOD_SIZE_IN_MILLISECONDS_LOW_LATENCY = 10`（低延迟档默认周期 10ms ⇒ 总缓冲约 30ms）
  - `MA_DEFAULT_PERIOD_SIZE_IN_MILLISECONDS_CONSERVATIVE = 100`
- 周期/延迟模型（官方文档）：
  > "Generally speaking, the smaller the period, the lower the latency at the expense of higher CPU usage and increased risk of glitching due to the more frequent and granular data deliver intervals."
  > "`periodSizeInFrames` and `periodSizeInMilliseconds` members of the device config are **just hints**, and are not necessarily exactly what you'll get."
  > "`performanceProfile` ... `ma_performance_profile_low_latency` (default) or `ma_performance_profile_conservative`. This mainly affects the size of default buffers"
- WASAPI 低延迟共享模式（文档 15.1 节）：
  > "Starting with Windows 10, **miniaudio will use low-latency shared mode where possible** which may make exclusive mode unnecessary."
  > "Low-latency shared mode will be disabled when using an application-defined sample rate which is different to the device's native sample rate. To work around this, set `wasapi.noAutoConvertSRC` to true ... miniaudio's internal resampler being used instead which will in turn enable the use of low-latency shared mode."
  实现侧确认：`miniaudio.h` 内部定义并调用 `ma_IAudioClient3_GetSharedModeEnginePeriod(...)`（源码 ~22151/23930 行）。
- WinMM 后端被官方文档用作反面示例："They also want to avoid using the WinMM backend on Windows because it's latency is too high."
- 采样率/格式换算：设备以原生格式打开，miniaudio 内部 data converter（含重采样器）在回调路径做配置格式 ↔ 后端格式转换；重采样器支持**运行时改比率**（见第 5 节）。
- 回调约束（与 NFR-L2 相关）：文档明确回调内不得调用 `ma_device_init/uninit/start/stop`（会死锁）；缓冲长度每次回调可能不同。

### 1.3 Qt Multimedia（QAudioSource/QAudioSink）是否适合 <50ms 低延迟

来源：doc.qt.io Qt 6 官方类文档（当前 6.12 版页面）+ qtmultimedia 6.9.2 源码。

- 官方定位文本（QAudioSource/QAudioSink 详细描述，两页同文）：
  > "The QIODevice interface is designed to be used from the application thread. A wait-free ringbuffer is used to communicate to the audio thread. The size of this ringbuffer can be configured with setBufferSize() and **defaults to 250ms**."
  > "**The preferred way to achieve low audio latencies is to use the callback based interface.** It allows you to read audio data directly from the audio device without having to go through a QIODevice. ... this callback function will be called from the audio thread."
- 版本演进（官方 API 标注）：`setBufferFrameCount`/`bufferFrameCount` since 6.10（`bufferFrameCount()` 文档："If called after start(), returns the actual buffer size being used. This may not be what was set previously"）；回调 `start(Callback&&)` since 6.11；`setNativePeriodFrameCount` since 6.12（对应平台原生周期）。
- Windows 实现源码（`qtmultimedia/src/multimedia/windows/qwindowsaudioutils.cpp`，6.9.2）：
  - 激活 `IAudioClient3`；`Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY | AUDCLNT_STREAMFLAGS_RATEADJUST, ...)`，`hnsBufferDuration = hnsPeriodicity = 设备默认周期（GetDevicePeriod）`，可选 `hardwareBufferFrames` 覆盖。
  - 即：Qt 的 Windows 后端 = **共享模式事件驱动 WASAPI，无独占模式路径**，引擎侧每流自动 SRC（AUTOCONVERTPCM）。
- 结论：Qt 官方文档没有把 QAudioSource/QAudioSink 定位成专业低延迟 API；其 QIODevice 模式默认 250ms 环形缓冲不适合 <50ms；6.11+ 回调接口 + 6.10/6.12 的硬件缓冲控制让 <50ms 变得**可行但需自行验证**（Qt 侧无任何官方延迟承诺数字）。**2026-10-14 反证注记**：正反两方向都缺实测——支持证据（Qt 论坛爆音贴）属 Qt5 时代、Qt6 Windows 后端已重写为 WASAPI，证据力打折；结论维持「不适合/需验证」的对冲表述，判【站得住（暂未找到反证）】。

### 1.4 虚拟音频设备方案盘点与分发政策（法律重点）

#### 1.4.1 VB-CABLE（VB-Audio）官方条款原文

条款页（页脚 "About Licensing / Distribution..." 指向）：`https://vb-audio.com/Services/licensing.htm`（2026-10 抓取，页面版权行 ©1998-2025）。

**Donationware 定义（原文）**：

> "The application is free to use, with all functions available (except optional features), but you are expected to pay your license if you find it useful or if you make any professional use."

**VB-CABLE 随第三方软件分发条款（原文，本节为法律结论依据）**：

> "**VB-CABLE Distribution with other product**
> [1] We allow to distribute VB-CABLE package (available one www.vb-cable.com) with your Application (free or commercial) and possibly embed it in your installation package (VB-CABLE silent installation) if our donationware model is applicable. "Donationware model is applicable", means the End User can see and identify VB-CABLE as VB-Audio application AND this End User must be in a position to donate/pay license if finding it useful. You will have to communicate on this and make it clear for your clients by mentioning at least:
>  The origin of VB-CABLE : www.vb-cable.com.
>  VB-CABLE is a donationware, all participations are welcome.
> In this case we encourage distributors (if they are a significant company) to make a significant donation as licensing fee (e.g 500, 1000, 2000 USD ...). Then we generate the related invoice, mentioning the rights to distribute and sub license VB-CABLE in the defined context or for an estimated number of licenses.
> [2] If you distribute VB-CABLE in a professional domain (to companies, institutions, organizations) where users / employees are not in position to see VB-CABLE and pay for a license, then you will have to buy the requested number of VB-CABLE licenses directly on our webshop (quantity can be set in cart summary).
> **Other VB-CABLEs like VB-CABLE A+B or C+D cannot be distributed or bundled with other product.**"

**对免费/商用分发者的另一官方建议（同页 Affiliation 节，原文）**：

> "For example, if installing VB-CABLE is necessary for your solutions to function properly, it is simpler to invite the user to install it as a prerequisite."

**批量授权价格（同页，原文数字）**：VB-CABLE 卷量授权 "100 Licenses: 3.61 EUR / unit; 1000 Licenses: 2.92 EUR / unit; 10.000 Licenses: 2.50 EUR/ unit"。

**对我们意味着（据原文归纳）**：
1. 主 VB-CABLE（Input↔Output 单根线）**允许随我们的安装包静默分发**（无论我方软件免费或商用），前提是 donationware 模型保持适用：用户能看到 VB-CABLE 出自 VB-Audio（至少标注来源 www.vb-cable.com + "VB-CABLE is a donationware, all participations are welcome"）且用户保有捐赠/购买许可的机会。
2. 若在企业/机构等最终用户无法自行付费的场景分发，则须按数量在 webshop 购买 license。
3. VB-CABLE A+B / C+D（额外线）**不可**随产品捆绑。
4. "引导用户自行安装"是官方明确认可的更简单路线（affiliation/referral 计划）。
5. 产品页原文同口径："VB-CABLE is a Donationware! All participations are welcome. Get your license ... on our webshop. Change the quantity in the cart to get Volume Licensing prices."（https://www.vb-audio.com/Cable/index.htm 与 https://www.vb-cable.com 同文）

#### 1.4.2 Virtual Audio Cable（Eugene Muzychenko，闭源商业）

官方页 software.muzychenko.net/vac.htm 在本网络环境返回 403/404，**条款原文未能取证 → 【未确认】**。仅记录：该产品为付费共享软件（_trial_ 带提示音/水印是社区常识），OEM 分发需联系作者——此句无一手来源，不作结论依据。建议法务侧后续人工核验或直接不采用。

#### 1.4.3 开源替代盘点（许可证逐一核验）

| 项目 | 许可证（取证方式） | 形态与状态（据其 README/源码） | 对我们的可用性 |
|---|---|---|---|
| **microsoft/Windows-Driver-Samples — audio/sysvad** | MIT（仓库 LICENSE 文件原文核验；MS Learn《Sample Audio Drivers》官方指引） | 官方虚拟音频驱动（SYSVAD）样例，可虚拟 speaker/mic；MS Learn 专页教你下载编译 | 自研驱动的正统起点；但需 WDK + 驱动签名（见下）才能给最终用户安装 |
| **VirtualDrivers/Virtual-Audio-Driver**（MikeTheTech） | MIT（GitHub API license 字段） | 虚拟 speaker + 虚拟 mic，基于 WDK；README 首行警告："This software is in beta and **requires test signing to be enabled**"；并提供付费定制（"Contact us for quotes on a custom build!"） | 未签名/测试签名 ⇒ 不能随产品分发给普通用户 |
| **JannesP/AudioMirror** | MIT | 源自 sysvad 样例改造的虚拟声卡；README 自述："This driver isn't finished and has a few quirks. If you want a build and ship ready solution this is sadly not what you were looking for." | 作者自认不可交付 |
| **duncanthrax/scream** | MS-PL（README："Scream is based on Microsoft's MSVAD audio driver sample code. The original code is licensed under MS-PL, as are my changes"） | "Virtual network sound card"：虚拟渲染设备把 PCM 组播到网络，接收端再播；提供 Win10 x64/x86/arm64 **签名**构建 | 形态是"虚拟扬声器→网络"，非本地环回线；可作注入点但引入网络接收器，不适合作为主方案 |
| **mkckr0/audio-share** | Apache-2.0（LICENSE 文件） | 本身**不含** Windows 虚拟驱动；README 详述痛点："At present, I haven't find a way to create virtual audio endpoint. The only way to achieve it is to write a virtual audio device driver. But **it need a EV Code Signing Certificate to sign it. Otherwise, user can't install it.**" | 其 README 是"自研+自签驱动成本高"的一手旁证 |
| **R1kkku/OBS-Virtual-Mic-Plugin** | **GPL-2.0**（GitHub API） | OBS 插件 + AVStream 虚拟麦克风驱动，把 OBS 处理后的音频暴露为系统录音设备 | 概念最贴合我们需求，但 GPL-2.0 与本项目红线（不引入 GPL 依赖）冲突，只能参考思路不可复用代码 |
| "serpent"（任务提示中提及） | — | GitHub 上按名称/描述多路检索（含 API、Web 搜索）**未找到**名为 serpent 的 Windows 虚拟音频驱动项目 | 【未确认】：可能不存在/已改名/未公开索引 |

**结论（开源替代）**：截至检索日，**不存在"许可证干净 + 已签名 + 面向最终用户 + 本地环回"的开源虚拟声卡可直接随包分发**。自研路线 = 基于 sysvad(MIT) 开发 + EV 代码签名证书（社区项目 audio-share 的 README 证实无签名装不上）。现实可选：① 引导/协助用户安装 VB-CABLE（条款允许且官方鼓励"作为前置安装"）；② 需要"零第三方依赖"卖点时才考虑自研驱动并预算签名成本。

#### 1.4.4 OBS 侧的正确接入点（官方源码证据）

来源：obsproject/obs-studio 仓库 master 源码。

- OBS 的 Windows 音频输入插件 `plugins/win-wasapi/`，locale 定义三种源：`AudioInput="Audio Input Capture"`、`AudioOutput="Audio Output Capture"`、`ApplicationAudioCapture="Application Audio Capture (BETA)"`（`data/locale/en-US.ini`）；源码 `enum class SourceType { Input, DeviceOutput, ProcessOutput }`（`win-wasapi.cpp`）。
- 按进程采集实现：`win-wasapi.cpp` 调 `ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, ...)` 且 `audioclientActivationParams.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK`（即 1.1 节 API 的官方级用例）。
- 设备输出采集 = WASAPI loopback（`AUDCLNT_STREAMFLAGS_LOOPBACK`，同文件）；OBS 用 Real-Time Work Queue（RTWQ）回调而非自建线程（`ARtwqAsyncCallback`、`RtwqPutWaitingWorkItem`），与 MS 低延迟文档建议一致。
- OBS 音频监听输出走 WASAPI：`libobs/audio-monitoring/win32/wasapi-output.c`。
- **OBS 不提供"虚拟音频输出设备"**：把 OBS 音频喂给 Discord 的通行做法仍是虚拟声卡（VB-CABLE 等）。OBS 的"虚拟摄像头"思路是**用户态 DirectShow filter**（`plugins/win-dshow/virtualcam-module/virtualcam-module.cpp`，注册 DirectShow source filter、NV12 媒体类型）——不需要内核驱动；音频侧没有对应的官方"虚拟麦克风输出"，仅有上述采集能力。
- "应用程序音频捕获"引入时间线（GitHub API 核验）：OBS 27.0 发布于 2021-06-01，其发布说明不含该功能；引入提交为 **769a29bd1a，2021-09-13**，提交信息原文："win-wasapi: Add support for capturing a process — Use new process output API, and retrofit existing WASAPI abstractions. **Marked as \"(BETA)\" until we figure out the crackling at 60 minutes.**"（与 locale 字样 "(BETA)" 互证）。具体首发的 27.x 小版本号未能从发布说明核实【未确认】；另注意提交自述存在"60 分钟爆音"待修问题，后续提交（2023-10-06 "Use provided timestamp for application audio capture"）持续修复。

#### 1.4.5 Discord 侧接入点

- 官方帮助文档只到"设备选择"层面：Voice & Video 设置里选择输入/输出设备（官方文章：《Where'd my Audio Input go? (+ Various Voice Issues)》https://support.discord.com/hc/en-us/articles/214925018 、《Discord Voice and Video Troubleshooting Guide》https://support.discord.com/hc/en-us/articles/360045138471 ）。
- **接入点结论**：虚拟声卡的 capture 端点（虚拟麦克风）作为标准 Windows 录音设备出现在 Discord 的输入设备下拉中即可被选用；Discord 没有公开的 DirectShow 音频采集或插件接口。
- Discord 桌面端内部是否直接用 WASAPI：**无官方文档 → 【未确认】**（社区通行说法是 Standard 子系统基于 WASAPI，但未找到 Discord 一手信源，不作为依据）。

### 1.5 实时变声软件的音频后端与缓冲策略（w-okada 等）

来源：w-okada/voice-changer 仓库源码与官方教程、RVC 官方仓库源码。

**w-okada VCClient（v2，当前 master）**：
- 客户端模式（浏览器/WebView 内）：**Web Audio AudioWorklet**。`client/lib/worklet/src/voice-changer-worklet-processor.ts`：`private BLOCK_SIZE = 128;`（AudioWorklet 渲染量子固定 128 帧）；播放侧 `playBuffer` 累积、`trancateBuffer()` 在缓冲 >2 块时丢弃（`while (this.playBuffer.length > 2) this.playBuffer.shift();`）——即用"限深队列 + 溢出丢弃"控延迟。
- 推理块大小（CHUNK）：`client/demo/src/components/demo/components2/102-2_ConvertArea.tsx:17` 提供 `nums = [8, 16, 24, 32, 40, 48, 64, 80, 96, 112, 128, 192, 256, 320, ..., 1024, 2048, 4096, 8192, 16384]`，UI 直接换算显示 `x*128*1000/48000` ms（最小 8×128=1024 采样=21.3ms）。**默认值 `inputChunkNum: 192`**（`client/lib/src/const.ts:532`）= 24576 采样 @48k = **512ms**。
- 服务端设备模式（进程直接控麦克风/扬声器）：Python `sounddevice`（PortAudio 绑定），`server/voice_changer/Local/ServerDevice.py`：`sd.InputStream/OutputStream(..., blocksize=block_frame, ...)`，`block_frame = currentInputChunkNum * 128 * (sampleRate/48000)`，**默认 `serverReadChunkSize: 256`**（`ServerDevice.py:47`）= 32768 采样 = **683ms @48k**；且提供 `sd.WasapiSettings(exclusive=True)` 的 **WASAPI 独占模式开关**（`exclusiveMode: bool = False`，`ServerDevice.py:52,306-314`）。
- 官方教程对两种模式的延迟定性（`tutorials/tutorial_device_mode_ja.md` 原文）：client device mode "多少遅延が増える"（延迟略增），server device mode "VC Client が直接マイク/スピーカを扱うので遅延が少ない"（直接控制麦克风/扬声器所以延迟更低）。
- 历史 v1 Python 客户端用 `pyaudio`（`client/python/vc_client.py:2`）。

**RVC（RVC-Project/Retrieval-based-Voice-Conversion-WebUI，MIT）**：
- `realtime_gui.py`：`import sounddevice as sd`（第 34 行），`sd.Stream(..., blocksize=self.block_frame, ...)`（第 755 行左右）。
- 块大小：`self.block_time = 0.25  # s`（默认，第 53 行）；GUI 滑条 `range=(0.02, 1.5)`、`resolution=0.01`（0.02–1.5 秒连续可调）；`crossfade_time = 0.05`、`extra_time = 2.5`；块长对齐到 10ms 整数倍（`zc = samplerate // 100`）。
- 即 RVC 实时链默认音频块 **250ms**、交叉淡化 50ms，SOLA 搜索窗额外预算。

**经验值归纳**：两家最流行的开源实时变声软件，音频块默认值都在数百毫秒（512ms / 250ms），但 UI 都允许下探到 ~20–25ms；说明管线本身支持小 chunk，默认值取大是**稳定性优先**（推理抖动、GC、网络）而非 API 限制。两者音频 IO 均为 PortAudio（sounddevice）或浏览器音频栈，未直连 WASAPI C++（w-okada 在 PortAudio 层暴露 WASAPI 独占开关）。

### 1.6 延迟测量方法（round-trip 与软件内嵌）

- **官方 round-trip 脉冲法**（MS《Low Latency Audio》"Measurement tools" 节原文）：
  > "In order to measure roundtrip latency, user can user utilize tools that play pulses via the speakers and capture them via the microphone. They measure the delay of the following path: The application calls the render API (AudioGraph or WASAPI) to play the pulse → The audio is played via the speakers → The audio is captured from the microphone → The pulse is detected by the capture API"
  同文档还给出环境准备：需装支持小缓冲的驱动（内置 HDAudio 驱动支持 128–480 采样）并说明了测量的是"播放 API → 扬声器 → 麦克风 → 捕获 API"全程。
- **软件内嵌（自查报告）先例**：
  - WASAPI：`IAudioClient::GetStreamLatency`（官方 API 文档："retrieves the maximum latency for the current stream"，100ns 单位）——共享/独占流均可用。
  - PortAudio：`PaStreamInfo::inputLatency/outputLatency`（官方 doxygen："This value provides the most accurate estimate of input latency available to the implementation. It may differ significantly from the suggestedLatency value passed to Pa_OpenStream()"）。w-okada/RVC 生态可由此拿到设备侧延迟估计。
  - WebRTC（历史先例）：`AudioDeviceBuffer::SetVQEData(int playDelayMs, int recDelayMs, int clockDrift)` 把播放/采集延迟与钟漂上报给音频处理模块（ReadyTalk/webrtc 镜像 `webrtc/modules/audio_device/audio_device_buffer.cc`，`RecordedDataIsAvailable(..., totalDelayMS, _clockDrift, ...)`）；现代版本（webrtc.googlesource.com/src 主线，经镜像核对）只保留统计日志。
- **系统适合性检查（非 round-trip）**：Resplendence LatencyMon（官方站自述 "LatencyMon: Check your system for real-time audio capabilities"，测量 DPC/ISR）。
- Superpowered 的 "Audio Latency Test" 移动端 App 与 oblique-audio RTL Utility（macOS）页面均 404，未能取证 → 不引用其具体做法。

### 1.7 时钟漂移与重采样补偿（双时钟域）

问题：全双工时 capture 端点与 render 端点是两个独立时钟域（消费级集成声卡/USB 麦克风尤其如此），长期运行必然漂移。通行做法及一手证据：

1. **ASRC（异步采样率转换）微调比率**——用可变比率重采样器持续微补：
   - soxr（ LGPL/GPL 双许可，我们可用 LGPL）：官方头文件 `src/soxr.h` 在 `soxr_set_io_ratio()` 前注明："For **variable-rate resampling**. See example # 5 for how to create a variable-rate resampler and how to use this function."（运行中随时调整 I/O 比率，`slew_len` 控制平滑）。
   - miniaudio：官方文档 "The sample rate can be **changed dynamically on the fly**. You can change this with explicit sample rates with `ma_resampler_set_rate()` and also with a decimal ratio with `ma_resampler_set_rate_ratio()`."（`ma_data_converter` 同款 API）。
2. **水位控制器 + 超限丢/补样本**——生产级参考实现：PulseAudio `src/modules/module-loopback.c`（同一进程内 source→sink 转发，天然双时钟）：
   - 模块参数即思路：`adjust_time=<how often to readjust rates in s> latency_msec=<latency in ms> max_latency_msec=... fast_adjust_threshold_msec=<threshold for fast adjust in ms>`。
   - 控制器：二阶漂移滤波 + Kalman 滤波（`drift_filter`、`drift_compensation_rate`、`latency_variance/kalman_variance`），按周期把采样率在基率附近微调（限制在 ±1% 内："drift rate ... larger than 1% of the base rate" 视为异常清零）。
   - 粗调：`/* Drop or insert samples if fast_adjust_threshold_msec was specified and the latency difference is too large. */` + `pa_memblockq_drop(...)`（"Dropping ... usec of audio from queue"）。
   - 欠载自适应：连续 underrun 则逐步抬高目标延迟（"Too many underruns, increasing latency to ..."）。
3. **框架内建/上层处理**：
   - WebRTC（历史 API）把 `clockDrift` 与播放/采集延迟一并交给 APM/NetEq 处理（见 1.6 引文）；现代实现改为统计与 NetEq 抖动缓冲。
   - Windows 共享模式引擎侧每流 SRC：`AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM`（Qt 的 Windows 后端即组合使用 AUTOCONVERTPCM + RATEADJUST，见 1.3 源码）——引擎替你消化"应用格式 ≠ 混合格式"，但不解决 capture/render 两设备间的漂移。

### 1.8 典型语音变声链路延迟预算分解（公开数据）与 150ms 现实性

可引用的公开分解数据很少（各产品不公布细分），以下全部来自可核验信源：

| 环节 | 公开数据 | 来源 |
|---|---|---|
| OS 捕获+回放（共享默认 10ms 缓冲） | Win10 较 Win8.1 全链路降 4.5–16ms；引擎渲染 1.3ms、捕获 ~0ms；每向缓冲默认 ~10ms ⇒ OS 往返约 20ms 量级 | MS《Low Latency Audio》 |
| OS 捕获+回放（IAudioClient3 小缓冲） | 每向最小 128 采样=2.66ms@48k（内置 HDAudio 驱动）；官方称共享小周期"latency comparable to exclusive mode" | 同上 + 《Exclusive-Mode Streams》 |
| 音频块（chunk，含缓冲+推理粒度） | w-okada 默认 512ms（可选最小 21.3ms）；RVC 默认 250ms（可调 20–1500ms）+ 交叉淡化 50ms | 两仓库源码（1.5 节） |
| 设备 API 层 | PortAudio `PaStreamInfo` 可自查 input/output latency（数值因主机 API 而异，官方未给通用数字） | PortAudio 官方 doxygen |

**150ms 预算的现实性判断**（【倾向】级结论，依据上述数字推演）：
- OS 层地板：共享小周期下 ~7–10ms、保守 10ms 默认缓冲下 ~20–25ms——~~都只占预算零头~~ **2026-10-14 反证修正：OEM 驱动现实下按 20–45ms/向计（见 §1 反证块），不是零头**；
- 主导项是**推理块 + 交叉淡化 + 模型前处理窗**：RVC 默认 250ms 块单独就超预算；w-okada 默认 512ms。但两者的 UI 均允许 21–25ms 级 chunk，说明"小 chunk 管线"工程上成立，瓶颈在推理耗时/抖动；
- 因此 150ms 全链路（含虚拟声卡环回与 OBS/Discord 消费）**在专用较快推理（小模型/量化/GPU）+ ≤64ms chunk + WASAPI 小缓冲下可达**，但显著优于社区默认配置，属激进目标，必须以 1.6 节脉冲法实测闭环（对应开放问题 O3）。**2026-10-14 反证注记**：以修正后的 OS 层口径（20–45ms/向）与虚拟声卡环回 10–30ms 重算，预算显著收紧——定性结论「可达但激进、必须实测」经住反证，但「激进」的分量上调；且注意 170/90ms 官方数字出自 **RVC README**（非 w-okada），w-okada 侧存在反例 #796（i9-10900K+RX6800XT 仍报 "impossible to make voice work in real time. Too much latency"，作者仅回复换更强硬件）——见 `falsify-realtime.md`。虚拟声卡环回区间已收窄：VB-Audio 官方指南 "256 samples … 5 ms … basically the minimum"，相邻实测 LoopBeAudio ~29ms、RME 硬件环回 ~12.5ms——**按 10–30ms 计**（错配可到数百 ms，MaxOutDelayMS 默认 500ms）。

---

## 2. 对我们 NFR-L1/L2/L3 的含义

**NFR-L1（全链路 ≤150ms）**：
- 预算分解建议：WASAPI 捕获（共享事件驱动，OEM 现实 ~20–45ms/向、小周期驱动 ~3–10ms 仅特定机型）+ 引擎 DSP（STFT 类引擎一帧量级）+ 推理 chunk（建议 ≤64ms 目标）+ WASAPI 回放（同捕获口径）+ 虚拟声卡环回（VB-CABLE 桶深可配置，按 10–30ms 计，须实测）。推理 chunk 与排队策略是主要可控项；OS 层在 OEM 机型上并非零头。
- 独占模式不必上：MS 官方明示 IAudioClient3 共享小周期已"comparable to exclusive mode"，且独占与 loopback 不兼容、霸占端点（与"虚拟声卡给 OBS/Discord"场景冲突——OBS 也要用设备）。
- 引擎必须配"限深队列 + 溢出丢弃/告警"（w-okada worklet 的 `trancateBuffer` 即此模式），否则推理抖动会转化为持续增长的延迟。

**NFR-L2（音频线程零堆分配）**：
- miniaudio 的 data callback 由后端音频线程调用，文档禁止在回调内 init/uninit/start/stop，且每次回调帧数可变——库层面没有"C++ 零堆分配承诺"；零分配要在我们引擎侧落实（P0 债 7 的 `processInto` 变体），配预分配环形缓冲。
- 若走 Qt：6.11+ 的 `start(Callback&&)` 明确"called from the audio thread"，同样要求回调内零分配；QIODevice 模式经 250ms 环形缓冲，不适合低延迟路径。
- 线程优先级：对齐 MS 建议——WASAPI 事件回调线程注册 MMCSS（Audio/ProAudio）或用 RTWQ（OBS 即此做法）。

**NFR-L3（全链路至多一次重采样）**：
- 捕获与回放都以设备/引擎混合格式（GetMixFormat，通常 48k）打开，使 `AUTOCONVERTPCM` 退化为直通；这同时支持把 canonical 采样率改为 48k 可配置（呼应 P0 债 4：视频源 48k 不再双重重采样）。
- miniaudio 场景注意其 WASAPI 规则：请求非原生采样率会禁用低延迟共享模式，除非 `wasapi.noAutoConvertSRC` + 内部重采样器（等于把这一次 SRC 收进我们可控范围）——无论哪条路径，都能做到"只有一次 SRC"，但应显式选一条。
- 漂移补偿（1.7 节）用 soxr/miniaudio 的**运行时微调比率**实现，属于 ppm 级连续修正，应在设计文档中与"结构性重采样"区分论述，避免和 L3 字面冲突。

**红线呼应（法律）**：
- 随包分发 VB-CABLE 需满足其 donationware 条款（标注来源 + 保留用户付费通道；组织场景购 license；A+B/C+D 不可捆绑）——建议默认走"引导用户自装 + 检测缺失时给出官方下载指引"路线，把"静默打包安装"作为获用户明确同意后的可选项。
- OBS-Virtual-Mic-Plugin 等 GPL-2.0 驱动思路可参考、代码不可用（项目红线：不引入 GPL）。
- 自研驱动（sysvad/MIT）需 EV 代码签名证书成本预算，短期内不推荐。

---

## 3. 结论分级

### 【已确认】（官方文档/条款原文/源码直接证据）
1. Win10+ WASAPI 共享模式：引擎渲染延迟 1.3ms、捕获 ~0ms；每向缓冲默认 10ms、驱动可声明小至 128 采样(2.66ms@48k)；IAudioClient3 共享小周期的延迟"与独占模式相当"且保留混音/互操作（MS 官方文档原文）。
2. 独占模式霸占端点（静音其他应用），且不能用于 loopback（MS 官方文档原文）。
3. WASAPI loopback 仅共享模式可用；Win10 1703+ 才支持事件驱动 loopback；按进程采集 = `AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK` + `ActivateAudioInterfaceAsync`（MS 文档 + OBS 源码双证）。
4. VB-CABLE 分发政策：允许随应用（免费或商用）打包/静默安装，条件是 donationware 模型适用（标注来源、用户可付费）；组织场景须购 license；A+B/C+D 禁止捆绑；官方亦推荐"引导用户作为前置安装"（VB-Audio 官方 licensing 页原文）。
5. 开源虚拟声卡现状：sysvad 官方样例 MIT；Virtual-Audio-Driver(MIT, 测试签名, beta)、AudioMirror(MIT, 未完成)、Scream(MS-PL, 网络形态)、audio-share(Apache-2.0, 无自有驱动)——没有可随包分发的已签名本地环拉开源方案；自研需 EV 签名（audio-share README 原文佐证）。
6. OBS 接入点：三种 WASAPI 音频源（输入设备/输出 loopback/按进程 loopback）；虚拟摄像头=用户态 DirectShow filter，无内核驱动；OBS 无官方虚拟音频输出设备（obs-studio 源码）。
7. Qt Multimedia Windows 后端 = WASAPI 共享事件驱动（IAudioClient3 + AUTOCONVERTPCM/RATEADJUST），无独占路径；QAudioSource/Sink 的 QIODevice 模式默认 250ms 环形缓冲，官方称回调接口(6.11+)才是低延迟首选（Qt 文档 + 6.9.2 源码）。
8. miniaudio：Windows 后端 WASAPI/DirectSound/WinMM，Win10+ 自动用低延迟共享模式；默认低延迟档 10ms×3 周期；周期只是 hint；公共域/MIT-0（官方 README + miniaudio.h）。
9. w-okada 音频栈：Web Audio worklet(128 帧量子) / PortAudio(sounddevice)，server 设备模式含 WASAPI 独占开关；默认 chunk 512ms（客户端）/683ms（server 设备），UI 可选最小 21.3ms（仓库源码）。
10. RVC 实时链：sounddevice，默认块 250ms（20ms–1.5s 可调）+50ms 交叉淡化（仓库源码）。
11. 漂移处理通行件：soxr `soxr_set_io_ratio` 变比率重采样、miniaudio `ma_resampler_set_rate_ratio` 运行时改比率、PulseAudio loopback 的水位控制器（漂移滤波+Kalman+超限丢样本+欠载加缓冲）（各官方源码/头文件）。
12. WebRTC 历史 API 把 playDelay/recDelay/clockDrift 上报 APM（镜像源码）；现代实现不再暴露该 API（镜像核对）。

### 【倾向】（多源交叉可推，但无单一官方承诺）
1. 150ms 全链路可达性：OS 层地板 ~10–25ms，剩余预算取决于推理 chunk 与抖动管理；社区产品默认 250–700ms 但管线支持 21–25ms chunk。判定"可达但激进"，需 O3 实测闭环。
2. Discord 接入 = 让虚拟麦克风出现在系统录音设备枚举里（依据：Discord 官方支持文档指引用户在 Voice & Video 设置中选择输入/输出设备；VB-CABLE 官方页自述其输出端"as Recording Device"出现在系统音频设备列表——两文档组合可推，Discord 侧未单独提供接入文档）。
3. Qt 回调模式可做到 <50ms：文档定性（"preferred way to achieve low audio latencies"）+ 平台原生缓冲可控（6.10/6.12 API），但无官方数字承诺，需实测。
4. "VB-CABLE 环回延迟桶深可配置且很小"：VB-CABLE 有内部缓冲配置（其控制面板），本次未取官方数字，不单列依据——并入需实测项。

### 【未确认】（查不到一手信源，禁止当事实用）
1. "serpent" 开源虚拟音频驱动：GitHub/搜索引擎均未检索到 → 可能不存在或已更名。
2. Virtual Audio Cable（Muzychenko）的许可/再分发条款原文：官方站点本网络不可达。
3. Discord 桌面端音频子系统是否 WASAPI 直连：无官方文档。
4. Superpowered Audio Latency Test 与 RTL Utility 的具体测量流程：页面 404。
5. WebRTC **现行**主线对 capture/render 钟漂的具体补偿实现（googlesource 不可达，镜像只见统计日志）：仅历史 API 可证。
6. VB-CABLE 虚拟声卡自身的环回延迟数值：官方未公布可引用数字。
7. 语音链路 DSP 段的公开延迟分解数值（各产品不公布细分；本表仅 OS 层与 chunk 层有据）。
8. OBS"应用程序音频捕获"首发的 27.x 小版本号（27.0 发布说明不含该条目；27.1+ 发布说明正文亦未检索到该词，功能存在与引入时间以提交 769a29bd1a 与源码为准）。

---

## 4. 参考链接清单

**Microsoft 官方文档**
- Low Latency Audio：https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio
- Exclusive-Mode Streams：https://learn.microsoft.com/en-us/windows/win32/coreaudio/exclusive-mode-streams
- Loopback Recording：https://learn.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording
- AUDIOCLIENT_ACTIVATION_PARAMS（进程 loopback）：https://learn.microsoft.com/en-us/windows/win32/api/audioclientactivationparams/ns-audioclientactivationparams-audioclient_activation_params
- IAudioClient::GetStreamLatency：https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getstreamlatency
- Sample Audio Drivers（SYSVAD 官方指引）：https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/sample-audio-drivers
- IAudioClient3（InitializeSharedAudioStream）：https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient3-initializesharedaudiostream

**VB-Audio（条款原文）**
- VB-CABLE 产品页：https://www.vb-audio.com/Cable/index.htm （镜像 https://www.vb-cable.com ）
- Licensing / Distribution 条款页：https://vb-audio.com/Services/licensing.htm
- WebShop（VB-CABLE-Windows）：https://shop.vb-audio.com/en/win-apps/11-vb-cable.html

**Qt 官方**
- QAudioSource：https://doc.qt.io/qt-6/qaudiosource.html
- QAudioSink：https://doc.qt.io/qt-6/qaudiosink.html
- qtmultimedia Windows WASAPI 实现：https://github.com/qt/qtmultimedia/blob/6.9.2/src/multimedia/windows/qwindowsaudioutils.cpp 与 qwindowsaudiosource.cpp

**miniaudio**
- 仓库/README：https://github.com/mackron/miniaudio
- 权威文档（miniaudio.h 文件头）：https://raw.githubusercontent.com/mackron/miniaudio/master/miniaudio.h ；https://miniaud.io/docs/

**OBS**
- win-wasapi 插件（音频源/进程采集）：https://github.com/obsproject/obs-studio/tree/master/plugins/win-wasapi
- 虚拟摄像头模块（DirectShow）：https://github.com/obsproject/obs-studio/tree/master/plugins/win-dshow/virtualcam-module
- 进程音频捕获引入提交（2021-09-13，含 "(BETA)" 自述）：https://github.com/obsproject/obs-studio/commit/769a29bd1a
- 27.0 发布说明（2021-06-01，经 API 核验不含 Application Audio Capture）：https://github.com/obsproject/obs-studio/releases/tag/27.0.0

**Discord 官方支持**
- Voice & Video 输入设备：https://support.discord.com/hc/en-us/articles/214925018-Where-d-my-Audio-Input-go-Various-Voice-Issues
- 音频故障排查指南：https://support.discord.com/hc/en-us/articles/360045138471-Discord-Voice-and-Video-Troubleshooting-Guide

**实时变声软件（源码）**
- w-okada/voice-changer：https://github.com/w-okada/voice-changer （client/lib/worklet/src/voice-changer-worklet-processor.ts、client/lib/src/const.ts、server/voice_changer/Local/ServerDevice.py、tutorials/tutorial_device_mode_ja.md）
- RVC：https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI （realtime_gui.py）

**虚拟声卡开源项目**
- Microsoft SYSVAD 样例：https://github.com/microsoft/Windows-driver-samples/tree/main/audio/sysvad （MIT）
- VirtualDrivers/Virtual-Audio-Driver：https://github.com/VirtualDrivers/Virtual-Audio-Driver （MIT）
- JannesP/AudioMirror：https://github.com/JannesP/AudioMirror （MIT）
- duncanthrax/scream：https://github.com/duncanthrax/scream （MS-PL）
- mkckr0/audio-share：https://github.com/mkckr0/audio-share （Apache-2.0；虚拟驱动讨论 https://github.com/mkckr0/audio-share/discussions/59 ）
- R1kkku/OBS-Virtual-Mic-Plugin：https://github.com/R1kkku/OBS-Virtual-Mic-Plugin （GPL-2.0，仅思路参考）

**重采样/漂移/测量**
- soxr（src/soxr.h）：https://github.com/chirlu/soxr
- PulseAudio module-loopback.c：https://github.com/pulseaudio/pulseaudio/blob/master/src/modules/module-loopback.c
- PortAudio PaStreamInfo：https://www.portaudio.com/docs/v19-doxydocs/structPaStreamInfo.html
- LatencyMon：https://www.resplendence.com/latencymon
- WebRTC AudioDeviceBuffer（历史镜像）：https://github.com/ReadyTalk/webrtc/blob/master/webrtc/modules/audio_device/audio_device_buffer.cc ；现行主线（本网络不可达）：https://webrtc.googlesource.com/src/+/refs/heads/main/modules/audio_device/audio_device_buffer.cc
