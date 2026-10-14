# round-3 — Windows 复验 runbook（WP-1.0 收官，关闭 R12/R13）

> **结果（2026-10-14 实测）：全绿，WP-1.0 收官**。R12/R13 已关闭（REQUIREMENTS §8）、CLAUDE.md P0 #8/#9 已摘牌、
> v1-development-plan WP-1.0 已标记完成。各项证据见下方各节「实测」小节；矩阵状态以 `matrix.csv` round-3 列为准。

> 在产品目标机（MinGW 工具链 + third_party 四套就位）上执行；逐项把命令输出粘进本文件，并更新
> `matrix.csv` 的 round-3 列。全绿后：REQUIREMENTS §8 R12/R13 标记关闭、CLAUDE.md P0 #8/#9 摘牌、
> v1-development-plan WP-1.0 标记完成。

## 0. 环境记录（实测）

- 目标机：Windows x64，Qt 6.11.1 MinGW + GCC 13.1 + Ninja + CMake 4.x，third_party 四套就位（`D:/qt6/...` 硬编码路径与本机吻合，NFR-M4 未动）。
- ffmpeg/ffprobe CLI 本机缺失（`third_party/ffmpeg/bin` 仅 5 个 DLL），临时下载 BtbN `n9.0.2 win64-lgpl-shared` 到仓库外 temp 目录用于造素材与体检，不入库。
- 素材偏差：lgpl 版无 libx264，tiny.mp4 改用内置 mpeg4 编码器（`--extract-audio` 为流复制，容器视频编码不影响用例有效性）。
- 观测手段：`add_executable(WIN32)`（架构审视 A9）致 GUI 子系统下 stdout 不继承控制台，`&` 直调无输出——改用 `Start-Process -RedirectStandardOutput` 捕获（不改产品代码；A9 待办不变）。
- RSS 测量注意：.NET `Process` 对象属性为启动快照缓存，轮询内须 `Get-Process -Id` 取新对象（sanity check：notepad 88.5MiB 正常）。

## 1. 编译（compile-mingw）

```
cmake --preset mingw-release
cmake --build --preset mingw-release
```

预期：0 error。产物 `src/bin/video_handler.exe`。（`CMakePresets.json` 的 `D:/qt6/...` 按本机路径调整——已知债 NFR-M4。）

> **实测（2026-10-14 21:31）**：`cmake --preset mingw-release` 配置通过（仅 Vulkan 头缺失警告，无关）；`cmake --build --preset mingw-release` 12/12 全绿 0 error，链接成功，`video_handler.exe` 刷新至 21:31:45（旧产物 10/13 16:45 不含 bb0e6ce/d2881f2 两次热修，本轮覆盖）。PASS。

## 2. CLI 冒烟（cli-5verbs / cli-with-models / ffprobe-health）

小素材（PowerShell 或 cmd）：

```
ffmpeg -y -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=30" -ac 2 -c:a pcm_s16le small.wav
ffmpeg -y -f lavfi -i "testsrc2=size=320x240:rate=25:duration=8" -f lavfi -i "sine=frequency=440:duration=8" -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest tiny.mp4
```

七个动词（模型放 exe 同目录的 `models/` 相对路径，缺省参即用默认路径；或显式传模型路径）：

```
video_handler --transcode-wav small.wav out1.wav
video_handler --denoise-wav   small.wav out2.wav
video_handler --ai-denoise    small.wav out3.wav
video_handler --pitch-wav     small.wav out4.wav 2.0
video_handler --sep-vocals      small.wav out5.wav <UVR模型.onnx>
video_handler --sep-instrumental small.wav out6.wav <UVR模型.onnx>
video_handler --extract-audio tiny.mp4
```

预期：全部 `OK:`；`ffprobe` 体检 out1–out6 为 `pcm_f32le / 44100 / 2ch / 30s`。AI/分离两动词是本轮新增覆盖（Linux 无模型权重，round-1/2 未跑）。

