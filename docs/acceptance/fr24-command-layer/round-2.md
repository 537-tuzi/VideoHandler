# round-2 — 片 D（GUI 迁移，Linux，2026-11-03）

环境：Linux x86_64 / gcc / Ninja / Qt6 apt / FFmpeg apt / ORT 1.30.0（fetch-deps）/ 两模型在位。
基线：`fd7badb`（片 C 合入；七槽仍直调引擎的最后一代码形态）。规格契约：`docs/spec/fr24-command-layer.md` **D6**。

## 改动清单（边界：只动 `src/service/ffextractor.{h,cpp}`）

| 文件:行 | 改动 |
|---|---|
| `src/service/ffextractor.h:1-10,30-31,45-46` | 头注释改述新职责（静态核心供命令层复用 + slots 为 GUI worker 壳）；删 `class IStreamEngine;` 前置声明与 `runJob`（引擎直调收尾），新增 `runRequest(const QJsonObject&, const QString&)`（命令层收尾）+ `#include <QJsonObject>` |
| `src/service/ffextractor.cpp:1-15` | include 换血：删 aidenoiser/pitchshifter/vocalseparator/fftranscoder（引擎构造与转码直调全部上移进命令层），增 commandlayer.h/QJsonObject；dspdenoiser.h 留（HumBase 枚举→请求字段映射） |
| `src/service/ffextractor.cpp:263-286` | `runJob`（probeSourceSpec 后自建引擎 + transcodeStreamWav 直调）→ `runRequest`：组 Ctx{cancelFlag, onProgress→emit progress} → `CommandLayer::execute` → 拆响应：`ok:true` 取 `result.output` 重建 `okMsg + "：<输出>"`；`ok:false` 直通 `error.message`；**兜底 catch 保留**（`任务异常终止：…`，与命令层 E_INTERNAL 双保险，防 execute 本身违约抛出穿透 worker 槽，spec D6） |
| `src/service/ffextractor.cpp:288-353` | 六个任务槽改「组 QJsonObject → runRequest」：doDenoiseWav（humBase/wind 字段化）、doDenoiseAiWav/doSepVocals（**壳内先做 appDir 相对解析 + 存在性检查 + GUI 缺模型文案**，再组 model 字段）、doPitchWav（semi 字段，okMsg 由槽参数 double 直接格式化）、doExtractAudio/doExtractVideo（不传 output=缺省自动命名同规则） |
| 不动 | `doProbe`（255-261，片 C 已拆 probeInfo/probeText，行为不变——本轮确认）；静态核心 probeInfo/probeText/probe/extractAudio/extractVideo（120-253 一字未动，命令层唯一 FFmpeg 遍历实现）；`mediaservice.{h,cpp}`、`mainwidget.*`、`commandlayer.*`、五引擎、`streamengine.h`、全部脚本（`git diff --stat fd7badb -- src/` = 仅 ffextractor 两文件，71+/66−） |
| 新建验收脚本 | `scripts/gui-slot-eqv.sh`（基线 worktree 双树对照编排）+ `scripts/gui-slot-probe.cpp`（七槽直调探针，转录 jobDone/probeDone/进度/取消） |

## 七槽对照清单（spec 风险 1 缓解，旧 = `git show fd7badb:src/service/ffextractor.cpp`）

逐槽四维对照：请求 JSON 形状 / 成功文案模板 / 失败文案来源 / 进度与取消通路。「等价」一列给出代码级论证；全表另有运行时实证兜底（见下节探针，基线 vs 新代码双树同媒体对照，转录逐字 diff + 产物 cmp）。

