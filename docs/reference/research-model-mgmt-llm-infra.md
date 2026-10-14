# 调研：模型下载管理与本地 LLM 基础设施（FR-8 / FR-24 / FR-A* / NFR-M4 支撑材料）

## 元信息

- 日期：2026-10-13 ~ 2026-10-14
- 性质：外部技术调研，只采信一手信源（官方文档页、官方 GitHub 仓库的 README / LICENSE / 源码、HF 模型卡原文）
- 抓取方式说明：本 Linux 环境中 huggingface.co 与 api.github.com 直连被墙。HuggingFace 官方文档（下载指南、环境变量参考）在墙内可达时抓取全文存档；HF 模型卡 README 经 hf-mirror.com（HF 官方内容的反向代理站）的 `/raw/` 端点获取原文；GitHub 仓库文件经 raw.githubusercontent.com 直取；Qt 经 doc.qt.io。所有关键结论均可回溯到下方「参考链接清单」中的 URL。
- 结论分级约定：【已确认】= 有官方文档/源码直接证据；【倾向】= 基于已确认事实的合理推断，或一手信源中的非规范性表述；【未确认】= 本次未查到一手证据，不下结论。

---

## 主要发现

### 1. 模型下载管理的通行做法

#### 1.1 huggingface_hub 的下载/断点续传/校验规范

来源：
- https://huggingface.co/docs/huggingface_hub/guides/download （官方下载指南）
- https://github.com/huggingface/huggingface_hub/blob/main/src/huggingface_hub/file_download.py （源码）
- https://github.com/huggingface/huggingface_hub/blob/main/src/huggingface_hub/constants.py （源码）

**API 层**（官方下载指南）：
- `hf_hub_download(repo_id, filename, revision, ...)` 下载单文件并缓存，返回快照内路径；`snapshot_download(repo_id, ...)` 整仓下载，内部复用前者、并发执行。
- 版本钉定：`revision` 接受 tag（`v1.0`）、分支（`test-branch`）、PR（`refs/pr/3`）、**全长度** commit hash（7 位短 hash 不行）。
- 文件过滤：`allow_patterns` / `ignore_patterns`（fnmatch 通配），可组合（如下载全部 `*.json`/`*.md` 但排除 `vocab.json`）。
- `local_dir` 模式：文件按仓库原结构落到指定目录，根下生成 `.cache/huggingface/` 元数据目录防重复下载；元数据变了才拉新版本。
- 干跑：CLI `hf download <repo> --dry-run` 或 API `dry_run=True`，返回 `DryRunFileInfo`（commit hash、文件名、大小、是否已缓存、是否会下载）——**适合做下载前磁盘占用确认 UI**。
- CLI URI 语法：`hf://[<TYPE>/]<ID>[@<REVISION>][/<PATH>]`。

**断点续传**（file_download.py 源码）：
- 文件先落到 `<blob路径>.incomplete` 临时文件；中断后重试时向服务器发 HTTP `Range` 头从已下载字节续传（源码 `headers["Range"] = _adjust_range_header(...)`）。
- 若服务器忽略 Range 返回 200，则回退从头重写（源码有显式处理分支）。
- 下载完成后校验 `expected_size` 与实际字节数，不符即报错。

> **2026-10-14 反证修正（`falsify-hf-llm.md`）**：上述「.incomplete + Range 跨运行续传」是**历史版本行为**。huggingface_hub 当前 main（~v2.2.0，2026-10）已改为 `{etag}.{uuid8}.incomplete` 进程唯一临时文件、`http_get` 不传 `resume_size`（Range 机制存在但代码路径不可达）、finally 删除残留——**HF 官方现行实现不做跨运行续传**（Xet 下官方原话 "Xet downloads can't be resumed today"，issue #4196 / xet-core PR #989）。对 FR-8 的含义：自研「resolve URL + Range + sha256」纯 HTTP 路线经实测**站得住**（Xet 仓库经 CAS bridge 仍以纯 HTTP 提供、Range 实测 206、etag==x-xet-hash==sha256；hf-mirror 官方 hfd.sh 即纯 aria2 路线），但定位须从「照抄 HF」改为「基于 resolve 契约自研」。镜像注意：Xet 命中时 CAS 流量按设计**绕过 HF_ENDPOINT**（utils/_xet.py），换前缀 ≠ 镜像完全兼容。

**完整性校验**（源码 docstring）：
- 文件的 ETag 即其内容哈希：git 普通文件是 git-sha1，LFS/Xet 文件是 sha256。缓存 blobs 直接以该哈希命名，快照内文件是指向 blob 的符号链接——**文件身份与完整性由「哈希命名 + 大小核对」共同保证**。

