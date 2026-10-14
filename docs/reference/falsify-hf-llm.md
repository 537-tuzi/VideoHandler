# 反证审查：HF 下载规范与本地 LLM 基建结论（对 research-model-mgmt-llm-infra.md）

## 元信息

- 日期：2026-10-14
- 性质：**反证审查**——不查证原结论，专门假设原结论是错的，找推翻它们的一手证据（官方源码 / 官方文档 / release notes / 本环境实测）。找不到反证的条目只写「暂未找到反证」。
- 审查对象：`docs/reference/research-model-mgmt-llm-infra.md`（2026-10-13 ~ 10-14）中影响 FR-8（模型管理器）、FR-A5（LLM 后端）、NFR-C4（镜像）的结论。
- 判级约定：【站得住】/【需修正】/【被推翻】/【原证据不足】；子条目单独判级。
- 信源可达性（本环境实测）：raw.githubusercontent.com、api.github.com（偶发限流）、hf-mirror.com 可达；huggingface.co 间歇可达（多次重试可成）。**本环境出口为海外 IP（AWS us 段），对 hf-mirror 行为的观测有 vantage 局限，见 1.4 的盲点标注。**

---

## 1. 「HF 最小正确下载集 = .incomplete + HTTP Range 续传 + sha256/ETag 校验 + models--org--name/{blobs,refs,snapshots} 布局」

### 1.1 子结论「`.incomplete` 临时文件 + HTTP Range 断点续传（照抄 HF）」——【被推翻】

这是本次反证的最重发现：**huggingface_hub 当前版本（v1.5+ 至 v2.2.0）跨运行的断点续传已经不存在**，原调研描述的是 0.x 时代（≈2025 年中之前）的实现。

一手证据：

1. **当前 main 源码**（2026-10-14 取自 https://github.com/huggingface/huggingface_hub/blob/main/src/huggingface_hub/file_download.py ）：
   - `_download_to_tmp_and_move` 写入的是**进程唯一**临时文件：`tmp_path = incomplete_path.with_name(f"{incomplete_path.stem}.{uuid.uuid4().hex[:8]}.incomplete")`（源码 L2003），以 `"wb"` 打开；
   - 调用 `http_get` 时**不传 `resume_size`**——`http_get` 内部完整的 Range 续传机制（`resume_size` + `headers["Range"] = _adjust_range_header(...)`，L338–478）从这条主路径**不可达**；重跑也因文件名含 uuid 无法找到上次残留；
   - `finally: tmp_path.unlink(missing_ok=True)`，源码注释原话："On failure, do not keep a partial file around: it could not be reused anyway since the temporary name is unique to this download"；
   - 改动原因的源码注释：共享 `<etag>.incomplete` 在 `flock` 静默失效的文件系统（Lustre/GPFS/部分 NFS）上会损坏缓存，故改为进程唯一名（注释引用 PR #4228）。
2. **huggingface_hub issue #4196「No resuming of downloads」**（2026-05-06 提出，**至今 open**，最后更新 2026-09-28；复现环境 `hf version 1.5.0`）：中断后重跑从 0% 重来，17GB GGUF 无法在慢链路下完成。关键评论：
   - 用户 ishan-1010（2026-06-10，核对了 main @a861924）：HTTP 路径（`HF_HUB_DISABLE_XET=1`）与 Xet 路径均复现"重跑归零 + `.incomplete` 孤儿文件堆积"；
   - 用户 MohammedAlkindi（2026-08-16，contributor）：指认 `file_download.py:1948`（uuid 临时名）与 `file_download.py:367`（Range 逻辑）"resume 机制完好但不可达"，并给出带补丁的对照复现（补丁后 `Range: bytes=10485760-` 生效、只补传缺失 14.5MB）；
   - **HF 官方维护者 Wauplin（2026-06-25）**："For the 'resume on failing download' we will try to figure out something but the solutions suggested above are not correct / considered. **We need a solution happening in `hf_xet` instead.**"——官方方向是续传做在 Xet 层，而非恢复 HTTP Range 跨运行续传。
