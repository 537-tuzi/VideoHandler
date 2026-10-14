# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

VideoHandler — Qt6 Widgets + FFmpeg + ONNX Runtime 的音频处理桌面软件。现状：离线处理（DSP/AI 降噪、人声分离、变调、视频音轨抽取）已完成核心；目标：实时处理 + RVC 变声 + 外挂训练器 + 对话式 Agent（见需求文档）。

## 必读文件

- `docs/REQUIREMENTS.md` — **需求基线**（决策 D1–D7、FR/NFR 全表、风险、版本规划、开放问题 O1–O4）。任何改动先对照此文档；需求变更先改文档再写码。
- `docs/spec/v1-development-plan.md` — **开发计划基线**（M1–M4 里程碑、工作包 WBS、DoD 与开放问题闸门；动工前先对照排序与依赖）。
- `README.md` — 第三方依赖获取方式。

## 构建与验证

- 目标平台 **Windows x64**；工具链 Qt 6.11 (mingw_64) + MinGW GCC 13.1 + Ninja + ONNX Runtime 1.30 CPU + FFmpeg 9 LGPL shared。
- 配置 `cmake --preset mingw-release`，构建 `cmake --build --preset mingw-release`，产物 `src/bin/`；debug 用 `mingw-debug` → `src/bin-dbg/`。
- ⚠ `CMakePresets.json` 与 `CMakeLists.txt` 末尾硬编码 `D:/qt6/...` 绝对路径（已知债，NFR-M4 要消灭）。**本 Linux 环境不可直接构建**；Windows 上需按机器调整路径。
- ⚠ `third_party/` 不入库（.gitignore 已配），按 README 手工获取（ffmpeg/ort/soundtouch/models 四套），缺失时构建必败。
- 改动引擎后必跑 CLI 冒烟（免 GUI 自动验证，`src/main.cpp` 的 run_cli）：
  ```
  video_handler --transcode-wav  <in> <out.wav>
  video_handler --denoise-wav    <in> <out.wav>
  video_handler --ai-denoise     <in> <out.wav> [model.onnx]
  video_handler --pitch-wav      <in> <out.wav> <半音数>
  video_handler --sep-vocals     <in> <out.wav> [model.onnx]
  video_handler --sep-instrumental <in> <out.wav> [model.onnx]
  ```
  另有 `--extract-audio <in>` / `--extract-video <in>`（FFExtractor 流复制，输出到源目录）；argc≥3 即进 CLI，无参数启动 GUI。
- 暂无单测框架；引擎正确性验收 = parity 测试（对照官方实现，−60dB RMS 准入，NFR-Q1，待建）。

## 架构（现状）

```
main.cpp            CLI 分支（argc>=3）或 GUI；GUI 为组合根（MediaService 注入 MainWidget）
view/mainwidget     唯一窗口；信号槽转发
service/mediaservice QThread worker 调度 + busy 锁 + 取消原子量（一次一任务）
service/ffextractor  任务执行者；静态核心供 CLI 复用
service/fftranscoder 唯一转码通路：解码 → swr 到引擎规格 → 引擎 → swr 回 44.1k → WAV(f32)
service/ 引擎        全部实现 IStreamEngine { inRate / inChannels / process / flush }
  dspdenoiser        RBJ biquad 陷波(50Hz×3) + 高通，无内部缓冲
  aidenoiser         GTCRN 流式（16k 单声道；STFT/WOLA 自实现；ORT 用 PIMPL 隔离）
  pitchshifter       SoundTouch 封装
  vocalseparator     UVR MDX-Net（44.1k 立体声；torch 反射填充 + WOLA + 窗²归一化）
util/fft.h           自研 FFT（基-2 + Bluestein 任意长度）
util/ffutil.h        FFmpeg 错误码/扩展名胶水
model/data.h         全局常量（kCanonicalRate=44100、后缀、模型相对路径）
```

关键约定（新代码必须遵守）：

- 引擎缓冲自持：`process()` 输出可滞后于输入，`flush()` 收尾；FFTranscoder 已按此假设处理任意滞后的引擎。
- 输出统一 44.1k/2ch float WAV；采样率/声道换算全部由 FFTranscoder 的两个 SwrContext 负责，引擎只声明自己要什么规格。
- ORT 引擎用 PIMPL（`struct Impl; std::unique_ptr<Impl>`）；模型参数（sample_rate/n_fft/hop/窗型/状态 shape）从 ONNX metadata 读，**勿硬编码**。
- FFmpeg 胶水的失败路径必须零泄漏：沿用现有 `do{}while(false)` + 集中释放 + 失败删半成品的风格。
- 注释与 UI 文案用中文；常量 `kCamelCase`，方法驼峰；每个头文件首行注释说明职责。
- Git：中文提交信息，单 `main` 分支，有 origin 远程；提交一律只署用户本人，**禁止添加 `Co-Authored-By` 等任何联合作者尾注**。

## 已知 P0 债（改引擎前先读；2026-10 代码审查结论）

