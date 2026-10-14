# C++ 语言特性核验与引擎注册表/命令层设计模式调研

- 建立：2026-10-14
- 任务：架构审视外部调研——①MinGW GCC 13.1 工具链的 C++20/23 特性可用性双源核验；②引擎注册表（NFR-M1）与无头命令层（FR-24）的设计模式一手先例（LLVM/OBS/Qt/VST3）；③PIMPL 真实成本、热路径虚调用、std::function 分配陷阱、错误处理统一策略、CMake 现代约定的一手取证
- 服务落点：`docs/spec/architecture-review.md`（架构审视主文档）、**NFR-M1**（引擎注册表）、**FR-24**（无头命令层）、NFR-L2（processInto 零分配）、NFR-M4（构建硬编码债）、NFR-L*（实时化）
- 方法：先逐行精读本仓库源码（引用带行号），外部只取一手信源（GCC 官方 release notes / libstdc++ 官方 status 页与 GCC 13 分支源码 / cppreference 编译器支持表 / LLVM·OBS·Qt·Steinberg 官方源码 / GotW 原文 / Agner Fog 微架构手册 / CMake·Qt 官方文档），逐条标注【已确认】【倾向】【未确认】
- 结论速览：
  1. GCC 13.1 + `-std=c++2b/c++23` 可用且推荐：`<expected>`（含单子操作）、`<span>`、jthread/stop_token、atomic wait/notify、`<ranges>`、`bit_cast`、`<format>` 全部就绪；**`std::expected` 仅在 C++23 模式编译时可用**（libstdc++ 用 `__cplusplus > 202002L` 门控整头），`ranges::to`/`std::print`/`flat_map` 在 GCC 14/15 才有，不可用。
  2. 注册表选**显式组合根注册**（LLVM `InitializePasses.h` 聚合式），否决静态自注册（初始化顺序 + 死链丢对象，LLVM 需 `ForcePassLinking` getenv 妙招兜底）与 CMake 生成清单（对 4 引擎单二进制是过度设计）。
  3. 虚调用在本仓库调用粒度（每秒音频块一次）下开销可忽略（误预测惩罚 Nehalem 实测 ≥17 周期，但单态调用点预测命中 ≈ 免费），不需要 CRTP。
  4. `std::function` SBO 阈值（libstdc++/x86-64）：平凡可复制 + ≤16B + 对齐 ≤8 → 栈内；本仓库 `[this]` 捕获 8B 走栈内，离线路径无分配；实时路径仍建议免 `std::function`。
  5. CMake 正规修法：机器路径迁入 gitignore 的 `CMakeUserPresets.json`（隐式 include `CMakePresets.json`）；顺带发现 presets version 6 要求 CMake ≥3.25 与 `cmake_minimum_required(3.21)` 不一致。

---

## 0. 仓库现状精读（引用基线）

### 0.1 引擎接口与实现

- `src/service/streamengine.h`：L7 `class IStreamEngine`；L10 虚析构 `= default`；L11–12 `inRate()/inChannels()` 纯虚；**L13 `virtual std::vector<float> process(const float* in, int64_t frames) = 0;`（每次堆分配返回——NFR-L2 processInto 的目标）；L14 `flush()`；L15 `lastError()` 默认返回空（事实上的错误通道）**。
- `src/service/dspdenoiser.h`：无 PIMPL（不依赖重头文件）；L10 `setup(int sampleRate, int channels, bool removeHum, bool removeWind)`；L14 `flush()` 返回空（无内部缓冲）；L16–20 `struct Biquad`，**L19 `float run(float x, int ch)` 是普通成员函数（非虚）——逐样本内循环无虚调用**。
- `src/service/aidenoiser.h`：PIMPL L20–21 `struct Impl; std::unique_ptr<Impl> impl_;`；L13 `setup(const QString& modelPath, QString* err)`（bool+出参错误）；L16 `inChannels() = 1`。
- `src/service/pitchshifter.h`：PIMPL L19–20；L13 `setup(int, int, double, QString*)`。
- `src/service/vocalseparator.h`：PIMPL L22–23；L14 `setInstrumental(bool)`；L17 非虚 `lastError()` 遮蔽基类同签名虚函数（L15 基类版有默认实现，此遮蔽无多态意义但语义一致）。

### 0.2 引擎创建与分发：两处重复的 if/else

- GUI 路径 `src/service/ffextractor.cpp`：L223 `doDenoiseWav`（栈上 `DspDenoiser`，按 `kCanonicalRate/kCanonicalChannels` setup）；L229–242 `doDenoiseAiWav`（**L230 模型路径 = `QCoreApplication::applicationDirPath() + "/" + kAiModelRelPath`**，不存在则 jobDone(false)）；L244–252 `doPitchWav`；L254–269 `doSepVocals`（**L255 同样的 exe 相对模型路径** + `setInstrumental`）。
- CLI 路径 `src/main.cpp`：L18–108 `run_cli`，对 `mode` 字符串 if/else（L23 `--extract-video`、L24 `--transcode-wav`、L33 `--denoise-wav`、L42 `--ai-denoise`、L61 `--pitch-wav`、L80 `--sep-vocals|--sep-instrumental`、L100 默认 extractAudio）；**L48/L86 CLI 模型默认路径是 `third_party/models/...`，与 GUI 的 exe 相对路径解析不一致**（同一引擎两套默认）。L110–112 `argc>=3` 进 CLI。
- 两处都在做「字符串 → 构造引擎 → setup 传参 → 交给 FFTranscoder」，这正是 NFR-M1 注册表 + FR-24 命令层要吸收的重复。

