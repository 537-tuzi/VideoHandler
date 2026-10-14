# 实时音频结论反证审查（falsify-realtime）

## 0. 元信息

- **审查性质：反证审查**。本文件刻意站在「原结论是错的」立场，只搜集能推翻原结论的证据；找不到反证的目标只记「暂未找到反证」，不视为证实。反证不充分的判级从低。
- 审查日期：**2026-10-14**
- 审查对象：`docs/reference/research-realtime-audiostack.md`（下称「原研究」）中的实时音频结论，及其下游影响 `docs/REQUIREMENTS.md` §5.1 延迟预算表（141–149 行）与 NFR-L1。
- 判级定义：
  - 【站得住】——反证失败，且原结论限定词与证据匹配；
  - 【需修正】——反证成立，原结论主体可保留但必须改限定条件或数字；
  - 【被推翻】——反证直接否定原结论；
  - 【原证据不足】——原结论所依赖的证据链本身拿不出一手来源。
- 取证方式与网络限制（诚实声明）：
  - MS Learn / voicemeeter.com 官方页：webReader 或 curl 直接抓取原文，逐字引用；
  - miniaudio.h：经 cdn.jsdelivr.net 取 master 快照（文件头自述 `v0.11.25 - 2026-03-04`），行号以该快照为准；
  - GitHub issues（mackron/miniaudio#949、w-okada/voice-changer#796/#686、RVC#2859）：匿名 REST API 拉取原文（部分请求撞限流，关键引文均在限流前取得并逐字录入本文件）；
  - w-okada 官方 README/tutorials：jsdelivr 拉取后本地 grep；
  - 无法取到原帖、仅凭检索摘要的条目，一律标注【检索级，未取到原帖】，不得与其他证据等同使用；
  - 本环境 WebFetch 域名核验被禁、raw.githubusercontent.com SSL 失败、old.reddit.com JSON 被拦——reddit/论坛部分内容只有检索级。

---

## 1. 反证目标一：「WASAPI 共享 + IAudioClient3 小周期 ~10ms/方向，128 采样=2.66ms@48k 可行」

### 原结论定位

- 原研究 38 行：「Win10+ 共享模式下，配 IAudioClient3 小缓冲，OS 侧双向缓冲可压到每向 ~2.7–3ms；默认（应用不指定缓冲）仍为 10ms」；213 行表格同口径。
- 原研究 227 行（§2）：「WASAPI 捕获（共享事件驱动，~3–10ms）…… OS 层不是瓶颈」。
- REQUIREMENTS.md 144 行：「输入捕获缓冲 ≤20ms | WASAPI 共享 + IAudioClient3 小周期（官方数字 ~10ms/方向，128 采样=2.66ms@48k 可行）」。

### 反证 A（官方 FAQ 直接推翻「任意设备可行」）：小缓冲需要驱动逐机型适配，OEM 决定，默认 10ms

MS Learn《Low Latency Audio》FAQ（2026-10-14 webReader 抓取原文）：

> "Will all systems that update to Windows 10 and later be automatically update to support small buffers? Will all systems support the same minimum buffer size? — **No, in order for a system to support small buffers it needs to have updated drivers. It's up to the OEMs to decide which systems will be updated to support small buffers.** Also, newer systems are more likely to support smaller buffers than older systems."

> "If a driver supports small buffer sizes, will all applications in Windows 10 and later automatically use small buffers …? — **No, by default all applications in Windows 10 and later will use 10-ms buffers**"

即：**2.66ms 小周期是「驱动主动声明才有的能力」，不是 Win10+ 的普遍属性**。原研究 38 行引用的「128–480 采样」区间在 MS 文档中有明确主语——"The inbox HDAudio driver"（内置 HDAudio 驱动）。且该文档的测量准备步骤要求用户**在设备管理器里手动把第三方声卡驱动换成微软内置 HDAudio 驱动**（"After rebooting, the system will be using the inbox Microsoft HDAudio driver and not the third-party codec driver"）——MS 自己测小缓冲都要先换掉 OEM 驱动。驱动侧义务：必须经 `DEVPKEY_KsAudio_PacketSize_Constraints2` 声明最小缓冲（"[Mandatory] Declare the minimum buffer size that is supported in each mode"）；不声明即默认 10ms。

