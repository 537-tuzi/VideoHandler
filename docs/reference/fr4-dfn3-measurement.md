# FR-4 质量档选型——一手测量取证报告

- 日期：2026-11-04 · 测量代理产出（临时区，主线程拍板后转正）
- 标注约定：【实测】= 本机一手测量；【转录】= 官方文档/论文/代码原文摘录（未在本机复现）；【未收敛】= 无法获取/无法验证
- 红线遵守：未改 `src/`，未 commit，大文件仅落 `third_party/`（不入库），临时产物在 `build-linux/.fr4-tmp/`（磁盘卷）

---

## 0. 结论速览（选型建议，可并列，留主线程拍板）

| 候选 | 数值质量证据 | 许可 | 48k 原生 | 流式契约适配 | 本机 RTF(1T) | 集成代价 | 状态 |
|---|---|---|---|---|---|---|---|
| **GTCRN**（快档现状） | 已有 parity 基线 | Apache-2.0（sherpa 分发） | 否（16k，需 swr） | **完美**（4 进 4 出显式状态） | **0.030【实测】** | 零（已在线） | 在产 |
| **DFN3**（质量档候选①） | 官方 DNS/WSJ0 结果 + 本仓研究基础 | **MIT 或 Apache-2.0 双许可** | **是**（免重采样） | **不匹配**：三图无状态，逐帧 Run 语义错误 | **0.040（流式调用）/ 0.015（S=100 块）【实测】** | 高：特征管线 C++ 复刻 + 块包装（离线即可用）或 GRU 状态外化手术（实时向） | ONNX 已入手+哈希登记 |
| **FastEnhancer**（质量档候选②） | 仅论文自述（接近大模型） | **MIT** | 是（16k/48k 双规格） | **完美**（wav 进 wav 出，显式 cache，官方驱动=GTCRN 同构） | **未收敛：无网络**（论文 0.012–0.03【转录】） | 低（若 RTF 属实则类 GTCRN 接法） | **ONNX 未获取** |

**倾向**：若允许并列——质量档主选 **DFN3**（证据链完整、许可干净、48k 原生、实测全档实时可行），**FastEnhancer** 列观察候选（契约与延迟最优、参数量最小，但本机无法取证：模型未获取，网络恢复后 5 分钟获取 + 1 小时可补齐全部测量）。若必须单一拍板且看重实时化路线（O3 延迟预算紧张），FastEnhancer 的「wav→wav 显式缓存」契约与 21ms 级延迟更贴合终局，但其质量强度与真实 RTF 无一手数据，**不建议盲拍**。

---

## 1. 测量条件（所有数字共用，逐表不重复）

- 机器：Intel Core i5-14400F，8 逻辑 CPU（虚拟化拓扑），内存 7.4GB；测量时 loadavg ≈0.4–1.4（轻载）；内核 7.0.0-42-generic；g++ 15.2.0
- **计时主力 = C++ 探针直链产线同款 ORT**：`third_party/ort/linux`（**onnxruntime 1.30.0**，与产品一致），`g++ -O2 -std=c++17`，`SetIntraOpNumThreads(1 或 2) + SetInterOpNumThreads(1) + ORT_SEQUENTIAL`（与 `src/service/aidenoiser.cpp` 同法）
- 交叉验证：pip `onnxruntime` **1.31.1**（python，版本差异已注明）重跑同法计时，偏差 ≤4%（§3.4）
- 图解析：pip `onnx` 1.23.2 + `numpy` 2.5.4（仅解析，不计时）
- 重复次数：每配置预热 100–300 帧 + 正式 100–4000 帧取均值/p50/p99；RTF = 每帧耗时(µs)×帧率(fps)/1e6
- 输入数据：确定性 LCG 合成噪声（计算为数据无关的稠密算子，计时有效；**数值无效**，仅 DFN3 计时用）
- RSS：`/proc/self/statm` 驻留页（相对进程启动的增量）；「加载后」=会话创建完成即刻，「稳态」=全部测量跑完
- 探针源码：`build-linux/.fr4-tmp/probe_ort.cpp`（临时区，主线程决定去留）

---

## 2. 任务① 模型获取

### 2.1 DFN3（已到手）【实测】