### 0.3 调度与转码通路

- `src/service/mediaservice.h`：L20 busy 锁、L21 `std::atomic<bool> cancelFlag`；QThread + moveToThread，每任务一个信号槽对；引擎不在这一层创建。
- `src/service/fftranscoder.h`：**L13 `using ProgressFn = std::function<void(int)>;`**（L16 常引用传递 + 默认空）。
- `src/service/fftranscoder.cpp`：L120–121 读 `engine.inChannels()`、`bypassOut` 直通判定；**L154 `const int chunk = engine.inRate();`（按秒分块——process() 调用粒度 ≈ 每秒音频一次）**；L193–194、L249 process 调用、L253 flush 调用。
- `src/service/ffextractor.cpp` L70–91：`copy_stream` 持 `const std::function<void(int)>&`，L91 `onProgress(...)`；GUI 侧 lambda 只捕获 `[this]`（`emit progress`）。
- FFmpeg 胶水零泄漏风格：ffextractor.cpp 匿名命名空间 L21–115（open_input/open_output/write_header/copy_stream/finish，do{}while(false) + 集中释放 + 失败删半成品）；`src/util/ffutil.h` L11–27 `audio_ext`、L30–34 `ff_err`（av_strerror → QString）。

### 0.4 常量与构建

- `src/model/data.h`：L7 `kCanonicalRate=44100`、L8 `kCanonicalChannels=2`、L11 `kAiModelRelPath`、L15 `kSepModelRelPath`、L5/6/9/10/12/13/14 输出后缀。
- `CMakeLists.txt`：L1 `cmake_minimum_required(VERSION 3.21)`；**L5–6 `CMAKE_CXX_STANDARD 20`（REQUIRED ON）**、EXTENSIONS ON；L8 AUTOMOC；L13–37 显式源文件清单（含头文件）；L39–44 硬编码 third_party include；L46–54 硬编码静态库 + **L51 mingw ld 直连 `swresample-7.dll`**；L60–64 `-Wall -Wextra -fno-keep-inline-dllexport -mthreads`；L72–75 输出目录；L77–97 POST_BUILD 拷 DLL/模型；**L100–101 `set(QT_BIN "D:/qt6/6.11.1/mingw_64")` / `set(MINGW_BIN "D:/qt6/Tools/mingw1310_64/bin")`（NFR-M4 债）**。
- `CMakePresets.json`：version 6；mingw-release/debug：Ninja、`binaryDir ${sourceDir}/build[-dbg]`、硬编码 `CMAKE_PREFIX_PATH "D:/qt6/6.11.1/mingw_64"`、`CMAKE_CXX_COMPILER "D:/qt6/Tools/mingw1310_64/bin/g++.exe"`、`CMAKE_MAKE_PROGRAM "D:/qt6/Tools/Ninja/ninja.exe"`。

---

## 1. GCC 13.1（MinGW）C++20/23 特性可用性【双源核验】

双源：①GCC 官方 libstdc++ Implementation Status（status 页，标注「首个包含实现的 GCC 版本」）+ GCC 12/13 release notes；②cppreference 编译器支持表（C++23 表已核；C++20 表部分行已核）。二者一致处标【已确认】，口径差异处注明。

### 1.1 库特性 → GCC 版本

| 特性 | 文档 | libstdc++ status 页【已确认】 | cppreference 表 | GCC 13.1 可用？ |
|---|---|---|---|---|
| `std::expected`（P0323） | `<expected>` | **12.1**（`__cpp_lib_expected 202202L`） | 12【已确认】 | ✅ |
| expected 单子操作 and_then/or_else/transform（P2505） | `<expected>` | **13.1**（`202211L`） | 13【已确认】 | ✅ |
| `std::span`（P0122） | `<span>` | 10.1 | 10【已确认】 | ✅ |
| `std::jthread`/`stop_token`（P0660） | `<thread>` | 10.1 | 10【已确认】 | ✅ |
| atomic wait/notify + `semaphore`/`latch`/`barrier`（P1135） | `<atomic>` 等 | **11.1** | 10（与 status 页差一档：cppreference 按合入 trunk 版本计，status 页按首个发布版本计；对 13.1 结论无影响）【已确认】 | ✅ |
| ranges 基础库（P0896） | `<ranges>` | 10.1 | 10【已确认】 | ✅ |
| `std::bit_cast`（P0476） | `<bit>` | 11.1 | 11【已确认】 | ✅ |
| `std::format`（P0645，含 P2216 编译期串检查/P2508 basic_format_string） | `<format>` | 13.1 | 13【已确认】 | ✅ |
| zip/slide/chunk/enumerate 等视图（P2321/P2442 等） | `<ranges>` | 13.1（GCC 13 release notes 明列） | 13【已确认】 | ✅ |
| `std::move_only_function` | `<functional>` | 12（GCC 12 release notes） | 12 | ✅ |
| `ranges::to`（P1206） | `<ranges>` | 14.1（容器成员 15.1） | 14 partial | ❌ |
| `std::print`（P2093） | `<print>` | 14.1 | 14 | ❌ |
| `flat_map/flat_set` | `<flat_map>` 等 | 未查到该行（cppreference：15，单源） | 15【已确认·单源】 | ❌ |
| `<stacktrace>` | — | 12 partial（GCC 12 release notes：「not built by default, requires linking to an extra library」；13.2 归入 `-lstdc++exp`/libbacktrace） | 12 partial/14 full | ⚠ MinGW 上 libbacktrace 支持存疑，不建议依赖【未确认】 |
| `std::chrono` | `<chrono>` | C++11 起全量；GCC 13 增 utc_clock/时区/format 支持（release notes） | — | ✅（精度用 `steady_clock` + `duration<double,nano>`，无版本障碍） |

