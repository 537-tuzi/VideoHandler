# FR-8 模型管理（片 A）验收 goal

## 总目标

落地 FR-8 片 A（spec `docs/spec/fr8-model-management.md`，§7.1 八项 OQ 已拍板）：模型管理核心
（内置目录表 + models.json 清单 + modelList 三视角合并 + QNAM 下载器 + 两形态导入）+ 命令面三动词
（modelList/modelDownload/modelImport）+ 验收基建（range_server + run-models + 取消探针）。
片 B（GUI）不在本片范围。

## sub goal matrix

| sub goal | 内容 | 状态 |
|---|---|---|
| SG-A1 | 基建：data.h 三常量（kModelsIndexFile/kModelsDefaultRootName/kDefaultModelEndpoint）+ errinfo 七类码（Net/E_NET，OQ-1） | round-1 PASS |
| SG-A2 | ModelManager 核心：目录表（2 条目，sha/URL 与 fetch-deps.sh 同源）/清单读写（QSaveFile，损坏按空）/三视角合并/下载器（HEAD 解析→ETag 对照→Range 续传→64KiB 增量写→sha256 收尾→同卷 rename→登记）/导入（by-id 指纹 + 自由导入 setup 试开） | round-1 PASS |
| SG-A3 | 命令面：三动词处理器 + BuiltinVerb.requiresInput 信封分叉（旧动词零漂移）+ 错误映射（E_PARAMS/E_FILE/E_MODEL/E_NET/E_CANCELLED） | round-1 PASS |
| SG-A4 | 壳与构建：main.cpp --json/CLI 分支 QCoreApplication（QNAM 事件循环前提）；CMake Network 组件 + modelmanager 两行 + Windows 部署补 Qt6Network.dll | round-1 PASS |
| SG-A5 | 验收基建：range_server.py（Range 206/注入/请求日志）+ run-models.sh（MDL 族）+ mdl-cancel-probe.cpp（取消/bundled） | round-1 PASS |
| SG-A6 | 门禁全绿：run-models / run-json / run-ws / thinshell-eqv / gui-slot-eqv / fake-engine-e2e（基线滚动）/ ci-smoke 全量 / 构建零警告 | round-1 PASS |

## 重大决策与关键信息

- **Qt 6.10.2 移除 `QNetworkRequest::setRedirectPolicy`**：改经 `RedirectPolicyAttribute`
  （int 取 RedirectPolicy 枚举）承载；缺省本即 NoLessSafeRedirectPolicy，显式钉住语义（NFR-C1）。
- **QNAM 掐断实测行为（round-1 关键发现）**：服务器 premature close 时 Qt 6.10.2 的
  `HttpStatusCodeAttribute` 残留 200 且 `finished` 正常触发——若按「status==200 → 成功」判定会把
  截断传输当完成（首版实现实踩，靠收尾 sha256 兜底拦截后修正）。判定树改为：netError 干净为准；
  HTTP 错误只认 `status >= 400`；416 特判（.part 已全量的残局）。详见 round-1.md。
- **下载器失败分类**（OQ-6 拍板执行）：取消/断连 → .part 保留续传；sha256 不符 → 删 .part；
  200 不支持 Range → 弃 .part 从头重写；登记失败 → 删最终产物。全部由 MDL 用例实证。
- **真网红线**：全部下载用例走本地 range_server.py（127.0.0.1）；GTCRN（GitHub 直连，非 hfSource）
  无法本地重定向，下载 e2e 以 UVR（hfSource，endpoint 可换前缀）承载，代码级论证见 round-1.md。
- **连带基线滚动**：engine 链入 Qt6Network 后，链 libengine.a 的探针编译（gui-slot-eqv.sh、
  gui-ws.sh）PKGS 补 Qt6Network；fake-engine-e2e.sh BASELINE_FILES 滚动到本片 7 文件。

## round-2 处置拍板（审查轮，2026-11-03）

- **P1-1/P2-1（自由导入路径/id 命名空间，同根）**：采纳最小修——占用三规则（dest 撞目录条目
  relPath / 同 id 异 sha / dest 撞其他 id 登记 path → E_PARAMS 拒收；同 id 同 sha 翻新幂等放行），
  结构修（dest 按净化 id 命名）超收口轮范围不采。红绿证据链与同库不可达推论见 round-2.md。
- **并发导入脏终态（A7b，过程发现未入正式分级）**：进程间并发非契约面（--json 单发、GUI busy
  锁、D1–D9 无跨进程锁要求），记档不修；双开场景若成真实反馈再议 flock。主线程未独立复现，
  仅边界记档不作 scope-out 依据。
- **P3-3 记档**：D3 步骤 1「取最终 URL」未实现（GET 重试轮重发原始 URL、重定向重走，语义等效
  多一跳）；真机首次下载人工过目。
- **P3-4 记档**：models.json 损坏可重建仅对目录条目成立（MDL-023/009b 收编路径）；自由导入条目
  损坏需重导入覆盖，不建磁盘扫描兜底（超片 A 范围）。
- **P3-6 记档**：MDL-060i 源不变性由 MDL-999 聚合哈希间接覆盖（matrix 说明列已注明）；
  modelImport id+for 同给时 id 优先（D6 未定义，行为确定）。
- **环境教训（入记忆配方 #9）**：/tmp 为带用户配额的 tmpfs，EDQUOT 下 fwrite 静默失败 →
  MDL-092 三轮「rc=1 + 0 字节响应」假象（根因 = 复现/跑批工作区遗留叠加逼近配额）。对策：
  jr stderr 保留回放、VH_KEEP 保尸、大体积跑批走磁盘卷 TMPDIR、复现脚本 cleanup 必删工作区。
  **F-6 复发**：VH_KEEP 行自身 `"$W"#` 紧贴又漏 324MB/轮（ci-smoke 修过同型坑、run-models 漏改），
  测试代理抓出后修复——「# 紧贴引号」是系统性坑，写脚本注释必须空格开头。
- **F-2 拍板（测试轮新发现，主线程已修）**：幂等命中加「登记 sizeBytes==stat 实值」（与 D1
  bundled「存在+size」同精度），截断/替换型损坏重下治愈（MDL-024t）；**等长翻字节为已知
  边界**——size 不变仍命中，代价 = D1「不逐次算 sha」拍板的固有精度，引擎 setup E_MODEL
  兜底 + 片 B 引导文案给「删除条目重下」解药；与 A7b 并发边界同挂「非契约面已知边界」档。

## 进展

- round-1（2026-11-03）：全绿（MDL 族 84 项断言 + 8 门禁）。证据 round-1.md；用例矩阵 matrix.csv。
- round-2（2026-11-03）：审查轮收口——P1-1/P2-1 修复（红→绿）、+5 用例（023z/062b/c/u/r）
  后 94 PASS 全绿×2 + run-json 36/36；MDL-092 环境根因闭环。证据 round-2.md。
