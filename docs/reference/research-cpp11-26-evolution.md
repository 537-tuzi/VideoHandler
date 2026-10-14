# C++11→C++26 语言与标准库特性演进全景（音频/DSP/实时视角）

- 建档：2026-10-14。外部信源访问快照均为 2026-10-14。
- 服务对象：VideoHandler v1.x（Windows x64 / MinGW GCC 13.1 / CMAKE_CXX_STANDARD 20（gnu++20）/ Qt 6.11 / ORT 1.30 / FFmpeg 9）的标准档位与特性采用决策。
- 与既有调研的关系：
  - **GCC 13.1 逐特性可用性明细不在此重复**，见 `docs/reference/research-cpp-engine-patterns.md` §1（含 `<expected>` 头级门控 `__cplusplus > 202002L` 的证据、MinGW atomic::wait condvar 回退结论、§8d 采用/不采用清单）。本文只在其之上做「版本演进全景 + C++26 展望 + 采用路线」。
  - 架构决策背景见 `docs/spec/architecture-review.md` §4.4（v2 接口若用 expected 须升 CXX_STANDARD 23；processInto 定案 bool + span 入参 + 复用出参，错误通道 bool + lastError）。
- 方法与标注体系：一手信源优先（open-std.org WG21 论文原文与工作草案 N 编号、gcc.gnu.org 官方 cxx-status 与 release notes、clang.llvm.org/cxx_status.html、MSVC conformance 官方页、cppreference 支持表交叉核对）。
  - 【已确认】= 已对一手原文核对，或引自本仓库已核实的既有调研（注明出处）。
  - 【倾向】= 多源一致 / 高置信记忆，但本环境未逐行核对原文，合入会议细节可能有偏差。
  - 【未确认】= 本环境核验失败（见 §4），结论给出口径与复核链接，采用决策不依赖此类条目。
- 核验环境如实声明：cppreference `compiler_support/23|26` 两页 WebFetch 均 403，webReader 虽有返回但正文未能留档比对；`clang.llvm.org/cxx_status.html` 抓取时 socket 断开；GCC 16 changes 页取到但要点未及摘录即被截断；MSVC conformance 页未取。凡受此影响的格子在表中就地标注，§4 给复核清单。

## 结论速览

1. **当前 gnu++20 档位下存在一批「已该用而未用」**（span / std::numbers / <bit> / std::format / [[nodiscard]] / optional / string_view / steady_clock 等，§3.1 带行号），全部 GCC 13.1 可用【已确认，engine-patterns §1】，零工具链成本，属纯执行力问题。
2. **升 C++23 的唯一硬收益是 `<expected>`**（头级门控，engine-patterns §1.2 已证实）；但 ranges::to / std::print / flat_map / mdspan 在 GCC 13.1 仍不可用【已确认，同上】——「升标准不升编译器」的收益边际明显收窄。架构 §4.4 预案维持：v2 接口定稿时一并切 gnu++23，不单独为此提前升档。
3. **C++26 对本项目真有分量的是三项**：std::simd（fft-rewrite 向量化正入口）、inplace_vector（RT 固定容量缓冲，NFR-L2 亲缘）、#embed（parity 测试 golden 资产内嵌，P0 债 1 的测试分发形态）。三项落地都需要 GCC 15+（或未落地），且**短期均有等价务实方案**（自动向量化 / 自研环形缓冲 / CMake 生成 C 数组头），不构成任何阻塞。
4. **明确不等**：reflection（4 引擎注册表手写即 20 行，属「少写样板」而非「写出原来写不出的东西」，且 MinGW GCC 落地最晚）、contracts（无编译器稳定实现，assert 先顶）、std::execution（RT 回调内天然禁用、离线编排 Qt 原生已覆盖）、hazard/RCU（静态只读目录无读侧回收问题）、linalg（矩阵计算全在 ONNX 内部）。
5. **升标准（→23）与换工具链（GCC 13.1→15/16）是两个正交决策**。后者被「Qt 官方在线安装器当前只配 MinGW 13.1 二进制」绑定（【倾向】，见 §3.4），换编译器引入 Qt 二进制兼容复核成本，列开放问题、不在本调研内拍板。

---

## 0. 版本坐标系（事实底座）

