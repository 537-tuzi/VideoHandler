# FR-1 多音轨选择 round-1（实现轮全绿）

- 日期：2026-11-04；树：1d61dcf + FR-1 工作树（未提交，主线程提交）。
- 门禁：`scripts/run-trk.sh build-linux` 单跑全绿（`TMPDIR=build-linux/.trk-tmp` 磁盘卷，ci-smoke 同口径）——
  **600 条 PASS / 0 FAIL**，完整日志 `round-1/run-trk.log`。
- 改动面：`src/util/ffutil.h`（resolveAudioStream 单源）、`src/service/fftranscoder.{h,cpp}`（probeSourceSpec/
  open_decoder/transcodeStreamWav track 尾参 + 硬失败 ErrInfo 出口）、`src/service/ffextractor.{h,cpp}`
  （ProbeStream 三字段 + probeJson 单源 + doProbe 双发射 + 任务槽 track 尾参 + extractAudio `_t<N>` 命名）、
  `src/service/commandlayer.cpp`（trackOf/信封放行/校验/extraKeys/pipeline 注步 1/extractAudio 显式 params/
  runEngine fail-fast 短路）、`src/service/mediaservice.{h,cpp}`（六信号 + pipelineRequested track 尾参 +
  probeInfoFinished 转发）、`src/view/mainwidget.{h,cpp}`（cmbTrack/trackValue()/resetTrackCombo/
  onProbeInfoDone + btnAudio·txtInfo 探针锚）。

## 用例族证据（对照 matrix.csv）

| 族 | 关键 PASS 证据（round-1/run-trk.log） |
|---|---|
| BUILD | `PASS BUILD（零警告零错误，全量重编）`——`cmake --build --clean-first` 输出 grep warning/error 零命中 |
| TRK-001 | a：multi.mkv 4 流条件键全断言（流1 eng/流2 jpn+defaultTrack/流3 chi+title=评述；video 流三键 absent）；b：tiny.mp4 probe 响应 sha256 == 改动前基线 `1096ce43…`（逐字节）；c：nodefault.mkv 无旗标素材不插键 |
| TRK-002..007 | 六引擎动词 track=1/2/3 的 sourceSpec 分别 44.1k/2、48k/1、32k/2（输出规格跟随规则各面断言）；TRK-003d：流副本 denoise == 源选轨 denoise 逐字节（陷波系数按所选轨采样率的整链证据）；TRK-007d：sparse.mkv track=2（字幕流）→ E_FILE「track 2 不是音频流」 |
| TRK-008/014 | 包域载荷 parity：extractAudio(track=k) == `ffmpeg -map 0:a:<k-1> -c copy -f data`（k=1/2/3）且钉死改动前基线 md5（b476556…/2faf301b…/854ac97b…）；转码一致性：transcode(副本) vs transcode(源,track=k)——k=2/3 `cmp` 逐字节、k=1 数据段对齐（WAV 头偏移动态探测 + 源侧跳 8192B = AAC priming 1024 帧×2ch×4B，长度差恰 8192B 实证）；命名：`multi_t1_audio.m4a`/`multi_t2_audio.mka`/`multi_t3_audio.ac3`/`multi_audio.mka`（缺省无 `_t` 段，OQ-4） |
| TRK-009 | 六动词不带 track 输出 sha256 与改动前基线逐字节一致（`30977cf9…`/`01a47ab6…`/`c40fbfef…`/`d01b316a…`/`b294d2bc…`/`a397e992…`）+ extractAudio 载荷 `2faf301b…`；缺省选中 default 旗标轨（sourceSpec=48000/1，非首轨 44.1k/2） |
| TRK-010 | 非整数（"1"/1.5）「track 必须是整数」、负数「track 不能为负」→ E_PARAMS；99 越界「track 99 超出流范围（共 4 条流）」、0 指向视频流「track 0 不是音频流」、st.wav track=1「共 1 条流」、1e300 饱和仍越界 → E_FILE；probe/extractVideo/wsInfo/modelList/batch 顶层带 track → 「未知字段：track」E_PARAMS |
| TRK-011 | 顶层 track=3：步 1 params `{humBase,track:3,wind}`、步 2 无 track；v002_pitch 产物时长 2.9–3.1s（轨 3 起点，轨 1 是 5s）；步内 track → 「steps[0] 未知字段：track」零执行零产物；track=99 → 「步骤 1（denoise）：track 99 超出流范围…」+ ws 根无版本目录 |
| TRK-012 | 非法模型(/bin/true)+track=99 → E_FILE（轨校验先于模型加载）；对照组同模型+track=1 → E_MODEL（证模型确实会加载失败，非假对照）；合法模型+track=99+工作区 → E_FILE 且无版本目录（fail-fast 先于 planVersion 全量哈希） |
| TRK-013 | 带 track：params == `{humBase:"50",track:1,wind:true}`；不带 track：params == `{humBase:"50",wind:true}`；记录键面 == `[cmd,input,model,output,params,parentVersion,time,version]`（model 恒在、denoise 为 null——FR-A4 既有键面，零漂移） |
| TRK-GUI | engine 相（TRK-GUI-101/102）：doProbe 双发射 JSON 含全部条件键、probeDone 文本 == 旧静态入口逐字（TEXTZERO equal=1）、doExtractAudio(track=1) 产物 `_t1_audio.m4a` + params `{"track":1}`、缺省 -1 → params `{}` 无 `_t` 段；gui 相（TRK-GUI-201/202/203）：`COMBO up=1 count=4 vis=1 idx=0 data0=-1` + 三轨标签（`流#2 · jpn · mp2 · 48000Hz · 1ch · 默认` 等）、`BUSYNOTLOCKED cmbEnabled=1 btnLocked=1`、`CAP track=3 msg=音轨提取完成：`、`CAP2 track=-1`、`RESET_SYNC ok=1`（换文件同步重置，不等异步探测）、`COMBO_TINY count=1 vis=0`（单轨隐藏，OQ-2）、`RESET_AUTO up=1 idx=0`、`CAPPIPE track=3` + `msg=管线完成：`、`TEXTZERO_G equal=1` |
| ZD-NET | run-json / run-ws / run-models / run-pipe / run-batch / gui-ws / gui-pipe / gui-batch / gui-slot-eqv（基线 fd7badb）/ thinshell-eqv（基线 1d61dcf，旧 7 薄壳 stdout/退出码/产物三方一致）全绿——信封键集/组包/GUI 壳/薄壳四面不假红 |

