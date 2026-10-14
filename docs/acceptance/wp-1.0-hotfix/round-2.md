# round-2 — A2 引擎异常屏障（Linux，2026-10-14 20:35）

## 改动清单（三层屏障，对应 R13）

| 层 | 文件 | 改动 |
|---|---|---|
| 契约 | `src/service/streamengine.h` | 类注释补「process/flush 不得抛出，失败归口 lastError()」 |
| 引擎内 | `src/service/aidenoiser.h/.cpp` | 新覆写 `lastError()` + `Impl::errMsg`；process/flush 的 processHop 调用圈 try/catch(Ort::Exception) |
| 兜底 | `src/service/ffextractor.cpp` | runJob 圈 try/catch(std::exception + ...)，异常→jobDone(false)（worker 槽不再可被穿透致 std::terminate） |

vocalseparator 原有 `runModel()` catch（→errMsg）即范本，本轮回归验证。CLI 路径无需改：引擎内 catch 已把 ORT 异常挡在 process/flush 内，错误经 fftranscoder `:197/:257` 的 lastError 检查点流出。

## 注入方法

`scripts/craft-mismatch-model.py` 生成「元数据齐全、图输入 x=[1,100,1,2]」的 ONNX（524B）：通过 setup 全部检查，引擎按 n_fft=512 实发 [1,257,1,2] → 首次 `sess->Run` 抛维度错位 Ort::Exception——精确命中 A2 目标路径。垃圾模型 = 256B urandom；flush 专用 = 0.01s@16k 输入（<1 hop，首次 Run 落在 flush 内）。

## 证据

| 注入 | 构建 | 结果 |
|---|---|---|
| 维度错位 | build-pre（修复前） | `已中止（核心已转储）` SIGABRT **exit=134**；terminate 消息即注入错位（Got 257 / Expected 100）；**半成品 out-*.wav 0 字节残留** |
| 维度错位 | build-post | `FAIL: AI 降噪推理失败：Got invalid dimensions…` **exit=1**，无崩溃，**无半成品残留**（失败删半成品路径生效） |
| 维度错位 + 0.01s 输入 | build-post | 同上（flush 路径 catch 验证） |
| 垃圾文件 | build-post | `FAIL: 加载 AI 模型失败：…Protobuf parsing failed.` exit=1 |
| 不存在路径 | build-post | `FAIL: 找不到模型 /nonexistent.onnx` exit=1 |
| 分离器 × 垃圾模型 | build-post | `FAIL: 加载人声分离模型失败：…` exit=1（原通道回归） |

## 成功路径回归（同轮 20:35:40 smoke run）

五动词 OK + ffprobe 体检同 round-1；RSS 对照 修复前 2,938,088KB → 修复后 58,864KB（与 round-1 同量级，波动为系统噪音）；**逐字节一致 PASS**——A2 屏障不改变成功路径输出。

## 未覆盖

- GUI 面：runJob 兜底 catch 的真实触发走 CLI 已覆盖等价路径（transcodeStreamWav 同函数），jobDone 信号投递待 round-3 GUI 冒烟顺带观察。
