# round-1 — Linux 侧热修验证（A1/A11/A12/A14，2026-10-14 19:36）

## 改动清单（本 round 验证对象）

| 项 | 文件 | 改动 |
|---|---|---|
| A1 转码内存无界 | `src/service/fftranscoder.cpp` | feedAi 分块后就地 drainEnhanced（+2 行） |
| A11 关窗假死 | `src/service/mediaservice.cpp` | 析构先置 cancelFlag 再 quit/wait（+1 行） |
| A12 流复制不可取消 | `src/service/ffextractor.cpp/.h` | copy_stream 增 cancel 检查点（包粒度） |
| A14 override 卫生 | `src/service/vocalseparator.h` | lastError 补 override 注释 |
| 构建移植 | `aidenoiser.cpp` / `vocalseparator.cpp` | Ort::Session 宽窄字符路径 #ifdef _WIN32（Windows 行为不变） |

## 方法

- 对照构建：同一 CMake harness（`docs/tmp/linux-build/CMakeLists.txt`，apt 系统库 ORT/FFmpeg/SoundTouch + Qt6），`build-pre` 指向修复前 worktree（`/home/psh/vh-prefix` @ fffed23 + 同款 wchar 补丁，`git diff` 核实仅 8 行插入），`build-post` 指向主仓库工作区。
- 脚本：`docs/tmp/linux-build/smoke.sh`（产物追加进同目录 `smoke-results.txt`）；素材与输出在 `/home/psh/vh-test/`（可再生成）。

## 证据（smoke-results.txt 19:36:47 run，7200s 素材）

- CLI 五动词全 OK；ffprobe 体检三个 WAV 均 `pcm_f32le / 44100 / 2ch / 30.0s`。
- A1 RSS 对照：修复前 **2,937,832 KB（2.80GiB）** / 1:41.82 → 修复后 **58,288 KB（57MB）** / 1:00.76（耗时 −40%）。
- 修复前后输出 `cmp -s` **逐字节一致： PASS**（修复只动排水时机，不动样本值）。

## 未覆盖（转 round-3）

- A11/A12 的 GUI 交互面（关窗时机、取消按钮）为代码读级验证，待 Windows 真机。
- Windows third_party 组合（BtbN/gyan DLL、ORT 1.30 Win、Qt MinGW）未编译。