| 槽 | 请求 JSON（新） | 成功文案（新旧同模板） | 失败文案来源 | 进度/取消通路 | 等价论证 |
|---|---|---|---|---|---|
| doExtractAudio | `{"cmd":"extractAudio","input":<in>}`（不传 output） | `音轨提取完成：<result.output>` | `error.message` 直通（底座 extract 文案：打不开文件/没有音频流/写包失败…，E_FILE/E_CANCELLED/E_INTERNAL 分类不改文本） | ctx.onProgress→emit progress（进 extractAudio 的 copy_stream 包粒度回调）；ctx.cancel=cancelFlag（包粒度检查点） | 旧 337-342 直调静态 `extractAudio(in,err,progress,cancel)`（outPath 空=自动命名）；新经 cmdExtractAudio 调**同一静态函数**、传同一空 outPath（explicitOutput 取不存在的 output 字段=空串），校验层 E_PARAMS 对 GUI 不可达（mainwidget 保证 input 非空）。文案：旧 `QStringLiteral("音轨提取完成：%1").arg(out)` = 新 `okMsg+"：%1".arg(out)` 同串 |
| doExtractVideo | `{"cmd":"extractVideo","input":<in>}` | `无声视频提取完成：<result.output>` | 同上 | 同上 | 旧 344-349 vs cmdExtractVideo，论证同上（kSuffixMute 后缀规则留在底座内部，两侧同函数同参数） |
| doProbe | 不组请求（片 C 设计：命令层与 GUI 各取所需，两个渲染端共用单一 probeInfo 实现，spec D6） | `probeDone(ok, probeText(info))` 中文渲染 | `probeDone(false, err)`（probeInfo 错误说明） | 无（探测无进度语义；不占任务槽） | 255-261 **未改动**：probe() = probeInfo+probeText 组合（188-197），与 fd7badb 逐字同源；片 C round-1 已证 probeInfo/probeText 平移逐字等价 |
| doDenoiseWav | `{"cmd":"denoise","input":<in>,"humBase":"50"\|"60"\|"off","wind":<bool>}` | `降噪完成：<result.output>` | `error.message` 直通（transcode 文案 + DspDenoiser 无 setup 失败面） | ctx 双通路进 transcodeStreamWav 分块循环（与旧 runJob 同一注入点） | 旧 282-288：probeSourceSpec→DspDenoiser.setup(rate,ch,hum,wind)→runJob(suffix=kSuffixDenoised, 0,0)；新 cmdDenoise 逐行同构（probeSourceSpec→setup→runTranscode→transcodeStreamWav 同参 0,0），缺省名同规则（outputOr 无 output 字段 = `absolutePath/completeBaseName+"_denoised"+".wav"`，与旧 runJob 266 行同式）。humBase 枚举→字符串无损三分支映射；off+off → E_PARAMS 对 GUI 不可达（mainwidget:172-175 前置拒绝）且旧路径同样到不了 |
| doDenoiseAiWav | `{"cmd":"aiDenoise","input":<in>,"model":<appDir>/kAiModelRelPath}`（**壳内先解析+查存在**） | `AI 降噪完成：<result.output>` | 缺模型：**壳内** `找不到 AI 降噪模型：<路径>`（GUI 原文案，命令层 `找不到模型 %1` 对 GUI 不可达）；setup 失败/转码失败：`error.message` 直通 | 同 denoise | 旧 290-304：appDir 解析+exists 检查+GUI 文案在槽内 → AiDenoiser.setup → runJob(kSuffixAi, kCanonicalRate/Channels)；新：**前两步原样留在槽内**（文案逐字同源），setup/转码上移 cmdAiDenoise（setupErr 同文本经 E_MODEL.message 直通），lockCanonical=true 即旧 runJob 的 44100/2 显式参。每调用自建 AiDenoiser 实例语义由 execute 内建获得 |
| doPitchWav | `{"cmd":"pitch","input":<in>,"semi":<double>}` | `变调完成（<semi> 半音）：<result.output>` | `error.message` 直通（PitchShifter setup 失败=E_ENGINE 同文本；转码族同上） | 同 denoise | 旧 306-316：probeSourceSpec→PitchShifter.setup(rate,ch,semi,err)→runJob(kSuffixPitch)；新 cmdPitch 同构；okMsg 由**槽参数 double** 直接 `.arg`（不经 JSON 往返，格式化逐字同 `QString::arg(double)`）；E_PARAMS（0/越界）对 GUI 不可达（mainwidget:53,193-195 前置拒绝）——纵深防御非行为变化 |
| doSepVocals | `{"cmd":"separate","input":<in>,"model":<appDir>/kSepModelRelPath,"stem":"vocals"\|"instrumental"}`（壳内先解析+查存在） | instrumental ? `伴奏提取完成：<out>` : `人声分离完成：<out>` | 缺模型：壳内 `找不到人声分离模型：<路径>`；setup 失败（E_MODEL）/WP-1.4 输出契约违规（E_MODEL「模型…」前缀文案）/转码失败：`error.message` 直通 | 同 denoise | 旧 318-335：appDir+exists+GUI 文案 → setInstrumental → setup → runJob(双 stem 后缀/文案, 44100/2)；新 cmdSeparate 同构（stem 字段↔setInstrumental 双射），双 stem 的后缀选择 `instrumental?kSuffixInstrumental:kSuffixVocals` 与旧同式 |