GCC 13 release notes 原文（C++23 部分）【已确认】：「Monadic operations for `std::expected`.」「Additions to the `<ranges>` header: views::zip … views::enumerate」；C++20 部分：「`<format>` header and `std::format.`」。

### 1.2 方言开关与默认值

- GCC 13 手册（C Dialect Options，13.2.0 文档）【已确认】：`-std=c++2b` 与 `-std=c++23`、`-std=gnu++2b` 与 `-std=gnu++23` 均列为同一档「The next revision of the ISO C++ standard, planned for 2023. Support is highly experimental」；**C++ 默认方言是 gnu++17**——不用 `-std` 就什么 C++20/23 都没有。
- **`<expected>` 的模式门控（决定性）【已确认】**：libstdc++ 13 分支 `include/std/expected` 整头包在 `#if __cplusplus > 202002L && __cpp_concepts >= 202002L` 里。即 `-std=c++20`（`__cplusplus == 202002L`）时该头是空的，**必须 `-std=c++23/c++2b` 才能包含**。本仓库当前 `CMakeLists.txt` L5 是 `CMAKE_CXX_STANDARD 20` → 想用 `std::expected` 必须升 23。
- MinGW 特有：libstdc++ 13 分支 `bits/atomic_wait.h` 中只有 `_GLIBCXX_HAVE_LINUX_FUTEX` 会推导出平台等待（`#define _GLIBCXX_HAVE_PLATFORM_WAIT 1`）；否则落入 `__waiter_pool_base`（16 桶 mutex+condvar 哈希池）。`config/os/mingw32/os_defines.h` 无任何 PLATFORM_WAIT/WaitOnAddress 定义【已确认，源码核验】→ **MinGW 上 `atomic::wait/notify` 功能可用但走 condvar 池回退路径，非 futex/WaitOnAddress 原生路径**（对离线管线无影响；实时化（NFR-L*）若用 atomic wait 做唤醒，延迟与惊群行为要按 condvar 池预期【倾向】）。

### 1.3 结论：「GCC 13.1 + -std=c++2b 可不可以用」

**可以，且建议升**。判定：目标特性里全部「必须项」（expected 单子、span、bit_cast、format）在 13.1 已定；`ranges::to/print/flat_map` 明确不可用（14/15），代码里不要出现。风险面：
- 官方文档对 C++23 标注「highly experimental」——但 `expected` 自 12.1、单子自 13.1 已带 `__cpp_lib_expected 202211L` 特征宏，属已定案 ABI【已确认】；
- 本仓库全部 C++23 需求集中在 `std::expected`（命令层错误通道），不触及 deducing this/静态 operator() 等 14 才齐的语言特性；
- 第三方头（ORT/SoundTouch/FFmpeg C 头）不受标准模式影响。

---

## 2. 引擎注册表先例（≥3 个一手开源先例）

### 2.1 LLVM Pass Registry——静态自注册 + 显式初始化**并存**的双轨【已确认，源码核验】

- **静态自注册**：`llvm/include/llvm/PassSupport.h` 提供 `template <typename passName> struct RegisterPass : public PassInfo`，其构造函数立即 `PassRegistry::getPassRegistry()->registerPass(*this);`，预期用法是 .cpp 文件全局 `static RegisterPass<YourPass> tmp("passopt", "My Pass Name");`；类注释明言不注册则 opt「will not be able to see the pass」。
- **显式初始化轨**：同文件 `INITIALIZE_PASS(...)` 宏生成 `void llvm::initializeXxxPass(PassRegistry&)`，内部 `llvm::call_once` 保证幂等；`llvm/include/llvm/InitializePasses.h` 顶部注释「This file contains the declarations for the pass initialization routines for the entire LLVM project」，约 400+ 条 `void initializeXxxPass(PassRegistry&);` 声明 + 按库分组的 `initializeScalarOpts`/`initializeCodeGen` 等聚合力——**由工具（组合根）显式调用**。
- **静态链接坑的铁证**：`llvm/include/llvm/LinkAllPasses.h` 用全局 `ForcePassLinking` 对象引用全部 pass 的 create 函数防止被裁，注释原话：「We must reference the passes in such a way that compilers will not delete it all as dead code, even with whole program optimization, yet is effectively a NO-OP. As the compiler isn't smart enough to know that getenv() never returns -1, this will do the job.」（守卫是 `if (std::getenv("bar") != (char*) -1) return;`）——静态自注册在静态链接下**必须**配这类 hack，否则对象文件整块被链接器丢弃。
- 坑清单：①静态初始化顺序不可控（fiasco）；②静态链接丢对象（上）；③测试无法只注册子集（注册是全局副作用）；④LLVM 最终给工具推荐的是显式 InitializePasses 轨。