3. **Xet 路径的官方口径**（xet-core PR #989，**open PoC**，2026-09-25，https://github.com/huggingface/xet-core/pull/989 ）：开头原话——
   > "**Xet downloads can't be resumed today** ([huggingface_hub#4196]): **the destination is always truncated.**"
   该 PoC 提出的方案是 opt-in 的 `reuse_existing` 模式（按 segment 校验哈希复用本地数据），**需要服务端 `/v1/reconstructions` 增发 verification hash 字段**，且"默认行为不变"。即 Xet 续传在 2026-10 仍是未落地的 PoC。
4. **版本时间线**（release notes，api.github.com）：v2.2.0（2026-10-08，当前）、v2.0.0 "The httpx2 release"（2026-09-24，HTTP 栈整体换 httpx2）、v1.31.0（2026-09-10）"More resilient downloads"、v1.29.0（2026-08-27）"**Fix Xet downloads rate limits**"（Xet 路径曾触发限流的旁证）。原调研引用的"file_download.py 源码"实为 0.x 布局（现版本 `http_get` 已在 `utils/_http.py`、Xet 逻辑在 `utils/_xet.py`，仓库经历 v1/v2 两轮重构）。

**但注意反向边界（不可矫枉过正）**：`http_get` 的 Range 机器仍在——**同一次调用内**的连接错误重试会用 `new_resume_size` 续传（L458–478）；被推翻的只是"跨运行/跨进程续传"。

### 1.2 子结论「自研下载器对 resolve URL 走纯 HTTP + Range」——【站得住】（本次实测新增支持证据）

原调研的 FR-8 落点其实是"自己实现下载器"，这条本次**实测通过**：

- `HEAD https://huggingface.co/Qwen/Qwen3-4B-GGUF/resolve/main/Qwen3-4B-Q8_0.gguf` → 302 → `https://us.aws.cdn.hf.co/xet-bridge-us/...`（**Xet 文件也经 CAS bridge 以普通 HTTP 提供**，响应头 `accept-ranges: bytes`、`etag = "c8aa2cf6..."(sha256)`、`x-xet-hash` 同值、`x-linked-size: 4280404704`）；
- 对该 CDN URL 发 `Range: bytes=0-1023` → **HTTP 206，实收 1024 字节**（2026-10-14 实测）；
- hf-mirror 官方自研的 `hfd.sh`（426 行，2026-10-14 取自 https://hf-mirror.com/hfd/hfd.sh ）正是纯 aria2c 方案（默认 `-x 4 -j 5`、aria2 `-c` 续传、`--tool wget` 备选），完全不走 Xet 客户端——佐证 resolve+Range 通路对 Xet 化仓库依然可用。

即：**「下载器 = resolve URL + HTTP Range + 按哈希命名」作为 FR-8 自研方案依然正确；错的是把它当成"HF 官方现在的做法"**。文档表述需把"照抄 HF 规范（已验证）"改为"基于 resolve URL 稳定契约自研（HF 自家实现 2026 年起反而不这么做了）"。另外必须新增 `HF_HUB_DISABLE_XET=1` 的等价开关认知（现 huggingface_hub main 明文读取该变量，源码 L2012 附近警告分支）。

### 1.3 子结论「sha256/ETag 校验 + models--org--name/{blobs,refs,snapshots} 布局」——【站得住】

- 当前 main 的缓存布局 docstring 仍是 `models--julien-c--EsperBERTo-small` + `blobs/refs/snapshots` 树（file_download.py L751/L892 附近）；
- 实测 Xet 文件最终 CDN 响应 `etag == x-xet-hash == sha256`，与「LFS/Xet 文件 ETag 即 sha256」一致；blob 按该哈希命名、快照符号链接的结构未变；
- v2.x 的 `local_dir` 模式还新增了本地文件 sha256 复核（`sha_fileobj`，file_download.py L1450：etag 是 sha256 时算本地哈希比对，命中即免下载）——比原调研记录**更强**，非反证。

### 1.4 子结论「镜像支持只需换 URL 前缀，hf-mirror 完全兼容」——【需修正】

新增两个原调研未覆盖的条件：

