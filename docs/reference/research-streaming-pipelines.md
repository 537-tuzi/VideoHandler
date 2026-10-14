# 流式音频/DSP 管线中间件架构对照：IStreamEngine v2 前置调研

- 日期：2026-10-14
- 调研任务：对照 7 个主流流式音频/DSP 中间件（SoX effects chain、FFmpeg libavfilter、GStreamer、JUCE AudioProcessor、Web Audio AudioWorklet、miniaudio node graph、speexdsp preprocess）的公开一手契约，审视本项目 `IStreamEngine` + `FFTranscoder` 管线形态，为 v2 接口演进提供先例依据。
- 服务落点：`docs/spec/architecture-review.md`（架构审视）+ NFR-M1（引擎注册表）+ NFR-L2（`processInto` 零分配变体）。
- 信源纪律：仅采信一手信源——各项目官方仓库的源码头文件 / 官方设计文档 / W3C 规范原文，全部下载至 `/tmp/vh-research/`（不入库）逐文件核验并记录行号；检索仅用于发现仓库路径。博客/教程转述一律不作为【已确认】依据。
- 标注规则：【已确认】= 一手原文核验；【倾向】= 一手信源有旁证但非直接原文，或多信源一致但未逐字核验；【未确认】= 本次未能取证。
- 方法与局限：SoX 取 SourceForge 官方 git master；FFmpeg 取官方 GitHub master；GStreamer 取官方 monorepo main（注意：GStreamer 2.x 重构后 base 库已从 gst-plugins-base 移入核心仓 `subprojects/gstreamer/libs/gst/base/`，设计文档移入 `subprojects/gst-docs/`）；JUCE 官方 GitHub master 已无 `juce_AudioProcessor.h` 旧路径，取 7.0.12 tag；miniaudio 取 master（0.11.25）；Web Audio 取 W3C editor's draft 单页规范。个别项因文档迁移降级为【未确认】，见 §11。

## 0. 本项目现状基线（对照基础）

### 0.1 IStreamEngine 契约现状

`src/service/streamengine.h`：
- `:11-12` `inRate()/inChannels()` —— 引擎声明期望输入规格；
- `:13` `std::vector<float> process(const float* in, int64_t frames)` —— 每次调用堆分配返回 vector；
- `:14` `std::vector<float> flush()` —— 一次性返回全部尾段，"终态调用，之后不得再 process"；
- `:15` `lastError()` —— 拉取式错误通道，空串为无错；
- `:6` 头注释明示"内部缓冲可致输出滞后"。

### 0.2 FFTranscoder 管线现状

`src/service/fftranscoder.cpp`（唯一转码通路）：
- 协商：`:120-121` 读引擎规格 → `:130-135` 建 toAi SwrContext（输入→引擎规格），`:136-144` 引擎输出≠44.1k/2ch 时建 toOut 回转 SwrContext。一次性预协商，运行期不变。
- 推送节奏：`:154` `chunk = engine.inRate()` 按秒分块；`:193-200` `aiIn` 攒满 chunk 才调 `engine.process()`。
- 滞后处理：引擎输出 append 进 `enhanced`（`:195/:250/:254`），`drainEnhanced`（`:156-183`）按 `kFlushFrames=8192`（`:19`）分批写盘。
- 收尾时序：`:227-235` 冲刷解码器 → `:236-246` 冲刷 toAi 残样 → `:247-252` 尾块喂引擎 → `:253-254` `engine.flush()` → `:256` drainEnhanced → `:258-267` 冲刷 toOut。
- 取消：`:205-207` 分块粒度检查原子量；错误：`:197/:255` process/flush 后查 `lastError()`。

### 0.3 两种引擎形态

- 无缓冲直通型 `DspDenoiser`（`src/service/dspdenoiser.{h,cpp}`）：`process()` 先整块拷贝输入到新 vector 再原地滤波（`dspdenoiser.cpp:59-63`），`flush()` 空实现（`dspdenoiser.h:14`），状态仅每声道 biquad z1/z2。
- 流式有状态型 `AiDenoiser`（`src/service/aidenoiser.{h,cpp}`）：内部 `pending` 攒 hop（`aidenoiser.cpp:190-202`），首帧吞一个 hop 补偿 STFT 延迟（`:120-123`）；`flush()` 补零成整 hop + 追加一个零 hop，再按 `totalIn - totalOut` 截断（"总输出不得超总输入"，`:207-216`）并重置全部状态（`:218-221`）。

### 0.4 现状问题清单（本次对照确认）

1. 【实锤，本次新发现】**`enhanced` 无界累积违反 NFR-P4**：`drainEnhanced` 仅在引擎 flush 之后被调用（全文件唯二调用点定义于 `fftranscoder.cpp:156`、调用于 `:256`），主解码循环（`:204-226`）中 `enhanced` 只增不减。2h 音频输出 44.1k/2ch/f32 ≈ 2.54GB，超出 NFR-P4 的峰值 RSS ≤2GB。此为架构问题而非实现笔误：接口层缺少"边推边拉"的强制节奏（见 §9 R5）。
2. `process()` 返回 vector：每秒块一次堆分配 + `enhanced.insert` 二次拷贝（`:194-195`），即 NFR-L2 要消灭的对象。
3. `flush()` 一次性返回全部尾段：尾段大小无上界约束（MDX 类大窗引擎尤甚），且"终态调用后不得再 process"使错误恢复只能重建引擎。
4. 协商时序无显式阶段：`setup()`（各引擎自定义）与 `inRate()/inChannels()` 查询分离，无"准备完成"边界，无块长/延迟申报。
5. 错误通道拉取式：`lastError()` 在 process 返回后才查，调用方须记得检查（`:197`），契约靠约定不靠签名。

---

## 1. SoX effects chain（libSoX）

