# FR-A4 片 A 需求 / 方案 / 改动 / 验证

需求与方案契约：`docs/spec/fr-a4-workspace.md`（D1–D8、§3 数据流、§4 改动面 A 行、§7.1 拍板）。
本文记录片 A 落地时的最终形态与验证口径；实现期偏差以 round-1 勘误为准（不回改 spec）。

## 需求（片 A 范围 = spec §4 A 行）

1. 工作区核心：身份解析（内容哈希）、目录规划（版本号递增跳孤儿）、两段提交（原子登记 +
   失败清理）、只读查询（wsInfo）。
2. `--json` 面接通：转码族 5 动词 + extract 族 2 动词接受 `workspace` 字段；互斥/拒收负例全 e2e。
3. OutInfo.truncated 收口（OQ-5）：转码器如实上报「中途读错 ≠ 正常收尾」，result 与版本记录各带一份。
4. direct 模式零漂移：不带 workspace 的全部行为与 fa15432 逐字节一致（thinshell/run-json 门禁）。
5. 验收基建：`scripts/run-ws.sh` 覆盖 spec §6 WS 十族，进 ci-smoke。

## 方案

- **数据布局（D1）**：`<root>/<stem>.<hash8>/workspace.json`（权威索引：source + versions[] + head）
  + `<root>/<stem>.<hash8>/vNNN_<cmd>/<产物>` + `<root>/index.json`（路径缓存：[{path,size,mtimeMs,ws}]，
  可丢弃可重建）。身份 = 内容哈希；stem 助读（先登记者留名，同内容跨 stem 复用既有目录）。
- **两段提交（D2）**：`planVersion`（stat 输入 → mkpath 根 → 缓存/哈希定位 → 建工作区[初始索引写失败
  自清目录] → 载权威索引 → 占位 vNNN_<cmd>）→ 任务（底座落盘进版本目录）→ `commitVersion`（重载索引
  → 组记录追加 + head 推进 + 同内容刷新 source 路径 → QSaveFile 原子写；失败 discard + E_FILE）。
  任务失败 → `discardVersion` 补删版本目录（底座已删产物半成品），索引逐字节不动。
- **版本记录（D3）**：`{version, time, cmd, params(filled 剥信封键), input{path,sha256,sizeBytes},
  model{path,sizeBytes,mtimeMs}|null, parentVersion|null, output{path(相对),outputSpec,frames,
  durationSec,truncated}}`。同工作区链式输入（产物路径回拼命中版本记录）→ parentVersion；
  跨工作区（哈希定位新工作区）→ 新 source.sha256 = 产物哈希。
- **接缝（D4）**：`workspace` 与 `output` 互斥（E_PARAMS）；probe/wsInfo 无落盘语义拒收 workspace
  （内置表 `allowsWorkspace=false`）；wsInfo 进内置表（`extraKeys={"root"}`、allowsOutput=false）；
  extract 族底座加 `outDir` 参数（工作区只给目录，命名规则单一来源仍在底座）。
- **truncated（OQ-5）**：`FFTranscoder::transcodeStreamWav` 读循环区分 EOF 与 I/O 错（`ret != AVERROR_EOF`
  → truncated=true），OutInfo 带出 → `transcodeResult` 进 result → commitVersion 转录进版本记录。

## 改动（`git diff --stat fa15432 -- src/ CMakeLists.txt` 恰此集）

| 文件 | 改动 |
|---|---|
| `src/service/workspace.h`（新，~45 行） | `VersionPlan` 结构 + `planVersion/commitVersion/discardVersion/queryInfo` 四接口 |
| `src/service/workspace.cpp`（新，~360 行） | 核心实现：sanitizeStem / streamSha256(64KiB 流式) / writeJsonAtomic(QSaveFile) / 根级缓存读写 / 权威索引载入 / resolveWsDir(根目录 `*.<hash8>` 扫描) / findParentVersion / nextFreeVersion(跳孤儿) |
| `src/service/commandlayer.cpp` | `runInWorkspace` 两段提交外壳（+148/−28 行区段）；workspace 信封专检（能力/类型/空串/互斥，位于 input 检查后 output 检查前）；`recordParams` 剥信封键；`transcodeResult` 增 truncated；`extractVerb` 统一 extract 族（direct=现状逐字节同路，ws=传版本目录）；`cmdWsInfo`；内置表增 allowsWorkspace/extraKeys 两列 |
| `src/service/fftranscoder.h/.cpp` | `OutInfo` 增 `bool truncated`；读循环区分 EOF/I/O 错；成功收尾回填 |
| `src/service/ffextractor.h/.cpp` | `extractAudio/extractVideo` 增 `outDir` 尾参（缺省空 = 现状行为不变）；缺省命名 `outDir 非空则跑在其内` |
| `src/model/data.h` | `kWsRootCacheFile` / `kWsIndexFile` / `kWsDefaultRootName` 三常量 |
| `CMakeLists.txt` | engine 库源加 `workspace.{h,cpp}` 两行 |

GUI/服务层（mainwidget/mediaservice/main）零改（片 B 范围）；engineregistry 零改（工作区不触引擎表）。

## 验证

- **WS 族（`scripts/run-ws.sh`，WS-001..049 十族）**：创建/身份/追加链/来源链全字段/原件只读/
  失败清理（只读根·缺模型·错位模型·坏缓存·坏索引）/孤儿跳过/查询面负例/截断素材/内存有界。
  判据总表 `matrix.csv`，证据 `round-1.md`。
- **硬门禁（direct 零漂移 + 注册表不回归）**：run-json 36 全绿；thinshell-eqv（基线 fa15432）16/16；
  fake-engine-e2e 全绿（基线清单同步：NFR-M1 已入 HEAD，清单收敛为本片 9 文件）；ci-smoke 全量
  （run-ws 已接线在 run-json 之后）。
- **构建**：`-Wall -Wextra` 零警告（主树增量 + fake-engine worktree 全新构建双证）。
- **改动面自证**：`git diff --stat fa15432 -- src/ CMakeLists.txt` 恰 7 改 + 2 新（见上表）。