**缓存布局**（file_download.py 源码 docstring 原文示例）：

```
$HF_HUB_CACHE/
└── models--<org>--<name>/
    ├── blobs/        # 实际文件，按 git-sha1 或 sha256 命名
    ├── refs/         # revision -> commit_hash（如 refs/main）
    └── snapshots/
        └── <commit_hash>/
            └── README.md -> ../../blobs/<sha>
```

- 环境变量（官方环境变量参考页 https://huggingface.co/docs/huggingface_hub/package_reference/environment_variables ）：`HF_HOME`（默认 `~/.cache/huggingface`）、`HF_HUB_CACHE`（默认 `$HF_HOME/hub`）、`HF_TOKEN`、`HF_HUB_OFFLINE`、`HF_HUB_DISABLE_TELEMETRY`、`HF_HUB_ETAG_TIMEOUT`/`HF_HUB_DOWNLOAD_TIMEOUT`（默认 10s）等，**全部在 import 时读取一次**。
- Windows 坑（官方明示）：无开发者模式/管理员权限时无法建符号链接，缓存退化为大文件重复存储（`HF_HUB_DISABLE_SYMLINKS_WARNING` 可关告警）。
- 传输后端：现为 Xet 存储（`hf_xet`，Rust xet-core 绑定）：文件切成不可变 chunk，下载时用 LFS sha256 查 CAS 服务拿重组元数据 + presigned URL，按需取 chunk 重组；`huggingface_hub>=0.32.0` 自动附带；`hf_transfer` 已弃用。HDD 可设 `HF_XET_RECONSTRUCT_WRITE_SEQUENTIALLY=1` 顺序写。

#### 1.2 HF_ENDPOINT 镜像切换机制（hf-mirror）

来源：
- https://github.com/huggingface/huggingface_hub/blob/main/src/huggingface_hub/constants.py （L67-70，机制的一手源码证据）
- https://hf-mirror.com （镜像站官网）

- **机制（源码级）**：`constants.py` 中 `ENDPOINT = os.getenv("HF_ENDPOINT", "https://huggingface.co").rstrip("/")`，下载 URL 模板为 `ENDPOINT + "/{repo_id}/resolve/{revision}/{filename}"`——即 `https://huggingface.co/<repo>/resolve/<rev>/<file>` 这个形态是稳定的公开契约，换前缀即换源。
- **注意**：HF 官方「环境变量」参考页**未列出** `HF_ENDPOINT` 条目（文档缺口），但它是源码中真实读取的变量。
- **hf-mirror 官网给出的四种用法**（原文归纳）：
  1. 网页直接下载（站内搜索，模型页 Files and Versions 里下文件）；
  2. `huggingface-cli download`（现 `hf download`）+ `HF_ENDPOINT=https://hf-mirror.com`；
  3. `hfd.sh`（镜像站自研，基于 aria2 的专用下载工具，断线稳定）；
  4. 纯环境变量（`HF_ENDPOINT=... python your_script.py`），非侵入式，适用于一切基于 huggingface_hub 的工具链。
- gated repo：镜像站**不支持登录**；需先到 HF 官网申请许可拿 Access Token，再 `--token hf_***` 带令牌下载。

#### 1.3 各桌面软件的模型目录布局与下载交互

| 软件 | 模型目录 | 可配置性 | 下载/导入交互 | 来源 |
| --- | --- | --- | --- | --- |
| Ollama | Windows `C:\Users\%username%\.ollama\models`；macOS `~/.ollama/models`；Linux（服务用户）`/usr/share/ollama/.ollama/models` | `OLLAMA_MODELS` 环境变量（源码 envconfig/config.go：默认 `$HOME/.ollama/models`） | `ollama pull`，支持 `ollama run hf.co/Qwen/Qwen3-4B-GGUF:Q8_0` 直连 HF | https://docs.ollama.com/faq ；https://github.com/ollama/ollama/blob/main/envconfig/config.go |
| LM Studio | `~/.lmstudio/models/<publisher>/<model>/<model-file>.gguf`（刻意保留 HF 仓库目录结构） | 设置页可改（文档「Changing the models directory」，位于 My Models 页） | 内置 Discover 搜索（关键词 / `user/model` / 整条 HF URL 均可）；量化档位选择建议（4-bit 起）；`lms import <path/to/model.gguf>` 导入外部已下载模型（实验性），或手工放入约定目录 | https://lmstudio.ai/docs/app/basics/download-model ；https://lmstudio.ai/docs/app/advanced/import-model |
| Jan | 从 HuggingFace 下载运行（Llama/Gemma/Qwen/gpt-oss 等） | — | 应用内模型市场 | https://github.com/menloresearch/jan （README Features） |
| sherpa-onnx | 模型为解包目录（tar/zip 展开） | 传参指定 | 预训练模型**双源发布**：HuggingFace 与 ModelScope 并列提供（README 链接表两列） | https://github.com/k2-fsa/sherpa-onnx （README） |