> **实测（2026-10-14）**：仓库根为 CWD，ai-denoise/sep 两动词不传模型参（验证 `main.cpp:48/:86` 缺省路径 `third_party/models/...` 生效）：
>
> | 动词 | 输出 | exit | 耗时 |
> |---|---|---|---|
> | --transcode-wav | `OK: …out1.wav` | 0 | 1s |
> | --denoise-wav | `OK: …out2.wav` | 0 | 1s |
> | --ai-denoise | `OK: …out3.wav` | 0 | 2s |
> | --pitch-wav 2.0 | `OK: …out4.wav` | 0 | 1s |
> | --sep-vocals | `OK: …out5.wav` | 0 | 55.6s |
> | --sep-instrumental | `OK: …out6.wav` | 0 | 55.5s |
> | --extract-audio | `OK: …tiny_audio.m4a` | 0 | 1s |
>
> ffprobe 体检：out1–out6 全部 `pcm_f32le,44100,2 | 30.000000` ✓；tiny_audio.m4a = `aac,44100,1 | 8.000000`（流复制保原轨，符合语义）。cli-5verbs / cli-with-models / ffprobe-health 全 PASS。

## 3. 错误注入回归（a2-inject-run-post / a2-inject-setup / sep-error-channel，可选）

PowerShell 造垃圾模型：`Set-Content garbage.onnx ([byte[]](1..256|%{Get-Random -Max 256}) -Encoding Byte)`

```
video_handler --ai-denoise small.wav bad.wav garbage.onnx
```

预期：`FAIL: 加载 AI 模型失败：…`，exit 1，**进程不崩、无 bad.wav 残留**。想复测 Run 期路径：在有 python3+onnx 的机器跑 `scripts/craft-mismatch-model.py` 生成模型后同样方式喂入，预期 `FAIL: AI 降噪推理失败：…`。

> **实测（2026-10-14）**：PowerShell 造 256B garbage.onnx，三条注入全过（消息为 GBK 控制台编码，已按 936 码页解码留证 `round-3/inject-log.txt`）：
>
> | 注入 | 输出 | exit | 半成品 |
> |---|---|---|---|
> | ai-denoise × garbage.onnx | `FAIL: 加载 AI 模型失败：Load model from …garbage.onnx failed:Protobuf parsing failed.` | 1 | 无 |
> | ai-denoise × 不存在路径 | `FAIL: 找不到模型 …nonexistent.onnx` | 1 | 无 |
> | sep-vocals × garbage.onnx | `FAIL: 加载人声分离模型失败：…Protobuf parsing failed.` | 1 | 无 |
>
> 进程均不崩。a2-inject-setup / sep-error-channel PASS。Run 期注入（a2-inject-run-post）为可选项，本机无 python+onnx，**SKIP**（Run 期路径 Linux round-2 已 PASS，维度错位模型注入与平台无关）。

## 4. RSS 复验（rss-windows，关 R12）

```
ffmpeg -y -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=7200" -ac 2 -c:a pcm_s16le long.wav
```

PowerShell 测峰值（对 `--denoise-wav` 与 `--ai-denoise` 各跑一次）：

```powershell
$proc = Start-Process -FilePath .\video_handler.exe -ArgumentList '--denoise-wav','long.wav','out-long.wav' -PassThru
$peak = 0
while (-not $proc.HasExited) { $peak = [Math]::Max($peak, $proc.PeakWorkingSet64); Start-Sleep -Milliseconds 500 }
"峰值 RSS: {0:N2} GiB" -f ($peak / 1GB)
```

预期：**≤ 2 GiB**（NFR-P4；Linux 参考值 57–59MB）。同时记录耗时。

