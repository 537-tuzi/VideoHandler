# NFR-M1 片 A 实施与验证方案

## 需求锚点

| 锚点 | 约束 | 落实 |
|---|---|---|
| NFR-M1（REQUIREMENTS.md:185） | 新增引擎不改 GUI/服务层代码 | REG-001 改动面白名单断言（worktree 金标准） |
| fr24 spec §5.1:224 留缝 | 「NFR-M1 换自描述条目，分发层重写为零」 | execute 尾部双查表；kVerbs/verbFields/verbAllowsOutput/if-else 链退役 |
| CMD 回归网（fr24 matrix.csv 36 条） | 校验语义与消息文案逐字不放松、不漂移 | 模板对照表（round-1.md）+ run-json 36 全绿零漂移 |
| spec D2 硬约束 1–4 | 未知字段白名单 / 模板逐字 / NaN 否定式 / 缺省不变 | validateParams + isDeclaredParam 单点供给 |
| spec D3 | probe/extract 族保留专有逻辑；GUI/CLI 壳零改动 | 内置小表 3 条目；`git diff --stat` 范围自证 |
| P3-1（错误分类） | 片 B 才动 | 片 A 错误分类维持现状前缀匹配（classifyTranscodeError 原样保留） |

## 方案摘要

按 `docs/spec/nfr-m1-engine-registry.md`（D1 编译期中央静态表 / D2 元数据驱动校验 / D3 动词面划分 /
D5 函数局部静态 / D6 落点 engine 库 / D7 分片），主线程 §7.1 拍板（OQ-1 detached worktree 等）。

```
execute（信封段原样：cmd → 双查表 → v → 未知字段 → input/output 防线）
  ├── engineTable() 命中 → runEngine：validateParams(→filled 补缺省) → semanticCheck
  │       → requiresModel 存在性 → probeSourceSpec → create(工厂闭包,吞 setup 异构)
  │       → runTranscode（lockCanonical/后缀由条目供给；收尾件不变）
  └── builtinVerbs() 命中 → probe/extractAudio/extractVideo 专有处理器（非引擎）
  └── 双查表未命中 → E_PARAMS「未知命令：%1」（不回显 cmd）
```

## 改动面（实际落地，HEAD 198493b → 工作树）

| 文件 | 改动 | 规模 |
|---|---|---|
| **新建** `src/service/engineregistry.h` | ParamSpec/EngineEntry/engineTable()/validateParams/isDeclaredParam 声明 | 54 行 |
| **新建** `src/service/engineregistry.cpp` | 5 条目 + 通用校验器（模板逐字兼容）+ 工厂闭包（HumBase 映射/double/模型路径/setInstrumental 前置）+ 枚举文案渲染 + id 唯一性 Q_ASSERT | 262 行 |
| `src/service/commandlayer.cpp` | 删 verbFields/verbAllowsOutput/kVerbs 与 5 个 cmd 处理器（逻辑入条目）；execute 尾部双查表；新增 runEngine（引擎路径通用段）与 builtinVerbs（内置小表）；outputOr 后缀参数化 QString；信封校验段（cmd 先于 v、未知字段、input/output 防线）原样保留；runTranscode/transcodeResult/classify* 保留 | +62/−115（净 −53，含审查 P2-1 setupErrorClass 防护） |
| `CMakeLists.txt` | engine 源列表 + engineregistry.{h,cpp}（字母序） | +2 行 |

**零改动承诺自证**：main.cpp / ffextractor.{h,cpp} / mediaservice / mainwidget / streamengine.h /
四引擎头文件与实现 / engine_test / fft_test / parity 脚本——`git diff --stat 198493b -- src/ CMakeLists.txt`
仅上述三文件（见 round-1.md §6）。commandlayer.h 零改动（execute/okResponse/errorResponse 签名不变）。

## 验证设计

| 族 | 内容 | 判据 |
|---|---|---|
| REG（金标准） | `scripts/fake-engine-e2e.sh`：detached worktree（片 A 临时提交为基线）注入假引擎 gain（样本乘 10^(gainDb/20)）+ 条目（gainDb 数字必填 [-24,24] 拒 0）+ CMake 两行 | REG-001 改动面白名单恰四处；REG-002 正例 result 全字段 + RMS 比值 ≈10^(6/20) 容差 1%；REG-003..007 负例 E_PARAMS 无 result 且模板与真引擎同构；REG-008 未注册动词不回显 cmd；用后清 worktree 复核 |
| CMD（回归网） | fr24 run-json.sh 36 条全量重跑 | 全绿零漂移（重点 CMD-020..028/031/033..035 校验文案与错误码） |
| 薄壳等价 | fr24 thinshell-eqv.sh 198493b build-linux | 16/16：stdout 逐字 + 退出码 + 产物 cmp 逐字节 |
| 槽级等价 | fr24 gui-slot-eqv.sh 198493b build-linux | 转录归一化逐字一致（nomodel 15 + withmodel 16 槽次）+ 分支内容断言 + 产物 8 件 cmp + 取消直通 |
| CI | ci-smoke 全量（FFT + 分块不变性 + parity 四族 + CLI 冒烟 + 体检 + run-json） | 全绿（89 PASS 子断言、零 SKIP） |
| Kind 覆盖（风险 3） | ad-hoc 负例四种 Kind × 正/负 + 多错并存声明序 | round-1.md §3（Bool/String/Enum 类型负例 run-json 未断言，ad-hoc 补证据） |
| 零警告 | touch 强制重编译 grep | 无 warn/error |