| 标准 | 俗称 | 出版物 | 关键时点 | 本项目关系 |
|---|---|---|---|---|
| C++11 | — | ISO/IEC 14882:2011 | 2011-09 | 全部现代设施的地基（move/atomic/thread/智能指针） |
| C++14 | — | ISO/IEC 14882:2014 | 2014-12 | 11 的补完（relaxed constexpr / make_unique） |
| C++17 | — | ISO/IEC 14882:2017 | 2017-12 | optional/variant/string_view/if constexpr |
| C++20 | — | ISO/IEC 14882:2020 | 2020-12 | **当前档位**：concepts/span/format/bit/atomic wait/jthread |
| C++23 | — | ISO/IEC 14882:2024 | 2024-10 出版【已确认】 | expected/out_ptr/move_only_function/print(GCC14) |
| C++26 | — | **尚未正式出版**；2025 年各次会议陆续把大件合入工作草案（WD），截至 2026-10 处于 CD/FDIS 流程阶段【倾向：正式 ISO 出版物未见，精确阶段未核，见 §4】 | — | 本文 §2 重点 |

编译器版本坐标（与 C++26 落地表共用）：GCC 13.1（2023-04，本项目锁定）、GCC 14.1（2024-05）、GCC 15.1（2025-04【倾向】）、GCC 16（2026 上半年【倾向：changes 页已存在，内容未留档】）；LLVM/Clang 19（2024 秋）、20（2025-03）、21（2025 秋）、22（2026-03【倾向】）；MSVC 为 VS2022 17.x → VS2026 14.5x 线【未确认：conformance 页未取】。

---

## 1. C++11→C++23 特性筛选（面向音频/DSP/实时/系统的取舍视图）

筛选原则：只列对**本项目**（流式 DSP 引擎、FFmpeg/ORT C 边界、Qt GUI、未来实时线程与 SPSC）有意义者；纯 Web/纯语法糖/无关域略去。GCC 13.1 可用性不在此重复（见 engine-patterns §1 表）。

### 1.1 C++11 — 地基（全部已在用或随时可用）

| 特性 | 一句话本项目意义 |
|---|---|
| move 语义 / 右值引用 | `process()` 返回 `std::vector<float>` 的所有权零拷贝转移全靠它；引擎间缓冲传递的地基 |
| `std::atomic` + memory_order | mediaservice 取消原子量的现役设施；未来 SPSC 队列的核心原语 |
| `std::thread/mutex/condition_variable` | worker 线程知识底座（当前用 QThread，语义同源） |
| `unique_ptr/shared_ptr` | ORT PIMPL `std::unique_ptr<Impl>` 的现役用法 |
| lambda | Qt 信号槽与算法回调用法已规范（现状良好） |
| `constexpr` / `static_assert` | `kCanonicalRate`、`kHop` 等常量纪律（现状良好）；`static_assert` 应更多用于规格断言（如 sizeof 帧结构） |
| `enum class` / `nullptr` / `override/final` | IStreamEngine 接口安全（现状良好） |
| `std::array` | 定长帧/窗缓冲（比 C 数组安全，与 C 数组同布局，可直接喂 C API） |
| `<chrono>` | O3 延迟实测（真机脉冲法）与 ≥2h RSS 复验的计时底座，**应改用 `steady_clock`**（§3.1-9） |
| variadic templates | 注册表 create 工厂签名预留 |

### 1.2 C++14 — 补完

| 特性 | 一句话本项目意义 |
|---|---|
| relaxed constexpr（函数内可写循环/分支） | FFT twiddle 预计算表做 `constexpr` 化的语法前提（fft-rewrite 方向） |
| `std::make_unique` | 已在用（现状良好） |
| 泛型 lambda（`auto` 参数） | 模板胶水减噪 |
| 二进制字面量 `0b` + 数字分隔符 `'` | DSP 掩码/位标志可读性（`0b1111'0000`） |
| return type deduction | 小工具函数减噪，热路径不必 |
| `std::exchange` | move 构造/缓冲交接惯用件 |

### 1.3 C++17 — 接口表达力

| 特性 | 一句话本项目意义 |
|---|---|
| `if constexpr` | 编译期分派标准写法：fft_core 按「2 的幂直通基-2 / 否则 Bluestein」分支、引擎按声道数特化 |
| `std::optional` | ONNX metadata 可缺失参数（sample_rate/n_fft 未声明时）的类型安全表达，**该用未用**（§3.1-8） |
| `std::string_view` | ffutil 扩展名胶水、FR-24 无头命令层参数的零拷贝视图，**该用未用**（§3.1-7） |
| `std::variant` + `visit` | 命令层参数（字符串/数值/路径）类型安全联合，FR-24 落地候选 |
| structured bindings | `auto [frame, plane] = ...` 多返回值解包 |
| inline variables | `data.h` 头文件全局常量的现役写法（`inline constexpr auto kAppTitle`，现状良好） |
| `[[nodiscard]]`（17 起）/ `[[deprecated]]` | 见 §3.1-5：`process()/flush()` 返回音频数据，丢弃即 bug，接口该加 |
| fold expressions | 模板参数包展开减噪 |
| CTAD | `std::array a{...}`、`std::lock_guard lk(m)` 少写模板参 |
| `std::byte` | 字节缓冲语义化（FFmpeg 边界仍以 uint8_t 为界，内部缓冲可用） |
| `shared_mutex` | 配置/注册表读多写少场景（当前规模不需要，记备查） |
| `atomic::is_always_lock_free` | 未来 SPSC/atomic_ref 的前置能力探测（静态断言「无锁」） |