1. **Xet 流量按设计绕过 `HF_ENDPOINT`**。当前 main 的 `parse_xet_file_data_from_response`（`utils/_xet.py` L48）从 resolve 响应头 `X-Xet-Hash` + Link `X-Xet-Auth` 判定走 Xet；一旦命中，chunk 重组面向的是 CAS 连接信息里的绝对 endpoint（`X-Xet-Endpoint`/refresh route），**不经过镜像前缀**。实测 Qwen/Qwen3-4B-GGUF 的 resolve 响应就带 `x-xet-hash`（该仓已 Xet 化）。对基于 huggingface_hub 的工具链，`HF_ENDPOINT` 指到镜像并不必然等于"全部流量走镜像"——除非镜像剥掉 X-Xet 头（则回退纯 HTTP，走镜像，反而可用）或用户设 `HF_HUB_DISABLE_XET=1`。
2. **hf-mirror 对本环境（海外出口 IP）实测是 308 回源而非代理**：`HEAD https://hf-mirror.com/Qwen/Qwen3-4B-GGUF/resolve/main/...（gguf 与 config.json 均同）` → `308 → https://huggingface.co/...`（Server: Caddy）；`/raw/` 端点同样 308。**盲点标注**：这大概率是镜像站对非 CN IP 的分流策略（公益镜像省带宽），CN 用户的实际体验（代理 vs 回源）本环境无法直接实证；但至少证明「换前缀 ⇒ 一定走镜像流量」不成立，FR-8 的镜像支持需把「回源重定向」与「Xet 头透传与否」两种行为都纳入设计（跟随重定向 + Range 仍可工作，前提是客户端能直连 huggingface.co/CDN）。
3. hf-mirror 首页教程仍推荐 `huggingface-cli download --resume-download ...`（2026-10-14 抓取原文）——该旗标在 huggingface_hub v1+ 已移除，镜像站文档未跟上 hub 1.x/2.x 的变化，进一步说明"镜像兼容性"是对 **hub 0.x 行为**的兼容。

**对 FR-8 的影响汇总**：下载器骨架（resolve URL + Range + sha256 命名 + blobs/snapshots 布局自管）不受影响且被实测加固；必须修正的是(a) 文档中"HF 官方如此实现"的表述与引用的源码结构（v1/v2 重构后已失真）；(b) 新增 Xet 分支说明：huggingface_hub 工具链走 Xet 时镜像/token/限速行为都不同，自研下载器应显式选择纯 HTTP 路径并对最终文件做 sha256 校验（CAS bridge 的 etag 即 sha256，可用）；(c) 断点续传是**我们的自研责任**，不能再以"对齐 HF 官方行为"为依据（HF 官方 2026-06 起自己也不续传了，Xet 续传还是 open PoC）。

---

## 2. llama.cpp 三条结构化输出路径

### 2.1 「/completion 的 `json_schema` 参数」——【站得住】

- 当前 release **b11588（2026-10-13）** 与 master 的 `tools/server/README.md` L573 均在："`json_schema`: Set a JSON schema for grammar-based sampling (e.g. `{\"items\": ...}`)，`{}` for any JSON. See tests for supported features. Default: no JSON schema."
- 无编译开关门控：`common/CMakeLists.txt` L86 无条件编入 `json-schema-to-grammar.cpp`——不存在"默认没编译进去"的问题（暂未找到反证 + 正面核实）。
- 原文档未写具体版本号，无可证伪的版本实体；当前锚点为 b11588（建议正式文档补版本锚定）。

### 2.2 「/v1/chat/completions 的 `response_format`」——【站得住】

b11588/master README L1346 原文仍支持 `{"type":"json_object"}`、`{"type":"json_object","schema":...}`、`{"type":"json_schema","schema":...}` 三种形态。补充两个实测核对出的边界（原文档未写，非反证）：

- 该端点要求模型有受支持的 chat template（默认回落 ChatML）——README 明示"Only models with a supported chat template can be used optimally"；
- `--jinja` 只在 **function calling / 工具调用** 时要求（README L1417），`response_format` 的 JSON 约束本身**不需要** `--jinja`。

### 2.3 「进程内 `common/json-schema-to-grammar.h`，头文件级、依赖极少、可单独复用」——【被推翻（"头文件级"部分）】

- master 与 b11588 的 `common/json-schema-to-grammar.h` 均已变为**纯声明头**：全文 24 行（743 字节），只剩
  `std::string json_schema_to_grammar(const common_json &, bool force_gbnf = false);` 与新增重载 `json_schema_to_grammar(const common_chat_schema_document &)`；