### 反证 B（实测设备上的 IAudioClient3 协商结果）：Realtek 驱动 min=max=default=480，小周期被驱动顶回 10ms

miniaudio issue #949《Windows Audio Latency》（2025-02-17 提出，2026-01-04 关闭；GitHub API 原文）。报告者在 Realtek 芯片组笔记本（Win10/Win11 各一台）上跑官方 latency 测量程序，调试日志原文：

```
DEBUG:     defaultPeriodInFrames=480
DEBUG:     fundamentalPeriodInFrames=480
DEBUG:     minPeriodInFrames=480
DEBUG:     maxPeriodInFrames=480
DEBUG: [WASAPI] Using IAudioClient3
DEBUG:     periodSizeInFramesOut=480
INFO: [WASAPI]
INFO:   Speakers (Realtek(R) Audio) (Playback)
```

即 **IAudioClient3 被调用了、也初始化成功了，但驱动声明的最小周期就是 480 帧=10ms，小周期申请被合法顶回**。同一 issue 实测 round-trip（播放→录制全程，报告者原帖）：

> "Result: Mac: 1ms,, Windows 10: **83ms** Windows 11: **93ms.** Windows is using WASAPI both are laptops with a Realtek chipset."

报告者后续改进测量程序并开独占模式再测（同 issue 评论原文）：

> "As baseline (internal sound card) shared mode: **45ms** / exclusive mode: **125ms** — Requested buffer size is 128 frames. No idea why latency is about 3 times as high, I was hoping for a reduction."

另一台旧 Win10 机器（同 issue）："Capture/Recording in Shared: about 45 ms … Capture Exclusive / Recording Shared: 30 ms … **Capture and Recording in Exclusive mode: 4-5MS**. Running on the old Windows 10 hardware easy on 64 frames."

USB 音箱设备：共享 ~70ms / 独占 ~40ms（同 issue 评论，API 限流前读取记录）。

### 反证 C（「独占模式兜底」不成立）

- 同 #949：新 Realtek 笔记本上独占模式 125ms，**比共享 45ms 更差 3 倍**；只有旧 Win10 机器独占做到 4–5ms。独占不是可靠兜底，效果高度依赖驱动。
- miniaudio 作者 mackron 在 #949 回复中自认独占路径实现不稳："…fiddly. It would not surprise me at all if there's some subtle errors in there."（评 miniaudio 独占模式实现）。

### 反证 D（旁证，低置信）

- JUCE 论坛 topic 35958（2019-11）：Win10 1903 WASAPI 延迟 40ms 级报告（一手论坛帖，抓取于本审查前期）。
- soundbridge.io 科普文称 WASAPI 共享典型 round-trip 20–35ms：营销向站点的概括数字，【检索级，未取到测量方法】，仅作方向性旁证。

### 判级：【需修正】

机制描述（IAudioClient3 存在、默认 10ms、128 采样是官方给出过的数字）都对，但「可行」二字缺了关键限定，且实测量级推翻「OS 层只占零头」：

1. **2.66ms/向是内置 HDAudio 驱动专属能力**；OEM Realtek 等常见驱动普遍未声明小缓冲（实测 min=max=480=10ms）。MS FAQ 明说由 OEM 决定、非普遍。
2. **实测共享模式 round-trip 45–93ms**（Realtek 笔记本 ×3 台、USB 设备 70ms）——不是「~20ms 量级」，更不是 7–10ms。REQUIREMENTS §5.1 的「输入捕获 ≤20ms」在普通用户设备上不保真：单方向估计 20–45ms。
3. 独占模式不能当兜底（实测可能更差 + 库实现不稳 + 原研究自己也引 MS 不推荐）。