### 2.2 OBS 插件模块——动态库入口约定【已确认，源码核验】

`libobs/obs-module.h`：`OBS_DECLARE_MODULE()` 展开为 `static obs_module_t *obs_module_pointer;` + `MODULE_EXPORT void obs_module_set_pointer(obs_module_t*)` + `obs_current_module()` + `obs_module_ver()`；`#define MODULE_EXPORT extern "C" EXPORT`——C 链接导出，宿主按名 `GetProcAddress/dlsym`；必需入口 `MODULE_EXPORT bool obs_module_load(void);`，文档注释「Required: Called when the module is loaded.」并要求在其中「load all the sources/encoders/outputs/services for your module」，返回 false 触发卸载。坑：跨 DLL 边界、符号名即契约（无版本内省）、适合「第三方编插件」生态，不适合进程内固定 4 引擎。

### 2.3 Qt 插件系统——元数据驱动的动态/静态双模【已确认，官方文档核验】

- 声明：插件类 `Q_PLUGIN_METADATA(IID "…iid…" FILE "jsonviewer.json")` + `Q_INTERFACES`；接口侧 `Q_DECLARE_INTERFACE(ViewerInterface, ViewerInterface_iid)`；JSON 元数据如 `{ "Keys": [ "jsonviewer" ] }`。
- 动态加载：`QPluginLoader loader(path); QObject* plugin = loader.instance();` + `qobject_cast` 测接口；静态插件枚举 `QPluginLoader::staticInstances()`。
- 静态链接：官方文档明确「`Q_IMPORT_PLUGIN()` macros are also needed in application code」，但「automatically generated by the build system and added to your application project」，不走 CMake 时需手写且定义 `QT_STATICPLUGIN`——即 Qt 静态插件 = **构建系统生成的显式注册**（不是运行时自注册）。
- 评价：`Keys` 元数据可在不加载代码的情况下被宿主读走（QPluginLoader::metaData），是四个先例中内省能力最强的；代价是拉入 QObject/moc 元对象机制。本仓库 4 个引擎全是进程内编译期已知类型，用 QObject 元对象换内省属于高射炮打蚊子。

### 2.4 VST3 模块入口——宿主驱动的显式生命周期【已确认部分 + 倾向部分】

`public.sdk/source/main/dllmain.cpp`（vst3_public_sdk 仓库）【已确认】：Windows DLL 导出 `extern "C" bool InitDll()`/`bool ExitDll()`，用 `static int moduleCounter{0}; // counting for InitDll/ExitDll pairs` 做引用计数，首次 `InitModule()`/末次 `DeinitModule()`；头注释「must be called from host right after loading dll」「this could be called more than one time!」。`GetPluginFactory`（IPluginFactory 单例、宿主经 LoadLibrary+GetProcAddress 取工厂再枚举类）定义在 SDK 其他文件，本调研未取到该文件源码【倾向：入口契约以上述 dllmain 注释为证；工厂细节未逐行核验，不影响本仓库结论】。

### 2.5 对比与选型

| 先例 | 注册时机 | 静态链接坑 | 可测试性 | 对本仓库适配度 |
|---|---|---|---|---|
| LLVM RegisterPass（静态自注册） | 静态初始化 | **有**（需 LinkAllPasses hack） | 差（全局副作用） | 低——单二进制静态链接正是坑高发区 |
| LLVM InitializePasses（显式聚合） | 组合根调用 | 无 | 好（可只注册子集/注入假工厂） | **高** |
| OBS obs_module_load | 宿主加载 DLL 时 | 不适用（动态库） | 中 | 低（无插件生态需求） |
| Qt QPluginLoader/Q_IMPORT_PLUGIN | 动态扫描/构建系统生成 | Q_IMPORT_PLUGIN 由构建系统生成（已是显式轨） | 好 | 中（元数据思路可借鉴，机制过重） |
| VST3 InitDll/GetPluginFactory | 宿主显式驱动 | 不适用 | 好 | 低（跨模块 C ABI 不需要） |

**结论（NFR-M1 落地形态）**：4 引擎、单二进制、要加无头命令层 → **LLVM「InitializePasses 式」显式组合根注册**。理由：单二进制 + 静态链接下静态自注册存在真实的死链丢对象风险（LLVM 为此维护 getenv hack）；显式注册天然可测试（测试里注册 mock 引擎）、初始化顺序确定、与现有 main.cpp 组合根风格连续。

---

## 3. PIMPL 真实成本与约定（GotW 一手）

信源：Herb Sutter GotW #100《Compilation Firewalls》（herbsutter.com/gotw/_100）【已确认】；旧站 gotw.ca（GotW #24/#28，含 fast pimpl 主题）本次无法访问【未获取，相关论点以 #100 为准】。要点原文：