- 实现移入 `common/json-schema-to-grammar.cpp`（43,518 字节，`#include "common.h" "trie.h" "unicode.h"`）——"单独复用一个头文件"不再成立，需连同 `.cpp` 及 `json.h`/`json-schema.h`/`trie.h`/`unicode.h` 依赖一起移植，FR-A2 的集成成本估算需上修；
- 原 `force_gbnf` 签名仍在（该子项站得住），新增的 `common_chat_schema_document` 重载说明官方在向 chat-schema 方向演化，移植时需留意 API 漂移。

**目标 2 总判级：【需修正】**——三条通路全部存在且默认可用（无反证），但第三条通路"头文件级、依赖极少、可单独复用"的描述已被源码结构变化推翻。

---

## 3. Qwen GGUF 官方/unsloth 源可再分发

### 3.1 「官方 Qwen/Qwen3-4B-GGUF 与 unsloth/Qwen3-4B-GGUF 为 Apache-2.0、可再分发」——【站得住】（2026-10-14 复核）

- 官方卡 frontmatter 实测：`license: apache-2.0` + `license_link: .../Qwen3-4B-GGUF/blob/main/LICENSE`；仓库文件清单含 `LICENSE`；量化档 `Q4_K_M/Q5_0/Q5_K_M/Q6_K/Q8_0` 与原文档一致（`https://hf-mirror.com/api/models/Qwen/Qwen3-4B-GGUF` 实取）。
- unsloth 卡 frontmatter 实测：`license: apache-2.0`、`license_link` 指向**基模型** `Qwen/Qwen3-4B` 的 LICENSE、`base_model: Qwen/Qwen3-4B`；全文检索 `license/redistribute/credit/commercial/attribution` **未发现任何额外再分发条款**（暂未找到反证）。

### 3.2 「GGUF 内嵌 metadata 的 license 与仓库声明不一致」的风险——【原证据不足 → 本次实测定性：载体不存在】

对官方 `Qwen3-4B-Q4_K_M.gguf` 做 Range 下载头部 262,144 字节并解析 GGUF v3 header（28 个 metadata KV）：**完全没有 license 类字段**（键空间只有 `general.architecture/type/name/finetune/basename/size_label` + `qwen3.*` + `tokenizer.ggml.*`）。即 GGUF 规范的通用 metadata 不携带许可信息，"内嵌与卡面不一致"这一风险向量不存在；合规依据**只能**是仓库卡 frontmatter + LICENSE 文件（与原调研的处理方式一致，且现在是实测结论而非推断）。

顺带一个非许可类发现：官方 GGUF 的 `general.name = "Qwen3 4B Instruct Awq"`、`general.finetune = "Instruct-awq"`——**官方 GGUF 是从 AWQ 量化权重转出的**，与其卡面 q4_K_M 等档位命名并存，对精度敏感的场景值得知道。

### 3.3 反向发现：Qwen 家族许可混合是实锤陷阱（强化原文档的【未确认】项）

- `Qwen/Qwen2.5-72B-Instruct`（基模型）frontmatter 实测：`license: other`、`license_name: qwen`——**非 Apache**；
- `bartowski/Qwen2.5-72B-Instruct-GGUF` 与 `lmstudio-community/Qwen2.5-72B-Instruct-GGUF` 均正确传导 `license: other (qwen)`——社区量化者在非 Apache 基模型上不会标 Apache。
- 结论：原文档"Qwen 历史版本存在非 Apache 许可、须逐仓库核对 frontmatter"的谨慎判断被一手证据**证实**，且应从"建议"升级为 FR-8 的硬性规则（导入/内置任何 Qwen 权重前核对 frontmatter license 字段）。另注：`bartowski/Qwen_Qwen2.5-72B-Instruct-GGUF`（旧命名）现返回 "Invalid username or password"（HF 对已改名/移除仓库的典型响应）——FR-8 若硬编码仓库路径需考虑改名风险。

**目标 3 总判级：【站得住】**（含两处补强：GGUF metadata 无 license 字段的实测定性；Qwen2.5-72B 非 Apache 的实锤）。

---