信源：`src/sox.h`、`src/effects.c`（SourceForge 官方 git master）。

### 1.1 生命周期与滞后契约【已确认】

效果处理器 `sox_effect_handler_t` 的回调序列（`sox.h:1569-1574`）：`getopts`（每效果一次）→ `start`（每 flow 一次，初始化）→ `flow`（处理）→ `drain`（输入结束后取尾）→ `stop`（每 flow 一次收尾）→ `kill`（每效果一次，含异常路径）。

核心签名（`sox.h:1219-1228`）：

```c
typedef int (* sox_effect_handler_flow)(
    sox_effect_t * effp,
    sox_sample_t const * ibuf,   /* 读入缓冲 */
    sox_sample_t * obuf,         /* 写出缓冲 */
    size_t *isamp,               /* 入参=ibuf容量；出参=实际消费样本数 */
    size_t *osamp);              /* 入参=obuf容量；出参=实际产出样本数 */
```

即**双向计数、调用方持有缓冲**：flow 可消费少于一整块、可产出零样本，滞后完全由两侧计数差表达。drain 与 flow 同形但无输入（`sox.h:1233-1241`）。链条层语义：`drain_effect` 反复调用直至产出 0 才推进到上游效果（`effects.c:388`：`if (!obeg) /* This is the only thing that drain has and flow hasn't */ effstatus = SOX_EOF;`）——**"drain 循环到零"是显式契约**。无 drain 尾的效果用默认实现直接返回 `*osamp=0, SOX_EOF`（`effects.c:45-51`）；直通效果默认 flow 是 `lsx_flow_copy`（`effects.c:36-40`）。

### 1.2 缓冲所有权【已确认】

**链条拥有传输缓冲，效果只拥有内部状态**：`sox_effect_t.obuf/obeg/oend`（`sox.h:1594-1596`）由 `sox_flow_effects` 按 `sox_globals.bufsiz` 统一分配（`effects.c:412`），flow 把输出写进 obuf 尾部空隙（`effects.c:247`：`obeg = sox_globals.bufsiz - effp->oend`）。多声道时 obuf 在交错/非交错布局间切换以免往返转换（`effects.c:210-239` 大注释）。效果可用 `lsx_effect_set_imin()` 声明"flow 需要的最小输入量"（`effects.c:110-119`），链条据此决定何时喂——这是**引擎反向声明块长**的先例。

### 1.3 规格协商【已确认】

建链期一次性协商：`sox_add_effect(chain, effp, &in, out)`（`effects.c:124-130` 注释）——`*in` 是本效果输入信号，`*out` 是期望输出建议；效果按 `SOX_EFF_*` 能力旗标（`sox.h:1059-1069`：`SOX_EFF_CHAN` 可改声道数、`SOX_EFF_RATE` 可改采样率、`SOX_EFF_LENGTH` 可改时长等）在 `start()` 中改写 `out_signal`，实际产出的规格写回 `*in` 供下一效果使用。运行期无再协商。`sox_signalinfo_t`（`sox.h:1349-1355`）= rate/channels/precision/length 四元组。

### 1.4 内存有界与取消【已确认】

全链内存 = O(效果数 × bufsiz)，每效果一块固定 obuf；`sox_flow_effects` 的调度从链尾反向回拉（demand-driven，`effects.c:449-461`），数据不积压。取消：`sox_flow_effects_callback` 返回非 SUCCESS 即中止（`sox.h:1268-1273`，`effects.c:466`）。软错误（削波）走 `clips` 计数器旁路（`sox.h:1589`），不中断流。

### 1.5 错误传播【已确认】

全部 int 返回码（`SOX_SUCCESS`/`SOX_EOF`，效果可返 `SOX_EFF_NULL` 表示"此配置下无事可做"被链剔除，`effects.c:158-164`）；文本经 `lsx_fail` 日志，不进返回值。

## 2. FFmpeg libavfilter

信源：`libavfilter/buffersrc.h`、`libavfilter/buffersink.h`、`libavfilter/avfilter.h`、`doc/filter_design.txt`（官方 GitHub master）。

### 2.1 图模型与帧契约【已确认】

应用侧通过 buffersrc（推入）+ buffersink（拉出）与图交互。`av_buffersrc_add_frame(ctx, frame)` **取走 frame 引用的所有权**；`frame=NULL` 标记 EOF（`buffersrc.h:196-213`）。拉出侧 `av_buffersink_get_frame(ctx, frame)` 返回值三态：`>=0` 得帧 / `AVERROR(EAGAIN)`"需向图喂更多输入" / `AVERROR_EOF`"此 sink 不再有输出"（`buffersink.h:135-149`）——滞后与终结全部由拉取侧返回码表达。`av_buffersink_get_samples` 可按精确样本数取（内部拷贝、明确标注"less efficient because it copies the data around"，`buffersink.h:151-168`）；`av_buffersink_set_frame_size` 让 sink 恒定返回固定帧长、EOF 末块补零（`buffersink.h:95-102`）。

### 2.2 滞后/drain 在过滤器层如何表达【已确认】

`doc/filter_design.txt`：`filter_frame()` 收帧后"若足够产出输出则立即推送；若一帧可产出多帧，至少推一帧、其余可暂存在过滤器内，但下次输入到来时必须先冲刷暂存"（`filter_design.txt:222-237`）。EOF 传播：`request_frame()` 返回 `0`（有进展）/`AVERROR(EAGAIN)`（暂无）/`AVERROR_EOF)`（终结）（`filter_design.txt:263-268`）；`activate()` 模型下过滤器内部暂存帧"可视为 FIFO 的一部分，据此推迟确认 EOF 状态"——即**先吐完暂存帧、再向下游转发 EOF**（`filter_design.txt:172-199`）。应用层收尾用 `avfilter_graph_request_oldest()`，文档自述"a convenience function to help drain a filtergraph in a balanced way"（`avfilter.h:1180-1199`）。

