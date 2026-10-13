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

## 4. 依赖版本矩阵（NFR-M3，实现时填 SHA256）

| 依赖 | 版本 | 来源 | 用途/备注 |
|---|---|---|---|
| Qt | 6.11.1 (mingw_64 / apt) | qt.io aqtinstall / apt | Widgets(GUI) + Core(引擎) |
| MinGW GCC | 13.1 | Qt Tools | Windows 工具链 |
| Ninja | 随 Qt Tools / apt | — | 生成器 |
| FFmpeg | 9.0 LGPL shared（BtbN win64-lgpl-shared-9.x；Linux 用 apt 或官方 tgz） | BtbN / apt | 见 §5 统一决策 |
| ONNX Runtime | 1.30.0 CPU | 官方 release tgz | Linux/Windows 各取对应包 |
| SoundTouch | 2.3.3 | surina.net 源码编译 | Linux 侧同法编 `.a`（工作量小，README 已有配方） |
| GTCRN 模型 | gtcrn_simple.onnx | sherpa-onnx release | 许可核验中（O1） |
| MDX 模型 | UVR-MDX-NET-Voc_FT.onnx | hf-mirror / HF | 标注来源（NFR-C2） |

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