## 4. whisper.cpp 词级时间戳与 silero-vad

### 4.1 「词级时间戳 `-ml 1` 满足字幕/对齐类需求」——【需修正】（精度限制必须写进前提）

- README 仍标注 **"Word-level timestamp (experimental)"**（原文档如实记录了"实验特性"，此点站得住）；但 `-ml 1` 的机制精度限制原调研未覆盖：
  - 实现机制（master `src/whisper.cpp` L8784 起 `whisper_exp_compute_token_level_timestamps`）：按 **timestamp token（`token.tid`）概率启发式**（`thold_pt`/`thold_ptsum`）插值分词时间——不是注意力对齐。README 的 DTW 专节现已不在（CLI 仍留 `-dtw MODEL` 与 `-wt N` 参数，`include/whisper.h` L121–128 的 `dtw_token_timestamps` 等 [EXPERIMENTAL] 参数在）；
  - 注意力对齐路径是独立的 DTW 实现（`whisper_exp_compute_token_level_timestamps_dtw` + `g_aheads` 预设，**现覆盖 tiny→large-v3-turbo 全部尺寸**，whisper.cpp L390–401——不是只有 large 系）；
  - 已知问题（issue 一手记录）：#3726（`--processors > 1` 时 token 时间戳错乱，2026-03，closed）、#3754（VAD + 开头音乐导致 token 时间戳错误，closed）、#375（2023 年提出的 OpenAI 注意力权重方案诉求）。社区大量实践以 `curl -C -` / aria2 式外部对齐或 DTW 兜底。
- 对 FR-A6 的修正建议：字幕/对齐需求应写明"词级时间戳来自 tid 启发式，长音频/多说话人/音乐场景会漂移；高精度需 `-dtw`（实验）或外部强制对齐"，不应把 `-ml 1` 当作免检能力。

### 4.2 silero-vad「MIT 许可、ONNX 可分发」——【站得住】（2026-10-14 复核）

- master `LICENSE` 实测全文：**MIT License, Copyright (c) 2020-present Silero Team**；
- README L137 原文仍在："Published under permissive license (MIT) Silero VAD has zero strings attached - no telemetry, no keys, no registration, no built-in expiration, no keys or vendor lock."；
- README 首行徽章 alt 文本的自相矛盾（alt 写 "License: CC BY-NC 4.0"、徽章图与链接均 MIT）**至今未修**——与原调研记录一致，以 LICENSE 文件为准的判断正确；
- ONNX 权重分发条款：`.onnx` 模型文件**直接位于仓库内**（`src/silero_vad/data/silero_vad.onnx`、`silero_vad_16k_op15.onnx` 等，git trees 实取）——即权重随仓库 MIT 一体分发，不存在"代码 MIT、权重另议"的分离条款（暂未找到反证）。

### 4.3 附带核对：whisper.cpp 内置 VAD——【站得住】

`models/download-vad-model.sh` 实测仍在：`src="https://huggingface.co/ggml-org/whisper-vad"`，可选 `silero-v5.1.2` / `silero-v6.2.0`——与原文档记录一致。

---

## 5. HF_TOKEN/匿名下载限速对「按需下载大模型」的影响

**【站得住】（政策未收紧到影响单模型按需下载的程度），附三条新信息**：

一手信源：HF 官方 rate limits 文档页（https://huggingface.co/docs/hub/rate-limits ，2026-10-14 实取全文）：

- 三桶限速：Hub APIs / **Resolvers（一切 `/resolve/` URL——文档点名 llama.cpp、LM Studio、Jan、ollama 等构造的就是这类）** / Pages；**5 分钟固定窗口**；超限 429 + `RateLimit` / `RateLimit-Policy` 标准头（IETF draft-ietf-httpapi-ratelimit-headers v9）；
- 档位表（页内标注 "in September '25"）：**匿名（按 IP）API 500 / Resolvers 3,000 / Pages 100（每 5 分钟）**；Free 登录用户 1,000/5,000/200；PRO 2,500/12,000/400；更高档为 Team/Enterprise。页内明示匿名与 Free 档配额 "subject to change over time depending on platform health"；
- 官方第一建议原话："First, make sure you always pass a HF_TOKEN … This is the number one reason users get rate limited"。

