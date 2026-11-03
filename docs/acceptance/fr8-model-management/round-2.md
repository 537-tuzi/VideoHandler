# round-2：审查轮 + 主线程修复与红绿验证（2026-11-03）

实现轮（round-1）落地后，按编排配方并行放审查代理（只读对抗审）与测试代理（独立验收）。
本文件记录：审查报告分级与处置、主线程复现、修复实现、红绿证据链、MDL-092 环境根因闭环。
测试代理报告落地后补记（见文末「测试轮补遗」）。

## 审查报告（十维：9 PASS / 1 FAIL）

审查代理独立取证：复跑 run-models 84/84、run-json 36/36、thinshell-eqv 16 PASS（顺带覆盖
main.cpp 补 QCoreApplication 的改动面——round-1 未单列的回归面）；git diff 7 文件与
fake-engine-e2e 基线逐字一致；目录表 sha/URL 与 fetch-deps.sh:14-19 逐字同源。

分级：**P0=0**；P1-1（导入两形态 FAIL 维度）；P2-1；P3×6。

## P1-1 主线程复现（工作原则 13/记忆配方 #7：采纳前必亲手复现）

审查称：自由导入 dest 无冲突检查，同名文件静默覆盖他 id 登记产物 → models.json 双条目同
物理路径 → 毒化 downloadModel 幂等信任链（`reg.path==dest && reg.sha256==目录sha` 即信不读
文件，`modelmanager.cpp:483-485`）。

主线程复现成立：by-id 导入 gtcrn（登记 `gtcrn@denoiser/gtcrn_simple.onnx`）→ 自由导入同名
`gtcrn_simple.onnx`（setup 试开过）→ `/tmp/.../models.json` 实测两 id 指向同一路径（复现目录
已清理）。审查同时给出真实场景推演：社区同族模型同名换版本重发（UVR 生态常见）→ 覆盖已验证
文件 → `modelDownload(uvr)` 幂等短路恒 ok 零网络返回「已装」而实际已是另一个模型。

## 处置拍板（主线程）

采纳审查建议 (a) 最小修（结构修 dest 按 id 命名虽可一并消 P2-1，但改既有 dest 语义，超出
收口轮范围）。三条规则（`modelmanager.cpp:661-690`，占用检查插在 sha 计算后、复制段前）：

1. dest 与内置目录条目 relPath 撞 → E_PARAMS「导入目标与内置目录条目路径冲突（请改文件名后重导）」；
2. manifest 同 id 已登记：异 sha → E_PARAMS「导入 id 已被占用（不同内容的模型请改文件名后重导）」；
   同 sha → 放行翻新幂等（内容已验，文件与清单同步翻新无害）；
3. dest 被其他 id 的登记 path 占用 → E_PARAMS「导入目标已被其他模型占用（请改文件名后重导）」。

附修：`modelmanager.cpp:361-362` sanitizeId 注释纠正（`QChar::isLetterOrNumber()` 对中文为
true，中文名原样保留为 id；「回落 model」仅全标点/emoji 名触发——行为比注释好，注释误导维护者）；
`modelmanager.h:21` 进度注释 0–99 → 0–100（收尾 `onProgress(100)` 与文档一致）。

同库内推论（记档）：dest=purposeDir/fileName 而 id=stem 净化，同 path 必同 stem → 同 id——
故规则 3 在 CLI 面只经 by-id 登记（其 path=目录条目 relPath，被规则 1 先拦）或手工/并发改写
的 manifest 触达，属纵深防御；回归用例以 python 预写清单复刻冲突态（MDL-062b）。

## 红绿证据链

- 回归用例（run-models.sh，独立库 `$LIBI2` 不污染主线态）：MDL-062c（目录条目路径冲突 +
  拒收零落盘）、MDL-062b（path 被其他 id 占用 + 清单不改写 + 不落产物）、MDL-062u（id 被异
  sha 占用）、MDL-062r/r2/rm/rs（同 id 同 sha 翻新幂等——防线加严的回归保护）。