### 2. llama.cpp 集成形态对照

来源：
- https://github.com/ggml-org/llama.cpp （README，MIT 徽章）
- https://github.com/ggml-org/llama.cpp/blob/master/grammars/README.md （GBNF 指南）
- https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md （server 全量 API 文档）
- https://github.com/ggml-org/llama.cpp/blob/master/common/json-schema-to-grammar.h （源码头文件）

- **项目基本面**：MIT 许可；「Plain C/C++ implementation without any dependencies」；建于 ggml 之上；量化支持 1.5/2/3/4/5/6/8-bit；CPU 后端之外有 CUDA/Vulkan/SYCL 等（本项目默认 CPU 正落在其第一等公民路径）。server 用 cpp-httplib（MIT）+ nlohmann/json（MIT）。
- **两种集成形态的官方入口**：
  - 进程内：链接 libllama（llama.h C API，README 文档目录有 "lib llama API" 专项文档）；`json_schema_to_grammar()` 公共设施在 `common/` 下提供。**2026-10-14 反证修正（`falsify-hf-llm.md`）**：「少量头文件、可单独复用」已失实——`json-schema-to-grammar.h` 现仅剩 24 行声明，实现移入 43.5KB 的 .cpp（依赖 common.h/trie.h/unicode.h），FR-A2 移植成本上修；核对基线 b11588（2026-10-13）。
  - 独立进程：`llama serve`（即 llama-server）提供 OpenAI 兼容 HTTP API + 内置 Web UI；`llama cli -hf ggml-org/<repo>` 可直接从 HF 拉模型运行——**自带 `-hf` 下载通道，说明官方把「从 HF 取模型」当作一等公民**。
- **桌面软件先例**：
  - server 形态：**Jan**——本地起 OpenAI 兼容 server（`localhost:1337`）供自身与其他应用调用（README Features 明示）。
  - 进程内形态：whisper.cpp / silero-vad 等 ggml 系生态是 C/C++ 进程内链接的通行做法；LM Studio 的引擎链接形态官方文档未说明（【倾向】其为内嵌动态库方式，因无子进程 server 配置项暴露）。
- **JSON schema 约束解码（当前版本，官方文档明示三条通路）**：
  1. `/completion` 端点采样参数 `json_schema`：直接传 JSON schema（如 `{"items":{"type":"string"},"minItems":10,"maxItems":100}`，`{}` 表示任意 JSON）；同级的 `grammar` 参数收 GBNF 文本。
  2. `/v1/chat/completions` 的 `response_format`：支持 `{"type":"json_object"}`（纯 JSON）与 `{"type":"json_object","schema":{...}}` / `{"type":"json_schema","schema":{...}}`（schema 约束）。
  3. 进程内 C++：`common/json-schema-to-grammar.h` 提供 `std::string json_schema_to_grammar(const common_json & schema, bool force_gbnf = false)`。
  - schema 支持的 feature 子集以 `tests/test-json-schema-to-grammar.cpp` 为准（文档原话），完整边界未逐一核验（见【未确认】）。
- **GBNF 语法能力**（grammars/README.md）：产生式规则、Unicode 字符类与区间、否定字符类、`*` `+` `?` `{m,n}` 重复、**token 级匹配**（`<[id]>` / `<text>`，可对齐特殊 token 如 `<think>`）、`root` 为入口。CLI 用 `--grammar-file` 加载。
- **工具执行/Agent 能力（server 内建，experimental）**：
  - `--tools read_file,file_glob_search,grep_search,exec_shell_command,write_file,edit_file,get_info`（或 `all`）；`--agent` = 开全部内建工具 + CORS 代理；启用后 CORS 默认收紧为 localhost（安全默认）。
  - MCP：`--mcp-servers-config mcp.json`（Cursor 兼容格式）挂外部 stdio JSON-RPC 工具服务，工具以 `<server>_<tool>` 命名暴露给模型；`--tools-runtime` 支持 docker/podman/ssh 沙箱执行。
  - 对我们的意义：llama.cpp 官方已经把「LLM 调工具」做进 server，但其工具是通用 shell/文件类；我们要的「音频处理确定性工具」走 FR-24 自定义更合适。

### 3. Qwen 系列 GGUF 量化包的获取与许可