### 1.4 C++20 — 当前档位（本项目的主战场）

| 特性 | 一句话本项目意义 |
|---|---|
| `std::span` | `IStreamEngine::process(const float* in, int64_t frames)` → `std::span<const float>` 的正解；processInto 定案亦以 span 为入参。**头号该用未用**（§3.1-1） |
| designated initializers | NFR-M1 引擎注册表条目 `EngineDesc{.name=..., .create=...}`（gnu++20 下 GCC 13.1 可用【已确认，engine-patterns §8d】） |
| concepts / requires | 注册表 key、SPSC 元素约束；当前规模手写约束收益有限，v2 接口层启用 |
| `<bit>`（bit_ceil / has_single_bit / rotl / countl_zero） | FFT 补零长度对齐、窗长判 2 的幂直读；fft-rewrite 落点。**该用未用**（§3.1-3） |
| `std::bit_cast` | float↔int32 位级重解释的标准件（PCM 格式转换、CRC/哈希）；FFmpeg `uint8_t*` 流式读不适用（保持 memcpy）。GCC 11.1 起【已确认，engine-patterns §1】 |
| `<numbers>`（`pi_v<float/double>`） | `util/fft.h:7` 手写 `kPi` 字面量应换源头常量（且 float 场景应显式 `pi_v<float>` 避免双精度转换）。**该用未用**（§3.1-2） |
| `std::format` | GCC 13.1 且 C++20 模式可用【已确认，engine-patterns §1】；CLI/日志格式化替代 snprintf 拼接 |
| atomic wait/notify | C++20 名义特性，但 **MinGW 下 libstdc++ 走 condvar 池回退而非 futex/WaitOnAddress**，RT 线程连 notify 都不该调【已确认，engine-patterns §1——升 23/26 不改变此结论，是平台债不是标准债】 |
| `jthread` / `stop_token` | GCC 13.1 可用【已确认，同上】；mediaservice 已有 QThread + 自定义取消原子量，迁移收益低（反证：不动） |
| `semaphore` / `latch` / `barrier` | 实时图多线程栅栏的将来件；当前一次一任务用不上 |
| coroutines | Qt 生态与调试器支持弱、堆分配语义不透明，**本项目不采用**【已确认取舍，engine-patterns §8d】 |
| `std::lerp` / `std::midpoint` | 窗函数斜坡、增益插值的语义件（小而正） |
| `[[likely]]/[[unlikely]]` | 热路径分支提示；编译器常忽略，价值有限，仅点缀 |
| `assume_aligned` | DSP 循环对齐提示（配合 SIMD 时有用） |
| constexpr 大扩展 / `consteval` / `constinit` | 常量正确纪律：查表数组 `constinit` 防静态初始化顺序坑 |
| feature testing `<version>` | 跨编译器门控正解（替代手写 `__cplusplus` 判断——注意 libstdc++ 自己的 `<expected>` 恰是反例，见 engine-patterns §1.2） |
| `string(_view)::starts_with/ends_with` | ffutil 扩展名判断直读 |
| `<source_location>` | 日志定位；与 Qt 日志重复，不用（engine-patterns §8d 已定） |
| `chrono` 日历/时区 | 与本项目无关，略 |

### 1.5 C++23 — 增量盘（GCC 13.1 视角的「拿得到 / 拿不到」）

