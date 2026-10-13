# VideoHandler 音频处理软件

基于 **Qt6 Widgets + FFmpeg + ONNX Runtime** 的离线音频处理工具，四大核心功能：

| 功能 | 实现 | 引擎 |
|------|------|------|
| 降噪（电流声 50Hz 谐波） | DSP 陷波 ×3 | 双二阶 IIR |
| 降噪（风声低频） | DSP 高通 | 双二阶 IIR |
| 降噪（模糊背景声） | AI 语音增强 | GTCRN（ONNX Runtime 流式） |
| 变调（-12~+12 半音，不变速） | SoundTouch | PitchShifter |
| 视频 / 音频分离 | FFmpeg 流复制 | — |
| 人声分离 / 伴奏提取 | MDX-Net | VocalSeparator（流式 WOLA） |

## 技术栈

- **GUI**：Qt6 Widgets
- **解码/封装**：FFmpeg（so-name 63/61/7，LGPL 套件）
- **AI 推理**：ONNX Runtime 1.30（C++ API）
- **变调**：SoundTouch 2.3.3
- **FFT**：自研 `util/fft.h`（基-2 + Bluestein 任意长度）

## 目录结构

```
src/
├── main.cpp          # 入口：GUI 或 CLI
├── model/            # 常量（data.h）
├── service/          # 引擎与转码层
│   ├── streamengine.h   # IStreamEngine 统一流式引擎接口
│   ├── fftranscoder.*   # 解码→重采样→引擎→编码 转码底座
│   ├── dspdenoiser.*    # DSP 降噪
│   ├── aidenoiser.*     # AI 降噪（GTCRN）
│   ├── pitchshifter.*   # 变调（SoundTouch）
│   ├── vocalseparator.* # 人声分离/伴奏（MDX-Net）
│   ├── ffextractor.*    # 任务执行者
│   └── mediaservice.*   # 服务编排
├── util/             # FFT 等 header-only 工具
└── view/             # 主窗口
third_party/          # 第三方依赖（不入库，见下）
```

## 第三方依赖（需自行获取，未入库）

仓库只含源码，`third_party/` 由下述方式获取并放到对应目录：

```
third_party/
├── ffmpeg/{include,lib,bin}   # 头文件 + 导入库 + 4 个 DLL
├── ort/{include,lib,bin}      # ONNX Runtime 1.30.0 CPU
├── soundtouch/{include,lib}   # 2.3.3 静态库
└── models/{denoiser,separator}# 两个 ONNX 模型
```

| 依赖 | 获取方式 |
|------|---------|
| FFmpeg 头文件+导入库 | gyan.dev 9.0.2（`libavformat-63.a` / `libavcodec-63.a` / `libavutil-61.a`） |
| FFmpeg 运行时 DLL | BtbN `win64-lgpl-shared-9.0`（`avcodec-63.dll` / `avformat-63.dll` / `avutil-61.dll` / `swresample-7.dll`） |
| ONNX Runtime | 1.30.0 CPU 版（`libonnxruntime.a` + `onnxruntime.dll` + `onnxruntime_providers_shared.dll`） |
| SoundTouch | https://www.surina.net/soundtouch/soundtouch-2.3.3.tar.gz ，MinGW 编译：`g++ -O2 -DSOUNDTOUCH_FLOAT_SAMPLES=1 -c source/SoundTouch/*.cpp` → `libSoundTouch.a` |
| GTCRN 降噪模型 | https://github.com/k2-fsa/sherpa-onnx/releases/download/speech-enhancement-models/gtcrn_simple.onnx |
| MDX-Net 人声分离模型 | https://hf-mirror.com/Politrees/UVR_resources/resolve/main/models/MDXNet/UVR-MDX-NET-Voc_FT.onnx |

## 构建

环境：Qt6（mingw_64）+ MinGW 13.1 + Ninja。`CMakePresets.json` 内的 `CMAKE_PREFIX_PATH` / 编译器路径需按本机调整。

```bash
cmake --preset mingw-release   # 或 mingw-debug
cmake --build --preset mingw-release
```

Release 产物落 `src/bin/`，Debug 落 `src/bin-dbg/`。POST_BUILD 自动把 FFmpeg/ORT DLL、模型、Qt/MinGW 运行时收进 exe 目录，实现自包含分发。

## CLI 用法

```bash
video_handler --extract-audio <输入>                 # 提取音轨
video_handler --extract-video <输入>                 # 提取无声视频
video_handler --transcode-wav <输入> <输出.wav>       # 转码
video_handler --denoise-wav <输入> <输出.wav>         # DSP 降噪
video_handler --ai-denoise <输入> <输出.wav> [模型]    # AI 降噪
video_handler --pitch-wav <输入> <输出.wav> <半音数>   # 变调
video_handler --sep-vocals <输入> <输出.wav> [模型]    # 人声分离
video_handler --sep-instrumental <输入> <输出.wav> [模型] # 伴奏提取
```

无参数启动 GUI。

## 许可

本项目源码自用；第三方依赖遵循各自许可（FFmpeg 采用 LGPL 套件、SoundTouch 为 LGPL、GTCRN/MDX-Net 模型见各自来源）。