### 2.3 所有权与零拷贝【已确认】

引用计数 AVFrame：`filter_frame` 收到的引用归本过滤器所有；`ff_filter_frame` 交出后不得再碰；链路 `fifo` 归框架所有（`filter_design.txt:73-99`）。就地修改需可写：`av_frame_is_writable()` 检查、输入 pad 挂 `needs_writable` 旗标换取可写帧（`filter_design.txt:101-118`）——**就地处理是 pad 级显式声明**。

### 2.4 格式协商【已确认】

配置阶段一次性：`query_formats` 对每条链路给出支持列表 → 协商机制求交集 → 定格单一格式，全程发生在 `avfilter_graph_config()`（"Check validity and configure all the links and formats in the graph"，`avfilter.h:707-712`）——sink 侧文档明示"The format negotiated by the graph during configuration can be obtained using the accessor functions"（`buffersink.h:40-50`），且 sink 可用 AVOption 预约束 `sample_formats/samplerates/channel_layouts`（`buffersink.h:56-66`）。运行期：buffersrc 默认检查推入帧的规格突变（`AV_BUFFERSRC_FLAG_NO_CHECK_FORMAT` 关闭该检查，`buffersrc.h:38-41`）——即**运行期规格变化默认是错误而非再协商**。

### 2.5 内存有界【已确认/倾向】

设计目标原文："The purpose of these rules is to ensure that frames flow in the filter graph without getting stuck and accumulating somewhere"（`filter_design.txt:121-125`）。界由应用拉取节奏决定（sink 不拉则图内积压）【已确认此语义】；lavfi 内部 FIFO 是否有全局硬上限未逐行审计 `fifo.c`，【倾向】认为无全局上限、纯靠拉取驱动。

## 3. GStreamer

信源：设计文档 `part-negotiation.md`、`part-scheduling.md`、`part-element-transform.md`（gst-docs 子项目）+ `gstbasetransform.h`（monorepo main，GStreamer 2.x 位置）。

### 3.1 pad 协商模型【已确认】

两句基本法（`part-negotiation.md:13-15`）："1) downstream suggests formats；2) upstream decides on format"。四个原语：`GST_QUERY_CAPS`（问可能格式）/ `GST_QUERY_ACCEPT_CAPS`（验固定格式）/ `GST_EVENT_CAPS`（下发已定格式，缓冲流开始前）/ `GST_EVENT_RECONFIGURE`（下游要求上游重协商）（`part-negotiation.md:17-22`）。**运行期再协商是一等公民**：下游可发 RECONFIGURE 上行，上游在下一缓冲前换格式重协商（`part-negotiation.md:83-85, 145-173`）；元素拒收缓冲用 `GST_FLOW_NOT_NEGOTIATED` 链函数返回值（`part-negotiation.md:76-81`）。三种协商模式：Fixed（解码器类，caps 定死）/ Transform（caps 透传变换）/ Dynamic（转换器类，依下游 caps 查询结果定输出）（`part-negotiation.md:312-333`）。

### 3.2 调度模型：chain vs loop【已确认】

`part-scheduling.md`：推模式 = 上游 pad 暴露 loop 函数反复调用（"allowed to block whenever it wants"），下游 pad 暴露 **chain 函数**，`_push()` 即调 chain（`part-scheduling.md:6-17, 38-42`）；拉模式 = 下游 loop 函数经 `_pull_range()` 从暴露 `getrange()` 的上游拉数据（`part-scheduling.md:20-27`）。调度模式由 pad 激活时自选（`part-scheduling.md:29-36`）。

### 3.3 GstBaseTransform【已确认】

变换基类的三态：passthrough（不碰数据）/ in-place / 仅改元数据（`part-element-transform.md:31-43`）。虚方法对（`gstbasetransform.h:190-196, 299-302`）：

```c
GstFlowReturn (*transform)    (GstBaseTransform *trans, GstBuffer *inbuf, GstBuffer *outbuf);
GstFlowReturn (*transform_ip) (GstBaseTransform *trans, GstBuffer *buf);
```

**transform 就是 processInto 的 GObject 形态**：出缓冲由基类经 `prepare_output_buffer` 从协商好的 allocator/bufferpool 分配（`gstbasetransform.h:169-177, 285-288`），子类只填数据；可原地则实现 `transform_ip`。变换率非 1:1 的元素用 1.6+ 的拆分钩子：`submit_input_buffer`（收一块）+ `generate_output`（"repeatedly until it either generates an error or fails to generate an output buffer"，`gstbasetransform.h:200-213`）——**滞后由"提交/产出"分离 + 反复调用表达**，本项目的 chunk 攒批与其同构。丢弃用哨兵 `GST_BASE_TRANSFORM_FLOW_DROPPED = GST_FLOW_CUSTOM_SUCCESS`（`gstbasetransform.h:69-73`）。caps 变换双向可推演（`transform_caps`，`gstbasetransform.h:115-125`）。

### 3.4 背压与错误【已确认/未确认】

背压主通道 = 链函数返回值（GstFlowReturn）同步向上传播（`part-negotiation.md:79-81` 可见 NOT_NEGOTIATED 一例）【已确认机制】；线程间解耦靠显式 queue 元素 + 容量限制，但现行设计文档树已无 queue 专篇（原 `part-queue` 未检索到），具体容量/leaky 语义【未确认】。错误双通道：流控/错误统一的 GstFlowReturn 返回值 + 异步 GstMessage/GstBus（"post an error message on the bus"，`part-negotiation.md:296-298` 一例）。

## 4. JUCE AudioProcessor

信源：`juce_AudioProcessor.h`、`juce_AudioProcessorGraph.h`（官方 GitHub 7.0.12 tag；master 已重构无此路径）。