- **藏什么**：五种方案评分，「Option 4: put all private non-virtual members in the Pimpl」10/10「This is the ideal.」；「You can't hide virtual member functions in the Pimpl, even if the virtual functions are private.」「protected members should never go into a Pimpl since putting them there makes them worthless.」
- **回指针成本**：把 this 传给 Impl 函数优于存回指针（后者「adds another level of indirection」且「can get out of sync」）。
- **特殊成员函数**：「You still need to write the visible class' destructor yourself and define it out of line in the implementation file」，原因「unique_ptr's destructor requires a complete type in order to invoke delete」；移动/拷贝赋值同理须在 .cpp 定义；「The above pattern does not make the visible class either copyable or movable by default」（用户定义析构抑制移动 ctor）；好消息「Pimpl'd types are very move-friendly types because they only have to copy a single pointer value.」
- **收益量化**：「converting just a few widely-visible classes to use Pimpls has halved the system's build time.」
- 成本对价（编译防火墙 vs 运行时）：每次成员访问多一次解引用 + 一次堆分配；GotW #100 未讨论 fast pimpl（对齐存储内联优化）——不采信【已确认其未讨论】。

**本仓库对照**：aidenoiser.h:20-21 / pitchshifter.h:19-20 / vocalseparator.h:22-23 的 PIMPL 正是标准用法——隔离 ORT/SoundTouch 重头文件到 .cpp，公开类只剩 QString/unique_ptr；dspdenoiser 无重依赖故不 PIMPL，属合理的不一刀切。接口（streamengine.h:10-15 的虚函数）留在公开类，符合「virtual 不能藏进 Pimpl」的 GotW 规则。改进点：`vocalseparator.h:17` 的非虚 `lastError()` 遮蔽应改为 `override`（多态一致性，架构审视主文档已另有记录）。ORT 隔离 = **标准做法**，无需改。

---

## 4. 热路径虚调用 vs CRTP vs 函数指针

信源：Agner Fog《The Microarchitecture of Intel, AMD and VIA CPUs》（agner.org/optimize/microarchitecture.pdf，已下载并本地抽取文本）【已确认】：

- 「An indirect jump or call is a control transfer instruction that has more than two possible targets. A C++ program can generate an indirect jump or indirect call with a switch statement, a function pointer, or a virtual function.」（间接跳转/调用＝switch/函数指针/虚函数；老 CPU 每 BTB 位点只记一个目标——「It will always be predicted to go to the same target as it did last time」；Pentium M 起多目标预测，理论完美周期 mn，引 Driesen & Hölzle TRCS97-19。）
- 「The number of wasted clock cycles is approximately equal to the length of the pipeline.」；Nehalem 节：「The measured misprediction penalty is at least 17 clock cycles.」

**应用到本仓库的调用粒度分析**（这一步是推理，标【倾向】）：
- `process()`/`flush()` 只在块边界被调：fftranscoder.cpp L154 `chunk = engine.inRate()` → 每秒音频一次虚调用；且一次任务内调用点是**单态**的（同一 engine 引用反复调）——分支目标预测器完全命中，代价≈一条间接 call 指令本身，对比每块内 44100×2 样本的 DSP/ORT 计算，占比 <1e-5，**无优化价值**。
- 逐样本内循环无虚调用：dspdenoiser.h L19 `Biquad::run` 是普通成员函数，会被内联进 process() 循环。
- 结论：**不需要 CRTP/函数指针**。即使实时化后 chunk 缩小（如 512 样本/块 ≈ 11.6ms@44.1k），仍是每块一次间接 call；只有当虚调用进到逐样本内循环才值得消除——本仓库架构不会出现该形态。函数指针相对虚函数无收益且丢多态。
- 盲点：未对当代微架构（Zen4/Intel 7+）跑基准实测；Agner 数据为 PM/Nehalem 时代，量级（误预测 15–20+ 周期）在当代仍同量级【未确认·未实测，但结论对量级不敏感】。

---

## 5. std::function 分配陷阱（libstdc++ 13 源码一手）

信源：libstdc++ 13 分支 `include/bits/std_function.h`【已确认，逐行核验】：

- SBO 缓冲：`union _Any_data { _Nocopy_types _M_unused; char _M_pod_data[sizeof(_Nocopy_types)]; }`；`_Nocopy_types` = {void*, 函数指针, 成员函数指针} 并集。x86-64（Itanium ABI）成员函数指针 16B → **`_M_max_size == 16`、`_M_max_align == 8`**；整个 `std::function` 对象 32B（16 缓冲 + manager + invoker）。
- 栈内判定：`__stored_locally = is_trivially_copyable && sizeof ≤ 16 && alignof ≤ 8 && 8 % alignof == 0`；栈内走 placement-new 进 `_M_pod_data`，否则 `new _Functor(...)` 堆分配。
- 判据表（x86-64）：捕获 1 个指针/int（4–8B）→ 栈内；捕获 2 个指针（16B）→ 栈内（恰好）；**捕获 3 个指针（24B）→ 堆**；捕获 `std::string`/`shared_ptr` → 堆（非平凡可复制）；对齐 16 的结构 → 堆。

**本仓库判定**：`fftranscoder.h:13` `ProgressFn = std::function<void(int)>`；GUI lambda 只捕获 `[this]`（ffextractor.cpp copy_stream L70–91 消费）→ 8B 平凡可复制 → **栈内零分配**【已确认，按源码判据推导】。风险在未来：一旦 lambda 捕获变多（如 this+path+两个标志）仍 ≤16B 没事，捕获 QString 副本即堆分配——每次构造/析构各一次。离线管线每任务只构造一次，无碍；**实时路径（NFR-L*）的回调建议直接用裸函数指针 + `void* ctx`，或模板参数**，不引入 std::function。

---