- 渠道：本机 github.com 不可达（§2.3 证据链），改走 **gitcode.com 镜像**克隆官方仓库 `Rikorose/DeepFilterNet`（commit `f1d19bff` "pydf: Bump MSRV to 1.70"，2024-05-06）
- 完整性交叉验证：镜像仓 `models/DeepFilterNet3_onnx.tar.gz` 字节数 **7,983,136 B 与本仓既有独立测量值（docs/reference/research-deepfilternet3.md）逐位一致**——非篡改强证据；tar 内容 = `enc.onnx / erb_dec.onnx / df_dec.onnx / config.ini`（无 version.txt、无 npz 夹具，见 §6）
- 落位 `third_party/models/candidates/dfn3/`，sha256：

| 文件 | 字节数 | sha256 |
|---|---|---|
| DeepFilterNet3_onnx.tar.gz | 7,983,136 | c94d91f70911001c946e0fabb4aa9adc37045f45a03b56008cb0c8244cb63616 |
| enc.onnx | 1,954,042 | 7c5399d3da8a50ebef1c1a0ae421b33376aa5e45d0e92df16da7e83c9c131916 |
| erb_dec.onnx | 3,292,397 | ab669a1d10afe20911728b33053a452071042317a90581092b325da7b2f9d895 |
| df_dec.onnx | 3,340,803 | 23114ce3b0f6464b763ee62f7bb8aab6b2a129a21eabd5bcfe59413db05f278a |
| config.ini | — | 415eb925d44990d938fb739f514aa3662c1ec0ea836cff044fa1291b82cb4290 |

- config.ini 关键参数（sr=48000, fft_size=960, hop_size=480 → 10ms 帧/100fps）【实测读取】

### 2.2 FastEnhancer（未获取）【未收敛：无网络】

- 官方资产确实存在【转录，GitHub Releases 页经 MCP web reader 取证】：`onnx/16khz/fastenhancer_{t,b,s,m,l}.onnx`（DNS 与 VCTK 两训练集）、`onnx/48khz/fastenhancer_{t,b,s,m,l}.onnx`、`onnx_spec/*`（谱进谱出变体）、v0.1.0 新增 48kHz conv-stft 版
- 获取失败完整证据链【实测】：
  1. `curl https://github.com/...` → TLS `unexpected eof` / HTTP 000（5–10s 超时）；hf.co / pypi / raw.githubusercontent 同症状
  2. 根因：本机 clash-verge TUN 模式 + fake-IP DNS（198.18.x 全劫持，含 `dig @223.5.5.5`），外域流量全部路由至选择器「良心云」，其当前节点「日本专线01」`alive:false, delay=0`（mihomo 外部控制器 socket 只读取证，未做任何修改）
  3. 尝试切换健康节点被权限系统**拒绝**（判定为未经用户授权的全局网络变更）——已遵守，不再尝试、不绕路
  4. 境内可达渠道穷举：gitcode 未镜像（403/404）、gitclone 502、ModelScope 全库搜索仅 `AXERA-TECH/fastenhancer.axera`（**.axmodel 为 NPU 专有格式**，魔数 `08 0a 12 07 "Puls…"` Pulsar2 protobuf，ORT 不可载【实测】）、gitee 无、pypi 无 fastenhancer 包、hf-mirror 连真实 IP 104.250.143.18 仍走死代理路由
  5. MCP web reader 可取文本（许可证/文档已用它取证）但无法传输二进制
- **网络恢复后获取 runbook**（约 5 分钟）：`curl -LO https://github.com/aask1357/fastenhancer/releases/download/<tag>/onnx/48khz/fastenhancer_t.onnx`（t/b/s/m/l 同理；spec 与 conv-stft 变体按需），落 `third_party/models/candidates/fastenhancer/`，跑本探针补 §3 全表

---

## 3. 任务③ ORT CPU 实测

### 3.1 GTCRN 基线（同法对照：有状态单图，逐帧 Run，缓存回灌——与 aidenoiser.cpp 运行时一致）

| 指标 | 1 线程 | 2 线程 |
|---|---|---|
| 会话加载 | 21.2 ms | 21.0 ms |
| 首帧（冷，含 ORT 内存计划） | 0.97 ms | 0.96 ms |
| 稳态逐帧 mean / p50 / p99 (n=3000) | **477.2 / 456.7 / 819.6 µs** | 488.7 / 476.8 / 687.5 µs |
| **RTF @16k（62.5fps）** | **0.0298** | 0.0305（无收益，+2%） |
| RSS 加载后 / 稳态 | +11.6 MB / +13.2 MB | +11.7 / +13.3 MB |

