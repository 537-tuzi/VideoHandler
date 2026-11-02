# round-1 — 片 C（核心 + CLI + 验收基建，Linux，2026-11-03）

环境：Linux x86_64 / gcc / Ninja / Qt6 apt / FFmpeg apt / ORT 1.30.0（fetch-deps）/ 两模型在位。
基线：`4657ccd`（FR-24 改动前最后一代码提交；`b45dba6` 仅文档）。规格契约：`docs/spec/fr24-command-layer.md`。

## 改动清单

| 文件 | 改动 |
|---|---|
| 新建 `src/service/commandlayer.h`（28 行） | `CommandLayer::execute(QJsonObject, Ctx) -> QJsonObject` + `Ctx{cancel, onProgress}` + `okResponse/errorResponse` 公开构造 |
| 新建 `src/service/commandlayer.cpp`（355 行） | 严格 schema 校验（信封 v/cmd/未知字段/input/output+原件只读防线）+ 8 个 `cmdXxx` 处理器（9 动词形态，separate 双 stem）+ 六类错误码分类（`classifyTranscodeError`：已取消 → 引擎 lastError → 文件族前缀 → 兜底）+ 缺省命名 `outputOr`（§5.3 缝）+ execute 最外层 catch → E_INTERNAL |
| `src/service/fftranscoder.h:17-29` / `.cpp:134,150-151,325-332` | `OutInfo{srcRate,srcChannels,outRate,outChannels,frames}` 可选出参（默认 nullptr 不动现有调用方）；解码真值在 `open_decoder` 后捕获，**仅成功收尾单点回填**（`av_write_trailer` 后、`return outPath` 前），失败路径不填（spec 风险 2） |
| `src/service/ffextractor.h:4-26,36-44` / `.cpp:130-197,201-255` | `probe` 拆 `probeInfo()`（结构化 ProbeInfo/ProbeStream）+ `probeText()`（中文渲染逐字平移），probe 槽内部改调两者、行为不变；`extractAudio/extractVideo` 加可选显式 `outPath`（空 = 原自动命名；显式路径在输入成功打开后才落位，失败路径绝不误删既有文件） |
| `src/model/data.h:10` | 新增 `kSuffixTranscoded = "_transcoded"`（transcode 缺省命名，GUI 无对应按钮） |
| `src/main.cpp`（145→159 行重写） | `runJson`（stdin→fromJson→execute→stdout 单 JSON 文档+\n，fwrite 保 UTF-8）+ `runCli` 七动词薄壳（argv→组请求→execute→`OK:/FAIL:` 逐字保持 + 旧退出码；模型缺省 third_party 相对由薄壳显式注入）+ 未知动词 FAIL（D7 收紧 #1）+ pitch 范围校验（D7 收紧 #2）+ `--json` 多余参数回 E_PARAMS JSON |
| `CMakeLists.txt:17-18` | engine 源列表 + commandlayer.{h,cpp}（D9） |
| `docs/acceptance/wp-1.2-linux-build/scripts/ci-smoke.sh:80-81` | ffprobe 体检段后追加 run-json.sh 调用（parity 同款接线） |
| 新建 `docs/acceptance/fr24-command-layer/scripts/` | `run-json.sh`（CMD 用例族编排）+ `json_check.py`（断言器）+ `craft-mismatch-model.py`（坏模型 v2）+ `thinshell-eqv.sh`（薄壳等价） |

不动的（片 C 边界）：ffextractor 七个 do* 槽、mediaservice、mainwidget、五引擎、streamengine.h、parity 全套脚本。

## 证据

### 1. CMD 用例族（`bash scripts/run-json.sh build-linux`，全绿）

正例关键断言值（响应实测，非推断）：

