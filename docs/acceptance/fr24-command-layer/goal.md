# FR-24 无头命令层 总目标

## 目标

落地 JSON 进 / JSON 出的无头命令层（`CommandLayer::execute`，纯函数、与传输/线程模型解耦）：9 动词形态 + 严格 schema 校验 + 六类错误码 + 输出规格可观测（P0 #4 行为进合同）+ `--json` CLI 壳 + 旧 7 动词改薄壳（回归面零改动），并用 CMD 用例族 + 薄壳等价实证收口。规格契约：`docs/spec/fr24-command-layer.md`（D1–D9）。

## sub goal matrix

| sub goal | 内容 | 状态 |
|---|---|---|
| SG-1 片 C：核心 + CLI + 验收基建 | `commandlayer.{h,cpp}`（execute + 8 个 cmd 处理器/9 动词形态 + 严格校验 + 六类错误码 + 缺省命名 + output==input 防线）+ `transcodeStreamWav` OutInfo 出参 + `probe` 拆 `probeInfo/probeText` + extract 显式 outPath + `main.cpp` `--json` 与旧 7 动词薄壳 + `run-json.sh`/`thinshell-eqv.sh` 用例族 + ci-smoke 接线 | **round-1 全绿（含 round-1b 审查处置）**：run-json 36 PASS（正例/确定性 17 + 负例 19；负例核心 14 条 + v=2 + NaN 薄壳 CMD-035 + 子断言）+ 薄壳等价 16/16（基线 4657ccd worktree 三方比对）+ ci-smoke 全量不回归（parity 四族 + 冒烟 + 体检）+ 对抗式审查 0 P0（P1-1/P2 三修落地）+ 独立验收 7 项全 PASS |
| SG-2 片 D：GUI 迁移 | ffextractor 七个 do* 槽改「组请求 → execute → 拆响应 → jobDone」，mediaservice/mainwidget 零改动，GUI 文案逐字重建（含「已取消」特判） | **round-2 全绿（Linux，含 round-2b 审查处置）**：六任务槽改 runRequest（doProbe 片 C 已就绪不动），mediaservice/mainwidget/commandlayer 零改动（git diff 范围断言）；槽级等价 gui-slot-eqv 全绿（基线 fd7badb 双树探针：nomodel 15 + withmodel 16 槽次文案/进度逐字一致 + 缺模型/带模型分支内容断言 + 产物 8 件 cmp 一致 + 「已取消」双路直通 + 无半成品）；对抗增补 4 PASS（坏模型经 GUI 槽文案直通/stderr 语义/GUI 前置拒绝面/offscreen）；ci-smoke 全量不回归（parity + CMD 36 + 冒烟 + 体检）；审查 0 P0 产品码零改动（P1 工具×1 修复 + P1 文档×1 更正 + P2×1 补录）；**GUI-WIN PENDING**（Windows 真机交互随 WP-1.0 round-4） |

## 重大决策