**修正建议**：REQUIREMENTS §5.1 输入捕获行改为「共享默认 ~10ms 周期 ×2–3 周期 ⇒ 单向 20–30ms 典型；仅当驱动声明小缓冲（GetSharedModeEnginePeriod 的 min < default）时才可能 3–10ms；预算按 30ms 计，实测项按设备报告」。原研究 227/218 行「OS 层不是瓶颈/只占零头」应改为「OS 层在最常见 OEM 驱动上是 20–45ms/向的实测量级，占 150ms 预算的 1/4–1/3」。

---

## 2. 反证目标二：「miniaudio Win10+ 自动启用 IAudioClient3」

### 机制核实（源码，v0.11.25 master 快照 2026-10-14）

- WASAPI 后端共享模式确有 IAudioClient3 默认通路（`miniaudio.h` 23909–23998 区域）：`ma_IAudioClient3_GetSharedModeEnginePeriod(...)` → `InitializeSharedAudioStream(...)`；失败则逐级回退普通 `IAudioClient`（23962/23968/23983 行附近）。
- 文档头 8865 行："Starting with Windows 10, miniaudio will use low-latency shared mode where possible which may make exclusive mode unnecessary."
- 三个已核实的限定（源码注释与条件）：
  - 23913–23918 行注释：MSDN 文档与实机相反，`AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM`/`SRC_DEFAULT_QUALITY` 与 `InitializeSharedAudioStream` 组合必得 0x88890021，故此情形禁用低延迟共享模式；
  - 23922 行条件：应用请求采样率 ≠ 设备原生率（即走 AUTOCONVERTPCM）时禁用，除非 `wasapi.noAutoConvertSRC`（3641–3646 行文档）；
  - 23940 行：`actualPeriodInFrames = ma_clamp(actualPeriodInFrames, minPeriodInFrames, maxPeriodInFrames);` —— **申请周期被驱动声明的 [min,max] 钳制**。

### 反证（针对「自动启用 ⇒ 自动低延迟」的隐含推论）

- **默认档不会申请小周期**：`MA_DEFAULT_PERIODS = 3`、`MA_DEFAULT_PERIOD_SIZE_IN_MILLISECONDS_LOW_LATENCY = 10`（12216–12227 行）。默认低延迟档申请的就是 480 帧=10ms 周期（总缓冲 30ms/向）——被 Realtek 驱动钳制后结果完全相同（见目标一反证 B 的日志：`periodSizeInFramesOut=480`）。要小周期必须显式设 `periodSizeInFrames`，且驱动配合。
- **实测**：#949 的 45–93ms round-trip 就是在「miniaudio 自动启用了 IAudioClient3」的机器上测出来的。
- 结论准确性核对：原研究 §3 第 8 条措辞为「Win10+ 自动用低延迟共享模式；默认低延迟档 10ms×3 周期；周期只是 hint」——**该条本身准确**，无夸大。

### 判级：【站得住】

反证未能推翻原表述（机制、默认值、hint 性质均与源码一致）。但记录一个传导性风险：REQUIREMENTS 144 行把它引作「≤20ms 输入捕获」的选型依据——**「自动启用 IAudioClient3」≠「拿到小周期」**，中间隔着驱动声明（目标一）。选型结论（miniaudio 可用）不受影响；延迟预期必须按目标一的修正值计。

---

## 3. 反证目标三：「RVC 实时 150ms 激进可达（区间证据 w-okada 官方 170ms/ASIO 90ms）」

### 反证 A：170ms/ASIO 90ms 两个数字溯源失败

- 任务描述称该区间证据来自「w-okada 官方」。本次将其官方材料全部拉取并 grep（jsdelivr，2026-10-14）：`README_en.md`、`tutorials/tutorial_rvc_en_latest.md`、`tutorials/tutorial_device_mode_ja.md`、`tutorials/tutorial_monitor_consept_ja.md`、`vc_readme_en.md`——**均无 170ms、90ms 或任何总延迟毫秒数承诺**。
- 原研究 §1.5/§1.8 亦无此二数（只有默认 chunk 512/683/250ms 与「buf+res」模型）。
- w-okada 官方教程对延迟的唯一表述是定性模型（`tutorial_rvc_en_latest.md` 150 行）：