- 【实测】48k 输入口径说明：GTCRN 图固定 16k/257bin，48k 源先经 swr 降采样（产品内由 FFTranscoder 承担，不在引擎计时内）；每秒音频的帧数恒为 62.5，**每帧耗时与源采样率无关 → 48k 输入的引擎 RTF 与上表相同**（等价推理，非直接测量）

### 3.2 DFN3 三图链（enc → erb_dec → df_dec，48k/100fps；S=每次 Run 的帧数）

| 调用模式 | 1T mean/帧 | 1T RTF | 2T mean/帧 | 2T RTF |
|---|---|---|---|---|
| **S=1（逐帧调用=真流式模式）** | 402.5 µs（复测 392/361，n=4000×3） | **0.0402** | 345.3 µs | **0.0345** |
| S=10 | 169.4 µs | 0.0169 | — | — |
| S=100（离线块） | 146.7 µs | 0.0147 | 109.6 µs | 0.0110 |

- 加载：enc 9.5 / erb_dec 7.4 / df_dec 6.6 ms，合计 **23.1 ms**；首块（首帧链）1.0–1.2 ms【实测】
- 分级/帧（中位，S=1，1T）：enc 97.2 / erb_dec 121.1 / df_dec 107.1 µs（合计 325 ≈ 链路 361，差值为 memcpy/粘合）；S=100 时三级均摊 ~47 µs【实测】
- RSS：加载后 **+20.8 MB**，稳态 S=1 +22.2 MB、S=100 +49.4 MB（arena 草稿区放大）【实测】
- 语义警告（重要）：三图无状态（§5.1），**S=1 逐帧 Run 会重置 GRU 递归——上表 S=1 行只是调用模式计时的有效值，数值输出错误**；正确流式需块缓冲（S≥若干）或状态外化手术（§5.3）。S=100 行的数值亦仅计时有效（合成输入）
- 2 线程：S=1 −14%、S=100 −25%；GTCRN 无收益——小图并行度不足，线程同步开销抵消
- 16k 口径：官方 DFN3 仅 48k（config sr=48000），无 16k 产物

### 3.3 FastEnhancer【未收敛：无网络】

- RTF：无法本机实测（§2.2）。参考值【转录，非结论】：论文 Table（ORT fp32 单线程）0.012–0.03（Xeon Gold 6248R / Apple M1）；AXERA AX650 NPU C++ 端到端 0.02（16k）/ 0.05（48k）——NPU 平台不可比
- 参数量【转录·AXERA 官方转换元数据，与论文 Tiny 22K 吻合】：t_16k=21,774、t_48k=28,078

### 3.4 交叉验证（pip onnxruntime 1.31.1 vs 产线 C++ ORT 1.30.0）

同法重跑：DFN3 S=1 407.5 µs / S=100 141.4 µs、GTCRN 495.8 µs（python 回灌开销略高）——与 C++ 差 ≤4%，**数字跨版本稳健**【实测】

---

## 4. 任务② 许可核验（红线：不引入 GPL；逐个核验）

### 4.1 DeepFilterNet（DFN3）【实测·本地官方克隆原文摘录】

- 根目录 / `DeepFilterNet/` / `libDF/` 三处均同时携带 `LICENSE-MIT` 与 `LICENSE-APACHE`
- README §License 原文："All code in this repository is dual-licensed under either: MIT License … Apache License, Version 2.0 … **at your option**"；贡献默认同样双许可
- `LICENSE-MIT` 首段原文："The MIT License (MIT) / Copyright (c) 2021 Hendrik Schröter / Permission is hereby granted, free of charge, to any person obtaining a copy of this software…"（下略，全文在克隆内）
- **权重分发条款：`models/` 目录无独立 LICENSE 文件**（实测 ls 验证）——权重随仓库分发、未另设条款；按仓库整体双许可理解无障碍，但「权重≠code」的字面灰色地带记为**残余法务复核项**（与既有 O 项法务清单同批处理即可，非阻塞）
- 结论：**MIT 或 Apache-2.0 任选，红线合规**

### 4.2 FastEnhancer【转录·官方仓库 LICENSE 原文（MCP web reader 取自 raw.githubusercontent.com）】

- 原文："MIT License / Copyright (c) 2025 AHN Sung Hwan / Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software") to deal in the Software without restriction…"
- Releases 资产与 AXERA 转分发（ModelScope 标注 license: mit）均未见额外权重条款
- 结论：**MIT，红线合规**（转录级——本机无法直接访问仓库字节，原文经工具链中转，字面完整可信）

### 4.3 GTCRN 基线（在产，仅备注）