来源：
- https://github.com/QwenLM/Qwen3/blob/main/README.md （L395-398）
- https://huggingface.co/Qwen/Qwen3-4B-GGUF （模型卡原文，经 hf-mirror `/raw/` 获取）
- https://huggingface.co/bartowski/Qwen_Qwen3-4B-GGUF （同上）
- https://huggingface.co/unsloth/Qwen3-4B-GGUF （同上）

- **官方许可**：Qwen3 README 明文「All our open-weight models are licensed under Apache 2.0」，许可文件在各 HF 仓库内。官方 `Qwen/Qwen3-4B-GGUF` 模型卡 frontmatter 亦标 `license: apache-2.0`。
  - 【倾向】Qwen3 一代全系（含 4B）为 Apache-2.0；**Qwen 历史版本（Qwen2.5 及更早的部分尺寸/代次）存在非 Apache 的自定义许可**，本次未逐一核验，落地时必须逐仓库核对 frontmatter `license` 字段（【未确认】项）。
- **官方 GGUF 包**（Qwen/Qwen3-4B-GGUF 模型卡原文）：
  - 量化档：`q4_K_M, q5_0, q5_K_M, q6_K, q8_0`。
  - 官方推荐运行方式：`./llama-cli -hf Qwen/Qwen3-4B-GGUF:Q8_0 --jinja --color -ngl 99 -fa -sm row --temp 0.6 --top-k 20 --top-p 0.95 ... -c 40960 -n 32768`（`-hf repo:quant` 直接选量化档）；ollama：`ollama run hf.co/Qwen/Qwen3-4B-GGUF:Q8_0`。
  - 模型特性：thinking / non-thinking 单模型切换（`/think` `/no_think` 软开关）；原生 32,768 上下文，YaRN 可到 131,072；GQA 32Q/8KV；官方宣称 agent/工具集成能力领先（此为官方宣传语，非独立评测）。
- **社区量化包再分发政策**：
  - **unsloth/Qwen3-4B-GGUF**：frontmatter 显式 `license: apache-2.0`，`base_model: Qwen/Qwen3-4B`——量化产物按 Apache-2.0 发布，允许再分发（保留许可与声明）。
  - **bartowski/Qwen_Qwen3-4B-GGUF**：frontmatter 无独立 license 字段，仅 `base_model_relation: quantized`、`base_model: Qwen/Qwen3-4B`——【倾向】其量化物法律上随基模型 Apache-2.0 传导，但卡面未自证。
  - 结论（对 FR-A5 的含义）：走「引导用户从 HF 下载官方或社区 GGUF」路线时，Apache-2.0 仓库（Qwen 官方 GGUF、unsloth）合规再分发/内置引导链接无障碍；若未来要**随安装包附带**量化权重，选 Apache-2.0 源并携带 LICENSE 即可（bartowski 源建议只引导不内嵌）。
- **4B 级 CPU 推理的工具调用可靠性公开评测**：【未确认】——本次一手信源（官方 README/模型卡）中无独立评测数据；官方模型卡的 agent 能力表述为宣传性质。建议后续以 Berkeley Function Calling Leaderboard 等为线索补充核查（本次未查）。

### 4. whisper.cpp 与 silero-vad

来源：
- https://github.com/ggml-org/whisper.cpp/blob/master/README.md
- https://github.com/snakers4/silero-vad/blob/master/README.md
- https://github.com/snakers4/silero-vad/blob/master/examples/cpp/README.md
- https://github.com/snakers4/silero-vad/blob/master/LICENSE

**whisper.cpp**：
- 许可：MIT（README 徽章 + LICENSE）。
- 模型格式：**ggml 单文件二进制**（参数 + mel filters + 词表 + 权重全打包），官方转换脚本产出，模型从 `ggerganov/whisper.cpp` HF 仓库下载（`models/download-ggml-model.sh`）。
- 集成方式：C/C++ 进程内（whisper.h C API）；README 专门有 Bindings 讨论区，列数十种语言绑定——C API 消费面极广，稳定性事实上有保障。
- **词级时间戳**：官方支持，`--max-len 1`（`-ml 1`）即可按词输出（实验特性，README 给出完整逐词时间戳示例输出）。**2026-10-14 反证注记（`falsify-hf-llm.md`）**：`-ml 1` 机制是 timestamp-token 概率启发式（whisper.cpp L8784），README 标 experimental，已知错例 #3726/#3754——高精度对齐需 `-dtw`（实验，aheads 预设已覆盖全模型尺寸）或外部对齐器；FR-A6 的「词级时间戳」需求按此定精度预期。
- 附加能力：tinydiarize 说话人分段（`-tdrz`，实验）；`--max-len` 控制分段行长。
- **内建 VAD**：`--vad` 参数 + VAD 模型即启用「先 VAD 切语音段、只把语音段喂 whisper」流程，官方直接支持 **Silero VAD 的 ggml 版**（`models/download-vad-model.sh silero-v6.2.0` 从 `ggml-org/whisper-vad` 下载 `ggml-silero-v6.2.0.bin`，约 864KB）。
- 实时输入示例（microphone）README 有专门小节。

