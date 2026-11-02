# WP-1.1 round-5：SG-7 落地——输出规格跟随源，消灭 48k 源双重重采样（2026-11-02）

P0 债 #4 / NFR-Q3（spec `docs/spec/canonical-rate-48k.md` §2 拍板：跟随源，非「canonical 改 48k」）。
改动前：48k 视频源 → swr 降 44.1k → 引擎 → 输出 44.1k，两次无谓重采样；改动后：48k 源全链 48k 直通（bypass 条件自然成立），44.1k 源行为不变。

## 1. 改动清单

| 件 | 内容 |
|---|---|
| `src/service/fftranscoder.h:14-21` | 类注释改「输出规格默认跟随源」；新增 `probeSourceSpec`（轻量探测）；`transcodeStreamWav` 尾加 `outRate=0, outChannels=0`（0=跟随源；追加在 onProgress 之后，既有调用点零改动即得跟随源语义） |
| `src/service/fftranscoder.cpp:19-25` | 新增 `normalizeOutSpec`：rate 夹取 8k–192k（无效回落 kCanonicalRate）、ch 折 min(源,2)（无效回落 kCanonicalChannels）；显式传入的兜底值经同一规整原样通过 |
| `src/service/fftranscoder.cpp:115-129` | `probeSourceSpec`：avformat 打开 → find_stream_info → best audio 流读 codecpar 的 sample_rate/nb_channels 即关（`do{}while(false)`+单点 close 零泄漏）；失败不拦任务（后续 open_decoder 给正式报错），回落兜底规格 |
| `src/service/fftranscoder.cpp:64-79` | `open_wav` 头三处（sample_rate/ch_layout/time_base）从 kCanonical 改为传入规格 |
| `src/service/fftranscoder.cpp:144-155` | 输出规格在 open_decoder 后规整（未指定取 dec 实际规格）；**bypass 判据从「==kCanonical」改为「引擎规格==输出规格」**（移入 do 块，规整后才能判） |
| `src/service/fftranscoder.cpp:165-168,190-203,288-298` | toOut 重采样目标（layout/rate）与 write_samples/convOut 的声道维全部改用输出规格（原 kCanonicalChannels 硬编码三处） |
| `src/service/ffextractor.h:23-24` `ffextractor.cpp:222-237` | `runJob` 透传输出规格（缺省 0=跟随源） |
| `src/service/ffextractor.cpp:241-246,266-272` | doDenoiseWav/doPitchWav：setup 前先 `probeSourceSpec`，陷波/高通系数与 SoundTouch 均在**源率**上参数化（两引擎本就采样率参数化，仅原调用处定死 44.1k） |
| `src/service/ffextractor.cpp:260-262,291-293` | doDenoiseAiWav/doSepVocals：**显式传 kCanonicalRate/kCanonicalChannels**（中文注释注明有意保留，GTCRN/MDX 模型工作点不动，spec §2） |
| `src/main.cpp:29-43,76-83,105-108` | CLI 同构：--transcode/--denoise/--pitch 三动词 probe 后按源率 setup；--ai-denoise/--sep-* 显式传兜底规格（注释注明有意保留） |
| `src/model/data.h:7-8` | kCanonicalRate/Channels 数值不变，注释改「兜底规格（AI/分离路径输出、源信息缺失回落）；transcode/denoise/pitch 输出跟随源」 |
| `scripts/gen_inputs.py` | 新增 SR_48=48000 与素材 white48k/sine48k（5s 48000/2ch，确定性 seed 同源） |
| `scripts/gen_golden.py` | `dsp` 分支 sr 参数化：取素材在 fixtures.json 登记的 sr 元数据（44.1k 素材同旧路径，金样本哈希不变、PAR-DS 数值逐位复现） |
| `scripts/pitch_check.py` | 加 `--sr`（rfftfreq/峰频换算，原 44100 硬编码）与 `--expect-cents`（单程变调期望移位；默认 44100/0，PAR-PT 口径不变） |
| `scripts/sr_check.py`（新） | PAR-SR 体检器：CLI 输出 WAV 头断言（sr/ch/dtype）+ 可选 bit-exact 比对 + `--dump` 裸流导出（供 compare.py/pitch_check.py 复用） |
| `scripts/run_parity.sh` | 新增 PAR-SR 族块（风格同 PAR-DS：缺依赖整族 SKIP、PARITY_REQUIRED=1 升级 FAIL；依赖 = video_handler + ffmpeg + scipy）；PAR-SR-003/004 不设独立判定，同轮 PAR-DS/PT/MD 全绿 + ci-smoke 体检断言即证据 |

