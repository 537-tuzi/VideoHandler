# round-3 — Windows 复验 runbook（WP-1.0 收官，关闭 R12/R13）

> 在产品目标机（MinGW 工具链 + third_party 四套就位）上执行；逐项把命令输出粘进本文件，并更新
> `matrix.csv` 的 round-3 列。全绿后：REQUIREMENTS §8 R12/R13 标记关闭、CLAUDE.md P0 #8/#9 摘牌、
> v1-development-plan WP-1.0 标记完成。

## 1. 编译（compile-mingw）

```
cmake --preset mingw-release
cmake --build --preset mingw-release
```

预期：0 error。产物 `src/bin/video_handler.exe`。（`CMakePresets.json` 的 `D:/qt6/...` 按本机路径调整——已知债 NFR-M4。）

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

## 3. 错误注入回归（a2-inject-run-post / a2-inject-setup / sep-error-channel，可选）

PowerShell 造垃圾模型：`Set-Content garbage.onnx ([byte[]](1..256|%{Get-Random -Max 256}) -Encoding Byte)`

```
video_handler --ai-denoise small.wav bad.wav garbage.onnx
```

预期：`FAIL: 加载 AI 模型失败：…`，exit 1，**进程不崩、无 bad.wav 残留**。想复测 Run 期路径：在有 python3+onnx 的机器跑 `scripts/craft-mismatch-model.py` 生成模型后同样方式喂入，预期 `FAIL: AI 降噪推理失败：…`。

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

## 5. GUI 交互（gui-cancel / gui-close-window）

- **A11 关窗**：GUI 里起一个 2h 素材任务，任务进行中直接关窗——预期进程数秒内退出（不假死、不留后台进程）。
- **A12/A11 取消**：抽音轨任务进行中点取消——预期 ≤1s 生效、半成品文件被删；AI 降噪任务同测一次。
- 顺带观察：AI 任务正常完成时 jobDone 消息投递正常（round-2 的 runJob 兜底未触发即正常路径）。

## 6. 收尾清单（全绿后）

1. `matrix.csv` round-3 列全 PASS；本文件粘贴各项输出。
2. REQUIREMENTS §8：R12 「Windows 同款复验后关闭」→ 已关闭；R13 → 已关闭。
3. CLAUDE.md P0 债 #8/#9 摘牌（改写为已修历史或删除）。
4. v1-development-plan WP-1.0 行标记完成。
5. 提交（两笔：热修源码 / 文档+验收），中文提交信息。
