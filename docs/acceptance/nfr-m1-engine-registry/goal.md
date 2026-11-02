# NFR-M1 引擎注册表 总目标

## 目标

落地元数据驱动的引擎注册表（`src/service/engineregistry.{h,cpp}`，进 engine 静态库）：转码族 5 动词
（transcode/denoise/aiDenoise/pitch/separate）收敛为「条目 + 通用校验器 + 工厂闭包」，`execute`
尾部改双查表（引擎表 → 内置小表 probe/extract 族 → 未命中 E_PARAMS），if/else 分发链、`kVerbs`、
`verbFields`/`verbAllowsOutput` 退役——**新增引擎 = 新 .h/.cpp + 注册表追加条目 + CMake 两行，
命令层 / GUI / 服务层零改**（NFR-M1 金标准，REG 族实证）；严格校验语义与消息模板逐字兼容
（CMD 36 条零漂移）。第二硬目标 P3-1（结构化错误通道 `ErrInfo` + 前缀匹配退役）归片 B。
规格契约：`docs/spec/nfr-m1-engine-registry.md`（D1–D7 + §7.1 主线程拍板）。

## sub goal matrix

| sub goal | 内容 | 状态 |
|---|---|---|
| SG-1 片 A：注册表 + 元数据驱动校验 | `engineregistry.{h,cpp}`（ParamSpec/EngineEntry/engineTable/validateParams/isDeclaredParam + 5 条目 + 工厂闭包吞 setup 异构）+ `commandlayer.cpp` 双查表改造（信封段原样保留、5 个 cmdXxx 处理器入条目、`outputOr` 后缀参数化）+ CMake 接线 + REG 假引擎验收（fake-engine-e2e.sh）+ CMD/薄壳/槽级/ci-smoke 全量回归 | **round-1 全绿**：REG-001..008 全 PASS（改动面白名单恰四处 / e2e 正例 RMS 比值偏差 0.0000% / 负例模板逐字同构 / 未注册动词拒收）+ run-json 36 全绿零漂移 + 薄壳等价 16/16 + 槽级等价全绿（nomodel 15 + withmodel 16 槽次 + 产物 8 件 cmp）+ ci-smoke 全量（parity 四族 + 冒烟 + 体检 + run-json）+ 构建零警告 + 零改动范围自证（`git diff --stat 198493b -- src/` 恰三文件）；**审查轮**无 P0/P1（P2×2 已修：setupErrorClass UB 防护 + Kind::String 覆盖 REG-009..011，见 round-1 §8）；**测试轮 6/6 对抗 PASS 零缺陷**（多错首错序 5/5 推导自旧码 / 缺省回填链 6/6 真模型 / 信封边界 7/7 / REG-001 白名单构造性破坏实证 / 零改动独立断言 +62/−115）；**REG-WIN PENDING**（Windows 真机随 WP-1.0 round-4） |
| SG-2 片 B：结构化错误通道 + 前缀匹配退役 | `errinfo.h`（ErrCode/ErrInfo）+ `IStreamEngine::lastError()` 改型 + 底座十余错误点逐一赋码 + `classifyTranscodeError`/`isFileError`/`classifyExtractError` 删除 + OQ-4 三处承载面修正（送包解码失败/写包失败/写文件尾失败 E_INTERNAL→E_FILE）+ ERR 映射表 + PARITY 族全量重跑 | **round-2 全绿**：errinfo.h 落地（E_* 字面量唯一源 errCodeText）、三分类器 + kErr* 全删（grep 零残留）、新旧映射表逐点对照行为差恰 OQ-4 三条消息族五个代码点（round-2 §2）；ERR 族 11 用例（OQ-4 δ1/δ2×2/δ3 全 e2e 实锤 + 坏输出契约两条 make_bad_model 注入流 + setup 期失败两条 + 取消双路 + CMD-032 全文），1 条代码级（模型推理输出缺失不可构造）；全量回归 = ci-smoke（parity 四族 + run-json 36 零漂移）+ thinshell 16/16 + 槽级等价（「已取消」直通）+ fake-engine-e2e 重跑 + ZERO-DIFF 恰 14 文件 + 零警告；ERR-TRAILER-TC 转码侧 trailer e2e 补收（初报「静默截断」经审查 20 组 + 主线程 3 组 + 测试代理复测三路反证推翻系误记，P3 债撤销，round-2 §5-3/§8/§9）；**审查轮** P1×1（已裁决更正）+P2×4（全修：脚本完整性断言/§7-4 事实/映射表补点/文案列精确化）+P3×3（ulimit 注解/errinfo.h 吞错 Q_ASSERT/观察项）；**测试轮 6/6 PASS 零 P0-P2**（OQ-4 三差新旧二进制对照实证 + CMD-032 逐字节保真 + 吞错对抗零命中 + engine_test 直调不回归，P3 配方勘误已更正）；**REG-WIN PENDING**（Windows 真机随 WP-1.0 round-4） |

## 重大决策