- **48k 跟随源**（P0 #4 行为进合同）：CMD-003/004 `outputSpec{48000,2}` + `engineResampled:false` + ffprobe 实文件 `pcm_f32le,48000,2`；
- **48k aiDenoise**：CMD-006 `engineResampled:true` + `outputSpec{44100,2}`（锁兜底）+ durationSec∈[4.9,5.0]（(N//hop)·hop 约定）+ ffprobe `pcm_f32le,44100,2`；
- CMD-002：`frames=220500`、`durationSec=5`、`sourceSpec==outputSpec=={44100,2}`；
- CMD-008（48k 源 separate）：`engineResampled:true` + 输出 44.1k/2；CMD-001 probe：streams[0] video/h264/320x240、streams[1] audio/aac/44100/1ch；
- CMD-010/011 缺省落源目录：`<dir>/tiny_audio.m4a`、`<dir>/tiny_mute.mp4` 实存。

负例 14+2 条全绿：E_PARAMS×10（非法 JSON/未知 cmd/未知字段/缺 input/semi:"abc"/semi:15/semi:0/humBase:"61"/off+off/output==input/v=2）、E_FILE×2（打不开/无音频流）、E_MODEL×1（模型缺失）、E_ENGINE×1（坏模型推理期）——全部断言 `absent:result` + exit 1；CMD-032f 无半成品；CMD-033i 输入逐字节未动；CMD-021r 响应原文逐字节确定；CMD-012 同请求两遍 cmp 一致。

### 2. 薄壳等价（`bash scripts/thinshell-eqv.sh 4657ccd build-linux`，16/16 全绿）

基线 worktree 构建（third_party 软链共享）后同参数双跑，三方比对（stdout 逐字 diff / 退出码 / 产物 cmp）：

- 成功路径 12 形态：transcode 44.1k 与 48k、denoise 缺省与 hum60、ai 显式与缺省模型、pitch +2.0/−2.5、sep 双 stem、extract 双形态——全部 `OK: <同路径>` 逐字一致、产物逐字节一致；
- 失败路径 4 条：hum=61 参数错、模型缺失、argc 不足用法错、无音频流——`FAIL:` 文案逐字一致、rc=1、双方均无产物。

有意收紧点（D7，不算回归）：未知动词旧=静默 extractAudio→新=FAIL；pitch 越界/0 旧=接受→新=FAIL。worktree 用后已清（`git worktree list` 无残留）。

### 3. ci-smoke 全量不回归（`bash docs/acceptance/wp-1.2-linux-build/scripts/ci-smoke.sh build-linux`，全绿）

FFT 属性 + 五引擎分块不变性 + parity 四族（PAR-DS/GT/MD/PT/SR 实测 −51.4~−123.9dB 全过、48k bit-exact 240000/240000）+ CLI 冒烟 w1–w6 OK + ffprobe 体检 + **run-json 35 条全绿**（本包接线行）。

## 本轮修过的坑（负例实打实暴露）

1. `isFileError` 前缀表首版用 `QLatin1String` 装中文——Latin-1 无多字节，前缀永不匹配，CMD-029/030 落 E_INTERNAL；改 `QStringLiteral` 后归位 E_FILE。
2. wp-1.0 坏模型配方（s0/s1/s2 命名）被 WP-1.1 后 GTCRN setup 命名契约拒收（E_MODEL）——写 v2 配方（命名合规 + 维度错位）走推理期 E_ENGINE。
3. extract 族 `open_output` 曾传空参数而非落位后的本地路径（extract 动词全灭）——修正为 `path`。
4. CMD-008 首版用 44.1k 源断言 `engineResampled:true`——MDX 工作点即 44.1k/2，实测 false；改 48k 源后断言成立。

## 薄弱 / 未验证点

- **GUI 路径未迁**（片 D）：ffextractor 七槽仍直调引擎，本片它们不经命令层；GUI 文案/取消/进度链路（Ctx.onProgress/cancel 的真实消费）无运行时验证。
- **E_CANCELLED / E_INTERNAL 无运行时触发用例**：CLI 单发同步不可达取消（D3 刻意）；E_INTERNAL 仅兜底 catch（本轮无触发器，分类分支靠 isFileError 反例间接覆盖）。
- `--json` 在 Windows 的控制台编码/AttachConsole 行为未验（OQ-4 不做，真机随 round-2/WP-1.0 round-4）。
- CMD-032 依赖 python3 onnx，裸机无 onnx 时 SKIP（CI parity 依赖已含 onnx，门禁不虚）。
- extract 族缺省命名仍留在底座（编解码器依赖），工作区包接管落盘位置时需连底座规则一起评估（§5.3 缝的边界已注释）。

## 审查与独立验收处置（round-1b，主线程落地）

对抗式审查（0 P0 / 1 P1 / 2 P2 / 5 P3）与独立测试验收（七项全 PASS + 对抗用例 15/15 判对）结论「可合入」，以下为处置记录：

- **P1-1（已修，拍板=改 classify）**：WP-1.4 输出契约校验族（vocalseparator.cpp:217/222/230 三条「模型…」文案）原落 E_ENGINE，与 D3 表 E_MODEL 行矛盾——spec D3 与 §6 自相矛盾（D3 说契约不合规→E_MODEL、§6 说维度错位→E_ENGINE），实现跟了 §6。拍板改代码：`classifyTranscodeError` 引擎 lastError 分支内「模型」前缀 → E_MODEL（NFR-R4：Agent 据此引导重下载；损坏 UVR 模型报「引擎 bug」会走错恢复路径）。锚点是 WP-1.4 负例测试固化的契约文案而非任意文本；运行期异常族（「推理失败：…」/「音频太短…」）无「模型」前缀，分流干净。触发路径受 MD5 查表护甲不可构造 e2e 负例，以代码级证据收口（审查代理已代码推演路径唯一性）。
- **P2-1（已修）**：NaN 半音数绕过否定式比较（NaN 比较恒 false）——shell（main.cpp）与 cmdPitch（commandlayer.cpp）范围校验改否定式 `!(semi>=-12 && semi<=12)`，NaN 落拒收；新增 CMD-035 薄壳负例（`--pitch-wav … nan` → FAIL + 无半成品）。JSON 面天然免疫（JSON 无法表达 NaN），GUI spinbox 免疫。
- **P2-2（已修）**：json_check.py 两处 bool 断言加 `isinstance(v, bool)` 前置（数字 0/1 不得冒充 bool）。
- **P3-5（已修）**：CMD-001 补 `streams.0.codec=h264`、`streams.1.channels=1` 两断言，matrix 声称成真（字段本就在 probeInfo 里）。
- **P3-1（记录）**：E_INTERNAL 实际承载面（送包解码失败/写包失败/写尾失败/内存不够/重采样器起不来）与 D3 行文漂移——「写文件头失败」在 E_FILE 表而「写文件尾失败」不在，属底座文案历史不一致；Agent 语义上「送包解码失败」（源损坏）报 E_INTERNAL 有误导，随注册表包把底座错误结构化（错误码通道替代文案前缀匹配）时一并消灭，goal.md 已挂账。
- **P3-2（挂账 FR-A4/后续）**：截断源 transcode 会 ok:true（av_read_frame 中途 I/O 错被当 EOF，底座既有行为）——OutInfo 加 truncated 标志属底座改动，随后续包。
- **P3-3（挂账 FR-A4）**：extract 显式 output 指名覆盖既有文件、失败会删除该路径（与 transcode 族 NFR-R1 一致）；「拒绝覆写或临时名落盘」随工作区包收紧。
- **P3-4（记录）**：--json 模式 FFmpeg 仍向 stderr 打日志（D1 只约束 stdout，实测 stdout 恒纯 JSON）；OQ-2「CI 安静」表述未把这类噪声算入，真机 round 验证时注意。

主线程复核：三处代码修复后重建 + run-json 36 判定 + thinshell 16/16 + ci-smoke 全量复跑全绿（见下节）。