> "The lag time from speaking to conversion is `buf + res` seconds."

  连官方都不给总延迟数字（因为它取决于用户硬件的 res）。**170/90 的出处须由上游提供，否则应从我们的论证链中删除。**

### 反证 B：社区实测——高端硬件也压不到实时

w-okada issue #796《what is the best hardware configuration to have minimal latency?》（2023-09-06，GitHub API 原文）：

> "I'm currently using the software on an i9 10900K, 32GB ram and RX6800XT. But **it's impossible to make voice work in real time. Too much latency.** Based on your experience, what is the best hardware configuration to achieve minimal latency?"

作者 w-okada 本人回复（2023-09-06）：

> "Using a better-performing GPU/CPU can further reduce latency. It's as simple as that."

——作者未否认延迟问题，只答「换更强的硬件」。旁证：评论区推荐的山寨设置库 `YunaOneeChan/Voice-Changer-Settings` 现已 404（GitHub API 核验），社区量化数据无处可查。

其他案例：

- w-okada issue #686：RTX 3060 12GB + i5-4460，延迟 10–20 秒（设置明显异常的故障案例，弱证据，只说明「坏配置可以坏到离谱」）。
- RVC issue #2859（2026-09-21）：GPU 过载后 VST 模式延迟持续偏高不回落（说明推理抖动会残留在管线里，印证「限深队列+丢弃」的必要性）。
- Reddit r/TransVoicechangers 用户称游戏中 w-okada 延迟大到只能妥协接受 8–10 秒：【检索级，未取到原帖，JSON 被拦】。

### 反证 C：官方默认值佐证

w-okada 默认 chunk 512ms/683ms、RVC 默认 250ms + 50ms crossfade（原研究 §1.5 已核）——两家最流行实现的默认值都远超 150ms，且没有任何官方材料声称过 150ms 级。

### 盲点（如实记录）

- **RTX 3060 级 GPU 上 RVC 单 chunk 推理耗时的一手 benchmark 未找到**（检索引擎质量差 + 无公开系统数据）。因此「chunk 64ms + 推理 ≤30ms」这一关键乘积项无实测锚点，150ms 判断在正反两个方向都缺定量证据。

### 判级：【原证据不足】

- 「170ms/ASIO 90ms」区间证据**无一手出处**，不得引用；
- 「150ms 激进可达」在社区一手证据（#796 等）下**方向偏悲观**：i9+RX6800XT 级硬件都报「无法实时」；
- 原研究自身对此的分级是【倾向】「可达但激进」并以 O3 实测为闸门——该谨慎立场未被推翻，可保留；但 REQUIREMENTS/后续文档若引用任何具体毫秒数（170/90），须先补出处。

---

## 4. 反证目标四：VB-CABLE 环回延迟量级（原研究标「未知」）

### 新取得的一手官方证据（VB-Audio 官网，2026-10-14 curl 直接抓取核验）

`https://voicemeeter.com/how-to-optimize-your-latency-in-voicemeeter/`（官方 QUICK TIP 页）原文：

> "A buffer size of 256 samples at 44.1 or 48 kHz results in a latency of 5 ms. **This is basically the minimum latency we can expect to manage reliable from the Windows Operating System. Below 256 samples the risk of dropouts and sound distortions is high.**"

> "The lowest latency is usually delivered by an ASIO driver, configured with a buffer size around 256 samples. If there is no ASIO Driver present, the WDM/KS device can be used. Some of them could work with 256 sample buffer, without stability issues, but some may not."

这是 VB-Audio（VB-CABLE 同厂）对虚拟音频链路缓冲地板的官方工程立场：**5ms/256 采样是可靠下限，更低则爆音风险高**。

