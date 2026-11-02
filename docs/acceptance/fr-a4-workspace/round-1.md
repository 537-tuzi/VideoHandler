# FR-A4 片 A round-1 验证记录

- 基线：HEAD=fa15432（工作树不提交，全部门禁在未提交形态跑）
- 构建：`cmake --preset linux-test && cmake --build --preset linux-test` 零警告（-Wall -Wextra）；
  另有 fake-engine-e2e worktree 全新构建（含本片全部 9 文件）零警告双证。
- 环境：Linux（GCC/Qt6/FFmpeg apt 栈 + ORT 1.30 third_party），与 ci-smoke 同栈。

## 1. 用例族全绿（判据总表 matrix.csv）

```
bash docs/acceptance/fr-a4-workspace/scripts/run-ws.sh build-linux
→ == run-ws 全绿 ==（exit 0；WS-001..049 共 54 个 PASS 行含子断言，0 FAIL 0 SKIP）
```

要点抽录（完整输出见运行回放）：

- WS-001..005：首任务建 `st.<hash8>/v001_denoise/st_denoised.wav` + workspace.json 全 schema + index.json 回填。
- WS-006..008：同内容换路径 → **同一**工作区（dir=st.<hash8>、version=2、source.path 刷新为 alias 路径）；
  同名不同内容 → 不同工作区。
- WS-009..015：v001 记录 append-only（规范化序列化逐字节比对）；产物作输入 → 同工作区 parentVersion=1（v004）；
  删缓存后产物作输入 → 跨工作区，新 source.sha256 == 产物哈希。
- WS-016..023：filled 参数（缺省回填 {humBase:'50',wind:true}）/ 模型摘要（绝对路径+size+mtimeMs）/
  extract 记录 output 恰 {path} / wsInfo 与权威索引逐字段一致。
- WS-024..028：源 sha256 前后不变（denoise/extractVideo）；output==input、workspace+output、probe 拒收三负例文案断言。
- WS-029..036：只读根 E_FILE 零残迹；缺模型 E_MODEL 无版本目录；**错位模型 E_ENGINE 任务期失败 → 版本目录
  补删 + 索引逐字节不动**（craft-mismatch-model.py 注入）；根缓存损坏容忍；权威索引损坏 E_FILE。
- WS-037/038：预置 v007_denoise 孤儿 → 下一版本 v008，号不复用。
- WS-039..046：查询不落盘 + 五个参数负例。
- WS-047/048：截断素材 ok:true + truncated=false + 可解前缀（frames 70000:80000）。
- WS-049：`峰值 RSS：direct=54.1MiB workspace=55.1MiB Δ=1.0MiB`（30min 素材首任务含全量哈希）。

## 2. 硬门禁（direct 零漂移 + 注册表不回归）