### 4.1 processBlock 契约【已确认】

```cpp
virtual void processBlock (AudioBuffer<float>& buffer, MidiBuffer& midiMessages) = 0;  // :209
```

**同一缓冲既入又出、调用方（host）持有**：文档详述声道映射规则——2 入 4 出时缓冲有 4 声道，前 2 声道为输入，处理器须覆写全部 4 声道；多出的输出声道可能含垃圾（`juce_AudioProcessor.h:150-186`）。块长不恒定（"may be more or less than the estimated value given to prepareToPlay... must be able to cope with variable-sized blocks"，`:188-192`），偶发零样本块（`:192`）。线程约束原文："it's going to be called by the audio thread, so any kind of interaction with the UI is absolutely out of the question"（`:196-206`）。**无 drain、无返回码**：延迟经 `setLatencySamples(int)`（`:841`）向 host 申报；尾巴经 `getTailLengthSeconds()` 纯虚（`:844`）声明，离线渲染由 host 持续调 processBlock 直到尾巴走完——JUCE 是"host 拉动 + 声明式尾长"而非"显式 flush"的反例。

### 4.2 准备期与总线协商【已确认】

`prepareToPlay(double sampleRate, int maximumExpectedSamplesPerBlock)`（`:127`）：采样率"will remain constant until playback stops"（`:119-120`）；块长是"strong hint"，"You should program defensively in case a buggy host exceeds this value"（`:121-125`）——内部缓冲应在此 resize。释放配对 `releaseResources()`（`:133`）。总线布局协商在 prepare 之前：host 查询 `isBusesLayoutSupported(const BusesLayout&)`（`:1329`）筛选、`setBusesLayout` 失败即拒、`getNextBestLayout` 兜底（`:590-603` 一带）；`BusesLayout` 是输入/输出 AudioChannelSet 数组（`:312`）。运行期无格式再协商。离线/实时模式切换：`setNonRealtime(bool)`（`:960` 一带）。

### 4.3 图与错误【已确认/倾向】

`AudioProcessorGraph : public AudioProcessor`（`juce_AudioProcessorGraph.h:45`）——图本身是处理器，`addNode` 挂节点（`:255`），组合与节点同构。错误通道：processBlock 返回 void，无错误码；"processBlock 内禁止分配/锁/抛异常"的严格 RT 准则原文在 JUCE 官方教程而非头文件，本次未复核教程页，【倾向】（头文件仅可确认 audio-thread 约束与 UI 禁令原文）。

## 5. Web Audio AudioWorklet

信源：W3C Web Audio API editor's draft（webaudio.github.io 单页规范，§1.32 AudioWorklet）。

### 5.1 process() 契约与零分配【已确认】

回调签名（§1.32.5.3）：

```
callback AudioWorkletProcessCallback =
    boolean (FrozenArray<FrozenArray<Float32Array>> inputs,
             FrozenArray<FrozenArray<Float32Array>> outputs,
             object parameters);
```

时间量子：render quantum 默认 128 帧（§ AudioContextOptions："The AudioContext's render quantum size is the default value of 128 frames"；私有槽 `[[render quantum size]]`）。缓冲复用原文（§1.32.5.3.1）："the inputs and outputs arrays will be reused between calls so that no memory allocation is done. However, if the topology changes... new arrays are reallocated"——**零分配靠"框架持有 + 跨调用复用 + 仅拓扑变化才重分配"达成**。outputs 每次零填充（写多少是多少）；输入侧滞后表达：无活跃上游时 `inputs[n]` 是空数组（"the only circumstance under which the number of elements of inputs[n] can be zero"）。

### 5.2 尾巴/生命周期【已确认】

返回值即生命周期：转换型节点输入断开后应继续返回 `true` 一段时间输出尾巴（tail-time）；源头型节点产完再返回 `false`；缺省返回值等价 `false`（§1.32.5.3 "The return value of this callback controls the lifetime..."）。**没有显式 flush——尾巴靠"持续产静音衰减块 + 返回 true"表达**，与 JUCE 同属"拉动 + 声明式"一派。

### 5.3 错误传播【已确认】

process 内抛异常 → 渲染算法置 `[[callable process]] = false`、后续恒输出静音（故障隔离），并经 control thread 异步派发 `processorerror` 事件（§"queue a task to the control thread to fire an event named processorerror"）；处理器构造期异常同样走 `processorerror`。返回值不承载错误。

## 6. miniaudio node graph

信源：`miniaudio.h` 单头文件（官方 GitHub master，版本 0.11.25，`miniaudio.h:3748-3751`）。

### 6.1 节点回调：SoX flow 的总线化推广【已确认】

```c
void (* onProcess)(ma_node* pNode,
                   const float** ppFramesIn,  ma_uint32* pFrameCountIn,
                   float**       ppFramesOut, ma_uint32* pFrameCountOut);   // :10680
```

语义原文（`miniaudio.h:10669-10679`）：入口处 `pFrameCountOut` = 各输出总线**容量**、`pFrameCountIn` = 输入现有帧数；出口处回调把二者改写为**实际产出/实际消费**。教程示例明确："If you're doing resampling, you'll need to be sure to set both pFrameCountIn and pFrameCountOut properly"（`:2136-2148`）——**双计数天然支持变速率引擎**，另配可选 `onGetRequiredInputFrameCount`（"the number of input frames that are required to output the specified number of output frames"，`:10683-10690`）让框架精确预拉而非估算。

### 6.2 滞后、尾巴与缓冲所有权【已确认】