### 相邻产品实测（Gig Performer 社区帖 3960，2020-05，一手抓取）

- LoopBeAudio（另一款虚拟音频线）实测环回 ~29ms（经 ASIO4ALL、512 采样最小缓冲）；
- RME UCX 硬件环回实测 ~12.5ms。

### 失败配置的量级（检索级）

- SDR 论坛用户报 VAC/VB-CABLE 链路 150–250ms（缓冲错配案例）：【检索级，未取到原帖】；
- Muzychenko VAC FAQ：虚拟线延迟 ≈ 中断间隔量级（~10ms 级）：【检索级，官网本环境 403】；
- VB-Audio 论坛/手册：`MaxOutDelayMS` 类内部缓冲默认 500ms 量级：【检索级】——错配时可到数百 ms。

### 判级：【原证据不足 维持】（VB-CABLE 专属数字仍缺），但量级区间可收窄

原研究 §3【未确认】第 6 条（官方无 VB-CABLE 环回数字）维持成立。反证审查的贡献是把「未知」收窄为可用的工程区间：

- **设计下限 ~5ms/端**（官方 256 采样声明）；典型两端环回 10–30ms（LoopBeAudio 29ms 实测旁证）；错配上限数百 ms。
- 对预算的含义：若链路是「我们 → VB-CABLE → Discord/OBS」，虚拟声卡一环按 **10–30ms** 计合理；REQUIREMENTS 表若按 0 计则少算，按 30ms 计属合理偏保守。

---

## 5. 反证目标五：「Qt Multimedia 不适合 <50ms 低延迟」

### 支持原结论的证据（原研究已列 + 本次补充）

- QAudioSource/QAudioSink QIODevice 模式默认 250ms 环形缓冲：官方文档字面（原研究 §1.3 引文，本次未再核）。
- Qt 官方无任何低延迟毫秒承诺，只有「callback 是 preferred way」的定性。
- Qt 论坛 topic/61138（2015-11，一手抓取）：用户 paulmasri 报 Qt 音频低于 ~5000 采样缓冲即爆音；Lifetime Qt Champion SGaist 回复："low latency is currently not in available with Qt… use a framework like PortAudio"。

### 反证尝试（找「Qt 做到 <50ms」的正例）：暂未找到反证

- 检索 Qt6 QAudioSource/QAudioSink 低延迟实测成功案例：无一手测量帖。只有 API 可用性文档（`start(Callback&&)` since 6.11、`setBufferFrameCount` since 6.10、`setNativePeriodFrameCount` since 6.12）。
- 反偏置声明（该证据对原结论同样有削弱作用，如实记录）：**topic/61138 是 Qt5 时代的经历**；Qt6 Windows 后端已重写为 WASAPI 共享事件驱动（原研究 §1.3 有 6.9.2 源码证据），旧爆音数据不能直接外推到 Qt6。同理，Qt6 也拿不出 <50ms 的正例。

### 判级：【站得住】

- 「QIODevice 模式默认 250ms、不适合 <50ms」——文档字面事实，无可反证。
- 「回调模式 <50ms 可行但需自行验证」——正反双方都无实测，维持原研究的对冲表述是正确的；不建议升级为「可行」。

---

## 6. 反证目标六：150ms 全链路预算可达性整体审查（最坏情形算术）

### 原预算口径

- REQUIREMENTS.md 141–149 行表：输入捕获 ≤20 + 重采样 ≤5 + STFT ≤40 + GPU ≤30/chunk + 重叠相加/输出 ≤30 ⇒ 合计目标 ≤150ms。
- 任务侧算式：「20 + 5 + chunk(64–96) + 30 + 30 ≤ 150」。

### 用本次反证数据重算（关键修正：OS 层与虚拟声卡不再按最优值计）