1. **无 parity 测试**：GTCRN/MDX 前端为手推实现，须对照 sherpa-onnx / audio-separator 建 golden 测试（NFR-Q1，最高优先）。
2. `util/fft.h`：Bluestein 全 float 有精度风险；MDX 对 2 的幂帧也走 Bluestein（慢且亏精度，应直通基-2）；radix-2 无 twiddle 预计算表。
3. `vocalseparator` 输出读法与参数双双有错（2026-10-14 调研实锤，`docs/reference/research-mdx-uvr-audio-separator.md`）：输出按 frame-major 读，实际是 plane-major `[b,4,dimF,dimT]`；`nFft=dimF*2` 推导错误，MDX 模型无 metadata、须按「末尾 10MiB MD5 查 UVR 官方参数表」取参（Voc_FT 实为 n_fft=7680、compensate=1.021）——现实现对 Voc_FT 产出废音。修复方案见 `docs/spec/separator-output-validation.md`。
4. `kCanonicalRate=44100` 对视频源（48k）造成两次重采样；canonical 应改 48k 可配置（NFR-Q3）。
5. `dspdenoiser` 50Hz 硬编码（60Hz 电网地区无效）。
6. LGPL 合规：SoundTouch 静态链接 + FFmpeg 构建混用（gyan 导入库 + BtbN DLL）需修正（NFR-C3）。
7. `IStreamEngine::process` 每次堆分配返回 vector——实时化前需加 `processInto` 零分配变体（NFR-L2）。

## 方向（详见 REQUIREMENTS.md 第 9 节）

- 执行顺序：**正确性兜底（P0 债 1–5）→ 离线闭环（注册表/DFN3/管线串联/批处理）→ RVC 变声 → 实时化**。
- 架构演进：引擎注册表（NFR-M1）+ 无头命令层（FR-24，GUI 与未来 Agent 共用）+ 工作区版本化（FR-A4）。
- 开放问题：仅剩 O3 RVC 延迟实测（实时化前，真机脉冲法；v1.2 定级「未证实」，变声链最坏算术 181ms 超支）。O1/O2/O4 已于 2026-10-14 调研关闭（`docs/reference/research-index.md`）；残余 = 发版前法务复核项（sherpa 书面确认、ContentVec 作者 MIT 声明确认——权重继承链已证不存在、Qwen 逐仓库 LICENSE 核对，随 NFR-C2 落实）。

## 红线

- `third_party/` 与模型权重永不入库。
- 不引入 GPL 依赖；保持 LGPL/MIT 组合（D1 免费公开发布）。模型许可证逐个核验（文档第 6 节红线表）。
- 不内置克隆音色模型；RVC 社区模型由用户导入，变声功能须带使用须知（NFR-C5）。
- 一切处理保持流式、内存有界（NFR-P4：2h 音频峰值 RSS ≤2GB），禁止整文件载入。
- 原件只读是新工作区机制（FR-A4）的根基：任何新功能不得改写用户源文件。

---

## 当前本地运行环境

- 所有命令一律使用 Bash / POSIX 语法，不要混用 PowerShell 或 CMD 语法
- 包管理优先使用 apt；Python 项目优先使用系统 python3 / venv
- 路径分隔符为 /，使用正斜杠而非反斜杠
- 脚本文件注意添加执行权限（chmod +x），Windows 的 CRLF 换行符需转为 LF
- 工作环境为中文，注释/文档/交流/PR 等内容默认都使用中文

## 工作原则

1. **先读再改**：读源码 / 读当前实现 / 翻文档，确认现状再动手。没读过不允许动手
2. **目标驱动**：在开始前将模糊的指令转化为可验证的目标
3. **优先复用 + 新建需理由**：先搜同类实现；只有现有文件确实装不下才能新建，新建要说得出理由
4. **用原生能力**：框架 / 语言自带的先用，不自己拼字符串再解析
5. **不堆兜底也不过度设计**：一条清晰路径，不加"兼容旧逻辑 / 多种方式任选 / 未来也许用到"的参数 / 抽象
6. **参数归参数**：请求 / 函数参数就只当参数处理，不混用环境变量
7. **代码精确编辑**：只修改必要部分，不要顺手顺便修复旁边的代码，发现问题可以反馈，但不要直接改
8. **遇阻不绕路**：分析根因给完整方案，不 silently skip / workaround / 打补丁
9. **不要停**：没解决 / 完成就不要停
10. **行为变了就同步文档**：改逻辑 / 接口 / env 顺手更新 README 和部署说明
11. **用过即修**：按文档 / 脚本执行撞到错，修问题 + 同步更新文档 / 脚本，下次直接可用
12. **持续沉淀**：用户画像 / 使用习惯 / 项目隐性规则 / 反复踩的坑 / 外部资源，触发自动记忆（auto memory）归档
13. **先论后证**：结论性陈述(bug 根因 / 分析判断 / 验证结果 / 事实)先给结论、再附可核验证据(`file:line` / 命令输出 / 断言结论正确的最小验证)。**工具的"成功"回执不算证据**：声称已落盘 / 已提交 / 已开 PR / 已删除前，用一条独立只读命令拿 ground-truth(`ls` 看字节数 / `git status` / `gh pr view --json url,state`)——退化或异常的工具会返回假成功
14. **分析无 ground-truth，靠反偏置兜底**：纯分析判断(没有代码可编译 / 测试当场证伪)时，结论最易被确认偏误 + 讨好式假收敛带偏。两条栏杆：(a) 主动找**反证**，不只堆支持证据——找不到反证 ≠ 结论成立，只是"暂未找到"；(b) 证据不足就标**"未收敛 / 盲点"**，不强凑皆大欢喜的中庸结论。多候选时 ≥3 并给排除理由(无编译器兜底，标准更严)
15. **指令不扩大解读**：具体动作只按指定范围执行，完成即停，不从旁边信息自行扩展任务。指令有歧义时先结合最近上下文作最小解释，仍无法确定才问一句，不先展开调查；根因和动作明确时直接执行，有误伤风险先提示。
16. **由点到面闭环**：从用户真实目的和痛点出发，不只处理表面单点；完成后检查同因问题、关联场景、受阻功能及新引入的影响。授权范围内一并解决并验证，超出范围则说明影响和建议；明确的具体动作仍按"指令不扩大解读"执行。