| 特性 | 一句话本项目意义 | GCC 13.1 |
|---|---|---|
| `std::expected` | v2 无头命令层/注册表错误通道（架构 §4.4 定案：不进热路径、不跨 Qt 边界） | 可用（**须升 gnu++23 档位**，头级门控）【已确认】 |
| expected 单子（and_then/or_else/transform） | 错误传播链式化 | 13.1 可用【已确认，engine-patterns §1】 |
| `move_only_function` | 注册表 create 返回独占工厂（比 `std::function` 少一次堆分配语义），签名即「不可拷贝」的表达 | 【倾向：GCC 12 起】 |
| `std::out_ptr` | `unique_ptr` 与 FFmpeg/ORT C 出参（`av_frame_alloc` 式双指针）桥接的标准件 | 【倾向：GCC 12 起】 |
| `std::byteswap` | WAV/RIFF 与跨端 PCM 字节序 | 【倾向：GCC 12 起】 |
| `std::unreachable` | 不可达分支的 UB 明示（辅助优化器 + sanitizer） | 【倾向：GCC 12 起】 |
| `string::contains` | 扩展名/路径判断 | 【倾向：GCC 10 起，20 档位即可用】 |
| monadic optional（`and_then` 等） | optional 链式取值 | 【倾向：GCC 12.1 起】 |
| constexpr `<cmath>`（P0533） | 窗函数/Hann 编译期算表的可行性前提 | 【未确认：GCC 版本未核，§4-4】 |
| `std::mdspan` | MDX 输出 `[b,4,dimF,dimT]` plane-major 的多维视图表达（vocalseparator 修错的读法表达力） | 不可用（GCC 14.1 起【倾向】） |
| `ranges::to` | 离线批处理管道收尾 | 不可用（GCC 14.1）【已确认，engine-patterns §1】 |
| `std::print` | 格式化输出 | 不可用（GCC 14.1）【已确认，同上】 |
| `flat_map/flat_set` | 小型只读映射（后缀→枚举）的缓存友好形态 | 不可用（GCC 15 起【倾向】） |
| `std::generator` | 协程生成器；本项目 coroutine 整体不采用（同 §1.4） | 略 |
| `std::stacktrace` | 诊断；MinGW libbacktrace 链接坑，不依赖【已确认取舍，engine-patterns §8d】 | 略 |
| `start_lifetime_as` | 隐式生存期字节重解释的规范化；与 bit_cast 互补，**音频位级戏法宁可 memcpy（可移植且编译器不可删除）** | 【倾向：GCC 12 起，本项目不建议用】 |
| deducing this | 递归 lambda/CRTP 减噪；本项目无刚需 | 【倾向：GCC 14 起】 |
| `auto(x)` 显式退化拷贝 | 小语义件 | 【倾向：GCC 12 起】 |

---

## 2. C++26 重点展开（本项目视角）

### 2.0 标准状态总述

C++26 的大件在 2023-11 Kona 至 2025 年中历次会议陆续合入 WD【倾向：逐项见下，会场细节未逐一复核】。截至 2026-10-14：**ISO/IEC 14882:2026 正式出版物未见**，流程应处 CD 投票后的 FDIS/出版前夜（C++23 从冻结到出版间隔约 21 个月，可类比）【倾向，精确阶段未核，复核入口见 §4-1】。**「已在 WD/CD」≠「编译器可用」**——C++26 的语言大件（reflection/contracts）落地速度显著慢于 C++20 同期。

### 2.1 语言特性

| 特性（论文） | 标准状态 | 编译器落地（快照 2026-10-14） | 本项目判词 |
|---|---|---|---|
| **静态反射 P2996**（编译期 `^^T`、`std::meta`） | 2025-02 Hagenberg 合入 WD【倾向】 | GCC 15 起初步支持（`-freflection`，实验性）【倾向：GCC 15 changes 宣布】；Clang/MSVC 未发布【未确认：进度未核】 | **不采用**。4 引擎注册表手写 ~20 行；反射是「少写样板」不是「写出原来写不出的东西」；MinGW GCC 落地最晚。远期若 Agent 层做序列化/命令自描述再评估 |
| **契约 P2900**（pre/post/contract_assert + 语义分级） | 2025-06 St. Louis 合入 WD【倾向】 | 三家均无稳定实现；GCC 契约分支在开发、未入发行版【未确认：GCC 16 是否含之未核】；MSVC 仅旧 P0542 时代实验开关 | **不等待**。processInto 前置条件（`in.size() % frame == 0`、RT 上下文声明）正是天然落点，但用 assert/自定义宏（可分级开关）等价顶住；编译器落地前是纸面特性 |
| pack indexing `T...[I]` P2662 | 2023 年合入（Kona/Varna 会场未核【倾向】） | GCC 14【倾向】；Clang 20 前后【未确认】 | 变参模板元编程减噪，本项目变参用法浅，甜头级 |
| placeholder `_` P2169 | 2024 Tokyo 合入【倾向】 | GCC 15± / Clang 21±【未确认：两侧版本均未核】 | 甜头语法，不构成任何决策 |
| `= delete("reason")` P2573 | 2023 Kona 合入【倾向】 | GCC 13 已实现（需 `-std=c++2c` 档位）【倾向】 | 删重载给理由（`process(const std::string&) = delete("音频数据请用 span")` 式防御）；即便 GCC 13.1 有实现，当前 gnu++20 档位拿不到，不折腾 |
| constexpr 异常 P3068 | 2025 年合入【倾向：会场未核】 | 无稳定实现【未确认】 | 与本项目无交集（RT 路径本就禁异常） |
| trivial relocation P2786 | 2025 年合入【倾向】 | 【未确认】；各家实现早期 | `vector<float>` 扩容/搬移从逐元素 move 变 memmove 级——音频大缓冲的理论收益；但库端落地远未完成，`reserve` 足额即可规避重分配，不依赖 |
| structured bindings 引入包 P1061 | 合入【倾向】 | GCC 15 部分【未确认】 | 甜头，略 |
| `static operator[]` P2589 等小件 | 合入【倾向】 | GCC 14【倾向】 | 略 |

