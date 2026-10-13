# CLAUDE.md

VideoHandler — Qt6 Widgets + FFmpeg + ONNX Runtime 的音频处理桌面软件。现状：离线处理（DSP/AI 降噪、人声分离、变调、视频音轨抽取）已完成核心；目标：实时处理 + RVC 变声 + 外挂训练器 + 对话式 Agent（见需求文档）。

## 必读文件

- `docs/REQUIREMENTS.md` — **需求基线**（决策 D1–D7、FR/NFR 全表、风险、版本规划、开放问题 O1–O4）。任何改动先对照此文档；需求变更先改文档再写码。
- `README.md` — 第三方依赖获取方式。

## 构建与验证

- 目标平台 **Windows x64**；工具链 Qt 6.11 (mingw_64) + MinGW GCC 13.1 + Ninja + ONNX Runtime 1.30 CPU + FFmpeg 9 LGPL shared。
- 配置 `cmake --preset mingw-release`，构建 `cmake --build --preset mingw-release`，产物 `src/bin/`；debug 用 `mingw-debug` → `src/bin-dbg/`。
- ⚠ `CMakePresets.json` 与 `CMakeLists.txt` 末尾硬编码 `D:/qt6/...` 绝对路径（已知债，NFR-M4 要消灭）。**本 Linux 环境不可直接构建**；Windows 上需按机器调整路径。
- ⚠ `third_party/` 不入库（.gitignore 已配），按 README 手工获取（ffmpeg/ort/soundtouch/models 四套），缺失时构建必败。
- 改动引擎后必跑 CLI 冒烟（免 GUI 自动验证，`src/main.cpp` 的 run_cli）：
  ```
  video_handler --transcode-wav  <in> <out.wav>
  video_handler --denoise-wav    <in> <out.wav>
  video_handler --ai-denoise     <in> <out.wav> [model.onnx]
  video_handler --pitch-wav      <in> <out.wav> <半音数>
  video_handler --sep-vocals     <in> <out.wav> [model.onnx]
  video_handler --sep-instrumental <in> <out.wav> [model.onnx]
  ```
- 暂无单测框架；引擎正确性验收 = parity 测试（对照官方实现，−60dB RMS 准入，NFR-Q1，待建）。

## 架构（现状）

```
main.cpp            CLI 分支（argc>=3）或 GUI；GUI 为组合根（MediaService 注入 MainWidget）
view/mainwidget     唯一窗口；信号槽转发
service/mediaservice QThread worker 调度 + busy 锁 + 取消原子量（一次一任务）
service/ffextractor  任务执行者；静态核心供 CLI 复用
service/fftranscoder 唯一转码通路：解码 → swr 到引擎规格 → 引擎 → swr 回 44.1k → WAV(f32)
service/ 引擎        全部实现 IStreamEngine { inRate / inChannels / process / flush }
  dspdenoiser        RBJ biquad 陷波(50Hz×3) + 高通，无内部缓冲
  aidenoiser         GTCRN 流式（16k 单声道；STFT/WOLA 自实现；ORT 用 PIMPL 隔离）
  pitchshifter       SoundTouch 封装
  vocalseparator     UVR MDX-Net（44.1k 立体声；torch 反射填充 + WOLA + 窗²归一化）
util/fft.h           自研 FFT（基-2 + Bluestein 任意长度）
util/ffutil.h        FFmpeg 错误码/扩展名胶水
model/data.h         全局常量（kCanonicalRate=44100、后缀、模型相对路径）
```

关键约定（新代码必须遵守）：

- 引擎缓冲自持：`process()` 输出可滞后于输入，`flush()` 收尾；FFTranscoder 已按此假设处理任意滞后的引擎。
- 输出统一 44.1k/2ch float WAV；采样率/声道换算全部由 FFTranscoder 的两个 SwrContext 负责，引擎只声明自己要什么规格。
- ORT 引擎用 PIMPL（`struct Impl; std::unique_ptr<Impl>`）；模型参数（sample_rate/n_fft/hop/窗型/状态 shape）从 ONNX metadata 读，**勿硬编码**。
- FFmpeg 胶水的失败路径必须零泄漏：沿用现有 `do{}while(false)` + 集中释放 + 失败删半成品的风格。
- 注释与 UI 文案用中文；常量 `kCamelCase`，方法驼峰；每个头文件首行注释说明职责。
- Git：中文提交信息，单 `main` 分支，有 origin 远程。

## 已知 P0 债（改引擎前先读；2026-10 代码审查结论）

1. **无 parity 测试**：GTCRN/MDX 前端为手推实现，须对照 sherpa-onnx / audio-separator 建 golden 测试（NFR-Q1，最高优先）。
2. `util/fft.h`：Bluestein 全 float 有精度风险；MDX 对 2 的幂帧也走 Bluestein（慢且亏精度，应直通基-2）；radix-2 无 twiddle 预计算表。
3. `vocalseparator` 只验输入 shape，从不验输出 shape/布局（换模型会静默产废音）。
4. `kCanonicalRate=44100` 对视频源（48k）造成两次重采样；canonical 应改 48k 可配置（NFR-Q3）。
5. `dspdenoiser` 50Hz 硬编码（60Hz 电网地区无效）。
6. LGPL 合规：SoundTouch 静态链接 + FFmpeg 构建混用（gyan 导入库 + BtbN DLL）需修正（NFR-C3）。
7. `IStreamEngine::process` 每次堆分配返回 vector——实时化前需加 `processInto` 零分配变体（NFR-L2）。

## 方向（详见 REQUIREMENTS.md 第 9 节）

- 执行顺序：**正确性兜底（P0 债 1–5）→ 离线闭环（注册表/DFN3/管线串联/批处理）→ RVC 变声 → 实时化**。
- 架构演进：引擎注册表（NFR-M1）+ 无头命令层（FR-24，GUI 与未来 Agent 共用）+ 工作区版本化（FR-A4）。
- 开放问题（动工前须关闭）：O2 ContentVec 替代（变声开发前）、O3 RVC 延迟实测（实时化前）、O1/O4 许可证核验（发版前）。

## 红线

- `third_party/` 与模型权重永不入库。
- 不引入 GPL 依赖；保持 LGPL/MIT 组合（D1 免费公开发布）。模型许可证逐个核验（文档第 6 节红线表）。
- 不内置克隆音色模型；RVC 社区模型由用户导入，变声功能须带使用须知（NFR-C5）。
- 一切处理保持流式、内存有界（NFR-P4：2h 音频峰值 RSS ≤2GB），禁止整文件载入。
- 原件只读是新工作区机制（FR-A4）的根基：任何新功能不得改写用户源文件。