| 环节 | 原预算 | 反证后的现实区间 | 依据 |
|---|---|---|---|
| 输入捕获（共享 WASAPI，OEM 驱动） | ≤20ms | **20–45ms**（周期 10ms×2–3 + 引擎/调度；实测 round-trip 折半亦 ≥22ms） | 目标一反证 B |
| 重采样 | ≤5ms | ≤5ms（未找到反证） | — |
| chunk 粒度 | 表内未单列（146 行注明「64–96ms 与 STFT 叠加」） | **64–96ms** | 原研究 §1.5 |
| STFT/DFN3 算法延迟 | ≤40ms | ≤40ms（未找到反证；审查范围外） | — |
| GPU 推理/chunk | ≤30ms | **无实测锚点**（#796：高端卡也报无法实时） | 目标三 |
| 重叠相加+输出缓冲 | ≤30ms | **20–30ms**（对称于输入侧） | 目标一 |
| 虚拟声卡环回（VB-CABLE） | 表内**未列** | **10–30ms** | 目标四 |
| 消费端（Discord/OBS 输入缓冲） | 表内**未列** | ≥10ms，无一手数字（盲点） | — |

三种情形合计（不含消费端）：

- 最好情形（驱动声明小缓冲 + chunk 64 + 各项达预算）：20+5+64+40+30+20+10 ≈ **189ms** —— 注意：即使全部按原预算数值代入，只要把 146 行自己声明的「chunk 与 STFT 叠加」算进去，**原表 5 行相加（125ms）漏掉了 chunk 64–96ms 这一项**；
- 典型情形（OEM 驱动 30ms/向 + chunk 96）：30+5+96+40+30+30+20 ≈ **251ms**；
- 只有把 chunk 当作「已包含在 STFT 40ms 行内」且 OS 层取 20ms 最好值时，20+5+40+30+30+30 ≈ 155ms——仍贴着 150ms 红线。

### 判级：【需修正】

1. **算术层面**：REQUIREMENTS §5.1 表把「chunk 64–96ms」与「STFT 40ms」的叠加关系藏在备注里、未进合计行；显式相加后最坏 ~190–250ms，超出 150ms。要么把 chunk 并入某一行并写清口径，要么把 NFR-L1 改为分级目标（如软件内 ≤150ms / 全链路 ≤200ms）。
2. **口径层面**：NFR-L1 说「全链路」（原研究 220 行明确含虚拟声卡环回与 OBS/Discord 消费），但 §5.1 表不含这两行——表与 NFR 口径不一致，属审查中发现的新问题。
3. **数据层面**：OS 层实测 45ms/向级（目标一）推翻「OS 只占零头」的乐观估计；GPU 推理 ≤30ms/chunk 无任何一手 benchmark 支撑（目标三盲点）。
4. 原研究「可达但激进 + O3 实测闸门」的**定性结论本身经住了反证**（没有任何一手数据证明 150ms 轻松可达）；错的是把它具象成的预算表数字。

---

## 7. 判级汇总

| # | 原结论 | 判级 | 一句话理由 |
|---|---|---|---|
| 1 | WASAPI 共享 + IAudioClient3，~10ms/方向、128 采样=2.66ms 可行 | **【需修正】** | MS FAQ：小缓冲须 OEM 驱动适配、默认 10ms；Realtek 实测 min=max=480、round-trip 45–93ms |
| 2 | miniaudio Win10+ 自动启用 IAudioClient3 | **【站得住】** | 源码核实为真；但「自动启用 ≠ 拿到小周期」（默认申请就是 10ms 周期 + 驱动钳制） |
| 3 | RVC 实时 150ms 激进可达（170ms/ASIO 90ms） | **【原证据不足】** | 170/90 在 w-okada 官方材料中溯源失败；#796 高端硬件报「无法实时」；正向 benchmark 缺失 |
| 4 | VB-CABLE 环回延迟量级（原标「未知」） | **【原证据不足 维持】** | VB-CABLE 专属数字仍无官方出处；但官方 5ms/256 采样地板 + 相邻实测把区间收窄为 10–30ms 典型 |
| 5 | Qt Multimedia 不适合 <50ms | **【站得住】** | 未找到 Qt6 <50ms 正例；注意支持证据多为 Qt5 时代，对 Qt6 证据力打折 |
| 6 | 150ms 全链路预算可达 | **【需修正】** | chunk 项未进合计行；OS 层按实测 20–45ms/向计后，最坏 ~190–250ms；表口径（软件内）与 NFR-L1 口径（全链路）不一致 |