## 6. 错误处理统一策略先例（FFmpeg C 胶水 + C++ 引擎混合层）

- **FFmpeg 官方示例风格**（doc/examples/decode_audio.c，release/7.1）【已确认】：检查 `ret < 0` → `fprintf(stderr, "…")` + `exit(1)`；仅结尾一处 `goto end` 集中释放（fclose/avcodec_free_context/av_parser_close/av_frame_free/av_packet_free）；**该示例未用 av_strerror**（本仓库 ffutil.h:30-34 已补上 av_strerror→QString，优于示例）。
- **本仓库现状即一个成立的混合策略**：FFmpeg 胶水层 do{}while(false)+集中释放+失败删半成品（ffextractor.cpp L21–115）+ `QString* err` 出参贯穿 setup/transcode；UI 层（mainwidget.cpp:207-213 onJobDone）吃 bool+msg。与 Qt 惯例（bool + errorString）一致。
- **C++ 包装层先例**：avcpp（h4tr3n/avcpp，异常派 av::Exception）本次两条镜像路径均 404 未能核验【未确认·未获取】；libplacebo/Qt Multimedia 等未取一手源码，不引为证据。
- **std::expected 可用性**：见 §1——GCC 12.1 起有、13.1 含单子，但**必须 C++23 模式**。
- **分层建议（FR-24 前提）【倾向】**：
  1. FFmpeg 胶水层与引擎 `setup` 维持 `bool + QString* err`（现状、Qt 一致、零异常）；
  2. FR-24 命令层引入**结构化命令结果**（`{ok, humanMsg, machineCode}`，machineCode 映射 CLI 退出码/未来 Agent 错误枚举）——这是 GUI 与 Agent 共用的关键抽象；
  3. `std::expected` 只在命令层内部纯 C++ 逻辑（如参数解析 → 引擎构造链）用 `expected<unique_ptr<IStreamEngine>, CommandError>` 表达「构造即失败」，收益是链式 and_then 消灭多层 if；**不进 `processInto` 热路径、不跨 Qt 边界**（QString 生态无需 expected<QString,E> 的额外搬运）。若不升 C++23，用同结构的轻量 `Result<T,E>` 结构体等价实现，语义不受损。

---

## 7. CMake 现代约定与硬编码路径的正规修法（仅方案）

- **file(GLOB) 官方立场**【已确认，cmake.org file() 手册原文】：「We do not recommend using GLOB to collect a list of source files from your source tree.」理由：增删源文件不改 CMakeLists.txt 时生成器无从感知需重新配置；`CONFIGURE_DEPENDS`「may not work reliably on all generators」且每次重建有检查成本。→ 本仓库 CMakeLists.txt L13–37 显式清单**已是官方推荐形态，保持不动**。
- **presets 机制**【已确认，cmake-presets(7)】：schema version → CMake 版本映射 1→3.19 … **6→3.25**、7→3.27；`cacheVariables` 支持任意缓存变量（含 `CMAKE_PREFIX_PATH`）；`toolchainFile` 字段自 version 3；`include` 自 version 4；「CMakeUserPresets.json implicitly includes CMakePresets.json, even with no include field, in all versions of the format.」
  - ⚠ 顺带发现的不一致：本仓库 presets version 6 要求 CMake ≥3.25，而 CMakeLists.txt L1 `cmake_minimum_required(VERSION 3.21)`——装 3.21–3.24 的机器会「CMakeLists 能配但 presets 拒收」。修法二选一：presets 降到 version 3–5，或 `cmake_minimum_required` 升 3.25。