| 门禁 | 命令 | 结果 |
|---|---|---|
| run-json 36 | `bash docs/acceptance/fr24-command-layer/scripts/run-json.sh build-linux` | 36/36 PASS 全绿 |
| thinshell-eqv | `bash docs/acceptance/fr24-command-layer/scripts/thinshell-eqv.sh fa15432 build-linux` | 16/16（8 动词形态+48k+显式/缺省模型 stdout 逐字一致、产物 cmp 一致；4 失败路径文案逐字一致） |
| fake-engine-e2e | `bash docs/acceptance/nfr-m1-engine-registry/scripts/fake-engine-e2e.sh` | 全绿（基线清单同步后重跑；REG-001 改动面白名单断言过 = 假引擎经本片 commandlayer 全链工作） |
| ci-smoke 全量 | `bash docs/acceptance/wp-1.2-linux-build/scripts/ci-smoke.sh build-linux` | 全绿（fft/分块不变性/parity 四族/CLI 冒烟/体检/run-json 36/**run-ws 已接线**） |
| 改动面自证 | `git diff --stat fa15432 -- src/ CMakeLists.txt` | 恰 7 改 + 2 新（CMakeLists/data.h/commandlayer/ffextractor.h+.cpp/fftranscoder.h+.cpp + workspace.h/.cpp） |

fake-engine-e2e 基线同步说明：其完整性断言要求「拷贝清单 == 主树 vs HEAD 实际漂移」。NFR-M1 片 A/B
已随 832e49b/813f6daf 入 HEAD，原清单（含 errinfo/engineregistry 等已提交文件）不再等于实际漂移；
按断言设计目的把清单收敛为本片 9 文件后复跑全绿（REG-001 的「命令层零改」金标准在新基线下重证）。

## 3. round 内发现并修复：同内容跨 stem 身份断裂（产品 bug）

首跑 WS-006 红灯：st.wav 已建工作区 `st.0b2bb088` 后，同字节的 alias.wav（缓存 miss → 哈希 → 以自身
stem 铸名）开出了第二个工作区 `alias.0b2bb088`——目录名嵌 stem 使「身份 = 内容哈希」（spec D1）在跨
stem 场景断裂，version 归位也全错。

修复（`src/service/workspace.cpp`）：新增 `resolveWsDir(root, hash8, stem)`——缓存 miss 算完 sha256 后
**先扫根目录 `*.<hash8>`**，命中即复用既有工作区（先登记者留名），无命中才以输入 stem 铸新名；
planVersion 与 queryInfo 两侧同修。hash8 碰撞（32bit）取舍与目录名含 hash8 同源（spec §8 风险表）。
修复后 WS-006..008 及全部下游链路用例转绿。

同轮脚本侧修复（不改判据）：四处 `msg-contains=<含空格文案>` 未加引号被词分裂（json_check 报「未知断言」）；
WS-019 模型 path 断言改绝对路径（产品记录 absoluteFilePath）；WS-023 heredoc argv 索引两处错位。

## 4. 版本号语义的两头实证

- 号不复用：预置 `v007_denoise` 孤儿目录（kill -9 残迹的等价形态：占位目录无登记）→ 下一版本 v008（WS-037/038）。
- 号可回收：WS-030b 的 v005_aiDenoise 占位目录被 discard 整目录删除（索引从未登记该号）→ WS-033 的
  v005_denoise 复用无歧义。两行为同源于 `nextFreeVersion`：**目录在即不复用，目录删且未登记则可回收**。

## 5. 失败清理三落点全覆盖

| 两段提交阶段 | 注入手段 | 用例 | 结果 |
|---|---|---|---|
| plan 段（根/目录/索引建） | 只读根目录 | WS-029/029f | E_FILE 工作区根建不了，零残迹 |
| 任务段（引擎推理失败） | 维度错位 ONNX（craft-mismatch-model.py） | WS-030b/-i/-f | E_ENGINE，版本目录补删，索引逐字节不动 |
| 登记前校验段 | 缺失模型路径 | WS-030/031/032 | E_MODEL（runEngine 存在性检查先于 plan），索引不动 |
| 登记段（QSaveFile 写失败） | ——e2e 不可构造，见 §6-B | —— | 代码级映射收口 |

## 6. 不可构造面：代码级映射（同轮法：WS-IDX-ATOMIC 方法论）

**A. truncated=true 分支（转码器中途读错 ≠ EOF）**

Linux 常规文件构造不出「av_read_frame 返回非 EOF 负值」：mp3 中段置零 → 送包解码失败提前 E_FILE；
ts/avi 截尾 → 干净 EOF；wav 谎报 data size → 仍是干净 EOF（解码器按实际读收尾）。链路映射：
`fftranscoder.cpp` 读循环 `ret < 0 && ret != AVERROR_EOF → truncated=true` → `OutInfo.truncated`
（fftranscoder.h）→ `commandlayer.cpp transcodeResult` → `result.truncated` → `commitVersion`
转录 `output.truncated`。贯通断言：WS-001（result 键在）、WS-017/WS-048（记录键在）均以默认值 false
断言字段存在与落位；真值触发待 Windows round-4 或坏块设备环境补（记 WS-WIN 待办）。

**B. 登记段 QSaveFile 写失败（两段提交第二段）**

无 root 无法构造「plan 成功但 commit 时 workspace.json 写失败」（只读目录会在 plan 段 mkpath 先败）。
代码映射：`workspace.cpp commitVersion` 尾部 `writeJsonAtomic` 失败分支 → `discardVersion`（删版本目录
含产物）+ E_FILE「工作区索引写不了：…」→ runInWorkspace 转错误响应。同构旁证：①WS-030b 走同一
discardVersion 清理路径（任务段）；②初始索引写失败自清所建目录（planVersion 内，同函数风格同通道）。

**C. kill -9 孤儿目录**

不可注入进程信号到 CLI 单任务中途再断言（产物半成品 + 占位目录残迹形态不定）；以「预置空 vNNN_<cmd>
目录」这一残迹子集等价模拟（WS-037/038：跳过不复用）。孤儿含半成品产物的情形由 append-only + 号不复用
天然消解（半成品永不入索引，后续任务绕开该号）。审查补记（P3-1）：引擎抛异常路径同理——runTask 若违
约抛出走 execute 兜底 catch（commandlayer.cpp 尾）回 E_INTERNAL，版本目录+半成品成孤儿；该形态依赖
streamengine.h「不得抛出」契约自律，残迹后果与 kill -9 同类（号跳过机制兜底）。

## 7. 勘误（实现偏差，不回改 spec）

1. **D6 direct 零漂移口径**：--json 转码族 result 新增 `truncated` 键（OQ-5「truncated 进本包」的
   自然落点——任务结果如实上报，不能只在 workspace 记录里有）。零漂移门禁定义不变：旧 7 动词
   stdout/退出码/产物三方逐字节（thinshell 16/16）+ run-json 36 字段断言，全绿。旧动词文案不含该键。
2. **D3 params 剥离清单**：`recordParams` 剥 `v/cmd/input/output/workspace/model` 六键——cmd/input/model
   在记录顶层各有归属槽位（cmd/time 同级；input 独立对象；model 独立槽），params 只留引擎参数 filled 副本。
3. **D1 落实补强（§3 产品 bug 的规格化表述）**：spec 文字「工作区目录 <stem>.<hash8>」未明说跨 stem
   复用；落地取「身份=内容哈希」的必然推论——缓存 miss 时根目录扫同 hash8 既有目录，先登记者留名。
4. **模型摘要 path 记绝对路径**（absoluteFilePath），不回显请求原样字符串。
5. **WS-030b 依赖 python3 onnx 模块**（craft-mismatch-model.py）：缺则 SKIP，`PARITY_REQUIRED=1` 时
   升级 FAIL（与 parity 族同语义；本轮环境实跑无 SKIP）。

## 8. 弱点清单（诚实面）

- truncated=true 真值触发、登记段写失败注入：e2e 不可构造，仅代码级映射 + 字段贯通断言（§6-A/B）。
- 取消（E_CANCELLED）路径未测：--json 面无取消输入（GUI 壳才挂 cancel 原子量），工作区层对取消的清理
  与任务失败同路（resp.ok=false → discardVersion），随片 B GUI 接线补验。
- D7 单写者假设未做并发测试（两进程同时写同一工作区的行为未定义，spec 已声明域外）。
- 缓存键 path+size+mtimeMs 的已知取舍（spec §8 风险 3）：同路径同 size 同 mtime 内容变过的极端场景会
  误命中，误命中最坏后果 = 进错工作区 + 版本记录 `input.sha256` 记错（假溯源一层；产物仍只进版本目录、
  原件只读不破）——spec §8 原文口径。
- WS-MEM 判据为峰值 RSS 同量级（32MiB 窗口）而非地址空间精确剖析；30min 单声道素材（~55MiB）足以证伪
  整文件载入假设，但不覆盖 2h@44.1k 立体声全链（那由既有 NFR-P4 分块排水机制保证，本片哈希增量 64KiB 有界）。
- Windows（WS-WIN）：QSaveFile rename 原子性、只读根负例、路径行为随 WP-1.0 round-4 真机补验。

## 9. 审查轮（对抗审查代理，八维度）

**总判：P0=0；P1×1（已修+回归用例）、P2×2（一修一挂）、P3×8（三修/两记档/三维持）。八维度 7 PASS / 1 FAIL（FAIL 项即 P1）。**

### P1 权威索引类型损坏 → 版本史静默清空（已修，WS-036b/c/d）

- **形态**：`workspace.json` 的 `versions` 为字符串——合法 JSON、对象、含键，逃过修复前
  `loadWsIndex` 的 `isObject()+contains()` 两检；`commitVersion` 的 `idx.value("versions").toArray()`
  对非数组返回**空数组**，追加后重写 → 一次 ok:true 任务销毁全部既有版本记录（append-only 不变式
  被击穿；产物在盘但溯源链断）。`nextFreeVersion` 同缺陷在 plan 段即读错。
- **主线程独立复现**（配方教训 #7，先复现后修）：建 2 版本 → versions 改字符串 → 第三次 pitch 任务
  `ok=true` → 索引只剩 1 条、head=3、盘上 v001-003 目录俱在。
- **修复**：`loadWsIndex` 三键类型全检（versions 数组 + head 数字 + source 对象），不合格一律
  E_FILE「工作区索引读不了」（与不可解析垃圾同文案同码）。head 字符串原可「自愈」改为拒收：自愈依赖
  `nextFreeVersion` 的 max() 兜底属碰巧正确，非设计不变式；文件首版无格式兼容负担，格式唯一可全检
  （真相源坏→拒 的契约字面落实）。
- **修复后探针**：同注入 → E_FILE、索引原样未被改写、无新版本目录（无孤儿）；WS-036b/c/d 固化为回归门禁。

### P2 处置

- **P2-1 goal.md SG-2 与 spec OQ-2 拍板矛盾（已修）**：SG-2 误写「workspace 开关（QSettings 记忆
  缺省关）」，spec D5/§7.1 拍板为「无开关硬切、七槽缺省走工作区」——误写比被否决候选 (b) 还反着缺省，
  片 B 若照抄即绕过机制。已改为拍板口径（QSettings 只记根路径）。
- **P2-2 direct result 键集无门禁钉死（挂片 B 收尾轮）**：`truncated` 增键暴露 run-json/thinshell 均
  不钉 direct 正例 result **键集**（CMD-021r 逐字节比对只覆盖未知 cmd 错误响应形态）。随片 B 收尾轮
  在 fr24 套件补 direct 正例键集精确断言（json_check.py 加 keyset 断言或 python 深比较）。

### P3 处置（修 3：P3-2/3-5 + 文档记 2；维持 3）

- **P3-2 已修**：WS-018b 补 aiDenoise 源 sha256 前后对照（WS-RO 族原有模型动词空缺）。
- **P3-3 已修**：WS-041/042 断言收紧为全文「root 缺失」「root 必须是字符串」（原 `msg-contains=root`
  子串过宽；matrix.csv 本就记全文，脚本与表对齐）。
- **P3-5 已修**：fake-engine-e2e.sh 删除清单外冗余拷贝 engine_test.cpp（未漂移文件，拷贝与
  「清单=漂移全集」叙事不齐）；复跑全绿（REG-001 白名单仍恰四处）。
- **P3-1 记档**：§6-C 补引擎异常路径孤儿形态（execute 兜底 catch 回 E_INTERNAL，依赖 streamengine
  「不得抛」契约自律，号跳过机制兜底）。
- **P3-7 记档**：§8 缓存误命中后果补「版本记录 input.sha256 记错（假溯源）」一层，对齐 spec §8 原文。
- **P3-4 维持**：WS-006 首跑红灯为叙述性证据（复现需回退修复，不值得；代码路径与叙述一致）。
- **P3-6 维持**：probe+output+workspace 双违例时报「未知字段：output」（键序迭代所致，确定且合理，备查）。
- **P3-8 非缺陷**：sanitizeStem 对 CJK isLetterOrNumber=true，中文名保留，无退化问题。

### 审查通过面（摘要）

原件只读全路径（workspace 全部写点在 root 下、源只被只读触碰、版本目录为新建不可能命中 input）、
信封互斥与零漂移静态复核（extractVerb direct 分支与旧壳逐字节同路、recordParams 剥六键正确、
validateParams filled 整份副本恰留引擎参数）、truncated 判据（av_read_frame 正常收尾恒 EOF、取消走
循环内原子量不回填）、run-ws 断言总体 typed、CLAUDE.md 四处增改与实现对应、弱点清单如实。

### 修复轮自记（诚实面）

修复新增用例时引入过一次变量撞名：新根 `$W/wst` 与既有 WS-047 截断素材根同名，`ls -d | head -1`
排序后 WS-048 取错工作区（versions[0] 是 extractAudio 记录无 truncated 键）→ 首复跑 WS-048 FAIL。
改 `$W/wstype` 后全量复跑 58 PASS 全绿。教训：改共享脚本前先 grep 既有变量名。

### 修复后回归（全量）

run-ws 58 PASS（54 旧 + 4 新：WS-018b/036b/036c/036d，0 SKIP）、fake-engine-e2e 全绿（基线断言 +
REG-001 白名单）、构建零警告。ci-smoke 全量见提交前主线程复跑记录。

## 10. 测试轮（独立测试代理，修复后终态二进制）

**总判：PASS（有条件）——四项任务独立复现通过，对抗增补 8 族未击穿任何不变式。**

- **双二进制锚定**（响应配方教训 #6 的通告）：修复前（05:33 自建，workspace.cpp=f405c293）/ 修复后
  （05:43 自建 =当前源码，mtime 变化经 sha256 证实内容未变）。全部门禁双跑：run-ws 54→**58 PASS×6 连跑**
  （flake 检查）、run-json 36×2、thinshell 16:16×2、构建零警告×2。
- **P1 独立确认 + 读路径同洞**：测试方独立复现版本史静默清空（与审查方/主线程三路独立）；另发现修复前
  `wsInfo` 会把 `versions:"bogus"` 原样泄漏（读路径同洞）——修复后写读两路均 E_FILE，收敛验证通过。
  并确认修复比通告更强（head 自愈/source 容忍形态也统一 E_FILE，与契约口径一致）。
- **对抗面**（均 PASS，超出实现方自测）：互斥负例增 3 形态（extract 族互斥/类型先于互斥/根指向普通文件
  E_FILE 零残迹）；chmod 555 两级注入（工作区目录级「版本目录建不了」新失败点 + 根级）；孤儿目录两种
  形态（合法极简索引+空壳 → v002；head=2 预造 v003 → v004、清单=[1,2,4]）；跨 stem 双向（先登记者
  留名正反向）；3 级来源链逐字段（三条 input.sha256 与独立 hashlib 重算一致）。
- **direct 零漂移独立背书**：自建 fa15432 基线二进制 8 动词双跑 diff——probe/extract 三动词逐字节一致；
  转码族 5 动词唯一差异 = result 尾部 `,"truncated":false`（与 §7-1 勘误完全吻合，无未声明漂移）；
  5 条负例路径逐字节一致。
- **性能抽查**（记录量级非门槛）：QCryptographicHash ≈195MB/s（tmpfs，coreutils 硬件加速路径的 1/3）；
  缓存命中第二任务 0.14s 与 direct 持平（stat 免哈希生效）。
- **D-3/D-4 观察**：缓存误命中可构造（spec §8 风险 3 实证，原件无损伤）；病态双 hash8 目录 fail-safe。

### D-2 挂账【P3·pre-existing·主线程已复现】direct 模式 symlink 绕过 output==input 防线

- 形态：`QFileInfo::absoluteFilePath` 不解析符号链接——`vlink.wav→victim.wav` 时
  `--transcode-wav victim.wav vlink.wav` 两侧路径字符串不同 → 放行 → 源文件经 symlink 被覆写。
- 测试方用 fa15432 基线二进制证实同行为（非本片回归）；**主线程独立复现**（教训 #7）：victim sha256
  `3ea74b2c→bc973069`、rc=0 报 OK，成立。
- 处置：**不在本片修**（direct 模式语义零触碰是 OQ-4 拍板的硬门禁；workspace 模式天然免疫——无 output
  字段、产物只进新建版本目录）。与 fr24 P3-3（direct 显式覆写语义）同域同族挂账，收口方向
  `canonicalFilePath`（解析 symlink 后比较），随 direct 模式语义域统一拍板（建议并入 P3-3 挂账条目）。

### WS-048 单次红灯（双独立调查闭环）

测试方修复后首跑遇 WS-048 红灯，其调查（机制模拟复现同一 Traceback + 产品侧排除论证）与主线程
§9 修复轮自记（当时正并发编辑脚本、wst 撞名、改名 wstype）互为印证：**测试侧并发编辑碰撞，非产品
flake**。测试方标注其推定缺物证（中间版脚本被覆盖）——主线程对话记录即物证（grep 实录 227 行
`$W/wst` 与新增块同在），两路独立结论一致，收敛。

### 测试轮盲点（与 §8 同口径）

truncated=true 真值 / 登记段写失败注入 e2e 不可构造（测试方亦未构造出，并证实任何目录只读形态都在
plan 段先败）；D7 并发、取消、2h 全链、WS-WIN 未测（域外/挂账不变）。