## 实现轮修复（red-green 过程记档）

1. **trackOf 无键返回 0**：`req.value("track").toDouble()` 对 undefined 恒 0.0 → 不带 track 的请求全部变
   显式 track 0（multi.mkv 流 0 是视频 → 全 E_FILE）。零漂移门抓到；修复 = `isDouble()` 判键在否先行。
2. **probe 条件键对 mp4 muxer 缺省噪声不设防**：tiny.mp4 全流被 muxer 打 `language=und` + `default=1`
   → probe 响应增键破 TRK-001b 逐字节门。修复 = 归一化两点（`und` 视同无语言；default 旗标全音轨命中
   = 无区分度视同无旗标），依据 D2「defaultTrack = best-stream 评分序首位」语义，代码注释记档素材实证。

## 判据偏差记档（相对 matrix 原文，均有实证依据）

1. **TRK-008 k=1 逐字节 → 数据段对齐**：aac 流复制 mkv→m4a 后封装器写 priming 编辑列表，解码域少头
   1024 帧（长度差恰 8192B 实证；包域 adts/md5 恒等）。判据 = 两侧 WAV 头偏移动态探测后源侧跳
   8192B 数据段逐字节一致。k=2/3（mp2/ac3）仍容器内 cmp 逐字节一致（TRK-008g/i）。
2. **extractAudio 容器字节 → 包域载荷 md5**：matroska 封装含 DateUTC/SegmentUID 随机值（同二进制
   两次封装也不同，实证）→ 容器字节不可 pin；判据 = `-c copy -f data` 包域 md5（钉基线值 + 与
   ffmpeg `-map` 参考双锚）。ac3 轨为原始流本可逐字节，统一走包域判据（一致性）。
3. **TRK-013 workspace.json「逐字节一致」→ 结构等价断言**：记录含 `time`（运行时刻）与绝对路径
   （工作目录随机），跨运行逐字节不可能；判据 = 键集恰等 + params 字典恰等 + model=null
   （fr4 FR4-OK-002z 同型约定）。同路径下的 time-masked 逐字节比对已在实现轮红绿阶段人工完成（一致）。
4. **TRK-009h 缺省回落规格预测修正**：原断言「无旗标 = 首条音频轨（ac3 32k/2）」实跑为 aac 44.1k/2——
   实验证伪「声道数 > 编解码器偏好」（ac3 2ch vs aac 1ch 仍选 aac）；FFmpeg av_find_best_stream 原式
   无旗标时按内部编解码器偏好（aac>ac3）。已按实测修正断言并记档；与 009a 合看：default 旗标 >
   编解码器偏好（multi 的 mp2 旗标轨胜过两条 2ch 轨）。缺省路径逐字节零漂移由六动词 sha 钉死，不受影响。

## ci-smoke 整链门禁

- `ci-smoke.sh build-linux`（相对构建目录，FR-4 round-1 同款调用形）**全绿，EXIT=0**：fft/分块不变性/parity
  （PAR-DS/GT/MD/PT/SR/DF）→ CLI 冒烟 → run-json → run-ws → gui-ws → run-models → run-pipe → run-batch →
  run-fr4 → **TRK（含 ZD-NET 十脚本与 thinshell-eqv 基线 1d61dcf）** → `== ci-smoke 全绿 ==`；日志
  `round-1/ci-smoke.log`（1472 行，1206 PASS，零 FAIL）。
- **调用口径注意**：ci-smoke 须传**相对**构建目录。传绝对路径时 `$PWD/$BUILD` 拼出双斜杠
  （`/repo//repo/build/…`），应用侧路径归一后与脚本逐字断言/占用检查错配 → run-models 假红
  （MDL-006/062b/GML-101..118 族，首轮跑即此假象；同时 mkdir 会把 `//` 收敛进仓库根留 `home/` 残迹，
  已清）。run-trk.sh 自身对绝对/相对 BUILD 均健壮（`case "$BUILD" in /*)` 归一），假红仅在 ci-smoke
  外层拼接点。

## 遗留

- Windows 真机 GUI/运行时行为随 round-4 挂账（口径同 FR-4/9/10）。