- **OQ-1..4 主线程拍板转录（2026-11-03，spec §7.1）**：OQ-1 → (a) 假引擎验收 = 临时 detached worktree（单 main 分支红线不动，diff 白名单即证据，build+e2e 后整树丢弃）；OQ-2 → 片 B 硬切一刀（无外部实现者，双轨只会让前缀匹配活下去）；OQ-3 → transcode 入表为直通条目（直通 = 无滤波 DspDenoiser 本就是现实现，留特例则硬编码残留）；OQ-4 → 承载面随结构化一并修正（三处非 CMD 断言路径，e2e 零回归风险）。
- **REG-001 的 diff 基线 =「片 A 临时 detached 提交」**：假引擎注入前先把片 A 四文件在 worktree 内 commit（detached，无分支引用，用后即弃）——`git status --porcelain` 恰四处（gainengine.{h,cpp} 新增 + engineregistry.cpp/CMakeLists.txt 修改）才是「新增引擎零改命令层」的严格断言；若直接以 198493b 为基线，commandlayer.cpp 的片 A 改动会混进白名单。脚本对已提交形态可复跑（cached 空则跳过 commit）。
- **缺省值单源：defStr 经 validateParams 的 filled 出参生效**：校验器回填「补缺省后的参数副本」，semanticCheck/outputSuffix/create 一律消费 filled——denoise 的 off+off 语义检查与 separate 的 stem 双后缀因此不重复实现缺省（wind 缺省 true 若检查原始请求会误拒，filled 是语义正确性的前提）。
- **model 的 requireString 先于 params 声明序**（不进 params 列表）：现状 aiDenoise/separate 均为 model → 其余参数 → 存在性的顺序，requiresModel 布尔即携带「model 必填 + 白名单 ∪{model} + 存在性检查」三重语义，条目不必重复声明；未知字段白名单 = params 名集 ∪（requiresModel ? {model} : {}）由 `isDeclaredParam` 单点供给。
- **-Wextra 零警告 ⇒ 聚合全字段显式初始化**：designated initializer 跳过字段触发 `-Wmissing-field-initializers`（GCC 对带默认成员初始化器的聚合仍告警），ParamSpec/EngineEntry 全字段写全——顺带防字段序漂移；假引擎注入条目同风格。
- **aiDenoise/separate 路径新增一次 probeSourceSpec 调用**（通用段统一探测后传 create）：现状仅 dsp/pitch 三路探测；probe 为幂等轻量探测（打开读流参数即关、失败回落兜底），AI 族 create 闭包忽略源规格（工作点由模型 metadata 锁定），行为与输出零变化（槽级等价 + run-json 全绿实证）。
- **Number kind 的「缺失」与「类型错」同文案**（`%1 必须是数字`）：现状 semi 即 `!sv.isDouble()` 一刀切（:221），不引入 requireString 的「缺失」区分——REG-003/004 同文案是该语义的同构证据而非缺陷。
- **审查轮（合入前，2026-11-03）**：无 P0/P1；P2×2 已修——P2-1 `setupErrorClass` nullptr UB 缺口（runEngine 处 Q_ASSERT 拦开发期 + `kErrEngine` 三目兜底，当前五条目路径不可达、行为零变化）；P2-2 Kind::String 分支零 e2e 覆盖 + round-1 §3/matrix KIND-S 表述 overclaim（KIND-S 实为 requiresModel 的 model 三态独立分支，非 Kind::String）——假引擎补 String 必填参数 `note`，REG-009..011 三态负例入脚本全绿，文档勘误同步。P3×5 处置见 round-1.md §8。

## 关键信息

- **消息模板对照表**（渲染处全部在 `engineregistry.cpp` validateParams / `commandlayer.cpp` runEngine，旁注断言用例编号）见 round-1.md §模板对照。
- **中文必须 QStringLiteral / QLatin1String 只装 ASCII**（fr24 首版实锤）：枚举值（50/60/off、vocals/instrumental）与字段名是 ASCII 可用 QLatin1String；含中文的模板串一律 QStringLiteral——本次无新增中文 Latin1String，类型错误已由 CMD/REG 断言拦住。
- **NaN 拒收**：范围判定否定式 `!(x >= min && x <= max)`（NaN 比较恒 false → 落拒收），薄壳 argv 面 `toDouble("nan")` 造出的 NaN 在 CMD-035 拦下；JSON 面无 NaN 字面量。
- REG 族脚本（`scripts/fake-engine-e2e.sh`）不进 ci-smoke（分支证据制，spec §6 门禁接线）；CMD 族已由 ci-smoke 内 run-json 承接，零接线成本。json_check.py 断言器复用 fr24 的（交叉引用，不复制）。
- 断言含空格的消息片段须整体加引号（`"msg-contains=不能为 0"`），否则 shell 拆词成未知断言——REG-006 首跑实锤后修复。
- Windows：真机复验随 WP-1.0 round-4（fr24 同款挂账口径）；`gui-slot-eqv.sh 198493b build-linux` 的基线 ref 即片 A 前提交。
- **片 B 移交两项**（审查 P3）——**均已随片 B 落地（2026-11-03）**：错误码字面量双源已收敛（条目持 `ErrCode`，字面量唯一源 errinfo.h errCodeText）；setup 期失败路径（create 返回 nullptr）e2e 已补（ERR-SETUP-AI/SEP，垃圾模型 → E_MODEL 两条「加载…失败」文案实锤）。
- **片 B 新决策**（round-2，详见 round-2.md §7 勘误）：ErrCode 按任务书「六类 + None」实现（spec D4 字面清单漏 Params，以勘误为准不改 spec）；写失败注入配方 = RLIMIT_FSIZE + `trap '' 25`（SIGXFSZ 忽略经 exec 继承），/dev/full 因无扩展名在格式猜测期早退不可用；~~transcode 侧 trailer 吞 EFBIG 静默截断~~——**审查轮推翻（P1）**：该现象不可复现（审查 20 组 + 主线程复测全得 E_FILE 正确报错），实现方误记；transcode 侧 trailer e2e 已补收 ERR-TRAILER-TC，无新增 P3 债。
