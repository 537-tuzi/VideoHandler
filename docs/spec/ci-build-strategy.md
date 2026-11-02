# 构建分层与 CI 策略（Linux 测试构建 + Windows 全量构建）

- 日期：2026-10-13
- 关联：NFR-M2（CI 必绿）、NFR-M3（依赖矩阵）、NFR-M4（消灭 D:/qt6 硬编码）、NFR-C3/R6（FFmpeg 混用）、REQUIREMENTS §6「Linux 保持可编译，CI 双跑」

## 1. 现状与证据

- 单一可执行目标 `video_handler`（WIN32），Qt Widgets/引擎/FFmpeg 胶水全在一个 target（`CMakeLists.txt:13-37`）。
- 硬编码 `D:/qt6/...`：`CMakeLists.txt:100-101`（部署拷贝）、`CMakePresets.json:11-13/24-26`（prefix/编译器/ninja）。
- 链接混用：gyan 导入库 `.a` ×3 + BtbN DLL 直连 swresample（`CMakeLists.txt:48-51`）——LGPL 与来源一致性风险（R6/NFR-C3）。
- `third_party/` 四套依赖手工获取（README 记录了全部 URL 与版本）。

## 2. CMake 分层（WP-1.2 核心）

```
libengine  静态库：streamengine/dspdenoiser/aidenoiser/pitchshifter/vocalseparator/
           fftranscoder/ffextractor + util + model/data.h
           依赖：Qt6::Core（QString）+ FFmpeg(libav*) + ORT + SoundTouch
video_handler  可执行：main.cpp + mediaservice + view/，链 libengine + Qt6::Widgets
engine_test    可执行（仅非 WIN32/测试构建）：headless 驱动 libengine——
           直调引擎（喂 WAV/原始 f32，绕过 GUI），作 parity 的 DUT 与属性测试宿主
```

- 编译器差异隔离：MinGW 专属选项（`-fno-keep-inline-dllexport`、`MINGW_HAS_SECURE_API`、POST_BUILD 部署）只留在 app 目标与 `if(WIN32)` 分支。
- 路径参数化：`QT_PREFIX`/`MINGW_PREFIX`/`NINJA` cache 变量（preset 提供默认，环境可覆盖）——消灭 D: 硬编码（NFR-M4 前置）；`third_party` 布局不变，Linux 侧允许 `third_party/ort/linux/` 等并列目录。
- 依赖获取脚本化：`scripts/fetch-deps.sh`（Linux）与 `.ps1`/`.sh`（Windows），按 §4 矩阵下载并验 SHA256——「干净仓库一条命令备齐依赖」。

## 3. CI（GitHub Actions，依赖矩阵就绪后接）

| job | runner | 步骤 | 产出 |
|---|---|---|---|
| linux-engine | ubuntu-latest | apt 装 qt6-base-dev/ninja/g++ + ffmpeg dev（apt 版本入矩阵）+ ORT 官方 Linux tgz → 配置 `linux-test` → 构建 libengine+engine_test → 跑 FFT 性质 + 分块不变性 +（素材缓存命中时）GTCRN/MDX parity | 测试报告；NFR-M2 门禁 |
| windows-app | windows-latest | aqtinstall Qt 6.11 mingw + Ninja + `fetch-deps` → `mingw-release` 构建 → lavfi 合成测试媒体跑六个 CLI 冒烟（`CLAUDE.md` 清单）→ 收集自包含产物 zip | 可分发目录；NFR-M4 雏形 |

- 缓存：third_party 按矩阵 SHA256 键控；Python venv（参考实现）单独缓存。
- 模型文件走 fetch-deps（gtcrn ~小 / UVR-MDX ~大，缓存重要）。
- 触发：PR + main push；linux-engine 必绿才允许合（分支保护后续配）。

## 4. 依赖版本矩阵（NFR-M3；2026-10-18 WP-1.2a 落地，fetch-deps.sh 内嵌哈希与此一一对应）