## 8. 对项目文档的具体修改建议（供采纳，本次不改）

1. REQUIREMENTS.md 144 行输入捕获行：加驱动依赖限定，预算值 20→30ms，加「按设备实测报告」验收语。
2. REQUIREMENTS.md §5.1 表：把 chunk 64–96ms 显式入表或并入 STFT 行并写清口径；补「虚拟声卡环回 10–30ms」行；明确 NFR-L1 的「全链路」是否含消费端。
3. research-realtime-audiostack.md 218 行「OS 层地板只占零头」→ 按 #949 实测改写；213 行表格补驱动前提。
4. 任何文档不得引用「170ms/ASIO 90ms」直到补出一手出处。
5. O3（RVC 延迟实测）闸门保持关闭状态，且实测必须覆盖「普通 OEM 声卡」而不只是开发机。

## 9. 盲点与未收敛项

- RTX 3060 级 GPU 单 chunk RVC 推理耗时：无一手 benchmark（正反两方向都缺锚点）。
- Discord/OBS 作为消费端的输入缓冲延迟：无官方数字。
- VB-CABLE 自身环回的官方/实测毫秒数：仍缺（区间 10–30ms 为推断+旁证，未收敛）。
- miniaudio #949 的 83/93ms 初测与 45ms 复测的差异未完全归因（测量程序改进 + 默认缓冲叠加），引用时注明不确定性。
- Reddit/论坛搜索摘要级条目（r/TransVoicechangers 8–10s、SDR 论坛 150–250ms）未取到原帖，仅方向性参考。

## 10. 信源清单（按证据等级）

**A. 一手直接抓取（原文逐字引用）**
- MS Learn《Low Latency Audio》：https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/low-latency-audio （2026-10-14 webReader）
- VB-Audio 官方延迟指南：https://voicemeeter.com/how-to-optimize-your-latency-in-voicemeeter/ （2026-10-14 curl 核验）
- miniaudio.h master 快照（v0.11.25，2026-03-04）：https://cdn.jsdelivr.net/gh/mackron/miniaudio@master/miniaudio.h （行号见目标一/二）

**B. 一手经 GitHub API 取原文**
- miniaudio#949：https://github.com/mackron/miniaudio/issues/949 （含调试日志与多轮实测）
- w-okada#796：https://github.com/w-okada/voice-changer/issues/796 ；#686 同仓库
- RVC#2859：https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI/issues/2859
- w-okada 官方教程（jsdelivr）：tutorials/tutorial_rvc_en_latest.md（150 行 buf+res 公式）、tutorial_device_mode_ja.md、tutorial_monitor_consept_ja.md、README_en.md、vc_readme_en.md（170/90ms 溯源失败的全部排查范围）

**C. 一手论坛帖（抓取于本审查前期）**
- Qt 论坛 topic/61138（2015-11）：https://forum.qt.io/topic/61138
- JUCE 论坛 topic 35958（2019-11）：https://forum.juce.com/t/35958
- Gig Performer 社区帖 3960（2020-05）：LoopBeAudio ~29ms / RME UCX ~12.5ms 实测

**D. 检索级（未取到原帖，不作独立依据）**
- r/TransVoicechangers 游戏场景 8–10s 妥协贴；SDR 论坛 VAC/VB-CABLE 150–250ms 案例贴；Muzychenko VAC FAQ 中断间隔量级说明；VB-Audio 论坛 MaxOutDelayMS 默认 500ms；soundbridge.io WASAPI 20–35ms 概括。