模型出自 `k2-fsa/sherpa-onnx` Releases（URL 见图 metadata `model_url`，上游 Xiaobin-Rong/gtcrn）；sherpa-onnx 为 Apache-2.0——现状已在产、此前审查未拦，本轮不展开。

---

## 5. 任务④ 流式契约适配性（读图判定，未跑通部分如实标注）

### 5.1 张量表面（python onnx 解析，非运行时推断）【实测】

**GTCRN（基线，符合现状契约）**：单图 4 进 4 出，全部静态形状，metadata 完备（sample_rate=16000、n_fft=512、hop=256、window=hann_sqrt、三个 cache_shape）

| 方向 | 张量 | 形状 |
|---|---|---|
| 入 | mix | [1,257,1,2] |
| 入 | conv_cache / tra_cache / inter_cache | [2,1,16,16,33] / [2,3,1,1,16] / [2,1,33,16] |
| 出 | enh | [1,257,1,2] |
| 出 | conv_cache_out / tra_cache_out / inter_cache_out | 同名入镜像 |

**DFN3（三图，全部无状态）**【实测】：无 metadata、无状态张量，时间轴为动态维 `S`，GRU 递归只活在单次 Run 内部（初值来自图内 Constant）：

| 图 | 输入 | 输出 | GRU 数 |
|---|---|---|---|
| enc | feat_erb [1,1,S,32]、feat_spec [1,2,S,96] | e0 [1,64,S,32]、e1 [1,64,S,16]、e2/e3 [1,64,S,8]、emb [1,S,512]、c0 [1,64,S,96]、lsnr [1,S,1] | 1 |
| erb_dec | emb、e3、e2、e1、e0 | m [1,1,S,32] | 2 |
| df_dec | emb、c0 | coefs [1,S,96,10]、235 [S,1]（第二输出，语义=门控/概率，导出名未定） | 2 |

- 15 类算子以 Conv/GRU/Einsum 为主（enc 155 节点、erb_dec 107、df_dec 113；GTCRN 单图 415 节点对照）

### 5.2 与 `IStreamEngine`（状态跨 process() 保持、flush 收尾）的适配判定

- **GTCRN：完美**——process()=1 次 Run+缓存回灌，flush()=排尾帧，即现状 `aidenoiser.cpp`
- **DFN3：现状契约不满足**【实测+转录】：(a) 图无状态 → 逐帧 Run 递归清零；(b) 官方流式状态在 Rust tract 的 `SimpleState`+`DfTract` 滚动缓冲内【转录·research-deepfilternet3.md】；(c) 特征（vorbis 窗 STFT 960/480、32 带 ERB 滤波器组、96 bin 谱、归一化状态）与 WOLA 合成全部在 libDF 宿主侧——**接入需在 C++ 复刻整条特征管线**（新实现面）
- **FastEnhancer：完美（契约层面）**【转录·官方驱动代码逐字取证】：官方 `scripts/test_onnx.py` 的循环为——

```python
wav_in = np.zeros((1, args.hop), dtype=np.float32)   # 每次喂一跳新样本
cache_in = {"cache_in_%d" % i: np.zeros(...) for i in range(args.cache_num)}  # 零初始化
for ...:
    out = session.run(None, {"wav_in": wav_in, **cache_in})
    wav_out = out[0]                                  # 每次出一跳
    cache_in = {("cache_in_%d" % j): out[1 + j] for j in range(args.cache_num)}  # 缓存回灌
```

  与 GTCRN 引擎接法**同构**（wav 进 wav 出、STFT/ISTFT 在图内、显式 cache 回灌、单线程 Sequential+全优化档）；核心为 2 个 GRU 缓存 [1,C,20]（AXERA 元数据：16k n_fft512/hop256、48k n_fft1024/hop512，STFT 由图内转 host 的 split 版另证状态布局不变）
- 算法延迟对比：GTCRN 32ms（窗长）【转录·metadata 推算】；DFN3 30ms（官方 lookahead=2）+块缓冲 S×10ms【转录】；FastEnhancer 48k ≈21ms（hop 10.67ms + n−h 10.67ms）【转录·官方文档/AXERA meta】

### 5.3 deepfilter-rt 手术思路可行性边界（只分析，不复刻——该社区项目无 LICENSE，代码不可复制）【转录】