| 依赖 | 版本 | 来源 | 用途/备注 |
|---|---|---|---|
| Qt | 6.11.1 (mingw_64 / apt) | qt.io aqtinstall / apt | Widgets(GUI) + Core(引擎) |
| MinGW GCC | 13.1 | Qt Tools | Windows 工具链 |
| Ninja | 随 Qt Tools / apt | — | 生成器 |
| FFmpeg | 9.0 LGPL shared（BtbN win64-lgpl-shared-9.x；Linux 用 apt 或官方 tgz） | BtbN / apt | 见 §5 统一决策；本 Linux apt 实测 62.3.100（FFmpeg 8），与 Windows 9 有次版本漂移，引擎层不受影响（仅 fftranscoder/ffextractor 用稳定 API） |
| ONNX Runtime | 1.30.0 CPU | 官方 release tgz | Linux 统一走 `third_party/ort/linux`（fetch-deps 下载，SHA256 `a5ed5a3cac51fbb2e90da632ae43d19212faaa20e76484e62bcb7c23ddb3b3fd`）；本机 apt 曾是 1.23.2（round-3 证据实际跑在 1.23.2 上，1.30.0 复跑差异 ≈−166dB 属版本噪声） |
| SoundTouch | 2.3.3 | surina.net 源码编译 | Windows 静态编 `.a`；Linux 本机 apt 实测 2.4.0（API 稳定），矩阵待 CI 定版 |
| GTCRN 模型 | gtcrn_simple.onnx | sherpa-onnx release | SHA256 `e77603ac0c23dac3227dd2d7135b3a585cbee2679048aecfa886657d3ae1b534`；O1 已关闭 |
| MDX 模型 | UVR-MDX-NET-Voc_FT.onnx | hf-mirror / HF | SHA256 `534b2070fcc7df514b13ef660dc8cbb328679c2374d04354a5c42bb14ecce111`；标注来源（NFR-C2） |

**parity 参考实现（WP-1.1 SG-6，2026-11-02 锁定；本机实跑版本，CI venv 同款 pin）**：

| 依赖 | 版本 | 用途 | 备注 |
|---|---|---|---|
| numpy | 2.5.4 | 素材合成/比较器/FFT 对照 | 本机 ~/.local；CI venv pin 同版 |
| scipy | 1.18.1 | PAR-DS 双精度 + float32 同构参考 | |
| sherpa-onnx | 1.13.8 | PAR-GT 金样本（OfflineSpeechDenoiser，num_threads=1） | |
| audio-separator | 0.47.0 | PAR-MD 金样本（官方管线） | wheel SHA256 `756f3b62…bfd9a8`（全量见 engine-parity/scripts/requirements.txt 注释）；py3.14 须 --no-deps 绕 diffq，CI venv 同法 |
| torch / torchvision | 2.14.1+cpu / 0.29.1+cpu | audio-separator 推理后端 | CPU 源（download.pytorch.org/whl/cpu） |
| onnxruntime(python) | 1.31.0 | audio-separator 依赖 | 与 C++ 侧 ORT 1.30.0 独立（python 侧仅参考管线用） |

环境重建配方唯一事实源：`docs/acceptance/engine-parity/scripts/requirements.txt`（本机 ~/.local+test/pydeps 双段式）与 `.github/workflows/linux-engine.yml`（CI venv，PARITY_REQUIRED=1）；换参考版本后 `gen_golden.py --force` 重生成金样本并自动登记新哈希。

矩阵是唯一事实源：fetch-deps 脚本、CI、THIRD-PARTY 清单（NFR-C2）都从它生成/对照。

## 5. FFmpeg 统一（WP-1.7，NFR-C3/R6）

- 决策：**全量切 BtbN win64-lgpl-shared**，弃 gyan 导入库；MinGW 链接方式统一为 DLL 直连（现有 swresample 已验证此法可行，`CMakeLists.txt:51` 注释）。
- 理由：gyan `.a` + BtbN DLL 混用是两套构建的版本/ABI 赌局；BtbN 一套同时供导入与运行时，LGPL 动态链接合规路径清晰（NFR-C3）。
- 验证：Windows 构建过 + 六 CLI 冒烟绿即可关闭。

## 6. 分期

1. WP-1.2a：CMake 分层 + 路径参数化 + `linux-test` preset + fetch-deps(Linux) → 本 Linux 环境可构建 engine_test（**首个可执行验收点**）。
2. WP-1.2b：GitHub Actions linux-engine job。
3. WP-1.7 + WP-1.2c：BtbN 统一 + windows-app job + 产物 zip。
4. NFR-M4 完整体（安装包）推迟到 M2 末——依赖矩阵与产物目录先就位。
