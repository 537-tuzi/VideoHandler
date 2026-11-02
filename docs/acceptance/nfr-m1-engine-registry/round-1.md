# round-1（片 A：注册表 + 元数据驱动校验）

- 日期：2026-11-03；基线 HEAD `198493b`（NFR-M1 开工 spec 已合入）；分支形态 = 主树工作树（未提交，主线程统一提交）
- 判据与用例表：`matrix.csv`（状态以此为准）；规格契约 `docs/spec/nfr-m1-engine-registry.md` D1/D2/D3/D5/D6 + §7.1 拍板
- 环境：Linux（gcc/Ninja，linux-test preset）、Qt6Core/FFmpeg/SoundTouch apt、ORT third_party、两模型 fetch-deps 就位

## 1. 改动面与构建

落地：**新建** `src/service/engineregistry.h`（54 行）/ `src/service/engineregistry.cpp`（262 行，进 engine 静态库）；
`src/service/commandlayer.cpp` 改造（+62/−115，净 −53（审查轮补 P2-1 setupErrorClass 防护 +2 行）：删 verbFields/verbAllowsOutput/kVerbs 与 5 个 cmdXxx
处理器、execute 尾部双查表、新增 runEngine 与 builtinVerbs、outputOr 后缀参数化 QString；**信封校验段
cmd→v→未知字段→input/output 防线原样保留**；runTranscode/transcodeResult/classify* 前缀匹配原样保留——片 B 才动）；
`CMakeLists.txt` +2（engine 源列表）。

零警告证据（touch 强制重编译，-Wall -Wextra）：

```
$ touch src/service/engineregistry.cpp src/service/commandlayer.cpp && cmake --build --preset linux-test 2>&1 | grep -iE "warn|error"
grep_rc=1 (1=无匹配=零警告)
```

零改动范围自证（spec D3 片 A 承诺）：

```
$ git diff --stat 198493b -- src/ CMakeLists.txt
 CMakeLists.txt               |   2 +
 src/service/commandlayer.cpp | 176 +++++++++++++++-----------------------------
 2 files changed, 62 insertions(+), 116 deletions(-)
$ git status --porcelain
 M CMakeLists.txt
 M src/service/commandlayer.cpp
?? src/service/engineregistry.cpp
?? src/service/engineregistry.h
```

→ main.cpp / ffextractor.{h,cpp} / mediaservice / mainwidget / streamengine.h / 四引擎 / engine_test /
fft_test / parity 脚本 / commandlayer.h 零触碰（ZERO-DIFF PASS）。

## 2. REG 族金标准（fake-engine-e2e.sh，detached worktree）

方法论（OQ-1 拍板 + goal.md 决策）：worktree detached@198493b → 片 A 四文件临时 detached 提交为 diff 基线 →
注入假引擎 gain（gainengine.{h,cpp} 44 行：样本乘 10^(gainDb/20)、IStreamEngine 直通形态）+ 注册表条目
（gainDb Number 必填 [-24,24] 拒 0、后缀 `_gain`）+ CMake 两行（python 锚点替换，锚唯一性逐一断言）→ 构建 →
断言 → worktree 整树丢弃。复跑 `bash docs/acceptance/nfr-m1-engine-registry/scripts/fake-engine-e2e.sh`：

```
== 构建（linux-test，零警告）==
  PASS 构建零警告
== REG-001 改动面白名单 ==
  PASS REG-001（commandlayer/ffextractor/mediaservice/mainwidget/main/streamengine 零改动）
    （git status --porcelain 恰： M CMakeLists.txt / M src/service/engineregistry.cpp /
      ?? src/service/gainengine.cpp / ?? src/service/gainengine.h，别无其他）
== REG-002 ==
  PASS REG-002（ok=true cmd=gain sourceSpec/outputSpec=44100/2 format=pcm_f32le
                engineResampled=false frames=220500 durationSec=5 exists:result.output）
== REG-002r ==
  RMS 比值 1.995262 vs 期望 1.995262（偏差 0.0000%）
  PASS REG-002r（RMS 比值）
== REG-003..007 ==
  PASS REG-003（缺失→E_PARAMS「gainDb 必须是数字」）
  PASS REG-004（"abc"→同上）
  PASS REG-005（30→「gainDb 须在 [-24, 24] 内」）
  PASS REG-006（0→「gainDb 不能为 0」）
  PASS REG-007（bogus→「未知字段：bogus」）
  （四处文案断言均逐字含于 error.message，exit 1，无 result）
== REG-009..011 ==
  PASS REG-009（note 缺失→「note 缺失」）/ REG-010（note:123→「note 必须是字符串」）
       / REG-011（note:""→「note 不能为空」）
  （Kind::String 分支唯一 e2e 覆盖——五真条目无 String 参数，审查 P2-2 补强后加入脚本）
== REG-008 ==
  PASS REG-008（frobnicate→E_PARAMS「未知命令」+ no-cmd）
== worktree 清理 ==
/home/psh/VideoHandler 198493b [main]
/home/psh/vh-prefix    fffed23 (detached HEAD)   ← 既有无关 worktree，脚本不动
  PASS worktree 已清（main 历史零痕迹，detached 提交无引用即弃）
== fake-engine-e2e 全绿 ==
```