- **Qt 官方对 CMAKE_PREFIX_PATH 的说法**【已确认，doc.qt.io「Making Qt available in CMake projects」】：「pass `Qt6_ROOT` as an environment or cache variable pointing to the Qt installation path, or by adding or appending to the `CMAKE_PREFIX_PATH` in a similar way.」（示例 `-DQt6_ROOT=…`）；交叉编译时「set `CMAKE_TOOLCHAIN_FILE` instead of search paths like `Qt6_ROOT` or `CMAKE_PREFIX_PATH`」，Qt 自带 `<Qt>/lib/cmake/Qt6/qt.toolchain.cmake「sets the required variables like `CMAKE_PREFIX_PATH`, `CMAKE_FIND_ROOT_PATH`, and `QT_HOST_PATH`.」
- **修法方案（NFR-M4，不实施）**：
  1. `CMakePresets.json` 只留机器无关结构（名称/binaryDir/编译选项），version 与 `cmake_minimum_required` 对齐；
  2. 新增 **gitignore 的 `CMakeUserPresets.json`**：`inherits` 基础 preset，`cacheVariables` 注入 `CMAKE_PREFIX_PATH`/`CMAKE_CXX_COMPILER`/`CMAKE_MAKE_PROGRAM`（或 `Qt6_ROOT`）；README 记一次生成模板——隐式 include 机制使零改动继承全部基础 preset；
  3. `CMakeLists.txt` L100–101 的 `QT_BIN/MINGW_BIN` 改为从目标推导（`$<TARGET_FILE:Qt6::Core>` 的目录）或同一 cacheVariables 传入；POST_BUILD DLL 拷贝清单同理收敛到变量；
  4. 更硬的整备（可选）：用 Qt 官方 `qt.toolchain.cmake` 作 `toolchainFile`，编译器/Ninja/前缀三件事一次解决——代价是 presets 至少 version 3 且交叉/本机语义统一，适合以后真做交叉编译时再上。

---

## 8. 核心问题回答

### a. NFR-M1 引擎注册表的具体形态

**形态 = 编译期已知、运行期只读的显式目录，组合根一次装填**（§2.5 选型依据）：

```cpp
// 描述子（建议 designated initializer，C++20 GCC10+ 可用）
struct EngineDesc {
    QString key;                 // "denoise-dsp" / "denoise-ai" / "pitch" / "separate"
    QString displayName;         // UI 文案（中文）
    int inRate; int inChannels;  // 静态可知的引擎规格；模型驱动的（GTCRN/MDX）填 0 由 factory 探测后回填
    ModelReq model;              // { None, Optional(kAiModelRelPath), Required(kSepModelRelPath) } + 默认解析根（exe 目录或 CLI 显式路径——统一 GUI/CLI 差异，见 §0.2 main.cpp:48,86 vs ffextractor.cpp:230,255）
    QString outSuffix;           // 对应 data.h 的 kSuffix*
    std::unique_ptr<IStreamEngine>(*factory)(const QVariantHash& params, QString* err);
};
// 组合根：catalog.registerEngine({...}) × 4，main 与 run_cli 共同调用（LLVM InitializePasses 式）
```

注册时机 = main()/run_cli() 入口一次；CMake 关系 = **无**（源文件显式清单新增一行即注册，正是本仓库既有风格的延伸，不需要代码生成）。不选 Qt 插件/QPluginLoader（无动态插件需求，元数据收益用 `EngineDesc` 平面结构即可获得）、不选静态自注册（§2.1 死链坑）、不选 CMake 生成 manifest（4 引擎不值得引入构建期代码生成）。

### b. FR-24 无头命令层与注册表的分层

```
CommandTable（FR-24：命令名 → {引擎 key, 参数解析器, 退出码映射}）   ← main.cpp run_cli 的 if/else（L23–100）收敛于此
  → EngineCatalog::resolve(key, params) → unique_ptr<IStreamEngine>  ← NFR-M1（factory 内做 setup + 模型路径解析）
    → FFTranscoder::transcodeStreamWav（现有唯一通路，不动）
```

GUI 的 ffextractor.cpp L223–269 各 do* 槽与 CLI 各分支都改为调 CommandTable/EngineCatalog——**一处定义引擎构造与参数**，GUI 只剩信号槽转发。命令层返回 §6.2 的结构化结果，为 Agent 复用预埋 machineCode。

### c. processInto 零分配变体的语言级签名（NFR-L2）

```cpp
// streamengine.h 追加（C++20 足够，不依赖 expected）
virtual bool processInto(std::span<const float> in,
                         std::vector<float>& outBuf,   // 引擎内部复用的暂存，容量自管
                         int64_t& outFrames,            // 本轮实际产出帧数（输出可滞后）
                         QString* err) = 0;             // 或沿用 lastError() 通道
