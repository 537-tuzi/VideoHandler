# FR-8 片 A 验收方案（plan）

## 需求

spec `docs/spec/fr8-model-management.md` §2 D1–D6 + §7.1 八项 OQ 拍板：

- **D1/D2** 模型库 `<root>/models.json` + `denoiser/`、`separator/` 目录；编译期 CatalogEntry 静态表
  （gtcrn / uvr-mdx-voc-ft，sha256/URL 与 `scripts/fetch-deps.sh:14-19` 同源）；modelList =
  目录 ∪ 清单 ∪ 磁盘三视角合并（installed/available/stale；bundled = appDir 随包存在 + size 匹配）。
- **D3** 下载器五步：HEAD 解析（跟重定向）→ ETag==sha256 对照 → Range 续传 .part（跨运行）→
  64KiB 增量写盘 + 块间取消检查 → 流式 sha256 → 同卷 rename → QSaveFile 登记；单连接；连接级失败
  重试 2 次，HTTP 4xx/5xx 不重试；429 专属文案。
- **D5** 三动词契约：modelList{root?} / modelDownload{id*,root?,endpoint?}（幂等，零网络请求）/
  modelImport{path*,id?,for?,root?}；root 缺省 `~/VideoHandler-Models`（OQ-3）；无 workspace（OQ-8）；
  无 modelDelete。
- **D6** 导入两形态：by-id 指纹对照目录条目；自由导入 for 必带 + create 闭包 setup 试开
  （setup 即权威校验器），license=unknown（用户自行核验）。
- **OQ-1/OQ-2/OQ-4/OQ-6**：E_NET 七类码；endpoint 请求字段（无环境变量）；单连接；.part 为下载
  缓存豁免 NFR-R1（取消/断连保留，sha 不符/200 重下删除）。

## 方案

- 核心在 `src/service/modelmanager.{h,cpp}`（静态目录表匿名 ns + QNAM 同步化局部事件循环）；
  命令层只加处理器与信封分叉（`BuiltinVerb.requiresInput` 末位缺省 true——旧条目零改，
  model 族 false 豁免 input 且 input 不进其白名单）；错误码产生点赋值后 errCodeText 透传。
- 验收 DUT：`--json` CLI 面（正/负例 + 幂等 + 续传 ground truth = range_server 请求日志断言
  `Range: bytes=N-`）；取消语义与 bundled 视角用 `mdl-cancel-probe.cpp` 直调（CLI 单发无取消
  通道，见 round-1.md 论证）；下载全部指向本地 `range_server.py`（真网红线）。
- 前置完整性闸（fr-a4 片 B 教训）：third_party 两模型 sha/size 必须与内置目录表一致，坏则硬 FAIL。

## 改动面

| 文件 | 改动 |
|---|---|
| `src/service/modelmanager.{h,cpp}` | 新建：目录表/清单/合并/下载器/导入（核心） |
| `src/service/commandlayer.cpp` | optionalString 助手 + 三处理器 + 三表条目 + requiresInput 信封分叉 |
| `src/service/errinfo.h` | ErrCode::Net + E_NET 分支 + 七类注释（OQ-1） |
| `src/model/data.h` | kModelsIndexFile / kModelsDefaultRootName / kDefaultModelEndpoint |
| `src/main.cpp` | --json 与 CLI 分支 QCoreApplication（QNAM 事件循环前提）+ 注释修正 |
| `CMakeLists.txt` | Network 组件 + Qt6::Network（两分支）+ modelmanager 两行 + Windows 部署 Qt6Network.dll |
| `docs/acceptance/nfr-m1-engine-registry/scripts/fake-engine-e2e.sh` | BASELINE_FILES 滚动到本片 7 文件（随基线合入滚动惯例） |
| `docs/acceptance/fr24-command-layer/scripts/gui-slot-eqv.sh` | 探针 PKGS +Qt6Network（engine 新链 QNAM 的连带滚动） |
| `docs/acceptance/fr-a4-workspace/scripts/gui-ws.sh` | 同上 |
| 本目录 `scripts/` | range_server.py / run-models.sh / mdl-cancel-probe.cpp（新建） |

## 验证

run-models.sh（MDL 族，round-1 全绿）+ 门禁组：run-json（36）/ run-ws（58）/ thinshell-eqv
（基线 5772bef）/ gui-slot-eqv / fake-engine-e2e（滚动后）/ ci-smoke 全量 / 构建零警告。
证据见 round-1.md 与 matrix.csv。