## 3. Kind 覆盖与声明序（ad-hoc，风险 3 收口；run-json 未断言的 Bool/Enum 负例 + requiresModel 三态）

主树 `build-linux/video_handler --json` 实跑（每条 exit=1）：

```
denoise  wind:"abc"          → {"code":"E_PARAMS","message":"wind 必须是布尔值"}
aiDenoise（无 model）         → {"code":"E_PARAMS","message":"model 缺失"}
aiDenoise model:123          → {"code":"E_PARAMS","message":"model 必须是字符串"}
aiDenoise model:""           → {"code":"E_PARAMS","message":"model 不能为空"}
separate  stem:5             → {"code":"E_PARAMS","message":"stem 必须是字符串"}
denoise  humBase:"61"+wind:"abc" → {"code":"E_PARAMS","message":"humBase 只接受 50、60 或 off"}   ← 声明序首错
separate（无 model）+stem:"bogus" → {"code":"E_PARAMS","message":"model 缺失"}                    ← requiresModel 先于 params
```

勘误（审查 P2-2）：上表第二～四行的「model 三态」走的是 validateParams 的 **requiresModel 独立分支**（:201-205），
不是 `ParamSpec::Kind::String` 分支（:211-219）——后者五真条目零使用，其三模板（缺失/必须是字符串/不能为空）
由假引擎 String 参数 `note` 的 REG-009..011 承接（本 round 补入脚本后全绿）。

## 4. 模板对照表（spec 风险 1：每条模板旁注断言用例编号）

| 模板 | 现状出处（commandlayer.cpp@198493b） | 新渲染处 | 断言 |
|---|---|---|---|
| `%1 缺失` | :70 requireString | validateParams String.required / requiresModel 分支 + 信封 input 段（requireString 保留） | KIND-S、REG-009、CMD-023（input） |
| `%1 必须是字符串` | :71 | validateParams String/Enum 类型分支 + 信封 output 段 | KIND-S、REG-010、KIND-E |
| `%1 不能为空` | :73 | validateParams String 空串分支 + 信封段 | KIND-S、REG-011 |
| `%1 必须是数字`（缺失同文案） | :221 cmdPitch `!sv.isDouble()` | validateParams Number `!v.isDouble()` | CMD-024、REG-003/004 |
| `%1 必须是布尔值` | :193 cmdDenoise | validateParams Bool `!v.isBool()` | KIND-B |
| `%1 只接受 %2`（顿号连接末值前「或」） | :188「humBase 只接受 50、60 或 off」/ :246「stem 只接受 vocals 或 instrumental」 | validateParams Enum + enumValuesText（值序 = spec.values 声明序） | CMD-027、KIND-ORD |
| `%1 须在 [%2, %3] 内`（`[-12, 12]`：负号无空格、逗号后一空格） | :224 | validateParams Number hasRange，`.arg(min).arg(max)`（double→"-12"/"12"） | CMD-025、REG-005（`[-24, 24]` 同构） |
| `%1 不能为 0` | :225 qFuzzyIsNull | validateParams Number rejectZero | CMD-026、REG-006 |
| `找不到模型 %1`（E_MODEL） | :211/:249 | runEngine requiresModel 存在性段 | CMD-031 |
| `humBase 为 off 且 wind 为 false：没有可执行的降噪项` | :197 | 条目 semanticCheck 闭包（消费 filled 补缺省后参数） | CMD-028 |
| NaN 否定式 `!(x>=min && x<=max)` | :223 | validateParams Number（NaN 比较恒 false → 落拒收） | CMD-035（薄壳 argv 面） |
| `未知命令：%1`（E_PARAMS，不回显 cmd） | :319 kVerbs | execute 双查表未命中 | CMD-021/021r、REG-008 |
| `未知字段：%1` | :332（verbFields 供给白名单） | execute 信封段（isDeclaredParam 供给，硬约束 1） | CMD-022、REG-007 |
| setup 失败归类 | :230 pitch→E_ENGINE / :214,:253 模型族→E_MODEL | 条目 setupErrorClass 字段（E_ENGINE/E_MODEL） | CMD-032（运行期 E_ENGINE 经 classify，不变） |
| 缺省值 | :180 humBase=Hz50 / :190 wind=true / :239 stem=vocals | ParamSpec defStr（"50"/"true"/"vocals"），filled 出参单源生效（硬约束 4） | CMD-004/005（缺省正例）/ CMD-008/009（stem 双后缀） |