拉模型："miniaudio starts at the node graph's endpoint node which then pulls in data from its input attachments, which in turn recursively pull..."（`:2098-2100`）。缓冲全部归图：节点输入经 `ma_node_base.pCachedData`（堆上固定容量缓存，`:10765` 注释"Fixed size"）预混后递给回调；图级 `pProcessingCache` 处理"调用方请求帧数不是 processingSizeInFrames 整数倍"的残量（`:10833`）。尾巴机制 = 旗标 `MA_NODE_FLAG_CONTINUOUS_PROCESSING`："makes it so the callback is always fired regardless of whether or not any input data is received. This is useful for effects like echos where there will be a tail of audio data..."（`:2271-2280`）；配合 `ma_node_state_started/stopped` + 定时状态切换（`ma_node_set_state_time`，`:10804-10808`）表达"尾段何时放完"。无输入时默认喂静音、`MA_NODE_FLAG_ALLOW_NULL_INPUT` 时传 NULL（`:2282-2289`）。

### 6.3 格式协商【已确认】

声道：建节点时各总线声道数定死（"The channel counts cannot be changed after the node has been initialized"）；输出总线接到输入总线须声道数一致否则 attach 失败；**节点自身输入/输出总线声道数可不同，框架自动转换**（"miniaudio will automatically convert the input data to the output channel count before processing"，`:2063`）。采样率：**不协商**——"The sample rate is defined by the underlying data sources. It's up to you to ensure they use a consistent and appropriate sample rate."（`:2106`）。块长：图配置 `processingSizeInFrames` 为首选处理块（`:10821`），变长请求由处理缓存吸收。

### 6.4 背压/实时与错误【已确认】

与 device 的关系：data_callback 归调用方时，"it's their responsibility to manually call `ma_engine_read_pcm_frames()` from inside their data callback"（`:1067-1073`）——图是回调内被拉动的纯计算层。实时性：读路径无锁（"Only `ma_node_graph_read_pcm_frames()` is expected to be lock-free"，`:2425-2427`），attach/detach 用自旋锁。图遍历内存有界：`preMixStackSizeInBytes` 默认每声道 512KB（`:10822`）。错误：API 层一律 `ma_result` 返回码；**onProcess 回调本身返回 void**——处理期错误无法经回调上抛，只能靠节点状态/日志。

## 7. speexdsp preprocess

信源：`include/speex/speex_preprocess.h`（xiph 官方 GitHub master）。

【已确认】全部契约在 89 行头文件内：

```c
SpeexPreprocessState *speex_preprocess_state_init(int frame_size, int sampling_rate); // :65
int speex_preprocess_run(SpeexPreprocessState *st, spx_int16_t *x);                   // :77
void speex_preprocess_estimate_update(SpeexPreprocessState *st, spx_int16_t *x);      // :86
int speex_preprocess_ctl(SpeexPreprocessState *st, int request, void *ptr);           // :94
```

- **frame 内联就地处理**：`x` 是"in and out"同一向量，"Must be same size as specified in state_init"（`:74`）——零拷贝、零分配的极限形态。
- 规格全在创建期锁死：帧长（10–20ms）+ 采样率为构造参数（`:59-64`）；每声道独立一个 state。
- 无 flush/drain（逐帧有界无尾巴）；无错误返回（返回值是 VAD 布尔数据"ONLY if VAD turned on"，`:75`，明确不是错误码）；参数走 ioctl 式 `speex_preprocess_ctl`。

对本项目的意义：DSP 类引擎（DspDenoiser）在 v2 里可声明的就是这一档契约——就地、无尾、无规格变化。

---

## 8. 横向对照总表（a–e × 7）

| 框架 | a. 滞后+收尾契约 / 缓冲归属 | b. 协商层/时机 | c. 零分配变体 | d. 内存有界/背压 | e. 错误传播 |
|---|---|---|---|---|---|
| SoX | flow 双计数允许 0 产出；drain 循环到零收尾；**链条持传输缓冲**，效果持内部状态 | 建链期一次性：signalinfo 逐级写回 + SOX_EFF_* 能力旗标 + start() 改写 | flow(ibuf,obuf,*is,*os) 本体即零分配 | 每效果固定 bufsiz 缓冲；demand-driven 反向调度 | int 返回码；SOX_EFF_NULL 剔除；clips 软错误旁路 |
| FFmpeg lavfi | 过滤器内部 FIFO；EOF 状态先吐暂存再下行；sink 三态返回(EAGAIN/EOF)；**引用计数转移所有权** | graph_config 阶段 query_formats→交→定格；运行期规格突变=错误 | 例外：帧由框架分配；换零拷贝靠 AVFrame 引用 + needs_writable 就地 | "frames flow without accumulating"；界=应用拉取节奏 | AVERROR 负数；AVERROR_EOF 哨兵；av_log |
| GStreamer | chain 收缓冲即所有权转移；EOS 事件前须吐完；BaseTransform submit/generate 分离表达非 1:1 | pad 级 CAPS 事件，**支持运行期 RECONFIGURE 再协商**（独一家） | transform(in,out) 出缓冲出自协商 bufferpool；transform_ip 就地 | GstFlowReturn 同步回传上游构成背压；线程间靠 queue 元素 | FlowReturn(流+错合一) + bus 异步消息双通道 |
| JUCE | **无 drain**：host 推定长块；latencySamples 申报延迟、tailLengthSeconds 申报尾巴；host 持缓冲 | host 主导：isBusesLayoutSupported 查询→setBusesLayout→prepareToPlay；无运行期再协商 | processBlock(buffer&) 同缓冲入出即本体 | host 即时钟；插件须 RT 安全，缓冲每调用固定 | 无返回码；RT 内禁令靠约定【倾向】；无错误通道 |
| Web Audio | 返回 true 续命=尾巴；inputs 空数组=上游断供；**框架持复用数组** | 每 quantum 按 channelCountMode 计算自适应（运行期、声明式规则） | process(inputs,outputs) 复用数组"no memory allocation is done" | 固定 128 帧量子 + 硬实时截止 | 异常→静音故障隔离 + processorerror 异步事件 |
| miniaudio | onProcess 双计数(可变速率)；尾巴=CONTINUOUS_PROCESSING 旗标 + stopped 定时；**图持全部缓冲** | 节点创建期定死总线声道；输入侧自动声道转换；采样率不协商(用户负责) | onProcess(ppIn,*nIn,ppOut,*nOut) 本体即零分配 | endpoint 拉动；preMixStack 512KB/ch 有界；读路径无锁 | ma_result(API 层)；onProcess 无返回(void) |
| speexdsp | 逐帧有界无尾巴；调用方持帧缓冲 | 构造参数锁死 | run(st,x) 就地即本体 | 天然有界（帧定长） | 无错误返回；ctl 返回 0/-1 |
| **本项目 v1** | process/flush 返回 vector，允许滞后；**引擎自持内部缓冲，返回值堆分配**；FFTranscoder 攒 enhanced | inRate/inChannels 一次性声明，管线两侧 Swr 适配（一次性） | 无（NFR-L2 待做） | aiIn 有界；**enhanced 无界累积（NFR-P4 违例，§0.4-1）** | lastError() 拉取式字符串 |