**「已取消」特判核实**（spec D6 硬约束）：`mainwidget.cpp:229` 为 `else if (msg != QStringLiteral("已取消")) QMessageBox::warning(...)`——仅当失败文案**逐字等于**「已取消」时不弹警告框（仅状态栏呈现）。链路核实：底座取消点返回字面量「已取消」（fftranscoder 分块循环 / ffextractor.cpp copy_stream:82）→ `classifyTranscodeError/classifyExtractError` 命中即 `errorResponse(E_CANCELLED, "已取消")`（**message 原样携带该字符串**，commandlayer.cpp:47,60）→ 槽直通 `jobDone(false, message)`。直通结果逐字一致，**且经 gui-slot-eqv 探针在 denoise/extract 双路运行时实证**（转录 `TASK cancel-* -> ok=0 msg=已取消`，新旧两侧同串）。

**成功文案一致性**：`okMsg + QStringLiteral("：%1").arg(out)` 与旧 runJob:279 / extract 槽的 `QStringLiteral("…：%1").arg(out)` 为同一拼接式；out 来自 `result.output` = 底座成功返回的同一 `outPath` 变量（转码族 outputOr 计算式与旧 runJob:266 同式；extract 族为底座内自动命名返回值）。

**行为差异穷举（新旧不同处，均有依据）**：
1. 底座异常（引擎违约抛出）的兜底文案：旧 `任务异常终止：…`（runJob catch）→ 新 `内部异常：…`（execute 内层 catch 先命中，E_INTERNAL，spec D3 既定分类）；槽内 catch 保留同文案，仅在 execute **本身**违约抛出时触达（双保险，spec D6）。注：旧 runJob 的 try 只包 transcodeStreamWav 调用，屏障之外的异常面见第 4 条。
2. E_PARAMS 类校验（input 空/output==input/semi 越界等）为新增纵深防御层，GUI 前置拒绝使其不可达——不在「文案不变」约束面内。
3. 进度/取消/内存语义零变化：同底座函数、同注入点、每调用自建引擎实例（execute 内建）。
4. **异常屏障覆盖范围扩大（审查 round-2b P2 补录，改进非回归）**：旧 runJob 的 try 只包 transcodeStreamWav——probeSourceSpec、引擎构造、`setup()`、extract 静态直调全在屏障外；而 AiDenoiser/VocalSeparator 的 setup 只 catch `Ort::Exception`（aidenoiser.cpp:318 / vocalseparator.cpp:399）。用户放置的模型若 metadata 谎报巨大尺寸 → setup 内容器 `assign` 抛 std::length_error/bad_alloc（非 Ort::Exception）→ 旧形态异常逃出 worker 槽，Qt6 事件处理器未捕获异常 → **std::terminate 整应用崩溃且无任何文案**；新形态被 execute 外层 catch（commandlayer.cpp:354-357）兜住 → `jobDone(false, 内部异常：…)`，GUI 存活。触发器真实存在（appDir 模型文件内容用户可控），初版「该路径 GUI 无可达触发器」论断不成立（WP-1.0 round-2 只证了 process/flush 屏障，未覆盖 setup 期非 Ort 异常）。

## 证据

### 1. GUI 槽级等价（`bash scripts/gui-slot-eqv.sh fd7badb build-linux`，全绿）