### 2.2 库特性

| 特性（论文） | 标准状态 | 编译器落地（快照 2026-10-14） | 本项目判词 |
|---|---|---|---|
| **std::execution（senders/receivers）P2300** | 2024 年合入 WD【倾向：St. Louis/Wrocław 之间，会场未核】 | **无发行版内置**：libstdc++/libc++ 均未 ship；参考实现 stdexec/libunifex【倾向】 | **不等待、不进 RT**。执行器有分配与调度语义保证问题，RT 回调内天然禁用；离线批处理编排 QtConcurrent/QThreadPool 已覆盖；远期 Agent 编排层若标准落地成熟再评 |
| **std::simd P1928** | 2024-03 Tokyo 合入【倾向】 | libstdc++ GCC 15 提供基本支持【倾向：GCC 15 changes 宣布 basic_simd/simd】；libc++ 部分【未确认】；MSVC 无（其自有 `std::experimental::simd` 旧案）【倾向】 | **本项目 C++26 第一优先关注**：fft-rewrite 的 twiddle 预计算与复数蝶形向量化正入口（MDX n_fft=7680、Bluestein 内层 m=16384 的循环热度）。注意：x86-64 上需 `-march` 才有真实收益，MinGW 可用。**短期不等**：`-O3 -ftree-vectorize` + restrict 指针 + 数据布局（AoS 复数→交错数组）先吃自动向量化 |
| **线性代数 P1673（BLAS 风味）** | 2024 Tokyo 合入【倾向】 | libstdc++ 未 ship【倾向】；参考实现 stdblas | **低优先**。矩阵/卷积全在 ONNX Runtime 内部；外层无矩阵需求。RVC 阶段若有显式矩阵再评 |
| **hazard pointers P2530 / RCU P2545** | 2024 Tokyo 合入【倾向】 | libstdc++/libc++ 均未 ship；Boost 有等价物【倾向】 | **无关**（反证要明说）：引擎注册表是启动期写、运行期只读的静态目录，读侧零同步成本，不存在读侧回收问题；SPSC 用顺序缓存行 + atomic 即可。这两件服务的是「高频动态增删的读多写少索引」，本项目没有这个形态 |
| **饱和算术 P0543（`std::add_sat` 等）** | 2023 Kona 合入【倾向】 | 未观测到 libstdc++ ship【未确认】 | PCM 求和/软限幅的语义件。短期等价物：`__builtin_add_overflow`、显式 clamp、或 `sdlimm` 式饱和内联（都已是成熟写法）。落不落地不影响架构 |
| **#embed P1967** | 2023 Kona 合入【倾向】 | Clang 19+【倾向】；GCC 16 是否已含【未确认：GCC 16 changes 未留档】；MSVC【未确认】 | **有真实用武之地**：parity 测试（P0 债 1）的 golden PCM/频谱数据可嵌进测试二进制，免去 fixtures 分发路径问题。但当前工具链 GCC 13.1 无；短期 CMake `configure_file`/脚本生成 `const unsigned char arr[]` 头完全等价——**诚实反证：xxd 式方案已够用，#embed 只是更优雅** |
| **inplace_vector P0843** | 2025-02 Hagenberg 合入【倾向】 | libstdc++ 未 ship（GCC 16 状态【未确认】） | **NFR-L2 亲缘**：固定容量、无堆分配、栈/成员内联的 vector——正是 RT 线程固定大小音频块缓冲的标准形态。短期自研 ringbuffer/定长数组即可；等它落地是「锦上添花」而非「等米下锅」 |
| hardened stdlib P3471 | 2025 年合入【倾向】 | libc++ 有实践（`-D_LIBCPP_HARDENING_MODE`）；libstdc++ 走自己的 `_GLIBCXX_ASSERTIONS` 宏线【倾向】 | 测试期开断言、发布关闭——**当下就用 `_GLIBCXX_ASSERTIONS`（GCC 13.1 已支持，非 C++26 物）**，不构成升级理由 |
| `function_ref` P0792 | 合入【倾向：会场未核】 | 【未确认：libstdc++ 版本未核】 | 非拥有回调视图（无堆、比 function 轻），引擎回调参数的轻量件；短期手写模板参数即可 |
| `submdspan` P2637 | 合入【倾向】 | 【未确认】 | 依赖 mdspan（GCC 14），远期 MDX 张量切片表达；略 |
| 调试支持 P2546 / `text_encoding` 等 | 合入 | 各家早期 | 与本项目无关，略 |