## 工作模式

- **要留证据链的重型验证**（改动会迭代多轮 / risky / 多业务形态各需独立验证）→ 按 `项目产物归档规范` 中 `docs/acceptance/<feature>/` 下划分规范：用例清单进 `matrix.csv`（case × round 的 PASS/FAIL 总表，状态以此为准）+ 每轮 `round-N.md`（记 PASS 证据）+ 跨轮 `scripts/` + 全绿后的`report.md`，其他可选。
- **复杂任务先分析**（scope 大 / 跨服务 / schema 变更 / 架构重构 / 改动>=50行且跨文件）→ 先全面分析清楚、落档 `docs/spec/<topic>.md`。
- **goal.md**：大型/多轮任务无法一轮完整实现，需维护`goal.md`确保不偏离总目标。先拆解`sub goal matrix`再subagent实现并验证通过，用户补充信息（调整/细节等），再更新`sub goal matrix`进入新一轮。每轮需要记录`sub goal`进展，遇到重大决策以及关键信息都记录一下。注意：*sub goal round不同于用例 × round*。

破坏性/风险性脚本需带 `--check` 零副作用自检，执行前先跑一次自检。部署按 `docs/ops/` runbook 走，不用记命令。

## 项目产物归档规范

**MUST**：所有非源码输出放 `docs/` 子目录，禁止散到 `docs/` 根或仓库根。类型不明时先看现有桶放最接近的；都不接近时问用户，**不要自建新顶层目录**。

### 目录结构

```
docs/
├── spec/              # 前置分析、方案、实施计划（开工前快照，不回头维护；不含事后复盘）
├── api/               # 本项目后端暴露的 API 契约（第三方 API 放 reference/）
├── acceptance/        # 验收方案/结果/脚本/证据/特性复盘；红线：大 binary 不入库 + 再生成配方 + 源头治理
│   ├── _shared/       # 跨 feature 复用的 e2e 工具包(auth/env/ui/api/report);≥2 feature 用过才进
│   └── <feature>/
│       ├── goal.md            # [总目标 + 唯一`sub goal matrix` + `sub goal`进展 + 重大决策 + 重要信息]
│       ├── plan.md            # [需求 + 方案 + 改动 + 验证]或[症状 + 根因 + 同根因 + 修复 + 验证]
│       ├── matrix.csv         # 用例 × round 的 PASS/FAIL 总表，状态以此为准；证据在 round-N.md
│       ├── report.md          # 全绿才写
│       ├── retrospective.md   # 可选，复盘归此处不进 spec/
│       ├── round-N.md         # 每轮验证记录，fix-rerun 新增不覆盖，N是数字
│       ├── round-N/           # 可选，仅当该轮有独立文件(截图/日志/dump/fixtures)才建目录存放
│       ├── scripts/           # 跨轮可复用的测试脚本 / debug 工具
│       └── fixtures/          # 跨轮复用的静态测试资产(图片/文件/权限 JSON);per-round 数据放 round-N/fixtures.json
├── tmp/               # 当前 session 的 ad-hoc 草稿；PR 合入后转走或删除，不得跨特性堆积
├── reference/         # 外部资料/调研/原始需求/第三方 API 文档（原文件名保留）
├── sql/               # DDL、ad-hoc 查询、数据修复脚本（框架管理的 migration 归源码树）
├── ops/               # 部署流程、运维笔记（凭据禁入库：放仓库外或 .gitignore）
└── manual/            # 最终用户说明（开发者指南放 <service>/CLAUDE.md 或 README.md）
```

**命名**：英文小写 + 连字符、2–6 词、不重复目录类型（`spec/plan-foo.md` 不写成 `spec-plan-foo.md`）；`reference/` 豁免。

例外：既有的 `docs/REQUIREMENTS.md` 是需求基线（见"必读文件"），留在 docs/ 根，后续修订也原地更新。