## 9. IStreamEngine v2 具体建议（每条对标先例）

### R1. `processInto` 双计数签名，取代返回 vector —— 对标 SoX flow + miniaudio onProcess

```cpp
enum class ProcStatus { Ok, Error };

virtual ProcStatus processInto(const float* in,  int64_t inFrames,
                               float* out,       int64_t outCapacity,
                               int64_t* consumedFrames, int64_t* producedFrames) = 0;
```

- 依据：SoX `sox.h:1219-1228`（ibuf/obuf + *isamp/*osamp）、miniaudio `miniaudio.h:10680`（`pFrameCountIn/Out` 入=可用/容量、出=消费/产出）。7 家中 5 家（SoX/GStreamer/JUCE/Web Audio/miniaudio/speexdsp）为调用方持缓冲；FFmpeg 是唯一例外且其代价是引用计数基础设施。
- 双计数（而非单计数）是刻意的：`AiDenoiser` 一类 STFT 引擎"消费≠产出"，单计数表达不了首帧吞 hop（`aidenoiser.cpp:120-123`）。miniaudio 明文支持该场景并另配 `onGetRequiredInputFrameCount`（`miniaudio.h:10683-10690`）；v1 的隐式行为（process 少吐）在双计数下成为显式契约。
- 调用方（FFTranscoder）预分配一对容量为 chunk + latency 余量的环形复用缓冲，`consumed` 推进读指针、`produced` 推进写指针——消灭 NFR-L2 针对的每次堆分配与 `enhanced.insert` 二次拷贝（`fftranscoder.cpp:194-195`）。

### R2. `drainInto` 循环到零，取代 `flush()` 返回 vector —— 对标 SoX drain

```cpp
virtual ProcStatus drainInto(float* out, int64_t outCapacity, int64_t* producedFrames) = 0;
// producedFrames == 0 即收尾完成；此后不得再 processInto
```

- 依据：SoX `sox.h:1233-1241`（drain 与 flow 同形、无输入）+ `effects.c:388`（产出 0 ⇒ SOX_EOF ⇒ 完成）；FFmpeg `request_frame` 三态返回（`filter_design.txt:263-268`）与 buffersink `AVERROR_EOF`（`buffersink.h:146`）同语义。
- 修复 v1 `flush()` 一次性返回无上界尾段的缺陷（§0.4-3）；MDX 大窗引擎尾段（数千帧窗长）不再要求整块驻留。`AiDenoiser::flush` 现有的"补零成 hop + 追加零 hop + 按总账截断"逻辑（`aidenoiser.cpp:204-222`）可原样改写为多次 drainInto 调用。

### R3. `prepare()` 显式准备阶段 + 块长申报 —— 对标 JUCE prepareToPlay + SoX imin + miniaudio processingSizeInFrames

```cpp
struct AudioSpec { int rate = 0; int channels = 0; };
virtual bool prepare(const AudioSpec& in, int preferredBlockFrames, QString* err) = 0;
virtual AudioSpec inSpec() const = 0;          // prepare 成功后恒定，对应 buffersink 配置后 accessor
virtual int preferredBlockFrames() const { return 0; }  // 引擎期望的分块（AiDenoiser = hop 的整数倍）
```

- 依据：JUCE `prepareToPlay(sampleRate, maximumExpectedSamplesPerBlock)`"strong hint + 防御式编程"（`juce_AudioProcessor.h:119-125`）；SoX `lsx_effect_set_imin`（`effects.c:110-119`，且链上校验 bufsiz 是否够）；miniaudio `processingSizeInFrames` + 处理缓存吸收非整倍请求（`miniaudio.h:10821,10833`）。
- 收益一：FFTranscoder 的 `chunk = engine.inRate()`（`fftranscoder.cpp:154`）改为问引擎，AiDenoiser 可要求 hop 整数倍、省掉 `pending` 攒批（`aidenoiser.cpp:195-199`）。收益二：把"setup→inRate 可查"的隐式顺序（v1 靠约定）变成签名上的硬时序。错误在 prepare 期即报（对标 graph_config 阶段失败即终止，`avfilter.h:707-712`）。
- 保持一次性预协商，不引入运行期再协商：文件源规格恒定，GStreamer 式 RECONFIGURE 与 Web Audio 式逐量子自适应在离线转码场景无需求（反例见 §10）。

### R4. 延迟申报 `latencyFrames()` —— 对标 JUCE setLatencySamples

```cpp
virtual int64_t latencyFrames() const { return 0; }
```

- 依据：JUCE `setLatencySamples`（`juce_AudioProcessor.h:841`）向 host 申报算法延迟；SoX 无此物、全靠 drain 兜底，是其设计缺口。
- 用途：进度条按输出计（现在按喂入计，`fftranscoder.cpp:198-199`，滞后引擎下进度恒偏差 latency）；实时化（O3 脉冲实测）后此值直接进延迟预算表。`AiDenoiser` 首吞 hop + WOLA 窗长/2 即为该值，从魔法行为变为申报数据。

### R5. 管线侧"边推边拉"写盘 —— 对标 SoX 链内持续流动 + FFmpeg 防积压设计目标

- 依据：SoX 主循环每个效果产出即被下游消费、obuf 固定容量滚动（`effects.c:240-337,403-461`）；FFmpeg 设计目标原文"frames flow... without getting stuck and accumulating somewhere"（`filter_design.txt:121-125`）。
- 修复 §0.4-1 的 NFR-P4 违例：FFTranscoder 在每次 `processInto` 后立即执行现有的 `drainEnhanced` 路径（函数已存在，`fftranscoder.cpp:156-183`，只是没在主循环调用），`enhanced` 上界变为 chunk+latency 量级。这是**无需等 v2 接口即可单独落地的 P0 修复**，v2 落地后该缓冲直接成为 R1 的复用对。

### R6. 状态码随调用返回，`lastError()` 降级为详情文本 —— 对标 SoX/miniaudio 返回码 + GstFlowReturn 单通道语义

- 依据：SoX 全链 int 返回码（`sox.h` 各 typedef 均注释 "@returns SOX_SUCCESS"）；miniaudio API 层 `ma_result`；GStreamer 把流控与错误合入同一枚举同步回传（`part-negotiation.md:79-81`）。
- 反例排除：JUCE 无错误通道（实时场景的妥协）、miniaudio onProcess 返回 void（处理期错误无出口）、Web Audio 故障隔离静音（实时兜底）——三者皆为实时约束所迫，离线管线应学 SoX/FFmpeg 的"每调用必带状态"。
- v2 落地后 `processInto/drainInto` 返回 `ProcStatus`，`lastError()` 仅存人读文本；调用方忘查错误即无法继续（签名强制），取代 `fftranscoder.cpp:197` 的约定式检查。

### R7. 可选就地处理申报 —— 对标 GstBaseTransform transform_ip / speexdsp run / FFmpeg needs_writable

```cpp
virtual bool canProcessInPlace() const { return false; }  // true 时管线可传 in == out
```

- 依据：speexdsp `run(st, x)` 纯就地（`speex_preprocess.h:74`）；GStreamer `transform_ip` 与 `transform` 二选一实现（`gstbasetransform.h:190-196`）；FFmpeg 以 pad 旗标 `needs_writable` 换可写帧（`filter_design.txt:115-118`）。
- `DspDenoiser` 直接受益：`dspdenoiser.cpp:60` 的整块拷贝（v1 接口强迫）在就地路径下消失，与 speexdsp 同档。默认 false 保持保守，避免 STFT 类引擎误用。

### R8. 引擎注册表形态（NFR-M1）—— 对标 SoX 效果表 / FFmpeg filter 注册 / miniaudio vtable

- 依据：SoX 静态 `s_sox_effect_fns[]` 工厂表 + `sox_find_effect(name)` 名字查找（`effects.c` 末段）；miniaudio 每节点类型一个 const vtable（`miniaudio.h:10710-10716`），实例 config 只带参数——**类型（代码）与实例（参数）分离**。
- 建议：注册表键 = 引擎 id + 工厂函数（返回 `unique_ptr<IStreamEngine>`），工厂参数为各引擎的强类型 config 结构（对标 `ma_node_config`/`sox_effect_options` 的 argv 解析反例——SoX 字符串 argv 是 CLI 包袱，不学）。能力元数据（inSpec 范围、canProcessInPlace、latency 上限）随注册条目暴露，供未来 FR-24 无头命令层做管线编排校验（对标 lavfi 图配置期即可整图验证格式连通性）。

### v2 接口全景（草案）

```cpp
class IStreamEngine {
public:
    virtual ~IStreamEngine() = default;
    // 协商（一次性；R3）
    virtual bool prepare(const AudioSpec& in, int preferredBlockFrames, QString* err) = 0;
    virtual AudioSpec inSpec() const = 0;
    virtual int preferredBlockFrames() const { return 0; }
    virtual int64_t latencyFrames() const { return 0; }        // R4
    virtual bool canProcessInPlace() const { return false; }   // R7
    // 处理与收尾（R1/R2）；steady-state 内不得堆分配
    virtual ProcStatus processInto(const float* in, int64_t inFrames,
                                   float* out, int64_t outCapacity,
                                   int64_t* consumed, int64_t* produced) = 0;
    virtual ProcStatus drainInto(float* out, int64_t outCapacity, int64_t* produced) = 0;
    // 错误详情（R6）；状态码已在上方返回值
    virtual QString lastError() const { return {}; }
};
```

调用时序（对标 JUCE"layout→prepare→process→release"与 FFmpeg"config→push/pull→close"）：
`prepare(in, block)` → 循环{攒块 → `processInto` → 就地写盘} → 循环{`drainInto`} 直到 produced==0 → 析构。取消仍由调用方原子量在块间隙检查（SoX flow-callback 先例，`sox.h:1268-1273`），不进引擎接口。

## 10. 未采纳的模式与理由

| 模式 | 出处 | 不采纳理由 |
|---|---|---|
| 引用计数帧 + 所有权转移 | FFmpeg AVFrame（`filter_design.txt:38-99`） | 单引擎串行链无共享/分叉需求；引用计数基础设施复杂度>收益。若未来做并行分叉（人声+伴奏双输出）再评估 |
| 运行期 caps 再协商（RECONFIGURE） | GStreamer（`part-negotiation.md:83-85`） | 文件转码源规格恒定；再协商的正确性成本（全链状态重置）无对应需求 |
| 逐量子声道自适应 | Web Audio（computedNumberOfChannels） | 离线管线规格在 prepare 期定死；引擎（GTCRN 单声道/MDX 立体声）本就要求固定规格 |
| 无 drain 的 host 拉动 + 声明式尾长 | JUCE（getTailLengthSeconds） | WAV 写盘需要确定终结点写头/尾；尾段样本必须真实产出而非 host 猜测。`latencyFrames` 采纳其申报思想，drain 语义保留 SoX 式 |
| 多总线混合图（bus/mixing） | miniaudio / JUCE Graph | 当前只有单链多引擎串联；注册表（R8）留了图化余地但不预建图抽象 |
| 故障静音隔离 | Web Audio（processorerror） | 实时兜底策略；离线转码遇错应即停删半成品（现有行为），静音污染输出更危险 |
| 处理期无错误出口 | JUCE void processBlock / miniaudio void onProcess | 实时约束的产物；离线场景每调用带状态码无代价且更安全 |

## 11. 反证与盲点

1. 【盲点】lavfi 图内 FIFO 是否存在全局硬上限：未审计 `libavfilter/fifo.c`/framesync 实现。本文仅主张"界由应用拉取节奏决定"（buffersink EAGAIN 语义 + request_oldest 文档支撑，`buffersink.h:142-147`、`avfilter.h:1180-1199`），不断言无上限。
2. 【未确认】GStreamer queue 元素的容量/leaky 具体语义：现行 gst-docs 设计文档树中已无 queue 专篇（原 `part-queue.md` 检索 404）；本文背压论述仅依赖 chain 返回值机制这一【已确认】部分。
3. 【倾向】JUCE "processBlock 内禁止分配/锁/抛异常"：头文件原文只有 audio-thread 与 UI 禁令（`juce_AudioProcessor.h:196-206`）；完整 RT 准则原文在官方教程页，本次未复核。v2 建议中"steady-state 禁堆分配"另有 Web Audio 规范原文（"no memory allocation is done"）与 SoX 结构性保证（调用方持缓冲）双重支撑，不依赖 JUCE 教程。
4. 【盲点】SoX master 与最新发行版（14.4.2）的差异未比对；本文全部行号以 SourceForge git master 为准（effects chain 自 2007 年起结构稳定，风险低）。
5. 反证尝试（针对 R1"双计数是主流"）：主动寻找"回调返回新分配缓冲"的主流先例——仅 FFmpeg 一家（框架代分配 + 引用转移），且其面向通用图而非单链；JUCE/Web Audio 虽由框架持缓冲但均为"借用式"（调用期间有效）而非返回所有权。未找到支持 v1"返回 vector"形态的先例。
6. 针对 R5 的反证尝试：检查是否存在"末尾统一写盘"的正当设计（如需要精确总时长写 WAV 头）——本项目用 avformat 写 WAV，header 可先写、trailer 收尾时更新（`fftranscoder.cpp:274` av_write_trailer），且现有 drainEnhanced 本就分块写盘，仅调用时机错位；故 R5 无结构性障碍。

## 参考链接清单

一手信源（均于 2026-10-14 取原文核验，本地留档 /tmp/vh-research/）：

- SoX（SourceForge 官方 git master）：
  - https://sourceforge.net/p/sox/code/ci/master/tree/src/sox.h （效果生命周期/flow/drain 契约、SOX_EFF_* 旗标、sox_effect_t）
  - https://sourceforge.net/p/sox/code/ci/master/tree/src/effects.c （链调度、缓冲所有权、drain 到零语义、imin）
- FFmpeg（官方 GitHub master）：
  - https://github.com/FFmpeg/FFmpeg/blob/master/libavfilter/buffersrc.h
  - https://github.com/FFmpeg/FFmpeg/blob/master/libavfilter/buffersink.h
  - https://github.com/FFmpeg/FFmpeg/blob/master/libavfilter/avfilter.h
  - https://github.com/FFmpeg/FFmpeg/blob/master/doc/filter_design.txt （协商/所有权/帧调度/EOF）
- GStreamer（官方 monorepo main）：
  - https://github.com/GStreamer/gstreamer/blob/main/subprojects/gstreamer/libs/gst/base/gstbasetransform.h （2.x 起新位置）
  - https://github.com/GStreamer/gstreamer/blob/main/subprojects/gst-docs/markdown/additional/design/negotiation.md
  - https://github.com/GStreamer/gstreamer/blob/main/subprojects/gst-docs/markdown/additional/design/scheduling.md
  - https://github.com/GStreamer/gstreamer/blob/main/subprojects/gst-docs/markdown/additional/design/element-transform.md
- JUCE（官方 GitHub，7.0.12 tag；master 已无此路径）：
  - https://github.com/juce-framework/JUCE/blob/7.0.12/modules/juce_audio_processors/processors/juce_AudioProcessor.h
  - https://github.com/juce-framework/JUCE/blob/7.0.12/modules/juce_audio_processors/processors/juce_AudioProcessorGraph.h
- Web Audio（W3C editor's draft，§1.32 AudioWorklet / §AudioContextOptions）：
  - https://webaudio.github.io/web-audio-api/
- miniaudio（官方 GitHub master，0.11.25）：
  - https://github.com/mackron/miniaudio/blob/master/miniaudio.h （Node Graph 章节 + ma_node_vtable）
- speexdsp（xiph 官方 GitHub master）：
  - https://github.com/xiph/speexdsp/blob/master/include/speex/speex_preprocess.h

本项目对照源码：
- `src/service/streamengine.h`、`src/service/fftranscoder.{h,cpp}`、`src/service/dspdenoiser.{h,cpp}`、`src/service/aidenoiser.{h,cpp}`