### 2.3 C++26 编译器落地快照表（汇总）

| | GCC 14.1 (2024-05) | GCC 15.1 (2025-04) | GCC 16 (2026) | Clang 19→22 | MSVC (VS2026 线) |
|---|---|---|---|---|---|
| 语言：pack indexing / delete(reason) / static operator[] | 有【倾向】 | 有 | 有 | 部分【未确认】 | 【未确认】 |
| 语言：placeholder `_` | 【未确认】 | 【倾向：有】 | 有 | 【未确认】 | 【未确认】 |
| 语言：**reflection P2996** | 无 | **实验（-freflection）**【倾向】 | 增强【未确认】 | 未发布【未确认】 | 无【倾向】 |
| 语言：**contracts P2900** | 无 | 无 | 【未确认】 | 无【倾向】 | 旧实验开关【倾向】 |
| 库：print / ranges::to / mdspan / generator | 有【倾向，engine-patterns 佐证前两项已确认】 | 有 | 有 | 部分 | 部分【未确认】 |
| 库：flat_map | 无 | 有【倾向】 | 有 | 【未确认】 | 【未确认】 |
| 库：**std::simd** | 无 | **有（基本）**【倾向】 | 增强【未确认】 | 部分【未确认】 | 无（另有实验 SIMD 库）【倾向】 |
| 库：execution / hazard / RCU / linalg / inplace_vector / add_sat | 无【倾向】 | 无【倾向】 | 【未确认】 | 无【倾向】 | 无【倾向】 |

本表快照 2026-10-14；【未确认】格的复核入口统一在 §4。**对本项目的可操作结论：MinGW GCC 13.1 与「C++26 任何实质件」之间隔着至少两级工具链（14→15）**，而工具链升级被 Qt 官方配套绑定（§3.4）。

---

## 3. 采用路线建议（本项目视角）

### 3.1 C++20 档位下「已该用而未用」清单（对照 src/ 抽查，2026-10-14）

现状基线：`src/` 标准库头仅见 `<memory> <cmath> <vector> <atomic> <array> <utility>`——**C++17/20 的表达力库存基本没动用**。以下按优先序（全部 GCC 13.1 + gnu++20 可用【已确认，engine-patterns §1】）：

1. **`std::span<const float>` 收编 IStreamEngine 签名**：`src/service/streamengine.h:13-14` 的 `process(const float* in, int64_t frames)` 裸指针+尺寸正是 span 的教科书场景；v2 `processInto(span 入参)` 定案（架构 §4.4）顺路完成。收益：长度不可分离、不可误传、const 正确。
2. **`std::numbers::pi_v<double>` 换掉手写常量**：`src/util/fft.h:7` `kPi = 3.14159265358979323846`（现值本身无精度错，但源头应标准化；float 场景显式 `pi_v<float>`）。fft-rewrite 时一并做。
3. **`<bit>` 换手写位判断**：fft_core 的「2 的幂」判断与补零对齐用 `has_single_bit/bit_ceil`；与 fft-rewrite 的 twiddle 预计算同一批改。
4. **`std::format` 收编字符串拼装**：CLI 输出/错误消息/未来日志（GCC 13.1 C++20 模式可用【已确认】）；顺带 `-Wformat` 类风险清零。
5. **`[[nodiscard]]` 上接口**：`process()/flush()` 返回音频数据，丢弃即静默丢数据——IStreamEngine 两方法加注即可（一次改动，永久收益）。
6. **designated initializers 进注册表**：NFR-M1 `EngineDesc` 落地时用 `.name/.create` 显式字段（gnu++20 可用【已确认】）。
7. **`std::string_view` + starts_with/ends_with**：`src/util/ffutil.h` 扩展名胶水、FR-24 命令行参数（不拷贝、不越界）。
8. **`std::optional`**：aidenoiser 从 ONNX metadata 读参数的「可缺失」语义（现在散布 bool 出参/默认值）。
9. **`std::chrono::steady_clock`**：O3 延迟实测工具（真机脉冲法）与 R12 ≥2h RSS 复验脚本的计时件——落地这两个验证时自然引入。
10. **`static_assert` 规格断言**：帧结构大小、`kRecvFrames` 与引擎 hop 的整除关系等编译期钉死。
11. （可选）`std::lerp/midpoint`：窗斜坡/增益插值处顺手。
12. （备查）`pmr::monotonic_buffer_resource`：RT 线程预分配 arena 的标准库形态——NFR-L2 实施时与自研环形缓冲二选一评估。
13. （备查）`atomic_ref`：SPSC 共享载荷原子视图；**注意 MinGW atomic wait 的 condvar 回退结论**（engine-patterns §1）——等待语义走 Win32 事件/semaphore 更稳，notify 永不出现在 RT 线程。
14. **不迁**：jthread/stop_token（mediaservice QThread+原子取消在役，迁移是纯风险）；coroutines（已定不采用）；source_location（与 Qt 日志重复）。

