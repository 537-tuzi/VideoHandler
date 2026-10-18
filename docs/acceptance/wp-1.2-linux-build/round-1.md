# round-1 — WP-1.2a Linux DUT 构建落地（2026-10-18）

交付物（对照 spec `docs/spec/ci-build-strategy.md` §2/§6.1）：

1. **CMake 分层**：`engine` 静态库（全部 IStreamEngine 引擎 + fftranscoder/ffextractor + util/model）+ `video_handler` 应用（main/mediaservice/view）+ `engine_test` 无头 DUT（仅非 WIN32）。Linux 侧 FFmpeg/SoundTouch 走 apt pkg-config，ORT 走 `third_party/ort/linux`（官方 1.30.0 tgz，与 Windows 同版本——消灭本机 apt 1.23.2 的版本漂移）。
2. **路径参数化**：`QT_BIN`/`MINGW_BIN` cache 变量替代 `D:/qt6` 硬编码（preset 供默认、`-D` 可覆盖，NFR-M4 前置）。
3. **`scripts/fetch-deps.sh`**：ORT tgz + 两模型，SHA256 矩阵内嵌（与 spec §4 一一对应），`--check` 零副作用自检。
4. **engine_test**：裸 f32 交错流直调引擎（绕过 GUI 与 FFTranscoder 重采样），分块不变性即其首个用途。

## 证据（命令与输出摘录）

- fetch-deps：`--check` 空库报缺失 exit=1 → 模型播种后哈希符合跳过、ORT 下载解包 → 复检全 OK exit=0（幂等）。
- 构建：`cmake --preset linux-test && cmake --build --preset linux-test` → 19/19 三目标零警告（顺手清掉 round-3 修复遗留的 process() 死变量 `avail`，否则该警告会带进 Windows round-4）。
- RPATH：`ldd engine_test | grep onnx` → 解析到 `third_party/ort/linux/lib/libonnxruntime.so.1`，免 LD_LIBRARY_PATH。
- 分块不变性（同输入，块帧数 = 一次性 / 4096 / 977（ai 另加 257），输出 cmp 逐字节一致）：

| 引擎 | in 帧 | out 样本 | 结果 |
|---|---|---|---|
| dsp | 1323000 | 2646000 | 一致 |
| pitch(2.0) | 1323000 | 2646000 | 一致 |
| ai（真 GTCRN） | 480000 | 480000 | 一致 |
| sep-voc（真 UVR） | 1323000 | 2646000 | 一致 |
| sep-inst（真 UVR） | 1323000 | 2646000 | 一致 |

  该属性正是能提前抓住 avail 截断除法假帧（round-3 崩溃）那类 bug 的回归网——一次性喂入不触发、小分块喂入必触发。
- CLI：新构建 7 动词全 OK；**非 ORT 三路径（transcode/denoise/pitch）与 round-3 构建输出逐字节一致**（分层零行为变化）；ORT 三路径差异量化：ai 均差 6.35e-10 / 峰差 1.5e-8，sep 均差 5.29e-09 / 峰差 2.3e-7（≈−166dB，ORT 1.23.2→1.30.0 纯浮点版本噪声，远低于 −60dB parity 阈值）。
- 失败路径：sep 短输入（0.1s）→ `FAIL: 音频太短…` exit=1；ai 垃圾模型 → `FAIL: 加载 AI 模型失败…` exit=1 且无 bad.wav 残留。

## 新发现（归 WP-1.4）

- 分离路径 RSS 在 **ORT 1.30.0** 下仍 ~2.6GB（2638664/2627704/2627552/2627672/2627660 KB 五次采样）——复证 round-3 发现与 apt 1.23 无关，是版本无关的稳态开销。

## 盲点声明（工作原则 14）

- **Windows 分支本机不可执行**：CMake 的 if(WIN32) 块在本 Linux 配置中不进入执行路径，仅保证逐字保留原行为（链接项/编译选项/POST_BUILD 均照搬原文件）+ 脚本级语法有效。真正的 MinGW 复验在 WP-1.0 round-4（顺带验本包，matrix 已列 `compile-mingw` PENDING 行）。