**silero-vad**：
- 许可：MIT（README 正文明文："Published under permissive license (MIT) ... no telemetry, no keys, no registration, no built-in expiration, ... no vendor lock"——与本项目红线天然一致）。注意：README 首行徽章的 alt 文本自相矛盾地写着 "License: CC BY-NC 4.0"，但徽章链接与正文均为 MIT，以 LICENSE 文件为准（MIT）。
- 模型格式：ONNX（另有 torch/ExecuTorch 路径）；`onnxruntime>=1.16.1` 即可纯 ORT 运行，无需 torch。
- C++ 集成先例：官方 `examples/cpp` 是**单 cpp 文件 + 链接 onnxruntime** 的流式示例（gcc 直接编译，测试于 ORT 1.12.1）——与本项目「ORT PIMPL 引擎」结构同构。
- 性能（README）：30ms+ 的一个 chunk 在单线程 CPU 上 <1ms；ONNX 路径可比 torch 快 4-5x。
- **对本项目的直接含义**：VideoHandler 已带 ORT 1.30 CPU，silero ONNX 可作为又一个 `IStreamEngine` 形态的引擎挂进现有架构；whisper.cpp 则自带 ggml 运行时（不依赖 ORT），并已内置 silero VAD——两条集成路线都开放。

### 5. Qt MinGW 应用分发打包

来源：
- https://doc.qt.io/qt-6/windows-deployment.html
- https://doc.qt.io/qtinstallerframework/index.html （4.11.0）
- https://nsis.sourceforge.io/License
- https://cmake.org/cmake/help/latest/module/CPackNSIS.html
- https://github.com/vslavik/winsparkle/blob/master/COPYING 、https://github.com/vslavik/winsparkle/blob/master/README.md

**windeployqt**（官方文档要点）：
- 位置 `<qt>/bin/windeployqt.exe`；接受 `.exe` 或含 exe 的目录，**扫描可执行文件的依赖**并复制所需 Qt DLL/插件到 exe 目录。
- Qt Online Installer 安装的 Qt 需先 `qtenv2.bat` 设置构建环境再运行。
- `--qmldir` 时调用 qmlimportscanner 扫 QML 依赖（我们是 Widgets，不涉及）。
- 默认复制编译器运行时：**MinGW 构建 → 复制 MinGW 运行时 DLL**；MSVC 构建 → 期望安装器包含官方 VC Redistributable，且明文警告「编译器散装 DLL 不许直接分发」。
- `--dry-run` 干跑、`--dir/--libdir/--plugindir` 定向输出。
- **官方明示的坑**：第三方库（文档举例数据库库；对我们即 FFmpeg/ORT/SoundTouch DLL 与模型文件）**不在扫描范围**，需自行随包分发——NFR-M4 的打包脚本必须手工补这一层。

**安装器选型三方**：
- **NSIS**：源码 zlib/libpng 许可；安装器产物无许可负担（压缩模块另有 bzip2/CPL-1.0/LZMA 特别例外条款，只影响修改 NSIS 本身）。脚本式，生态最老。
- **CPack NSIS generator**：CMake 内建模块，`cpack -G NSIS` 即出包；NSIS 本体仍需预装。与现有 CMake 工程零胶水成本。
- **Qt Installer Framework 4.11**：Qt 官方；离线/在线安装器、组件化安装（增删组件）、**内建更新工作流**（Updating Components / Promoting Updates 文档专章）、有 CLI（binarycreator 等）。许可随 Qt（GPL/LGPL/商业三选，打包工具本身用于产出自己的安装器不受 Qt 应用许可影响——【倾向】，未逐字核验 IFW 许可条文）。

**自动更新**：
- **WinSparkle**：MIT（COPYING 文件）。macOS Sparkle 的 Windows 对应物：**appcast（RSS 格式）更新源** + C API（`WinSparkle.dll` 预编译或 NuGet 分发），后台检查 + 用户确认安装的成熟模式。
- Qt IFW 的在线安装器/maintenance tool 自带组件更新能力，是 WinSparkle 的替代路线（【倾向】：二者取一，IFW 路线少一个第三方依赖但更新源是 IFW 自己的 repository 格式；WinSparkle 只需静态 HTTP appcast，更轻）。

