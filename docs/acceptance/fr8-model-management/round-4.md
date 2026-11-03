# round-4：片 B 审查轮 + 测试轮双报告甄别与修复（2026-11-03）

片 B（GUI 面）实现合并入主树后（round-3），按编排配方并行放审查代理（只读对抗审 16 文件 diff）
与测试代理（独立对抗验收，七组面）。两报告分级甄别（配方 #7：采纳前主线程亲手复现）后落地
六项修复 + 九个新用例，全门禁复跑全绿。

过程注记：两代理中途（12:05–12:07）因 API 五小时限额双双中断，12:44 限额重置后经消息恢复
原上下文续跑（已完成的取证不重跑），最终报告完整。

## 审查报告（十维：9 PASS / 1 FAIL〔spec 符合性〕）：P0=0；P1×2、P2×3、P3×4

### P1-1 自由导入模型被 bundled 遮蔽——D1 两级解析顺序跨 id 反转【已修】

- **机理**（主线程代码级复现成立）：`modelmanager.cpp` 目录条目未登记而 appDir 在位时标
  `installed+bundled=true`（path=appDir）；`ffextractor.cpp resolveDefaultModel` 取**首个**
  installed 不滤 bundled → 目录序在前的 bundled 条目压过目录外的库内自由导入条目。appDir
  有 gtcrn 随包 + 库内导入另一 aiDenoise 模型时（GUI 导入对话框恒开放该用途），用户导入的
  模型永不被选用——Windows 随包分发形态必然触发，v1 无档位选择 = 导入功能对该动词成死胡同。
  同 id 场景（GML-103）因登记分支先判不受影响，故 round-3 未暴露。
- **红**（主线程独立复现，build-linux 修复前 libengine + 探针 engine 相）：
  `MODELPATH /home/psh/vh-tmp/r4-red/app/models/denoiser/gtcrn_simple.onnx`（随包压过库内
  my-gtcrn，任务 ok=1 用错模型）。
- **修复**：resolveDefaultModel 选取条件加 `!bundled`（bundled 标志语义 = 「该条目当前解析到
  随包副本」，过滤后第一级恰为「库内登记」，appDir 兜底留给第二级，与 D1 两级语义精确对齐）。
- **绿**：同场景 `MODELPATH …/libe3/denoiser/my-gtcrn.onnx`；回归锚 GML-103b/bt 入编排
  （修复前该用例必红）。

### P1-2 D4 明文要求的 UI 注记「该源不受镜像影响」未实现【已修】

spec（fr8-model-management.md D4）明文「GTCRN 走 GitHub release 直连，文档与 UI 注记」，实现
零命中（主线程 grep 证实）。修复：模型管理对话框 endpoint 行下加注记标签（objectName
lblMirrorNote），文案「注：镜像前缀仅对 HuggingFace 源条目生效；GitHub 直连条目不受镜像影响。」，
GML-110n 整行锁字。NFR-C4/R4 镜像受众换源后「镜像没生效」自此有 UI 解释。

### P2 三项

- **P2-1 引导解药文案指向不存在的 UI 能力**【已修】：原文案「在模型管理中删除该条目」——
  对话框无删除功能（D5 明文不做 modelDelete）。改为「打开库目录，删除该模型文件后重新下载」
  （「打开目录」按钮在位；文件删后清单 stale → 重下治愈，F-2 幂等 else 分支支撑）。
  GML-114 只锁问询框计数，改文案零断言牵连。
- **P2-2 CLAUDE.md 未随片 B 同步**【已修】：FR-8 条目/方向节/架构树（mainwidget/mediaservice/
  modelmanager/ffextractor 四行）同步至片 A+B 状态。
- **P2-3 modelImport 无进度回调**【已修，跨片（根在片 A 代码、暴露面在片 B 对话框）】：复制环
  补 onProgress（0–99 按已复制字节、登记成功后收尾 100，与下载器同口径；试开/sha 段无粒度不报）。
  GUI 侧 runModelRequest 的 Ctx 早已接 progress 信号（ffextractor:231），核心层补回调即通，
  服务/壳层零改。真值锚 GML-116g（IMPORTPROG 100）。

### P3 四项（记档）

- **P3-1**：`E_MODEL` 字面量在 ffextractor:442 与 errCodeText 双源（结构化精确比对、无前缀匹配，
  合规；errinfo 无反向助手致消费方别无选择）——下次触碰时共享常量或加 errCodeFromText。
- **P3-2**：stale 自由导入行点「下载」→ E_PARAMS「未知模型 id」（id 不在目录表）；实际补救 =
  重新导入。记档不修（stale 行为边界，非本次范围）。
- **P3-3a 负例缺口已补**：GML-101n/nm（模型在位 + 输入缺失 → E_FILE 任务失败且 modelMissing=0，
  守卫「结构化信号仅随 E_MODEL」单条件负向钉死）；GML-114e 文案域断言维持可辩护（ORT 错误文本
  不定）不加强。
- **P3-4**：关窗时在途下载收尾最坏 30s（kTransferTimeoutMs，连接停滞须等下一 readyRead 见取消）
  ——与七槽长任务同风险族非片 B 新增，入 Windows round-4 冒烟清单。

### 审查轮附带的基线滚动

`fake-engine-e2e.sh` BASELINE_FILES + `src/service/modelmanager.cpp`（本轮导入进度修复新增
漂移；清单与主树实际漂移逐字一致已验证）。

## 测试报告（七组对抗面全 PASS）：D1×1（P2）、P3×2、覆盖空白清单

源态标注：测试代理分段标注 pre-fix / post-fix 壳+pre-fix 核心 / post-fix 全量（其 13:19 复验
链 13:17 重编 libengine），编排两轮 139 PASS 为 post-fix 源态。