spec 风险 1（GUI 迁移无自动化护栏）的针对性缓解：基线 fd7badb worktree 与当前树各建 libengine.a，同一探针源直调七槽，同媒体双跑两相（nomodel 15 槽次 / withmodel 16 槽次）：

- **GUI-EQV 转录逐字一致 ×2 相**：全部 jobDone/probeDone 文案（13 种成功/失败/缺模型/取消形态）+ 进度序列（如 denoise 族 `20,40,61,81,100`）归一化（媒体/bin 路径占位符）后 diff 为空；
- **GUI-CXL「已取消」双路直通** + 取消无半成品残留（NFR-R1）；
- **GUI-PRD 产物 8 件 cmp 逐字节一致**：tiny_audio.m4a 27630B / tiny_mute.mp4 105383B / st_denoised.wav / st48_denoised.wav（48k 跟随源）/ st_pitch.wav / st_ai.wav / st_vocals.wav / st_instrumental.wav（引擎链输出含 AI/MDX 推理逐位复现）；
- worktree 用后即清（`git worktree list` 无残留）。

探针覆盖缺口（初版如实，处置见 round-2b）：初版脚本 nomodel 相模型预置致缺模型分支空转（审查 P1，已修复并复跑真实行使）；坏模型文案经 GUI 槽已由 round-2b 对抗增补覆盖（推理期 E_ENGINE 三行 ORT 文案逐字一致 + 无半成品）；setup 期失败文案仍仅在命令层面覆盖（CMD-031/032），槽侧走同一 error.message 直通通路；off+off/semi=0 等 E_PARAMS 面对 GUI 不可达已经测试代理静态核对（mainwidget 前置拒绝 file:line 证据）。

### 2. ci-smoke 全量不回归（`bash docs/acceptance/wp-1.2-linux-build/scripts/ci-smoke.sh build-linux`，全绿）

FFT 属性 + 五引擎分块不变性 + parity 四族（PAR-SR 48k bit-exact 240000/240000、PAR-PT/GT/DS 全过）+ CLI 冒烟 w1–w6 OK + ffprobe 体检 PASS + **run-json 36 条全 PASS**（CMD-001..035 + f/i 子断言）。构建零警告（`cmake --build --preset linux-test`，-Wall -Wextra）。

### 3. offscreen GUI 冒烟（无参数 GUI 分支）

`QT_QPA_PLATFORM=offscreen timeout 6 ./build-linux/video_handler`：存活 6s 被 timeout 正常收割（exit 124），stdout 空，stderr 仅 `This plugin does not support propagateSizeHints()`——**基线 fd7badb（stash 后重建）同样输出此条**，为 offscreen QPA 平台固有限制（尺寸提示协议不支持），非本次改动引入；无任何 `QObject::connect` 失败 / QMetaType 注册 / 队列投递告警（MediaService 构造期 connect 面全通过）。

### 4. 改动范围断言

`git diff --stat fd7badb -- src/`：仅 `ffextractor.cpp`（71+/66− 内含注释）与 `ffextractor.h`——mediaservice/mainwidget/commandlayer/引擎零改动，信号签名（progress/jobDone/probeDone）与槽签名不变，busy 锁/QThread 调度/HumBase 元类型注册面原样。

## 薄弱 / 未验证点

- **GUI 真机交互未验**（headless 无显示）：按钮点击→队列投递→worker 线程执行→主线程回包的端到端链路、QMessageBox 弹窗呈现、取消按钮中途打断长任务的时序，须 Windows 真机（随 WP-1.0 round-4 或本包 Windows 轮）。探针为同线程直调槽体，覆盖槽内全部代码路径但不覆盖 Qt 跨线程投递面。
- 探针未注入坏模型走 AI/sep setup 失败文案（同文本通路已由 CMD-031/032 在命令层锁定）。
- Windows 真机：`--json`/薄壳/命令层/GUI 全面随 round-4 复验（CLAUDE.md 既有挂账）。

## 审查与独立验收处置（round-2b，主线程落地）