### 6. 桌面软件内嵌本地 Agent 的公开先例

来源：
- https://github.com/menloresearch/jan/blob/dev/README.md
- https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md
- https://lmstudio.ai/docs （文档目录含 "Use MCP Servers" 章节）

- **Jan**（Apache-2.0，Electron 桌面应用）：README Features 列出「Local AI Models（HF 下载运行）」「OpenAI-Compatible API（本地 localhost:1337 供其他应用）」「**Model Context Protocol: MCP integration for agentic capabilities**」「Custom Assistants」「Privacy First: Everything runs locally」。即 Jan 的 agent 形态 = 本地模型 + MCP 工具接入；其「计划/执行」分工未见公开架构文档（【未确认】）。
- **llama.cpp server 自身**：`--agent`/`--tools`（read_file、grep_search、exec_shell_command 等七件套）+ 外挂 MCP stdio server + docker/ssh 沙箱 runtime——C++ 单进程内「LLM 决策 + 确定性工具执行」的官方实现先例（面向 Web UI/HTTP 客户端）。
- **LM Studio**：官方文档有 "Use MCP Servers / Add to LM Studio Button" 章节，同为 MCP 接入路线。
- 【未确认】：**没有找到**以「本地 LLM 生成结构化计划（plan）→ 确定性执行器逐步执行」为明确架构描述的开源桌面项目一手文档。Jan/LM Studio/llama-server 三者都是「模型直接发起工具调用（reactive tool-calling）」型。VideoHandler 的 FR-A2（schema 约束的计划生成）+ FR-24（确定性命令层）组合在公开桌面软件中**暂无同构先例**，属于自行组合成熟部件（json-schema-to-grammar + 无头命令层）而非复刻既有项目。

---

## 对 FR-8 / FR-24 / FR-A* / NFR-M4 的可借鉴点

**FR-8 模型管理器**
1. ~~下载核心机制照抄 HF 规范~~ **2026-10-14 反证修正后**：下载核心 = 基于 HF 的 **resolve URL 契约**自研纯 HTTP 下载器（`{ENDPOINT}/{repo_id}/resolve/{revision}/{filename}` + HTTP `Range` 续传 + 完成后按 sha256 校验/命名 + `.incomplete` 临时文件）——HF 官方现行实现已不做跨运行续传（见 §1.1 修正），该路线经 Xet 仓库实测成立（206 / etag==sha256）；镜像支持把 ENDPOINT 前缀做成可配置项，但须注记：Xet 命中时 CAS 流量绕过 HF_ENDPOINT，镜像兼容性按 resolve 契约实测为准（hf-mirror 官方 hfd.sh 即此路线）。
2. 目录布局三档参考：LM Studio 式 `publisher/model/file`（直观、与 HF 对齐）；Ollama 式 `OLLAMA_MODELS` 环境变量可改目录（**建议采用**：一个环境变量 + 设置页双通道）；HF 式 blobs/snapshots 内容寻址（对我们 4-6 个模型文件的规模**过度设计，不建议**）。清单用 JSON manifest（记录 repo、revision/commit、sha256、本地路径）即可同时支撑「首次下载」「从文件导入」（导入 = 校验哈希/读模型 metadata 后登记）与「更新检查」。
3. UI 交互参考：LM Studio 的搜索 + 量化档位列表（含每档体积与推荐标记）+ 首次使用触发下载；下载前 `--dry-run` 式的「将下载 N 个文件共 X GB，其中已缓存 M 个」确认（`DryRunFileInfo` 同款信息结构）。
4. 双下载源先例：sherpa-onnx 的 HF + ModelScope 双列发布——FR-8 的「HF/镜像可配」直接对齐此实践；hf-mirror 的 gated-repo 流程（官网申请 token → 带 token 下载）值得在文档/UI 里预留 token 字段（RVC/克隆音色模型多为社区仓库，不一定 gated，但机制留口）。

**FR-24 无头命令层**
- llama-server 端点族（`/completion`、`/tokenize`、`/props`、`/slots`）与 Jan 的 localhost:1337 验证了「GUI 与外部调用方共用本地 JSON API」是成熟形态；FR-24 的 JSON 进/JSON 出契约可对齐 OpenAI 兼容风格（工具描述用 JSON schema 表达），未来对话 Agent 直接以客户端身份复用。

**FR-A2 结构化计划生成**
- 两条已验证通路：走 llama-server 时用 `response_format: {"type":"json_schema","schema":...}` 或采样参数 `json_schema`；进程内链 libllama 时移植 `common/json-schema-to-grammar.*`（**反证修正**：实现已移入 .cpp 且依赖 common/trie/unicode，非头文件级，成本上修；亦可直接链接编译产物）。GBNF 的 token 级匹配（`<think>` 等）可用于剥离思考段。