> **实测（2026-10-14）**：long.wav = 2h 正弦 s16 立体声 1.18GiB，输出经 ffprobe 验证 `pcm_f32le,44100,2 | 7200.000000`（2.37GiB，完整）：
>
> | 动词 | 耗时 | 峰值 RSS | 判定 |
> |---|---|---|---|
> | --denoise-wav | 5.7s | **22.1 MiB** | PASS |
> | --ai-denoise（ORT + gtcrn_simple） | 696.1s | **47.9 MiB** | PASS |
>
> 均远低于 2GiB 红线（余量 40+ 倍）；与 5min 素材对照实测 44.9MiB 同量级（测量自洽）。R12 达成关闭条件。
> 注：Windows 值远低于 Linux 57MB 属正常——CLI 路径不启 GUI，MinGW DLL 按需分页仅触碰页计入 working set。

### 4.1 byte-identity（matrix 有此列，runbook 补记）

修复前基线：`git worktree` 检出 fffed23（已验证与 4e0f64c 源码零差异、即 10/13 旧 exe 同源），`third_party` 目录 junction 接入，MinGW 直编通过（本素材路径全 ASCII，fffed23 窄字符路径即可，无需 wchar 补丁）；修复后 = HEAD d2881f2 构建。

> **实测**：同 small.wav、同 `--denoise-wav`，双版本 exit=0：
> `SHA256(out-pre) == SHA256(out-post) == 23F05A2275299B14FB4A223ADACB4BEC6886A8A0693859A2A2990D91BF37648F` —— **逐字节一致 PASS**（排水时机修复不动样本值，Windows 侧复证）。

## 5. GUI 交互（gui-cancel / gui-close-window）

- **A11 关窗**：GUI 里起一个 2h 素材任务，任务进行中直接关窗——预期进程数秒内退出（不假死、不留后台进程）。
- **A12/A11 取消**：抽音轨任务进行中点取消——预期 ≤1s 生效、半成品文件被删；AI 降噪任务同测一次。
- 顺带观察：AI 任务正常完成时 jobDone 消息投递正常（round-2 的 runJob 兜底未触发即正常路径）。

> **实测（2026-10-14）**：`monitor.ps1`（存 `round-3/`）每 200ms 采样进程 CPU 时间 + 半成品文件状态（日志 `round-3/mon2.log`、`round-3/mon3.log`；首轮监控脚本因 PS5.1 无 BOM UTF-8 解析问题失效无日志，改 ASCII 版后有效）：
>
> - **jobDone 正常路径**：GUI 跑 small.wav AI 降噪 ~2s 完成，状态栏「AI 降噪完成: …/small_ai.wav」，文件落盘 10,584,092 字节 ✓（截图经操作人确认）。
> - **A12 取消抽音轨**：cancel30min.mp4（30min/1.7GB 自造）流复制中取消——半成品 `cancel30min_audio.m4a` 29,355,537B → 200ms 内缩为 14MB 后消失（删除路径生效）；CPU 时间 1.3s 后不再增长（worker 停止）；操作人主观确认 <1s 生效。PASS。
> - **A12 取消 AI 降噪**：long.wav 任务进行中（半成品 75,497,472B 增长中）点取消——200ms 内文件消失；CPU 5.1s 后停止；主观秒级生效。PASS。
> - **A11 任务进行中关窗**：long.wav AI 降噪跑至半成品 50,593,792B（CPU 8.7s 增长中）点窗口 X——**下一采样即 pid=-（≤200ms 退出，无假死）**；事后查无 video_handler 后台残留。PASS。
> - 全程无崩溃（操作人确认两次进程消失均为其主动关窗）。

## 6. 收尾清单（全绿后）

1. `matrix.csv` round-3 列全 PASS（Run 期注入 SKIP）；本文件已粘贴各项输出。✅ 2026-10-14
2. REQUIREMENTS §8：R12 → 已关闭；R13 → 已关闭。✅ 2026-10-14
3. CLAUDE.md P0 债 #8/#9 摘牌。✅ 2026-10-14
4. v1-development-plan WP-1.0 行标记完成。✅ 2026-10-14
5. 提交（热修源码两笔已在历史 bb0e6ce/d2881f2；本轮文档+验收一笔，中文提交信息）。