- **「9 动词」= 8 个 cmd 处理器 + separate 双 stem 形态**：spec D2 标题「9 个动词」而参数表列 8 个 cmd（probe/transcode/denoise/aiDenoise/pitch/separate/extractAudio/extractVideo）、D5 写「8 个动词的 if/else」——按 D2 表落 8 个 `cmdXxx` 处理器，正例覆盖 9 个操作形态（separate×2）。计数口径差异非契约冲突。
- **E_FILE 分类用中文前缀表 + 引擎 lastError 优先**（commandlayer.cpp `classifyTranscodeError`）：底座错误只有文本一个通道；取消 → 引擎 lastError 非空（E_ENGINE）→ 文件族前缀（E_FILE）→ 兜底 E_INTERNAL。首版前缀表用 `QLatin1String` 装中文永远匹配不上（Latin-1 无多字节），CMD-029/030 实测暴露后改 `QStringLiteral`——已进 round-1 证据。
- **坏模型配方 v2**（`scripts/craft-mismatch-model.py`）：wp-1.0 round-2 的旧配方（缓存张量名 s0/s1/s2）已被 WP-1.1 后的 GTCRN setup 命名契约（conv_cache/tra_cache/inter_cache + *_out）在 setup 期拒绝（E_MODEL），到不了推理期；v2 命名合规 + 谱输入维度错位 [1,100,1,2] vs 实发 [1,257,1,2]，setup 全过、首帧 Run 抛 Ort::Exception → E_ENGINE。
- **`engineResampled:true` 断言必须配非 44.1k 源**：44.1k 立体声源喂 MDX（工作点 44.1k/2）本就免重采样，CMD-008 改用 48k 源（首版期望值想当然，实测 false 修正）。
- **result 含 `frames` 键**：spec D4 示意（output/sourceSpec/outputSpec/engineResampled/durationSec）的严格超集，OutInfo 帧数直接可断言（CMD-002 断 220500）。
- **缺省输出命名两处归属**：转码族收敛到命令层单一函数 `outputOr`（§5.3 工作区缝）；extract 族缺省名依赖源流编解码器（aac→.m4a 等），留在底座 `extractAudio/extractVideo` 内部同一规则（传空 outPath 即自动命名），不复制探测逻辑。transcode 无历史 GUI 后缀，新增 `kSuffixTranscoded = "_transcoded"`。
- **信封校验顺序：cmd 先于 v**：合法 cmd 在 v/字段错误下仍回显（D3「cmd 能识别才回显」的可回显子集）；v 仅接受 1（OQ-3）。
- **审查 P1-1 拍板：WP-1.4 输出契约违规 → E_MODEL（改 classify 非 改 spec）**：spec D3（契约不合规→E_MODEL）与 §6（维度错位→E_ENGINE）自相矛盾；按 NFR-R4 语义（E_MODEL=模型坏→引导重下载）改 `classifyTranscodeError`，「模型」前缀（三条 WP-1.4 负例固化文案）→ E_MODEL，运行期异常族 → E_ENGINE。触发受 MD5 查表护甲不可构造 e2e，代码级证据收口。详见 round-1.md round-1b。
- **审查 P2-1/P2-2/P3-5 三小修随片 C 落地**：NaN 半音否定式拦截（+CMD-035）、json_check bool 类型前置、CMD-001 补 h264/1ch 断言。P3-1（E_INTERNAL 承载面/底座文案前缀匹配之脆）挂账注册表包结构化错误通道；P3-2（截断源 ok:true）挂后续 OutInfo.truncated；P3-3（extract 覆写语义）挂 FR-A4；P3-4（FFmpeg stderr 噪声）记录在案。
- **片 D：模型缺省与「找不到…」文案留在 GUI 壳**（runRequest 化时拍板）：命令层 `找不到模型 %1`（无冒号）与 GUI 历史文案「找不到 AI 降噪模型：<appDir 路径>」不同源——spec D2「缺省策略上移给壳」的既定设计，ai/sep 槽先做 appDir 相对解析 + 存在性检查 + GUI 原文案，再组 `model` 字段（命令层的模型缺失分支对 GUI 不可达）。GUI 文案逐字不变的验证面因此落在壳内前置检查 + error.message 直通两段。
- **片 D：doProbe 不经 execute**（spec D6 明文）：probe 的 GUI 文本展示走 probeInfo+probeText（单一 FFmpeg 遍历、两个渲染端），命令层 cmdProbe 与 GUI 各取所需——「GUI 全操作走命令层」的例外由 D6 文本本身豁免（避免结构化 JSON→中文文本的有损往返）。
- **片 D：底座异常兜底文案由「任务异常终止：…」变「内部异常：…」**（唯一实质文本差异）：异常先被 execute 内层 catch（E_INTERNAL，D3 既定）命中；槽内 runRequest 兜底 catch 保留同文案仅防 execute 本身违约抛出。初版以「引擎契约不得抛出 + WP-1.0 round-2 屏障实证」论断该路径无可达触发器——**审查 round-2b 证伪并更正**（见下条），不违「文案逐字不变」约束面（约束的是正常可达路径，见 round-2.md 行为差异穷举）。
- **审查 round-2b P2：异常屏障覆盖范围实为扩大（改进非回归）**：旧 runJob 的 try 只包 transcodeStreamWav，probeSourceSpec/引擎构造/`setup()`/extract 直调全在屏障外；AiDenoiser/VocalSeparator 的 setup 仅 catch `Ort::Exception`，用户放置的模型 metadata 谎报巨大尺寸 → 非 Ort 异常（length_error/bad_alloc）旧形态逃出 worker 槽 → std::terminate 整应用崩溃无文案，新形态被 execute 外层 catch 兜住成 E_INTERNAL 文案且 GUI 存活。WP-1.0 round-2 屏障实证只覆盖 process/flush，不含 setup 期——「无可达触发器」旧论断的漏洞所在。
- **审查 round-2b P1：gui-slot-eqv.sh 缺模型相空转修复**：初版脚本在 A/B 双相循环**之前**预拷两模型到探针 bin 目录（appDir 解析位），nomodel 相实际带模型，ai-missing/sep-missing 走成功推理路径，缺模型分支从未行使、初版证据「缺模型文案已覆盖」失实（双侧同走错分支故等价 diff 仍绿，不可辨）。修复：模型落位后置到 A 相跑完之后 + 新增分支内容断言（A 相断 ai/sep-missing ok=0 + GUI 原文案入转录 / B 相断 ai/sep ok=1）+ stderr 断言改显式 if（顺收 SC2015 与未来 -e 误杀隐患）；主线程复跑全绿，缺模型分支真实行使。
- **片 D 验收方法论：槽级等价探针**（`scripts/gui-slot-eqv.sh` + `gui-slot-probe.cpp`）：GUI 无头不可交互 → 探针同线程直调七槽（覆盖槽内全部代码路径），基线 worktree 双树同媒体对照——文案/进度序列归一化 diff + 产物 cmp + 取消直通断言。与片 C 薄壳等价同方法论，复用于 Windows 轮前的任何 GUI 壳改动。

## 关键信息

- 响应契约：恒一个 JSON 文档 + 行尾 `\n`，UTF-8/Compact，键序 QJsonObject 字典序——CMD-021r 用响应原文逐字节比对锁死确定性。
- 薄壳两处有意收紧（D7，非回归）：未知动词 FAIL exit 1（旧：静默当 extractAudio）；`--pitch-wav` 半音 ∈[-12,12]≠0（旧：任意 double）。薄壳等价用例全部取合法参数域，收紧点单列（thinshell-eqv.sh 未覆盖、ci-smoke/PAR-SR 只用 ±2.0）。
- 旧薄壳经命令层后 `output==input` / output 空串会以 E_PARAMS 拒绝（D2 合同对所有动词生效）；旧 CLI 在该输入下本就走失败路径（建容器失败/直接覆写），等价实证未受影响。
- CMD-032 依赖 python3 onnx（CI parity 依赖已含）；缺 onnx 时该条 SKIP，`PARITY_REQUIRED=1` 升级 FAIL（parity 同款门禁语义）。
- Windows：真机复验随 WP-1.0 round-4 / 本包 round-2（spec §6）；`QTextStream(stdout)` 在旧薄壳逐字保留（Qt6 默认 UTF-8），`--json` 输出走 `fwrite` 原始字节。