对 FR-8 的判定：单仓 4–8 个文件的按需下载（每文件 1 次 HEAD + 1 次 GET + 若干 Range）距匿名 3,000/5min 余量巨大，**按需下载大模型不受影响**。需补充的风险：

1. 匿名配额按 IP 计——NAT/公司出口共享 IP 的用户会被他人用尽配额（官方文档语义 + 实践常见），FR-8 的 429 处理与"建议登录 token"提示仍必要；
2. **Xet 元数据刷新调用曾被限流**：huggingface_hub v1.29.0（2026-08-27）release 标题即 "Fix Xet downloads rate limits"——走 huggingface_hub+hf_xet 工具链大量并发下载时，Xet token 刷新是限流放大器（自研纯 HTTP resolve 下载器无此问题）；
3. 本次未找到"按仓库/按文件大小限速"的官方政策（暂未找到反证；限速只按请求数计）。

---

## 判级总表

| # | 原结论 | 判级 | 一句话理由 |
| --- | --- | --- | --- |
| 1a | HF 用 `.incomplete`+HTTP Range 跨运行续传（照抄即可） | **被推翻** | main 版 uuid 唯一临时名+不传 resume_size+失败即删；HF 官方确认"方案要做在 hf_xet"；Xet 官方原话 "can't be resumed today"（issue #4196 open、xet-core PR #989 PoC） |
| 1b | FR-8 自研：resolve URL+Range+sha256 命名 | 站得住 | 实测 Xet 仓的 CAS bridge 仍回 206/etag=sha256；hfd.sh 同路线 |
| 1c | blobs/refs/snapshots 布局、ETag=sha256 | 站得住 | main docstring 未变；实测 etag==x-xet-hash==sha256 |
| 1d | 「换前缀=换镜像，hf-mirror 完全兼容」 | **需修正** | Xet 命中时 CAS 流量绕过 HF_ENDPOINT；实测 hf-mirror 对本海外出口 308 回源（vantage 盲点已标注）；镜像教程仍用已废弃的 `--resume-download` |
| 2a | `/completion` `json_schema` | 站得住 | b11588+master README 均在，common 无条件编译 |
| 2b | `/v1/chat/completions` `response_format` | 站得住 | b11588+master 均在；response_format 不需 `--jinja`（工具调用才需要） |
| 2c | 进程内 json-schema-to-grammar.h「头文件级、依赖极少」 | **被推翻（该子项）** | 头文件只剩 24 行声明，实现移入 43.5KB 的 .cpp（common.h/trie.h/unicode.h 依赖） |
| 3a | 官方/unsloth Qwen3-4B GGUF Apache-2.0 可再分发 | 站得住 | 今日实测 frontmatter+LICENSE+无额外条款 |
| 3b | GGUF 内嵌 license 与卡面不一致风险 | 原证据不足→实测排除载体 | GGUF 通用 metadata 根本无 license 字段（解析 28 KV 实证） |
| 3c | Qwen 历史版本许可须逐仓核对 | 站得住（升格为实锤） | Qwen2.5-72B-Instruct = license:other(qwen)，量化仓正确传导 |
| 4a | whisper.cpp `-ml 1` 词级时间戳满足字幕/对齐 | **需修正** | tid 概率启发式，实验特性；高精度需 DTW（实验）或外部对齐；#3726/#3754 已知错例 |
| 4b | silero-vad MIT、ONNX 可分发 | 站得住 | LICENSE 实测 MIT；onnx 权重在仓库内随 MIT 分发 |
| 5 | 匿名限速不影响按需下载大模型 | 站得住 | 匿名 Resolvers 3,000/5min（官方页）；注意共享 IP 与 Xet 元数据限流前科 |

## 对文档/设计的落地动作建议

1. `research-model-mgmt-llm-infra.md` §1.1 与「FR-8 可借鉴点 1」改写：删除"照抄 HF 规范（已验证的最小正确集）"表述，改为"基于 resolve URL 稳定契约自研"；补 Xet 段（HF 自 2026-06 起不跨运行续传、Xet 续传为 open PoC、`HF_HUB_DISABLE_XET`）。
2. FR-A2 成本估算上修：json-schema-to-grammar 需移植 `.cpp` + 4 个依赖头；文档补 llama.cpp 版本锚（当前 b11588）。
3. FR-8 增加"仓库改名/401"容错（bartowski 旧仓已不可达实例）与 429 处理（Resolvers 桶、5min 窗口、共享 IP 场景建议登录）。
4. FR-A6 字幕能力表述补精度前提（tid 启发式 vs DTW）。