**FR-A5 本地 LLM**
- 模型取用：官方 `Qwen/Qwen3-4B-GGUF`（Apache-2.0，q4_K_M 起步）为默认推荐源；`llama -hf <repo>:<quant>` 官方下载通道存在，但为去 Python 化，FR-8 自管下载更符合红线（llama.cpp 的 `-hf` 依赖其内部实现，镜像切换不透明）。
- 形态：默认 CPU 与 llama.cpp 的定位一致；「可选 OpenAI 兼容云 key」与 Jan 的 Cloud Integration 同构。

**FR-A6/A7 ASR 与 VAD**
- silero-vad ONNX + ORT：与现有引擎架构同构（PIMPL + IStreamEngine），MIT 且无遥测条款，官方有单文件 C++/ORT 流式示例可对照实现（顺带成为 GTCRN 之外的第二个 parity 参照）。
- whisper.cpp：若引入则自带 ggml 运行时与 VAD（`--vad` + ggml-silero），词级时间戳 `-ml 1` 满足**粗粒度**字幕/对齐需求（高精度须 `-dtw` 或外部对齐，见 §反证注记）；与 ORT 路线二选一或并存（解码器复用 FFTranscoder 管线）。

**NFR-M4 单命令出安装包**
- 链路组合（全部一手确认过的部件）：`windeployqt --dir <stage>`（先 `qtenv2.bat`）→ 手工补第三方 DLL（FFmpeg/ORT/SoundTouch，官方明示不在扫描范围）→ CPack `-G NSIS`（CMake 内建）或 Qt IFW（组件化 + 官方更新工作流）。自动更新若要轻量，WinSparkle（MIT，静态 HTTP appcast）与「无遥测」红线兼容（appcast 拉取即更新检查，频度可控）。

---

## 结论分级

**【已确认】**（均有官方文档/源码直接证据）
1. hf_hub_download/snapshot_download 的 revision/patterns/local_dir/dry-run 语义；`.incomplete` + HTTP Range 断点续传；ETag=git-sha1 或 sha256；缓存 `models--<org>--<name>/{blobs,refs,snapshots}` 布局（官方文档 + 源码）。
2. HF_ENDPOINT 机制：huggingface_hub 源码读取该变量拼下载 URL，默认 huggingface.co（constants.py L67-70）；hf-mirror 官网给出四种镜像用法；gated repo 需 HF 官网 token。
3. Ollama 模型目录三平台路径与 `OLLAMA_MODELS` 环境变量（FAQ + 源码）；LM Studio `~/.lmstudio/models/<publisher>/<model>/` 结构、可改目录、`lms import`（官方文档）；sherpa-onnx 模型 HF/ModelScope 双源发布（README）。
4. llama.cpp MIT、纯 C/C++、`llama serve` OpenAI 兼容、`-hf` 直拉 HF 模型（README）。
5. llama.cpp 当前版本 JSON schema 约束解码三条通路：`/completion` 的 `json_schema` 参数、`/v1/chat/completions` 的 `response_format(json_object/json_schema)`、进程内 `json_schema_to_grammar()`（server README + 源码头）。
6. llama-server 内建 `--tools`/`--agent` 七种工具、MCP stdio server 挂载、docker/ssh 工具沙箱、启用即收紧 CORS 到 localhost（server README）。
7. Qwen3 全系 open-weight 模型 Apache-2.0（官方 README 明文）；官方 Qwen3-4B-GGUF 卡 frontmatter `license: apache-2.0`、量化档 q4_K_M~q8_0、`llama-cli -hf ...:Q8_0` 与 `ollama run hf.co/...` 官方用法。
8. unsloth GGUF 卡显式 apache-2.0；bartowski GGUF 卡无独立 license 字段、标注 base_model_relation: quantized（卡面原文）。
9. whisper.cpp MIT；ggml 单文件模型格式；词级时间戳 `-ml 1`；内建 `--vad` 支持 ggml 版 silero-vad（约 864KB）（README）。
10. silero-vad MIT（正文明文 + LICENSE）；ONNX 可纯 ORT 运行（onnxruntime>=1.16.1）；官方 C++/ORT 单文件流式示例；30ms chunk 单线程 <1ms。
11. windeployqt 的行为边界（扫描 Qt 依赖、默认带编译器运行时、第三方库不在范围、--dry-run）；NSIS zlib 许可；CPack 有 NSIS generator；Qt IFW 支持离线/在线安装器与组件更新；WinSparkle MIT、appcast 更新模式。
12. Jan（Apache-2.0）：本地模型 + localhost:1337 OpenAI 兼容 server + MCP agentic 集成 + Privacy First（README）。