- **红**（修复前二进制 09:00 构建）：062b/c/u 六断言全 FAIL（静默覆盖/双 id 同路径实锤）、
  062r 族 PASS（幂等既有行为）。`/tmp/rm-redo.log`。
- **绿**（修复后重建，零警告）：run-models **94 PASS 全绿**；磁盘卷 TMPDIR 下复跑再次 94 PASS
  全绿（稳定性双验）；run-json 36/36 全绿（信封面零漂移确认）。
- matrix.csv 同步：+MDL-023z（脚本 :189 既有断言漏登，P3-5）+ 062b/c/u/r 四行。

## MDL-092 间歇失败根因闭环（环境，非产品非脚本 bug）

现象：四轮完整跑批中 MDL-092（取消→续传闭环）三轮失败，形态恒为「退出码 1 + 响应文件 0 字节
+ stderr 空」。排查过程：

1. 最小复现（server A + cancel-mid 探针 + 续传请求，`/tmp/repro092.sh`）两轮全过 → 非确定性
   续传缺陷；
2. main.cpp 全路径先写 JSON 再退出（`emitJsonResponse` 无条件 fwrite）→ 无「rc=1 且零输出」
   代码路径；df 显示 /tmp 未满（排除卷满）；
3. 后续 Bash 工具本身报 EDQUOT → **/tmp 为带用户级配额的 tmpfs**；根因：主线程自己的最小
   复现脚本 cleanup 只 kill 服务不删工作区，三次复现各遗留 128MB（UVR 副本），叠加测试代理
   跑批遗留（210MB 级）逼近配额线 → 66MB 级模型下载写盘被 EDQUOT 掐断 → 响应文件创建 0 字节、
   下载器 ioError → ok:false（rc=1）但 fwrite 同样失败 → 空响应。第一轮绿/第三轮红 = 配额
   临界抖动，自洽。
4. 清理遗留（rm 三个 128MB + 528KB 复现目录）后连续两轮全绿稳定。

防再犯（已落进脚本/流程）：

- `run-models.sh` jr 断言失败时回放 stderr（原 `2>/dev/null` 把线索丢了）；
- `run-models.sh` cleanup 支持 `VH_KEEP=1` 保现场；
- 主线程复现脚本 cleanup 补 `rm -rf "$W"`；
- 大体积跑批（66MB×N 副本峰值）改用磁盘卷 TMPDIR（`.tmp-mdl/`），不与并发代理争 tmpfs 配额；
  ci-smoke 接线时同样处理（见 round-2 接线段）。

教训入记忆配方（#9）：**验收跑批的工作区放受配额 tmpfs 上时，「工具成功回执」与「文件非空」
都会说谎——EDQUOT 下 fwrite 静默失败、文件 0 字节。跑批脚本 stderr 一律保留、工作区可保尸、
大体积跑批走磁盘卷。**

## P3 记档（goal.md 同步）

- P3-3：D3 步骤 1「取最终 URL」未实现——GET 重试轮重发原始 URL、重定向重走一遍（语义等效、
  多一跳）；HEAD 只取 Content-Length/ETag。已接受偏离，真机首次下载人工过目。
- P3-4：models.json 损坏「可重建」仅对目录条目成立（downloadModel 收编路径 MDL-023/009b 治愈）；
  自由导入条目损坏即不可重建（文件在盘不可见，需重导入覆盖）。记档不修（重建需磁盘扫描兜底，
  超片 A 范围）。
