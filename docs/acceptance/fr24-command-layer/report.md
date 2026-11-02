# FR-24 验收报告：无头命令层（2026-11-03 Linux 全绿收官）

- 范围：`docs/spec/fr24-command-layer.md` D1–D9 全部设计条目——片 C（round-1：核心 + `--json` + 旧 7 动词薄壳 + CMD 验收基建）+ 片 D（round-2：GUI 七槽迁移）。
- 结果：**matrix.csv 43 用例 Linux 侧全 PASS**（CMD 37 条 + GUI 6 条；GUI-WIN 为 Windows 真机挂账 PENDING）；证据 `round-1.md`/`round-2.md`，工具 `scripts/`（run-json.sh + json_check.py + craft-mismatch-model.py + thinshell-eqv.sh + gui-slot-eqv.sh + gui-slot-probe.cpp）。
- M2 DoD「GUI 全操作走命令层」达成形态：六个任务槽（extract 双/denoise/aiDenoise/pitch/separate 双 stem）组 JSON 请求经 `CommandLayer::execute`；probe 按 D6 明文走 probeInfo/probeText 两渲染端（单一 FFmpeg 遍历）；mediaservice/mainwidget 零改动，GUI 文案（含「已取消」特判、缺模型文案）逐字不变并经双树探针运行时实证。

## spec 决策执行情况（D1–D9 对照）

| 决策 | 执行 | 证据 |
|---|---|---|
| D1 `--json` stdin/stdout 单文档 | ✅ UTF-8/Compact/键序确定/任何失败恒出合法 JSON | CMD-020/021r |
| D2 9 动词平铺 + 严格校验 + model 必填（缺省上移给壳） | ✅ 未知字段/类型/枚举/范围/output==input 全拒；GUI 壳 appDir 解析、CLI 壳 third_party 解析 | CMD-022..028/033/034；round-2 七槽对照表 |
| D3 六类错误码 + 退出码两档 | ✅ 含 WP-1.4 契约违规归 E_MODEL（审查 P1-1 拍板改 classify） | CMD-020..035 |
| D4 OutInfo 输出规格可观测 | ✅ sourceSpec/outputSpec/engineResampled/frames/durationSec，成功路径单点回填 | CMD-002..009 + f 交叉验证 |
| D5 commandlayer.{h,cpp} 进 engine 库、纯函数可重入 | ✅ GUI/CLI 两壳共用；每调用自建引擎实例 | round-2 GUI 复用即证 |
| D6 GUI 迁移（本包迁、最小步、文案逐字） | ✅ 七槽 runRequest 化，兜底 catch 双保险保留，mediaservice/mainwidget 零改动 | round-2 七槽对照 + GUI-EQV/CXL/PRD/DIFF |
| D7 旧 7 动词薄壳（回归面零改动 + 两处有意收紧） | ✅ OK/FAIL 文案与退出码逐字保持 | CMD-EQV 16/16；ci-smoke w1–w7 常绿 |
| D8 验收目录 + python3 断言 + 进 ci-smoke | ✅ run-json.sh 36 条为 ci-smoke 常设门禁 | ci-smoke 输出 |
| D9 engine 静态库落位 | ✅ CMakeLists engine 源列表 | round-1 改动清单 |

## 证据链（按验证面）

1. **命令层合同**：run-json 36 条（正例 9 动词形态含 48k 跟随源/engineResampled 可观测 + 确定性 cmp + 负例 19 含坏模型 E_ENGINE 无半成品）。
2. **旧动词回归网**：薄壳等价 16/16（基线 4657ccd 三方比对）+ ci-smoke w1–w7 + PAR-SR 三调用点常绿。
3. **GUI 迁移等价**：槽级双树探针（基线 fd7badb）——31 槽次文案/进度逐字一致、缺模型/带模型分支内容断言、8 产物 cmp 逐字节一致、「已取消」denoise/extract 双路直通、取消无半成品；坏模型经 GUI ai 槽（round-2b 对抗增补）三行 ORT 推理失败文案经 E_ENGINE message 直通逐字一致；offscreen GUI 冒烟无 connect/QMetaType 告警（唯一 stderr 条目经基线对照证为 offscreen 平台固有）。
4. **全量门禁**：ci-smoke（FFT 属性 + 五引擎分块不变性 + parity 四族 −51.4~−123.9dB + CLI 冒烟 + ffprobe 体检 + run-json）全绿；构建零警告。
5. **过程质量**：对抗式审查 round-1b 0 P0（P1-1/P2-1/P2-2/P3-5 修正在案）；round-2b 0 P0 产品码零改动（P1 工具×1 + P1 文档×1 修复更正、P2 行为差异第 4 条补录、对抗增补 4 项 PASS）。

## 遗留（非本包出口条件）

- **GUI-WIN**：Windows 真机 GUI 交互验收（按钮→队列投递→worker 线程→弹窗/取消时序；headless 探针不覆盖 Qt 跨线程投递面）——随 WP-1.0 round-4 或 FR-24 Windows 轮。
- Windows 全面真机复验（`--json` 控制台编码、薄壳、命令层）同上（OQ-4 AttachConsole 明确不做）。
- 挂账后续包：P3-1 结构化错误通道（注册表包）、P3-2 OutInfo.truncated、P3-3 extract 覆写语义（FR-A4）、OQ-5 旧 7 动词发版前清理、OQ-2 CLI 进度输出（Agent/NDJSON 缝已留）。
- 实质行为差异共两条（round-2.md 行为差异穷举 4 条，审查 round-2b 补录第 4 条）：① 底座异常兜底文案「任务异常终止→内部异常」（D3/D6 既定）；② **异常屏障覆盖范围扩大**——setup 期非 Ort 异常（如模型 metadata 谎报巨大尺寸）旧=std::terminate 整应用崩溃无文案、新=E_INTERNAL 文案 + GUI 存活（改进非回归）。