## 5. CMD 族 + 等价回归（主树 build-linux）

- **run-json 36 条**：全绿零漂移（正例 9 动词形态 + 确定性 + 负例 14 + 子断言；含 CMD-021r 响应原文逐字节
  `{"error":{"code":"E_PARAMS","message":"未知命令：frobnicate"},"ok":false,"v":1}`——键序/Compact 确定）。
- **薄壳等价**（thinshell-eqv.sh 198493b，基线 worktree 双跑）：

```
  PASS eqv-transcode-44k / denoise-44k / denoise-hum60 / ai-model / ai-default / pitch-pos / pitch-neg /
       sep-vocals / sep-inst / extract-audio / extract-video / transcode-48k（12 成功形态）
  PASS f-den-hum61 / f-ai-nomodel / f-transcode-u / f-noaudio（4 失败路径）
== 薄壳等价全绿：基线 198493b vs 新薄壳，stdout/退出码/产物三方一致 ==
```

- **槽级等价**（gui-slot-eqv.sh 198493b，双树探针直调七槽）：

```
  PASS GUI-EQV 转录逐字一致（nomodel：文案+进度序列，15 槽次）
  PASS GUI-EQV 缺模型分支真实行使（A 相：ai/sep ok=0 + GUI 原文案入转录）
  PASS GUI-EQV 转录逐字一致（withmodel，16 槽次）+ 带模型分支真实行使（B 相：ai/sep ok=1）
  PASS GUI-CXL「已取消」经 E_CANCELLED message 直通（denoise/extract 双路）
  PASS GUI-PRD 产物 8 件 cmp 逐字节一致（tiny_audio.m4a…st_instrumental.wav）
  PASS GUI-CXL 取消无半成品残留
== gui-slot-eqv 全绿 ==
```

- **ci-smoke 全量**（FFT 属性 + 分块不变性 + parity 四族 + CLI 冒烟 + 体检 + run-json）：`rc=0`，
  89 PASS 子断言、零 FAIL 零 SKIP（唯一 "fail" 字样命中为节标题「缺依赖族 SKIP，PARITY_REQUIRED=1 时 SKIP 即 FAIL」）。

```
== fft self 全绿 / numpy 对照全绿 ==
OK: dsp/pitch/ai/sep-voc/sep-inst 分块不变性（一次性 vs 4096 vs 977 cmp 逐字节一致）
PAR-DS-001..005 / PAR-AI / PAR-MDX / PAR-PT-001 / PAR-SR-001..002b: PASS（parity 全绿）
== CLI 冒烟（7 动词）OK w1–w6 == 体检 PASS == run-json 全绿 == ci-smoke 全绿 ==
```

## 6. 五条目转录对照（语义等价论证，逐条目 vs 旧 cmdXxx@198493b）

| 条目 | 旧处理器 | 等价论证（差异仅编号引用） |
|---|---|---|
| transcode | cmdTranscode :170-176 | 闭包 `setup(rate,ch,Off,false)` 直通 DspDenoiser + 后缀 `_transcoded` + lockCanonical=false 逐项同；probeSourceSpec 由 runEngine 统一调（原 :172 在处理器内，语义同：setup 前取源规格） |
| denoise | cmdDenoise :178-203 | 校验序 humBase→wind→semantic（params 声明序 + semanticCheck）；枚举 {50,60,off} 缺省 "50"、Bool 缺省 true；off+off 文案逐字；闭包 QString→HumBase 映射后 `setup(rate,ch,humBase,wind)` |
| aiDenoise | cmdAiDenoise :205-216 | requiresModel=true（requireString 三态文案同 :68-74）+ 存在性「找不到模型 %1」E_MODEL + setup 失败 E_MODEL（setupErrorClass）+ lockCanonical=true + `_ai`；差异：新增一次幂等 probeSourceSpec（闭包忽略源规格，行为零变化，槽级等证实证） |
| pitch | cmdPitch :218-232 | semi Number 必填（缺失=「必须是数字」同 :221）[-12,12] 否定式 + qFuzzyIsNull 拒 0（文案/顺序同 :224/:225）+ setup 失败 **E_ENGINE**（setupErrorClass，同 :230）+ lockCanonical=false + `_pitch` |
| separate | cmdSeparate :234-255 | 校验序 model→stem→存在性（requiresModel 先于 params + runEngine 顺序）+ stem 枚举缺省 vocals + setInstrumental 先于 setup（闭包内序同 :251-253）+ setup 失败 E_MODEL + lockCanonical=true + 双后缀（outputSuffix 按 filled 的 stem） |