- P3-6：MDL-060i 仅断言源文件存在性，「源 sha 前后不变」由 MDL-999 聚合哈希间接覆盖（IN/*.onnx
  在终闸集合内）——matrix 说明列已注明；另 modelImport `id`+`for` 同给时 for 静默忽略（id 优先，
  D6 未定义、行为确定性无问题）。

## 过程发现未入正式分级：并发导入脏终态（审查代理对抗用例 A7b）

两进程并发 modelImport 同库：一进程收 ok=true（sizeBytes:0）、另一 E_FILE、dest 最终不存在而
清单已登记。审查代理未列入最终报告（judged 超设计面）。主线程定性：**进程间并发非契约面**
（--json 单发单进程、GUI busy 锁一次一任务；D1–D9 无跨进程文件锁要求），响应失真是竞态边界
而非单进程缺陷；不修（文件锁超片 A 范围），记档挂账——若片 B GUI 后用户反馈双开场景再议
（候选：models.json 写入前 flock）。主线程未独立复现（并发时序构造成本高、审查代理已有实测
转录），按记忆配方 #7 该项**不作为 scope-out 依据**、仅作边界记档。

## 连带脚本修复（本）

- run-models.sh：jr stderr 保留回放；VH_KEEP 保尸开关；MDL-062 区回归用例；`cp` 目标移出 `$IN`
  （完整性终闸基准目录不得增删——第一版回归用例把拷贝放 $IN 致 MDL-999 FAIL，自伤已修）；
  062u 断言含空格文案补引号（json_check 按空白拆参，「未知断言」实为拆参假象）。

## 测试轮补遗（独立测试代理报告已归，主线程甄别处置如下）

测试代理双锚点认证：修复前（08:59 二进制）+ 修复后（09:24 二进制 = 1ef3964），十套门禁独立
复跑全绿（run-models 94×3 / run-json / run-ws / thinshell / gui-slot / fake-engine / gui-ws /
fft_test / ci-smoke 含 MDL 接线后），8 项对抗面闭合（续传边界含 416 残局与 cutmid 三腿 Range
复刻 / models.json 损坏 / 导入 P1-1/P2-1 修复后复测双向拒收 / 信封负例 / 幂等零网络 / 取消 /
并发粗探 A7a 5/5 干净 / bundled 视角）。

**新发现处置（主线程均亲手复现后落地，配方 #7）**：

- **F-2 已登记产物被篡改 → 幂等盲区**：主线程复现（截断 -1000B 后 modelDownload ok=true 且
  响应 sha=目录指纹与 sizeBytes=盘上实值 66761490 **自相矛盾**、零网络）；风险链 = D8 引导
  重下恒 ok 死循环（NFR-R4）。**修复**：幂等命中条件加「登记 sizeBytes == stat 实值」
  （`modelmanager.cpp:483-488`，与 D1 bundled「存在+size」同精度自洽）——截断/替换型损坏
  落 else 现算指纹 → 不符重下治愈（MDL-024t 三断言：日志增长/盘上 sha 还原/响应真值）。
  **等长翻字节仍过**（size 不变）：与 bundled 同边界（D1 拍板不逐次算 sha 的已知代价），
  引擎 setup E_MODEL 兜底 + 片 B 引导文案给解药（删除条目重下），记档 goal.md。
- **F-6 run-models.sh cleanup `"$W"#` 紧贴泄漏**：rm 实删不存在的 `<W>#` 路径，真 W 每轮
  泄漏 ~324MB（09:24 VH_KEEP 行引入，已进 1ef3964）——与记忆配方 #9「# 紧贴引号」同型坑
  （主线程在 ci-smoke 修过一次却在 run-models 漏改）。补空格修复，本轮跑批后 .tmp-mdl 零
  tmp. 残留（历史泄漏 1 个已清）。此坑曾直接再生产 /tmp 配额事故，教训并入配方 #9。
- **ci-smoke 编辑竞态**（测试代理 09:52 跑在 MDLTMP 相对路径旧版上连坐失败）：主线程 09:55
  绝对化修复，现行版全绿——非产品缺陷，如实记档。

**簿记项**：matrix 主行 86 + 子断言折叠（94 PASS 对账口径已注 MDL-024t 行）；round-1
「.part=112128B」为时序浮动值改约值措辞；`~/VideoHandler-Models` 空目录骨架（验收缺省根
残留）已清；`.tmp-mdl` 历史泄漏与代理调试残留已清。
