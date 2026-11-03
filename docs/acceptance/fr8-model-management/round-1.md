# round-1（2026-11-03）：FR-8 片 A 全绿

## 环境

- HEAD=5772bef（片 A 改动未提交，工作树态）；linux-test preset（g++ + Ninja，Qt 6.10.2）。
- 素材：third_party 真模型（gtcrn 535638B / UVR 66762490B，sha 与内置目录表逐字一致——前置闸）。
- 服务：`scripts/range_server.py`（python3 stdlib，127.0.0.1，8 实例：正常/坏内容/404/429/
  ETag 错/掐断/无 Range/GET 段 404）。

## 结果

`scripts/run-models.sh`：**全绿，84 项 PASS**（MDL-001..999，逐项见 matrix.csv）。

关键证据摘录（服务日志为续传 ground truth——客户端只看结果文件无法区分续传与整体重下）：

- MDL-030/031 断点续传：掐断服条（每请求发 200000B 即断）首腿 3 次 GET（`range=-` →
  `range=bytes=200000-` → `range=bytes=400000-`，重试自身即续传），E_NET 后 `.part`=600000B 保留；
  换正常服重跑 `GET ... range=bytes=600000- status=206` → sha256 与目录表一致 → 落位登记。
- MDL-021/023 幂等：服务日志行数前后不变（零网络请求）。
- MDL-044 .part 语义反面：内容坏 → 「下载内容 sha256 不符……已删临时文件」+ 无 .part 残留。
- MDL-090/092 取消闭环：cancel-pre 零请求；cancel-mid `.part`=112128B 保留；CLI 重跑 Range 续传收尾。

## 门禁

| 门禁 | 结果 |
|---|---|
| 构建（linux-test，触碰 TU 全重编） | 零警告零错误 |
| run-models.sh（MDL 族） | 全绿（84 PASS） |
| run-json.sh | 全绿（36 例，含 CMD-021r 逐字节） |
| run-ws.sh | 全绿（58 例） |
| thinshell-eqv.sh 5772bef build-linux | 全绿（旧 7 动词 stdout/退出码/产物三方一致） |
| gui-slot-eqv.sh | 全绿（探针 PKGS 滚动 +Qt6Network） |
| fake-engine-e2e.sh | 全绿（BASELINE_FILES 滚动到本片 7 文件） |
| ci-smoke.sh 全量 | 全绿（gui-ws.sh 探针 PKGS 同步滚动） |

## 实现期关键发现（会进后续维护面）

1. **Qt 6.10.2 移除 `QNetworkRequest::setRedirectPolicy`**：改经 `RedirectPolicyAttribute`
   （int 取 RedirectPolicy 枚举值）。缺省属性值本即 NoLessSafeRedirectPolicy，代码显式钉住
   （NFR-C1 防降级意图不变）。
2. **QNAM 掐断吞错（已修正）**：服务器 premature close 时，Qt 6.10.2 的
   `QNetworkRequest::HttpStatusCodeAttribute` 残留 200、`finished` 正常触发、`error()==NoError`——
   首版判定「status==200 → 本轮成功」会把截断传输当完成（首跑实踩：sha 校验兜底拦截了它，
   但 .part 被误删、续传失效）。修正后的判定树：`netError==NoError` 才算干净收尾；HTTP 错误只认
   `status>=400`（掐断的状态残留是 2xx）；416 特判为「.part 已全量残局」交哈希收尾。MDL-030 族
   即该路径的回归锚。此行为属 Qt 版本面，升 Qt 需复跑 MDL-030。
3. **工具债（range_server）**：python `socket.close()` 在 makefile 引用（rfile/wfile）存续时不关
   OS fd——掐断 8 秒无 FIN，掐断注入整体失效；必须 `shutdown(SHUT_WR)`。已修并留注释。

## 取消通道论证（MDL-CANCEL 为何走探针）

CLI `--json` 单发（stdin→stdout 一个 JSON 文档）无并发方，`CommandLayer::Ctx` 传空 →
`cancel` 指针为空 → 取消分支不可达。两个候选：给 CLI 加取消开关（为测试改产品面，拒绝）；
SIGINT 等价注入（对同步单发进程等价于杀死，非协作取消，测不到块间检查点）。故按 gui-ws /
mdl-cancel-probe 先例：探针直调 `ModelManager::downloadModel` 注入 cancel 标志（首个进度回调置位
→ 下一 64KiB 块间检查点中止），取消→E_CANCELLED→.part 保留→CLI 重跑续传闭环（MDL-092），
覆盖与 GUI 片 B 相同的代码路径（Ctx 同型透传）。

## 不可构造面与代码级映射

- **GTCRN 下载 e2e**：目录表 GTCRN 条目 `hfSource=false`（GitHub release 直连 URL），endpoint
  镜像字段对其无效（D4 有意为之——注记于代码与目录表）。真网红线禁止外网，故下载机器路径
  （HEAD/ETag/Range/重试/收尾）全部由 UVR 条目（hfSource=true，endpoint 指向本地服务）承载；
  两者的差异仅 URL 组装一行（modelmanager.cpp downloadModel 内三元），下载状态机共用。
  GTCRN 条目本身由 MDL-006（bundled 视角，真文件 size 匹配）、MDL-060/061（by-id 导入指纹对照，
  正/负例）覆盖。
- **HF 真实重定向/CDN 302 链**：本地服务不重定向。NoLessSafeRedirectPolicy 的生效面未 e2e——
  依据为 Qt 文档语义 + 属性缺省值同值（显式设置只防未来缺省漂移）；升级 Qt 或换镜像域名时
  首次真机下载需人工过一眼（片 B GUI 联调时自然覆盖）。
- **spec 坏模型用例的形态修正**：spec §6 MDL-IMPORT 坏模型引 wp-1.0 round-2 的维度错位模型
  （该模型 setup 全过、首推理才抛 E_ENGINE）。对导入而言 D6 明言「setup 试开即权威校验器」——
  维度错位模型会被导入**接受**（这不是 bug，是校验器边界的如实呈现）。故 MDL-063 改用随机字节
  （setup 即败 → E_MODEL），是 spec 意图的更强形态；维度错位形态不适用于导入面，特此记录。
- **真机 Windows 面**：随 round-4 真机挂账（Qt6Network.dll 部署行已入 CMake Windows 分支）。

## 连带滚动（基线合入惯例）

- `fake-engine-e2e.sh` BASELINE_FILES：FR-A4 片 B 6 文件 → 本片 7 文件（CMakeLists.txt、
  src/main.cpp、src/model/data.h、src/service/commandlayer.cpp、src/service/errinfo.h、
  src/service/modelmanager.{h,cpp}）；REG-001 白名单不受影响（注入仍恰触 4 文件）。
- `gui-slot-eqv.sh` / `gui-ws.sh` 探针 PKGS +Qt6Network：engine 链入 QNAM 后，链 libengine.a
  的探针必须随动，否则 undefined reference（实跑发现）。