## 参考链接清单（本次反证取证）

HuggingFace hub / Xet：
- https://github.com/huggingface/huggingface_hub/blob/main/src/huggingface_hub/file_download.py （main 实取 2026-10-14：L2003 uuid 临时名、L338–478 http_get Range、finally unlink、xet 分支）
- https://github.com/huggingface/huggingface_hub/blob/main/src/huggingface_hub/utils/_xet.py （L48 parse_xet_file_data_from_response）
- https://github.com/huggingface/huggingface_hub/issues/4196 （No resuming of downloads，open；含 Wauplin 2026-06-25、MohammedAlkindi 2026-08-16 评论）
- https://github.com/huggingface/xet-core/pull/989 （PoC "reuse existing local data"，open，2026-09-25；"Xet downloads can't be resumed today"）
- https://github.com/huggingface/xet-core/issues/1018 （2026-10-11 下载停滞）
- https://huggingface.co/docs/hub/rate-limits （三桶/档位表/429 头，2026-10-14 实取）
- huggingface_hub releases（api.github.com）：v2.2.0(2026-10-08) / v2.0.0 httpx2(2026-09-24) / v1.31.0(2026-09-10) / v1.29.0 Fix Xet downloads rate limits(2026-08-27)
- 实测记录（本环境，2026-10-14）：hf-mirror 308 回源（resolve/raw、gguf+config.json）、huggingface.co 302 → us.aws.cdn.hf.co/xet-bridge-us → Range 0-1023 = 206、`https://hf-mirror.com/hfd/hfd.sh`（426 行 aria2 方案）、`https://hf-mirror.com/`（教程含已废弃 `--resume-download`）

llama.cpp（master + b11588 双核对）：
- https://github.com/ggml-org/llama.cpp/blob/b11588/tools/server/README.md （L573 json_schema、L1346 response_format、L1417 工具调用需 --jinja）
- https://github.com/ggml-org/llama.cpp/blob/master/common/json-schema-to-grammar.h （24 行声明）
- https://github.com/ggml-org/llama.cpp/blob/master/common/json-schema-to-grammar.cpp （43,518 字节实现）
- https://github.com/ggml-org/llama.cpp/blob/master/common/CMakeLists.txt （L86 无条件编译）

Qwen / GGUF：
- `https://hf-mirror.com/api/models/Qwen/Qwen3-4B-GGUF`（文件清单）、`.../Qwen/Qwen3-4B-GGUF/raw/main/README.md`、`.../unsloth/Qwen3-4B-GGUF/raw/main/README.md`、`.../Qwen/Qwen2.5-72B-Instruct/raw/main/README.md`、`.../bartowski/Qwen2.5-72B-Instruct-GGUF/raw/main/README.md`（均经镜像 -L 实取 frontmatter）
- Qwen3-4B-Q4_K_M.gguf 头部 262KB Range 解析（本环境，GGUF v3，28 KV 无 license 字段，general.name="Qwen3 4B Instruct Awq"）

whisper.cpp / silero-vad：
- https://github.com/ggml-org/whisper.cpp/blob/master/README.md （"Word-level timestamp (experimental)"）
- https://github.com/ggml-org/whisper.cpp/blob/master/include/whisper.h （L121–128 DTW [EXPERIMENTAL]）
- https://github.com/ggml-org/whisper.cpp/blob/master/src/whisper.cpp （L8784 whisper_exp_compute_token_level_timestamps；L390–401 g_aheads 全尺寸预设）
- https://github.com/ggml-org/whisper.cpp/blob/master/models/download-vad-model.sh （silero-v5.1.2/v6.2.0）
- https://github.com/ggml-org/whisper.cpp/issues/3726 、issues/3754 、issues/375
- https://github.com/snakers4/silero-vad/blob/master/LICENSE （MIT 实取）、README L1/L137；git trees（src/silero_vad/data/*.onnx 在仓库内）