```

错误通道选 **bool + 出参/lastError()，不选 expected**：理由①现有约定就是 `setup(..., QString*)` + `lastError()`（streamengine.h:15），接口内混入 expected 造成双轨；②热路径 expected<T,E> 返回值无收益（T 为 out 参数、E 为低频事件）；③expected 强制 C++23 模式（§1.2），不应由最底层接口倒逼全局标准档位。旧 `process()` 保留为默认实现（内部调 processInto + 拷贝），迁移期两轨并存、验收后删旧。

### d. GCC 13.1 上值得立即采用 / 不采用清单

| 采用 | 理由 | 风险 |
|---|---|---|
| `std::span`（10.1 起） | processInto 签名刚需；头文件级零成本 | 无（C++20 模式即可用） |
| designated initializers / `std::variant`（C++20） | EngineDesc/参数结构清晰度 | 无 |
| `std::expected` + 单子（12.1/13.1，**需升 CXX_STANDARD 23**） | 命令层构造链错误传播（§6.3） | C++23 档官方措辞「highly experimental」，但 expected 族有特征宏已定案；第三方 C 头不受影响 |
| `std::format`（13.1） | 替代 QString::arg 的纯 C++ 日志格式化（可选） | 与 Qt 文案体系并存两套格式，非刚需 |
| `bit_cast`（11.1） | FFT/WOLA 位级重解释（如 float↔uint32 阈值比较）替代 memcpy 惯用法 | 无 |
| **不采用**：`ranges::to`/`std::print`/`flat_map`（14/15） | GCC 13.1 没有（§1.1） | 写了编不过 |
| **不依赖**：`<stacktrace>` | MinGW libbacktrace 支持未核验【未确认】 | 崩溃诊断改用 QtCrashHandler/WER |
| `std::atomic::wait` | MinGW 走 condvar 池回退（§1.2），离线够用 | 实时化前需实测唤醒延迟【未确认】 |

### e. CMakeLists/Preset 的正规修法（计划，见 §7）

三步：presets 机器无关化 + `CMakeUserPresets.json` 承载本机路径（隐式 include 机制）；`QT_BIN/MINGW_BIN`（CMakeLists.txt:100-101）从目标/缓存变量推导；presets version 与 `cmake_minimum_required` 对齐（3.25 或降 preset 版本）。GLOB 不引入（官方明文不推荐，现状显式清单保持）。

---

## 9. 本调研的未收敛点与盲点（反偏置清单）

1. **cppreference C++20 支持表**因 403/限流仅部分行核到（span/jthread/format/bit_cast/atomic-wait 等关键行已核）；未核行不影响结论（GCC status 页为一手权威源）。
2. **VST3 GetPluginFactory 工厂细节**未取到源文件（dllmain.cpp 已核，工厂契约【倾向】）。
3. **avcpp 异常风格**未能核验（镜像 404）——§6 未将其列为证据。
4. **当代 CPU 虚调用基准**未实测；§4 结论对量级不敏感但精确周期数未证。
5. **flat_map GCC 版本**为 cppreference 单源（15）。
6. MinGW `std::stacktrace` 可用性未核验。
7. gotw.ca（GotW #24/#28 原文，含 fast pimpl）不可达；PIMPL 论点以 GotW #100 为准。

## 10. 参考链接清单

**GCC / libstdc++（一手）**
- GCC 13 Release Notes（C++23/C++20 库特性）: https://gcc.gnu.org/gcc-13/changes.html
- GCC 12 Release Notes（std::expected/move_only_function/spanstream/stacktrace 首次入库）: https://gcc.gnu.org/gcc-12/changes.html
- libstdc++ Implementation Status（C++20/23 表，版本号权威源）: https://gcc.gnu.org/onlinedocs/libstdc++/manual/status.html
- GCC C++ Status（语言特性/方言）: https://gcc.gnu.org/projects/cxx-status.html
- GCC 13 手册 C Dialect Options（-std=c++23/c++2b、默认 gnu++17）: https://gcc.gnu.org/onlinedocs/gcc-13.2.0/gcc/C-Dialect-Options.html
- libstdc++ 13 `<expected>` 头（`__cplusplus > 202002L` 门控、`__cpp_lib_expected 202211L`）: https://github.com/gcc-mirror/gcc/blob/releases/gcc-13/libstdc++-v3/include/std/expected
- libstdc++ 13 `<bits/std_function.h>`（_Any_data/_M_max_size=16/__stored_locally）: https://github.com/gcc-mirror/gcc/blob/releases/gcc-13/libstdc++-v3/include/bits/std_function.h
- libstdc++ 13 `<bits/atomic_wait.h>`（LINUX_FUTEX→PLATFORM_WAIT、condvar 池回退）: https://github.com/gcc-mirror/gcc/blob/releases/gcc-13/libstdc++-v3/include/bits/atomic_wait.h
- libstdc++ mingw32 os_defines.h（无 PLATFORM_WAIT 定义）: https://github.com/gcc-mirror/gcc/blob/releases/gcc-13/libstdc++-v3/config/os/mingw32/os_defines.h
- cppreference 编译器支持表 C++23 / C++20: https://en.cppreference.com/w/cpp/compiler_support/23 ・ /20

**注册表先例（一手源码/文档）**
- LLVM PassSupport.h（RegisterPass 静态注册 + INITIALIZE_PASS 显式初始化）: https://github.com/llvm/llvm-project/blob/release/13.x/llvm/include/llvm/PassSupport.h
- LLVM InitializePasses.h（全项目初始化声明聚合）: https://github.com/llvm/llvm-project/blob/release/13.x/llvm/include/llvm/InitializePasses.h
- LLVM LinkAllPasses.h（ForcePassLinking getenv 防死链）: https://github.com/llvm/llvm-project/blob/release/13.x/llvm/include/llvm/LinkAllPasses.h
- OBS obs-module.h（OBS_DECLARE_MODULE/MODULE_EXPORT/obs_module_load）: https://github.com/obsproject/obs-studio/blob/30.0.2/libobs/obs-module.h
- Qt 6 插件指南（Q_PLUGIN_METADATA/Q_IMPORT_PLUGIN/QPluginLoader）: https://doc.qt.io/qt-6/plugins-howto.html
- VST3 public SDK dllmain.cpp（InitDll/ExitDll/moduleCounter）: https://github.com/steinbergmedia/vst3_public_sdk/blob/master/source/main/dllmain.cpp
- VST3 SDK superproject README（--recursive 子模块结构）: https://github.com/steinbergmedia/vst3sdk

**PIMPL / 性能（一手）**
- Herb Sutter, GotW #100 Compilation Firewalls: https://herbsutter.com/gotw/_100
- Agner Fog, The Microarchitecture of Intel, AMD and VIA CPUs（间接分支预测、Nehalem ≥17 周期）: https://www.agner.org/optimize/microarchitecture.pdf

**错误处理 / CMake / Qt（一手）**
- FFmpeg 官方示例 decode_audio.c（ret<0 + goto end 风格）: https://github.com/FFmpeg/FFmpeg/blob/release/7.1/doc/examples/decode_audio.c
- CMake file() 手册（GLOB 不推荐警告）: https://cmake.org/cmake/help/latest/command/file.html
- CMake cmake-presets(7)（version 映射、toolchainFile、隐式 include）: https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html
- Qt 6 Making Qt available in CMake projects（Qt6_ROOT/CMAKE_PREFIX_PATH/toolchain）: https://doc.qt.io/qt-6/cmake-making-qt-available.html
- Qt 6 CMake Getting Started: https://doc.qt.io/qt-6/cmake-get-started.html