对抗式审查（**0 P0 / 2 P1 / 1 P2 / 6 P3**，产品代码零问题）与独立测试验收（4 项 PASS + 1 项按处置作废）结论「可合入，产品代码零改动」，处置记录：

- **P1-1（已修，验收工具）**：gui-slot-eqv.sh 初版在 A/B 双相循环**之前**预拷两模型到探针 bin 目录（appDir 解析位）——nomodel 相实际带模型，ai-missing/sep-missing 走成功推理路径，**缺模型分支从未行使**，初版「缺模型文案已覆盖」声称失实（双侧同走错分支，等价 diff 仍绿不可辨）。修复：模型落位后置到 A 相跑完之后 + 新增分支内容断言（A 相断 `ai-missing/sep-missing ok=0` + GUI 原文案入转录，B 相断 `ai/sep ok=1`）+ stderr 断言改显式 if（顺收 SC2015 可读性与未来 `set -e` 误杀隐患——测试代理实测旧式双空情形复合语句退出码为 1）。主线程复跑修复版全绿：A 相 15 槽次 + 缺模型分支真实行使、B 相 16 槽次 + 带模型分支真实行使、产物 8 件 cmp、「已取消」双路、无半成品。
- **P1-2（已更正，文档）**：缺模型覆盖声称随修复复跑后为真；CMD-EQV round-2 标注「（ci-smoke）」不实（ci-smoke 只接 run-json.sh，thinshell 未随跑）→ 改「沿 round-1：main.cpp/commandlayer 零改动」。
- **P2（已补录，代码不动）**：行为差异穷举漏第 4 条——异常屏障覆盖范围扩大（setup 期非 Ort 异常旧=std::terminate 崩溃无文案、新=E_INTERNAL 兜住，改进非回归）；初版「无可达触发器」论断证伪更正（WP-1.0 round-2 屏障实证只覆盖 process/flush，不含 setup 期；审查给出行号已主线程独立核验：aidenoiser.cpp:318 / vocalseparator.cpp:399 / commandlayer.cpp:354-357）。见上文差异 4。
- **测试代理对抗增补（4 项 PASS，独立于实现方与主线程）**：
  - **坏模型经 GUI ai 槽**（堵初版探针缺口）：mismatch 模型置探针 bin 侧 `models/denoiser/`，双树 withmodel 相——`ai` 槽两侧 ok=0，三行 ORT 文案（`AI 降噪推理失败：Got invalid dimensions for input: x…Got: 257 Expected: 100`）经 error.message→QJsonObject→jobDone 往返**逐字一致**（多行文案无损直通，E_ENGINE message 保真运行时实证）；失败在推理期非 setup 期（与 CMD-032 同类）；无半成品；同场其余 6 产物 cmp 逐字节一致。
  - **stderr 断言语义实测**：四情形 bash 实测，`&&/||` 同级左结合解析为 `(A||B)&&C`，符合「任一非空即 FAIL」意图，无行为缺陷。
  - **GUI 前置拒绝面静态核对**：off+off（mainwidget.cpp:171-175 先于 :180 投递）、semi=0（:192-196 先于 :199）、越界（spinSemi `setRange(-12,12)` 钳制）、无旁路（七槽唯一投递源 = MediaService 信号 ← MainWidget 包装槽）——E_PARAMS 对 GUI 不可达成立。
  - **offscreen 冒烟复跑**：exit 124 / stdout 0 字节 / stderr 恰 1 行平台固有。
- **P3 处置**：探针 lambda connect 无 context（单线程直调、sender 全程存活，无风险；复用到多线程场景需补）；TASK 槽数只打印不断言（已由内容断言 + 探针退出码兜底）；gui-slot-eqv 未进 ci-smoke（一次性等价证据，GUI 面常设回归网随 Windows 轮评估）；引擎析构时点由 jobDone 后提前到前（非 QObject、无观察面）。

主线程复核：产品代码自实现轮以来零改动（`git diff fd7badb -- src/` 恒为 ffextractor 两文件）；「构建零警告」经 touch 强制重编译独立复证；ci-smoke 全量与修复版 gui-slot-eqv 均主线程独立复跑全绿。