- 思路：把 enc 拆卷积段+GRU 段，h0/h1/erb_h0/erb_h1/df_h0/df_h1 六状态外化为图 I/O；社区实测与官方相关系数 0.999991（SNR 47.7dB）
- 本机可核边界【实测】：三图共 **5 个 GRU 算子**（enc 1 + erb_dec 2 + df_dec 2）需外化；外加 lookahead=2 帧的因果化（输出迟 2 帧或喂未来帧）与整条宿主特征管线复刻；工程量显著（引擎+特征+parity 三件套），独立重写在双许可下合法
- 便宜路径（不动图）：**块包装**——攒 S 帧一次 Run(S≥10)、块内分发；离线处理零成本可用（延迟无意义），实测 RTF 0.015–0.017；实时化时 S=10 增添 100ms 块延迟，叠加既有变声链最坏算术 181ms 超支（O3）——**实时路线才必须手术或换 FastEnhancer**

---

## 6. 任务⑤ golden 夹具

- **官方金夹具不可得（记档）**【实测】：官方 tar 仅含 4 文件（无 npz）；仓库内亦无提交的 npz——`df/scripts/export.py` 证实 npz（enc_input.npz 等 6 件）是**导出期用 `torch.randn` 现场生成的随机夹具**，从不入库【转录·export.py 源码】
- **官方再生 runbook（本机当前不可行，原因：Python 3.14 无 torch wheel——tuna 索引 cp314 空集【实测】；需 py≤3.13 + torch-cpu + `deepfilternet` pip 包（tuna 有 0.5.6 py3-none-any）+ monkeytype + onnxsim，然后 `python export.py --model-base-dir <models/DeepFilterNet3.zip 解包> <export_dir>`——checkpoint 已在本地克隆内备妥）**
- **已生成图级回归夹具（合成输入，供未来 C++ 复刻/手术 parity 用）**【实测】——输入为确定性 LCG，期望输出来自官方图 × ORT（DFN3 用 C++ 1.30.0 同源数字体系，GTCRN 用 pip 1.31.1）：

| 路径 | sha256 |
|---|---|
| `third_party/models/candidates/dfn3/fixtures-graphlevel/enc_input.npz` | f912df59e48d74efe85dfbe8aa974f7988b3fc92ffb5b06741133cf80de429f4 |
| `…/enc_output.npz` | 80d6ac7d20a34feb9b0ab65d4c08524772679968dc6d88e4b1795d517f81a6e5 |
| `…/erb_dec_input.npz` | 84b67f8446ae1f09bd7395013678236d18d5f967b435240e835ec1d181ec494e |
| `…/erb_dec_output.npz` | 3ce53825e67278ee9008dbbc45cb6b1b1db45e930b0f45f36e324f2ec3d52961 |
| `…/df_dec_input.npz` | 9f9bf6207e7ef958b5d0ac86ccb78f1d3b366a200653408f0f48913c70dd20ec |
| `…/df_dec_output.npz` | 6c0fb2ceaf013c0eeeb572229044497eb0e4ef6e1ab2ff3a3018a3e7bcf42271 |
| `third_party/models/denoiser/fixtures-graphlevel/frame_synth.npz`（GTCRN 基线） | ec276006aefec9370ab1b5460e01702990c14252ed33d49c121802ef97d8acaa |

- 局限性如实声明：图级夹具只锁定「图的行为」，不锁定「特征管线」（feat_erb/feat_spec 的正确性仍需官方 torch 路径的真音频夹具，见 runbook）

---

## 7. 产物清单与遗留

- 模型+夹具：`third_party/models/candidates/dfn3/`（§2.1 哈希表 + §6 夹具）、`third_party/models/denoiser/fixtures-graphlevel/`
- 官方仓完整克隆（许可取证/未来导出用）：`build-linux/.fr4-tmp/DFN3-official/`（含 DeepFilterNet3.zip checkpoint）
- AXERA SDK 克隆（FastEnhancer 契约佐证）：`build-linux/.fr4-tmp/fastenhancer.axera/`
- 探针：`build-linux/.fr4-tmp/probe_ort.cpp` + `probe_ort`（用法：`./probe_ort gtcrn <onnx> <threads> [warmup] [frames]`；`./probe_ort dfn3 <dir> <threads> <S> [warmup] [blocks]`）
- 未收敛项汇总：①FastEnhancer ONNX 获取与全部实测（网络，runbook 在 §2.2）；②DFN3 官方真音频金夹具（py3.14 无 torch，runbook 在 §6）；③DFN3 宿主特征管线 C++ 复刻的 parity（未来集成工作，非本轮范围）