落地窗口建议：1/5 随 v2 processInto 定稿一起；2/3 随 fft-rewrite；4/7/8 随 FR-24 无头命令层；9 随 O3/R12 验证任务。**不另开「标准库现代化」专项**——避免无行为收益的纯扫荡（工作原则：代码精确编辑）。

### 3.2 升 C++23 的收益/成本（MinGW GCC 13.1 语境）

- **硬事实**（引用 engine-patterns §1.2，已确认）：`<expected>` 整头在 libstdc++ 门控 `__cplusplus > 202002L`——**升档是拿 expected 的唯一路径**；GCC 13.1 支持 `-std=gnu++23` 且 expected/单子在 13.1 可用。
- **收益盘点（GCC 13.1 下真拿得到的）**：expected + 单子（v2 错误通道，架构定案的用武点）、move_only_function（注册表独占工厂签名）、out_ptr（FFmpeg C 出参桥）、byteswap/unreachable/monadic optional（小件）。**拿不到的**：ranges::to、std::print、flat_map、mdspan——这四件要 GCC 14/15【已确认/倾向，engine-patterns §1】。净结论：13.1 上升 23 的增量 ≈ expected 一件大件 + 三件小件。
- **成本与风险**：Qt 6.11 对 C++23 档位无已知不兼容【未确认：需 Windows 一次全量冒烟再定】；FFmpeg/ORT/SoundTouch 均 C 头，不受档位影响；MinGW 运行时（winpthreads/libstdc++ dll）与档位无关的既有结论不变（atomic wait 回退是平台债）。
- **建议**：维持架构 §4.4 预案——**v2 接口（注册表 + 无头命令层）定稿时一并切 gnu++23**，切档同轮跑全量 CLI 冒烟；不提前、不为 expected 单独升档（当前 bool + lastError 通道对四引擎规模够用）。

### 3.3 C++26 值得等什么（RT/SPSC/audio 视角）

**跟进名单（按权重）**：
1. **std::simd**（fft-rewrite 的二期）：GCC 15+ 后的标准入口。短期路径已定：自动向量化 + 布局优化先做，`std::simd` 作为重写验收后的第二阶段（届时需连带 `-march` 决策与 MinGW 验证）。
2. **inplace_vector**（RT 缓冲标准化）：与 NFR-L2 同型；落地后替换自研定长容器是局部小改，非架构依赖。
3. **#embed**（parity golden 内嵌）：P0 债 1 建测试时先用生成式方案，GCC≥16 核实后再优雅化。
4. **contracts**（远期）：processInto 前置条件的标准形态；编译器落地前 assert/宏顶住。

**明确不等（反证）**：reflection（§2.1 判词）、std::execution（§2.2 判词）、hazard/RCU（§2.2 判词——本项目无动态读多写少索引）、linalg（矩阵在 ORT 内部）、placeholder/_/包索引等语法甜头、trivial relocation（reserve 足额即规避）。

**触发器式重估**（何时回头再看 C++26）：①Qt 官方 MinGW 工具链换代（→simd/flat_map 一次性到手）；②实时化立项（→inplace_vector/execution 重估）；③parity 测试资产定型（→#embed 重估）。

### 3.4 工具链升级（GCC 13.1→15/16）——正交开放问题

换 MinGW GCC 15/16 可一次性拿 std::simd/flat_map/print/ranges::to/mdspan 与 13.x 系 bug 修复；但【倾向，未在本环境核验】：Qt 6.11 官方在线安装器当前预编译二进制按 MinGW 13.1 配套发布，自换工具链意味着 Qt 需自编译或混用，ABI/运行时 DLL 组合进入自担风险区。**这是工具链供应链决策而非标准决策**，超出本调研职权，列为开放问题交架构评审（与 NFR-M4 消灭硬编码路径的窗口合并考虑）。