### 七组结论（要点）

1. **两级解析对抗**：stale 跳过、for 错位跳过（目录 for 胜过清单篡改）、无根查询零副作用、
   wrong-content（假 aiDenoise 条目）setup E_MODEL → 结构化引导不崩溃不残留（post-fix 复验
   一致——`!bundled` 过滤按设计不覆盖内容校验，归引擎）。
2. **modelMissing 触发面**：解析位文件损坏（库内截断）也触发 E_MODEL→modelMissing（非仅缺文件）；
   坏导入 E_MODEL 不误发（用户已在对话框）。
3. **busy 对抗**：节流下载中七槽/二次下载/二次导入全静默丢弃且无迟到泄漏、恰一次 modelOpFinished、
   重开对话框单实例仍 busy、关框不复锁。
4. **取消对抗**：MID 双击取消 .part=2×64KiB 保留、REDL 精确偏移续传（服务日志 range=131072-
   status=206）、导入取消（落 setup 窗）零残留。
5. **QSettings**：自定义源/换根跨进程记忆、endpoint 键污染 → 命令层 E_PARAMS + 警告 + 放锁、
   modelRoot 空串归一缺省根。
6. **对话框数据**：空库渲染、ghost 条目「已失效」、下载/导入后列表真值刷新、打开目录对不存在
   根 mkpath 落建。
7. **编排完整性**：两轮 139 PASS / 0 FAIL / 0 SKIP、MDL-999 终闸双过、GML 每轮全新
   mktemp 库/config 零互相影响、仓库树无测试残留。

### D1｜P2｜刷新后选中恒重置首行 → 下载键静默改目标【已修】

- **机理**（代码级实锤，mainwidget.cpp onModelListDone 尾部）：列表刷新 clear() 销毁选中后
  无条件 `setCurrentItem(topLevelItem(0))`。选中 uvr → 下载（取消/失败/成功任一触发刷新）→
  选中跳回首行 gtcrn → 再点「下载」下的是另一个模型；且错击 gtcrn（非 hfSource，镜像无效）
  会向 github.com 发起真外联（代理沙箱 strace 实证：零次连本地服务、DNS→198.18.0.16:443）。
  此亦解开其 MID/REDL 三次复现的「Connection closed」迷案（错击直连条目所致，非下载器缺陷）。
- **修复**：刷新前记选中 id，重建后按 id 恢复；无原选中/id 已不在列表才缺省首行。
- **回归锚**：GML-115r（SELRESTORED uvr-mdx-voc-ft，gui 相下载完成刷新后断言）。

### P3 两项

- **downloadModel mkpath 先于 endpoint 校验**【已修】：非法 endpoint E_PARAMS 拒绝前留空
  `<root>/separator/` 目录（代理 fix/lib-pep 实证）。URL 组装+校验前提到一切落盘副作用之前；
  MDL-051 请求加隔离 root + 子断言 MDL-051d（root 不存在）。
- **垃圾相对 modelRoot 原样入标签**【记档】：展示层如实呈现用户输入；列表只读不建目录无害。
  相对根端到端下载未验证（同列入覆盖空白）。

### 覆盖空白补面与挂账

- **导入复制环中段取消**（代理自认未对抗；其取消落 setup 窗）——**本轮补**：mdl-cancel-probe
  新增 import-cancel 模式（首个进度回调即置 cancel → 确定性落进复制环首块后的检查点），
  MDL-093 三断言（E_CANCELLED / .part 即删 / 库树零 .onnx 落位）。该用例兼为 P2-3 的红绿锚：
  修复前无 onProgress → cancel 永不置位 → 导入跑完 DONE → 必 FAIL。
- 挂账（超本轮范围，记 goal.md）：Windows GUI 面/真 DISPLAY 原生对话框；openUrl 真实拉起
  （offscreen 平台 no-op，环境属性）；相对 modelRoot 端到端下载；同库多进程并发写 models.json
  （round-2 A7b 同族，非契约面）。

## 门禁（修复后全量复跑，build-linux 13:27 零警告构建）

| 门禁 | 结果 |
|---|---|
| run-models.sh（MDL 96 + GML 44 + 2 前置闸） | **142 PASS 全绿**（独立目录预验两轮 + build-linux 正式轮） |
| run-json.sh | 全绿 |
| run-ws.sh | 全绿 |
| thinshell-eqv.sh 7df4772 build-linux | 全绿（旧 7 动词 stdout/退出码/产物三方一致） |
| gui-slot-eqv.sh 7df4772 build-linux | 全绿（七槽零漂移） |
| fake-engine-e2e.sh（基线滚动至 8 文件） | 全绿 |
| ci-smoke.sh 全量 | 全绿（280 PASS；PAR-MD SKIP 为既有本地降级档，CI 以 PARITY_REQUIRED=1 强制） |
| 构建 | 零警告零错误 |

## 本轮改动清单（src 4 文件 + 验收基建）

- `src/service/ffextractor.cpp`：resolveDefaultModel 加 !bundled 过滤（P1-1）+ 注释同步
- `src/view/mainwidget.cpp`：镜像注记标签（P1-2）/ 解药文案改指向真实能力（P2-1）/ 刷新选中
  按 id 恢复（D1）
- `src/service/modelmanager.cpp`：导入复制环 onProgress（P2-3）/ URL 校验前置于 mkpath（P3）
- 验收：mdl-gui-probe（MIRRORNOTE/SELRESTORED/IMPORTPROG 输出 + ai-importshadow/ai-badinput
  相位）、mdl-cancel-probe（import-cancel 模式）、run-models.sh（fixture LIBE3 + 9 断言）、
  matrix.csv（round-4 列 + 9 行）、fake-engine-e2e.sh 基线滚动
