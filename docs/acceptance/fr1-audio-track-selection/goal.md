# FR-1 多音轨选择验收 goal

## 总目标

按 spec `docs/spec/fr1-audio-track-selection.md`（D1–D8 + §7.1 主线程拍板）落地 FR-1：track 信封
字段（引擎六动词 + extractAudio + pipeline 顶层收；probe/extractVideo/wsInfo/model 族/batch/steps
拒）+ 三处选轨点统一 `resolveAudioStream`（track<0 = av_find_best_stream 原式逐字节现状）+
probeSourceSpec fail-fast 硬失败通道（先于 create/planVersion）+ ProbeStream 增
language/title/defaultTrack（probe JSON 条件插键零漂移）+ GUI 轨道下拉（topRow、≥2 音轨才显示、
「自动」=不带键、openFile 同步重置、不进 QSettings）+ 工作区 params.track 来源链 + 显式 track
缺省名轨标记（底座单源，缺省命名零漂移）。M2 离线闭环最后一包。

## sub goal matrix

| sub goal | 内容 | 状态 |
|---|---|---|
| SG-1 | 底座：resolveAudioStream 统一助手；open_input/open_decoder/probeSourceSpec 三点接 track（尾参缺省 -1，既有调用方与 engine_test 零改）；probeSourceSpec 硬失败出口（track≥0 不可解析即 false） | ✅ round-1 |
| SG-2 | 命令层：信封白名单一行（entry 守卫）+ extraKeys（extractAudio/pipeline）+ 整数/非负校验（E_PARAMS）；runEngine 透传 + fail-fast 短路；cmdPipeline 顶层 track 仅注步 1；recordParams 不改（track 自然入 params）；extractAudio 缺省名轨标记（底座命名单源处，无 track 逐字节零漂移） | ✅ round-1 |
| SG-3 | probe 面：ProbeStream 增 language/title/defaultTrack（条件插键——空/假不插，老素材逐字节零漂移）；probeJson 单源搬家（cmdProbe 组装共享） | ✅ round-1 |
| SG-4 | GUI：worker 单遍探测双发射（probeDone 文本逐字零漂移 + probeInfoDone JSON）；cmbTrack（topRow、≥2 音轨才显示、「自动」itemData=-1、openFile 同步重置、busy 不锁、不进 QSettings）；trackValue() 单源（FR-4 aiVerb 先例）+ 七槽/pipeline 尾参注入 | ✅ round-1 |
| SG-5 | 验收基建：run-trk.sh（TRK 族，先造 wav 再 mux 素材配方、规格错开、default 后轨、非连续 index）+ gui 探针（TRK-GUI）+ ci-smoke 接线 | ✅ round-1 |
| SG-6 | 门禁全绿：TRK/TRK-GUI 全族 + 零漂移回归网（run-json/run-ws/run-models/run-pipe/run-batch/gui-slot-eqv/thinshell-eqv + 既有六处精确键集断言不假红）+ 构建零警告 + 文档（CLAUDE.md/fr24）+ 中文提交 | ✅ round-1（run-trk 600 PASS 全绿；提交归主线程） |

## 重大决策与关键信息

- 全部设计决策见 spec §2（D1–D8）+ §7.1 拍板（A：D1 维持信封路线否决 ParamSpec，主线程四处锚点
  核实；B：OQ-1 E_FILE / OQ-2 隐藏 / OQ-3 不回显 / OQ-4 翻转采纳轨标记；C：对抗 26 条归置）。
- 设计轮三证据源交叉：spec 作者 + 对抗约束枚举 + 主线程实测（best-stream 评分序 default 旗标 >
  声道数 > 首现，采样率不破平；mkv 封装器自动给首音轨打 DEFAULT——「无 default」素材须显式清旗）。
- 承重红线：不带 track 的所有既有行为逐字节不变（缺省 = av_find_best_stream 原式，不得偷换
  「音轨 0」语义）；三处选轨点必须同轨（错配 = DSP 系数按错采样率静默劣化，素材规格错开使必炸）。
- Windows 真机随 round-4 挂账（口径同 FR-4/9/10）。

## 进展

- round-0（2026-11-04）：设计轮收口。spec（含 §7.1 拍板）+ 验收骨架（goal/plan/matrix）落档；
  实现轮输入 = spec + 对抗报告要点（已并入 §7.1）。
- round-1（2026-11-04）：实现轮全绿。SG-1..SG-6 全部落地；`run-trk.sh` 单跑 600 PASS / 0 FAIL
  （BUILD + TRK-001..014 + TRK-GUI engine/gui 两相 + ZD-NET 十脚本）且 **ci-smoke 整链 EXIT=0**
  （`ci-smoke.sh build-linux` 相对调用形，1206 PASS 零 FAIL，证据 round-1/ci-smoke.log；传绝对构建
  目录会使 `$PWD/$BUILD` 双斜杠 → run-models 假红，记档 round-1.md「ci-smoke 整链门禁」节），
  证据 round-1/run-trk.log +
  round-1.md（含两处实现轮 bug 修复与四项判据偏差记档）。实现轮关键事实：①缺省零漂移门先红后绿
  （trackOf 无键返 0 / mp4 muxer und+全员 default 归一两 bug 均由该门抓出）；②fail-fast 时序以
  「非法模型+越界轨 → E_FILE、同模型+合法轨 → E_MODEL」对照锚定；③无旗标回落 = FFmpeg 编解码器
  偏好（aac>ac3）而非首条音频轨——实测证伪旧预测并记档（round-1.md 偏差 4）。文档同步：fr24 动词表
  track 列 + probe 条件键 + track 语义 bullet、CLAUDE.md FR-1 条目 + 架构行 + 方向节。Windows 随
  round-4 挂账。