**【倾向】**（基于已确认事实的推断或一手信源中的非规范性表述）
1. bartowski 量化包法律上随 Qwen 基模型 Apache-2.0 传导（卡面未自证，仅 metadata 关联）。
2. LM Studio 对 llama.cpp 的链接形态为内嵌动态库（官方文档未说明，据其产品形态推断）。
3. 4B 量化（q4_K_M，约 2.5GB 文件）在主流 8GB+ 内存桌面 CPU 上可交互式运行（由量化体积与 llama.cpp CPU 优化事实推断，未做基准实测）。
4. 打包链路「windeployqt + 手工第三方 DLL + CPack/NSIS 或 IFW + 可选 WinSparkle」可在单命令内串成（各部件官方确认，整链未端到端实测——本环境无 Windows 构建）。
5. IFW 用于产出自有安装器不引入 Qt 应用许可义务（未见反证，未逐字核验 IFW 许可条文）。

**【未确认】**（本次未查到一手证据，禁止引用为依据）
1. 4B 级模型 CPU 推理下**工具调用可靠性**的公开独立评测（官方卡的宣传语不算评测；需另查 BFCL 等榜单原始页面）。
2. Qwen2.5 及更早历史版本的个别许可条款（是否全部 Apache-2.0 需逐仓库核验 frontmatter）。
3. llama.cpp JSON schema → grammar 支持的完整 feature 子集边界（官方指向 tests/test-json-schema-to-grammar.cpp，本次未抓取该测试文件）。
4. 「本地 LLM 出结构化计划 + 确定性执行器」为明确架构的开源桌面项目先例（只找到 reactive tool-calling / MCP 型：Jan、LM Studio、llama-server）。
5. Jan 内部 agent 的计划/执行分工细节（README 未披露架构文档）。
6. sherpa-onnx GTCRN/UVR 引擎的模型目录细节与本项目 parity 对照的具体可行性（属兄弟调研任务范围，本文不展开）。
7. HF 官方环境变量参考页未列 HF_ENDPOINT 的原因（事实已记录，原因未知）。

---

## 参考链接清单

HuggingFace（官方文档经墙内可达时存档；模型卡经 hf-mirror 代理取原文）：
- https://huggingface.co/docs/huggingface_hub/guides/download
- https://huggingface.co/docs/huggingface_hub/package_reference/environment_variables
- https://github.com/huggingface/huggingface/blob/main/src/huggingface_hub/constants.py
- https://github.com/huggingface/huggingface_hub/blob/main/src/huggingface_hub/file_download.py
- https://hf-mirror.com
- https://huggingface.co/Qwen/Qwen3-4B-GGUF
- https://huggingface.co/bartowski/Qwen_Qwen3-4B-GGUF
- https://huggingface.co/unsloth/Qwen3-4B-GGUF

llama.cpp / Qwen：
- https://github.com/ggml-org/llama.cpp
- https://github.com/ggml-org/llama.cpp/blob/master/grammars/README.md
- https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md
- https://github.com/ggml-org/llama.cpp/blob/master/common/json-schema-to-grammar.h
- https://github.com/QwenLM/Qwen3/blob/main/README.md

whisper.cpp / silero-vad / sherpa-onnx：
- https://github.com/ggml-org/whisper.cpp/blob/master/README.md
- https://github.com/snakers4/silero-vad/blob/master/README.md
- https://github.com/snakers4/silero-vad/blob/master/examples/cpp/README.md
- https://github.com/snakers4/silero-vad/blob/master/LICENSE
- https://github.com/k2-fsa/sherpa-onnx/blob/master/README.md
- https://github.com/k2-fsa/sherpa-onnx/blob/master/LICENSE

桌面软件（Jan / LM Studio / Ollama）：
- https://github.com/menloresearch/jan/blob/dev/README.md
- https://lmstudio.ai/docs/app/basics/download-model
- https://lmstudio.ai/docs/app/advanced/import-model
- https://docs.ollama.com/faq
- https://github.com/ollama/ollama/blob/main/envconfig/config.go

Qt / 打包 / 更新：
- https://doc.qt.io/qt-6/windows-deployment.html
- https://doc.qt.io/qtinstallerframework/index.html
- https://nsis.sourceforge.io/License
- https://cmake.org/cmake/help/latest/module/CPackNSIS.html
- https://github.com/vslavik/winsparkle/blob/master/README.md
- https://github.com/vslavik/winsparkle/blob/master/COPYING