transcode 也入表（OQ-3 拍板）：execute/verbFields 不留硬编码特例，「分发层重写为零」；直通 = 无滤波
DspDenoiser 本就是现实现（:173-175），入表只是把该事实放进数据。9 动词形态不变（8 cmd + separate 双 stem）。

## 7. 勘误与备注

- spec §1 现状盘点行号按 198493b 实读核对无漂移（:80-88/:170-255/:306-359/:313-317 等均命中）；spec §2-D2
  模板表未列 `%1 必须是数字`（:221 现状存在、CMD-024 断言锁定），新渲染器已实现并纳入本表——非冲突，spec 清单遗漏。
- REG-006 首跑断言 `msg-contains=不能为 0` 因含空格被 shell 拆词报「未知断言」，加引号修复后全绿（goal.md 关键信息）。
- gui-slot-eqv / thinshell 的基线 ref 均为 198493b（片 A 前提交）；`/home/psh/vh-prefix` 为环境既有无关 worktree，两脚本与 fake-engine-e2e 均未触碰。
- **REG-WIN PENDING**：Windows 真机（mingw-release 构建 + CMD/REG 面随跑）随 WP-1.0 round-4，fr24 同款挂账口径。

## 8. 审查轮（对抗式审查代理，合入前）

审查报告（9 维度：模板逐字/校验序/缺省回填链/双查表信封/setupErrorClass/probe 插入/REG 脚本强度/文档 overclaim/QJsonValue 边界）：
**无 P0/P1；P2×2 + P3×5**，处置如下——

- **P2-1 setupErrorClass nullptr UB 缺口（已修）**：`commandlayer.cpp` runEngine 原样直传 `entry.setupErrorClass`，
  未来条目「create 会失败但漏设该字段」→ `QLatin1String(nullptr)` strlen UB。修复 = 断言拦开发期 + `kErrEngine`
  三目兜底（当前五条目 create 永不失败，路径不可达，行为零变化；worktree 构建零警告复证）。
- **P2-2 Kind::String 零覆盖 + 文档 overclaim（已修）**：见 §3 勘误；假引擎补 String 必填参数 `note`，
  REG-009..011 三态负例进脚本并全绿（本 round 补跑）。
- P3 处置：错误码字面量双源（engineregistry 条目 vs commandlayer kErr*）→ **片 B ErrCode 化时收敛**；
  setup 期失败路径无 e2e 断言（非 CMD 断言路径，新旧覆盖持平）→ **片 B ERR 族顺手补**；REG-001 方法论边界
  （证明「增量注册零改命令层」而非「片 A 自身无预埋」，后者由审查 grep 实证闭环：commandlayer.cpp 无任何
  引擎类名命中）；plan/matrix 数字漂移 → 已更正（h 54 / cpp 262 / +62/−115 净 −53）。
- 审查代理独立复跑：run-json 36 全绿、ci-smoke 全量全绿、fake-engine-e2e 全绿、30+ 条 ad-hoc `--json`，
  与主线程复跑结论一致（thinshell/gui-slot 未由其复跑，由主线程复跑结果闭环）。

## 9. 测试轮（独立测试验收代理，合入前）

**6/6 任务 PASS，零 P0/P1/P2**（对抗增补 A–D 首错序/缺省链/信封边界/白名单强度 + 独立重跑 + 零改动断言）：
- 多错首错序 5/5（预期从 `git show 198493b` 旧 cmdXxx 推导）；缺省回填链 6/6（denoise off+缺省 wind=ok、
  off+显式 false=拒——semantic 真活；separate 缺省 stem 落 `_vocals`、显式 instrumental 无 output 落
  `_instrumental`——run-json 未覆盖形态补强）。
- REG-001 白名单**构造性破坏实证**：/tmp 改造脚本故意在注入段追加 commandlayer 一行注释 + 杂散文件，
  构建仍零警告、REG-002 仍 PASS，REG-001 独立 FAIL——非「靠构建失败间接抓住」。
- 时间线：验收期间主线程落 P2 修复（源码 03:29:49），测试代理增量重建（03:33:39）全量复跑锚定当前源码；
  全部 e2e 证据 = 修复后形态。P3 观察三条（REG-001 基线拷贝信任主树文件的固有边界 / B2 暴露力分析修正 /
  建议 spawn 测试代理后冻结 src/）见报告，无需返工。
