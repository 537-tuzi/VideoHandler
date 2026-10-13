# 采样率策略：跟随源，消灭 44.1k 往返

- 日期：2026-10-13
- 关联：P0 债 #4、NFR-Q3（视频源 48k 全程不经过 44.1k 往返）、FR-1 体验

## 1. 现状与证据

- `model/data.h:7` `kCanonicalRate = 44100` 全局常量。
- 输出规格硬编码：WAV 头写死 canonical（`fftranscoder.cpp:70-73`）；引擎输出回转重采样目标写死 44.1k 立体声（`fftranscoder.cpp:138`）；bypass 判据也锚在 canonical（`fftranscoder.cpp:121`）。
- 后果：48k 视频源 → 降 44.1k 进 DSP/变调 → 输出 44.1k，**两次无谓重采样**（NFR-Q3 违例）；GTCRN（16k）与 MDX（44.1k）是模型锁定的工作点，属必要重采样，不在此债内。

## 2. 方案决策

**输出规格 = 跟随源**（非「canonical 改 48k」）：

- `FFTranscoder::transcodeStreamWav` 增加输出规格参数（rate/channels），默认取**解码源的实际规格**（限幅 8k–192k；声道取 min(源, 2)）；引擎期望规格逻辑不变（`inRate/inChannels` 照旧，swr 链照旧）。
- 理由：把默认输出改成 48k 只是换一种损害（44.1k 源被升采样）；「跟随源」对任何源都零无谓重采样——DSP/变调引擎采样率无关（`dspdenoiser.cpp:14-36` 系数已按 rate 计算；SoundTouch `setSampleRate` 任意），48k 源全链 48k 直通，bypassOut 条件改为「引擎规格 == 输出规格」自然成立。
- GTCRN/MDX 路径：模型工作点不可变，48k 源仍要在引擎段重采样，但输出可回源率（48k）——回转一次是「回源」不是「去 44.1k」，符合 NFR-Q3 文义；代价是多一次 swr，默认关闭（输出跟引擎规格走），仅 FR-16（输出格式选择，v1.x）时暴露给用户。**本工作包不改这两条路径的行为。**
- `kCanonicalRate` 保留但语义收窄为「无源可依时的兜底」（探测失败/合成链路），不再作为输出默认。

## 3. 改动面

| 文件 | 改动 |
|---|---|
| `fftranscoder.h/.cpp` | 签名加输出规格（带默认值=跟随源）；`open_wav`/bypass/toOut 全部改用该规格 |
| `model/data.h` | kCanonicalRate 注释更新语义（值不变，避免无谓行为变化） |
| CLI（`main.cpp` run_cli） | 无感（用默认跟随源）；`--transcode-wav` 从此保留源率输出 |
| GUI（ffextractor→mediaservice→mainwidget） | 无感（沿用默认） |
| `CLAUDE.md` 关键约定 | 「输出统一 44.1k/2ch」改为「输出跟随源规格，兜底 44.1k」——实现合入时同步 |

## 4. 验证（挂 engine-parity matrix）

- PAR-SR-001：48k 立体声 WAV 源，`--transcode-wav`（直通引擎）→ 输出头 48k/2ch，样本与源 bit-exact（直通链应零重采样：bypass 条件成立时 swr 恒等）。
- PAR-SR-002：48k 源过 `--denoise-wav` / `--pitch-wav` → 输出头 48k/2ch；对照参考（scipy/SoundTouch 在 48k 直接处理）parity ≤ −60dB（证明全程无隐藏往返）。
- PAR-SR-003：44.1k 源回归——输出仍 44.1k，现有用例全绿（无行为回退）。
- PAR-SR-004：44.1k 源过 `--sep-vocals` → 输出 44.1k（MDX 路径行为不变）。
- NFR-Q3 证据：PAR-SR-001/002 的输出头 + bit-exact/parity 结果即量化证据。