设计取舍：**probe 放 fftranscoder 而非引擎/调用方**——规格规整逻辑（normalizeOutSpec）与转码侧共用一份；probe 只多一次 avformat 打开（毫秒级），不复用 open_decoder（那要连带起解码器，且转码本体自会再开）。AI/分离路径显式传兜底而非也跟随源，是 spec §2 明示「本工作包不改」（模型工作点 16k/44.1k 锁定，输出回源率属 FR-16 用户选项，v1.x 再开）。

## 2. 验证（round-5 实测，全部本机跑出）

**构建**：`cmake --build --preset linux-test` 全绿（14 目标，含 video_handler/engine_test/fft_test）。

**PAR-SR（新族，`run_parity.sh build-linux`，2:09 全绿 23 判定）**：

| 用例 | 实测 |
|---|---|
| PAR-SR-001 | white48k（240000 帧@48k/2ch）→ `--transcode-wav` → 输出头 **48000/2ch float32**、240000 帧；vs `ffmpeg -i white48k.wav -f f32le -ar 48000 -ac 2` 解码参考 **逐样本 bit-exact（240000/240000 全等）**——bypass 成立时 swr 恒等，零重采样实锤 |
| PAR-SR-002a | white48k → `--denoise-wav` → 头 48000/2ch float32；vs scipy RBJ+lfilter@48000 双精度参考（系数按 48k 计算，sr 取素材登记元数据）**parity −71.1dB**（门 −60，corr 1.000000，max\|d\| 2.36e-04，n=480000） |
| PAR-SR-002b | sine48k(440Hz) → `--pitch-wav +2` → 头 48000/2ch float32；谱判据（--sr 48000 --expect-cents 200）：**长度差 0 / 峰频 +200.41 音分（期望 200±10，偏差来自峰 bin 量化 ±0.5bin≈0.35 音分）/ 杂散抑制 48.2dB（≥40）/ RMS −0.00dB** |
| 反证 | 模拟旧行为（48k→44.1k polyphase→44.1k 系数滤波→回 48k）vs 同一 48k 金样本：**−10.2dB 必 FAIL**——若链路仍走 44.1k 往返不可能达到 −71.1dB，双重重采样消灭有量化证据 |
| PAR-SR-003 | 44.1k 回归：PAR-DS 四例 −50.9/−71.8/−75.6/−75.2dB 与 round-1 逐位一致；PAR-PT-001 谱判据全过（0.00 音分/59.0dB/+0.00dB）；ci-smoke w1–w6 体检仍 **pcm_f32le/44100/2ch**（断言未动，实测过） |
| PAR-SR-004 | 44.1k 分离/AI 不变：PAR-MD 8 例 −107.6~−145.4dB corr 1.000000（与 round-3 逐位一致）；w3 体检 44100 + 区间时长断言过 |

**全量 ci-smoke**（`bash docs/acceptance/wp-1.2-linux-build/scripts/ci-smoke.sh build-linux`）：FFT 属性 + 五引擎分块不变性 + parity 四族+新族 + 7 CLI 动词 + ffprobe 体检，**3:08 全绿**（round-4 为 3:29，时间差属运行噪声）。

**辅助冒烟**（/tmp，非门禁）：44.1k 立体声源三动词输出仍 44.1k/2ch、时长 5.000000 精确；48k 单声道源输出 48000/**1ch**（声道跟随源的新行为，min(源,2)）；mono/44.1k/48k 全部时长与源恒等。

**判据负例**（工具级，防空门）：头规格错配（期望 44100 vs 实际 48000）→ FAIL exit 1；未变调样本冒充 +2（--expect-cents 200）→ 峰频 +0.00 音分 FAIL exit 1；降噪输出冒充直通 bit-exact → 192000/96000 样本不等 FAIL exit 1。

## 3. 结论与遗留

- SG-7 完成：P0 债 #4 关闭（Linux 侧）；NFR-Q3 证据 = PAR-SR-001 bit-exact + PAR-SR-002 parity + 反证。
- 遗留：① **Windows round-4/5 真机复验**（既有闸门，随全量 Windows 冒烟走）；② CLAUDE.md 关键约定「输出统一 44.1k/2ch」需在合入时同步为「输出跟随源，兜底 44.1k」（spec §3 已列，提交由主线程做）；③ GTCRN/MDX 路径输出回源率（FR-16）不在本包；④ GUI 无采样率相关文案，无 UI 同步项。