---

## 4. 未收敛点与盲点（如实声明）

1. **C++26 精确流程阶段未核**：正式 CD/FDIS 投票与出版时点未取到 open-std 官方状态原文（papers/2026 目录页已取但未及比对 N 编号）。复核入口：`https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/` 与 `https://wg21.link/std`。「2026-10 尚未出版」为多源拼合【倾向】。
2. **编译器落地表多处【未确认】**：cppreference `compiler_support/23|26` 两页 WebFetch 403、webReader 正文未留档；`clang.llvm.org/cxx_status.html` socket 断；GCC 16 changes 页要点未摘录；MSVC conformance 页未取。受影响的格子集中在：Clang 各版本 C++26 细目、GCC 16 增量、MSVC 全列、placeholder_/constexpr cmath/GCC13 版本号。复核入口：`https://gcc.gnu.org/gcc-16/changes.html`、`https://gcc.gnu.org/projects/cxx-status.html`、`https://gcc.gnu.org/onlinedocs/libstdc++/manual/status.html`、`https://clang.llvm.org/cxx_status.html`、`https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance`、`https://en.cppreference.com/w/cpp/compiler_support/26`。
3. **合入会场/论 文修订号**：P2300（St. Louis vs Wrocław）、P2662（Varna vs Kona）等会场归属按【倾向】记，未逐篇开 P 原文核对；本文采用决策不依赖这些细节。
4. **零散 GCC 版本号**：out_ptr/byteswap/move_only_function（按记忆 GCC 12）、constexpr cmath、function_ref 落地版本未核——若 §3.2 采用清单动工前，先在 Windows 工具链上用 `<version>` 特性测试宏实测（`__cpp_lib_out_ptr` 等），比查表更硬。
5. **本文不改代码**：§3.1 清单全部为建议与行号证据，未动 `src/`；与 CLAUDE.md P0 债排序不冲突（正确性兜底先行，现代化随任务落地）。

## 5. 参考链接清单（一手信源）

标准与论文（open-std.org / wg21.link）：
- C++26 工作草案与 CD 状态：https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2026/ ；https://wg21.link/std （快照 2026-10-14）
- P2996 Reflection：https://wg21.link/p2996 ；P2900 Contracts：https://wg21.link/p2900 ；P2300 std::execution：https://wg21.link/p2300
- P1928 simd：https://wg21.link/p1928 ；P1673 linalg：https://wg21.link/p1673 ；P2530 hazard pointers：https://wg21.link/p2530 ；P2545 RCU：https://wg21.link/p2545
- P0543 saturation：https://wg21.link/p0543 ；P1967 #embed：https://wg21.link/p1967 ；P2662 pack indexing：https://wg21.link/p2662 ；P2169 placeholder：https://wg21.link/p2169 ；P2573 delete with reason：https://wg21.link/p2573
- P3068 constexpr exceptions：https://wg21.link/p3068 ；P2786 trivial relocation：https://wg21.link/p2786 ；P0843 inplace_vector：https://wg21.link/p0843 ；P3471 hardened stdlib：https://wg21.link/p3471 ；P0792 function_ref：https://wg21.link/p0792
- C++23 出版物 ISO/IEC 14882:2024（2024-10）；C++20 ISO/IEC 14882:2020

编译器官方：
- GCC C++ 支持状态：https://gcc.gnu.org/projects/cxx-status.html
- GCC 14/15/16 changes：https://gcc.gnu.org/gcc-14/changes.html ；https://gcc.gnu.org/gcc-15/changes.html ；https://gcc.gnu.org/gcc-16/changes.html
- libstdc++ 状态页（库特性表）：https://gcc.gnu.org/onlinedocs/libstdc++/manual/status.html
- Clang C++ 状态：https://clang.llvm.org/cxx_status.html ；libc++ C++26 状态：https://libcxx.llvm.org/Status/Cxx26.html
- MSVC 语言符合性：https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance

交叉核对（本环境部分 403，标注见 §4）：
- https://en.cppreference.com/w/cpp/compiler_support/26 ；https://en.cppreference.com/w/cpp/compiler_support/23 ；https://en.cppreference.com/w/cpp/26

本仓库关联：
- `docs/reference/research-cpp-engine-patterns.md`（GCC 13.1 可用表、expected 门控、MinGW atomic wait、§8d 采用清单）
- `docs/spec/architecture-review.md` §4.4（v2 接口与 C++ 标准决策）
- `docs/spec/fft-rewrite.md`（std::simd 的落点）；`docs/REQUIREMENTS.md`（NFR-L2/P4、FR-24、O3）
