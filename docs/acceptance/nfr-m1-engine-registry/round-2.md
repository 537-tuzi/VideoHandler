# round-2（片 B：结构化错误通道 ErrInfo + 前缀匹配退役）

规格：`docs/spec/nfr-m1-engine-registry.md` D4 + §7.1 OQ-2/OQ-4。基线 832e49b（片 A 合入后）。
硬切一刀（OQ-2）：`classifyTranscodeError` / `classifyExtractError` / `isFileError` / `kErr*` 常量全数删除
（commandlayer.cpp −115/+62 量级），无双轨无兼容参数；错误码改在**产生点**与中文文案同点诞生（`ErrInfo`），
消费方透传 `errCodeText(err.code)`。

## 1. 改动面与构建

- 新增 `src/service/errinfo.h`（叶子头，无依赖）：`enum class ErrCode { None, Params, File, Model,
  Engine, Cancelled, Internal }` + `struct ErrInfo { code; message; isEmpty() }` + `errCodeText()`——
  `E_*` 字面量唯一来源（errinfo.h:31-43）；六类与命令层响应错误码一一对应（勘误见 §7-1）。
- `streamengine.h`：`virtual ErrInfo lastError() const { return {}; }`（契约注释同步「返回 ErrInfo」）。
- `aidenoiser` / `vocalseparator`：`lastError()` 改型 override，Impl 存 `ErrInfo errMsg`；process/flush
  catch 三层屏障（WP-1.0）落码位不变，仅改赋值形态 `{ErrCode::Engine, …}`。
- `fftranscoder` / `ffextractor`：内部 helper（open_decoder/open_wav/write_samples/open_input/
  open_output/write_header/copy_stream/drainEnhanced/feedAi/finish）返回 `ErrInfo`，公开签名
  `ErrInfo* err`（probeInfo/probe 保留 `QString*`——探测文案非错误码通道）。
- `commandlayer.cpp`：删除三分类器与 `kErr*`；全部失败点改 `errCodeText(err.code)` + `err.message`；
  `errorResponse` 公开签名 `const char* code` 不动（main.cpp 三处 `E_PARAMS` 信封字面量零改）。
- `engineregistry`：`setupErrorClass` 改 `ErrCode`（条目 None×2 / Model×2 / Engine）。
- `engine_test.cpp` 一处 `lastError().message`（spec §1 称其不引用 lastError，勘误 §7-3）。
- CMakeLists：engine 源表 + `errinfo.h`。
- 构建零警告：touch 全量重编（-Wall -Wextra）grep 无 warn/error（片 A 口径同款复跑）。

## 2. 新旧映射表（OQ-4 三处行为差 + 其余逐点等价）

旧逻辑（832e49b commandlayer.cpp:33-64）：`已取消`→CANCELLED；引擎 lastError 非空且
`startsWith("模型")`→MODEL、否则 ENGINE；isFileError 11 前缀→FILE；兜底 INTERNAL。
新逻辑：产生点赋码，消费点透传。逐文案对照：

| 文案（产生点） | 旧码 | 新码 | 断言 |
|---|---|---|---|
| 打不开文件：/读不到流信息：/文件里没有音频流/视频流/不认识的音频编码/解码器起不来/建输出容器失败：/输出文件建不了：/写文件头失败：/建输出流失败/拷贝编码参数失败 | FILE | File（fftranscoder.cpp:30-91 / ffextractor.cpp:24-66） | CMD-029/030/031 回归 + ERR-NOINPUT |
| 送包解码失败：%1（fftranscoder.cpp:252） | **INTERNAL** | **File（OQ-4 δ1）** | ERR-DECODE |
| 写包失败（fftranscoder.cpp:108 文案无 :%1 尾巴）/ 写包失败：%1（ffextractor.cpp:91） | **INTERNAL** | **File（OQ-4 δ2）** | ERR-WRITEPKG ×2 |
| 写文件尾失败：%1（fftranscoder.cpp / ffextractor.cpp:222/250） | **INTERNAL** | **File（OQ-4 δ3）** | ERR-TRAILER ×2（extract + transcode 双侧 e2e，见 §3/§5-3 更正） |
| 内存不够 / 重采样器起不来（fftranscoder.cpp:50/100/166/175/185 + ffextractor.cpp:82 pkt alloc） | INTERNAL | Internal（不变） | 代码级（不可注入） |
| 已取消（fftranscoder.cpp:242 / ffextractor.cpp:81） | CANCELLED | Cancelled（不变） | ERR-CANCEL 双路 + GUI 槽级「已取消」直通 |
| 模型输出布局不符 / dtype 非 float32 / 推理输出缺失（vocalseparator.cpp:216/224/228，**产生点赋 Model**，P1-1 保留且消费者前缀匹配退役） | MODEL（消费侧 startsWith("模型")） | Model（不变） | ERR-LAYOUT / ERR-DTYPE e2e；缺失分支 §5 弱点 2 |
| 人声分离推理失败 / 音频太短（vocalseparator.cpp:242/453） | ENGINE | Engine（不变） | CMD-032 同构 + run-json 回归 |
| AI 降噪推理失败：%1（aidenoiser.cpp:345/357） | ENGINE | Engine（不变） | CMD-032 全文（多行 ORT 文案保真） |
| 引擎 setup 失败（create→nullptr，setupErrorClass=Model/Engine） | MODEL/ENGINE（表字面量） | Model/Engine（不变） | ERR-SETUP-AI / ERR-SETUP-SEP |
| 信封校验 / 兜底 catch（commandlayer.cpp） | PARAMS/INTERNAL | Params/Internal（不变） | CMD-001..035 回归 |

行为差 **恰三处**（OQ-4 拍板的三条消息族、五个代码点，INTERNAL→FILE）；其余逐点等价。
`grep -rn 'classify|isFileError|kErr' src/` 零残留；`"E_` 字面量仅剩 errinfo.h（唯一源）与
main.cpp 三处既有信封点（该文件零改动）。

## 3. ERR 族新用例（本轮新增证据，ad-hoc 于 /tmp/nfr-m1-err）

全部走主树 `build-linux/video_handler --json`（ERR-SETUP/坏输出契约除外，见 §4）：

- **ERR-NOINPUT**：transcode/denoise 不存在输入 → `E_FILE`「打不开文件：No such file or directory」rc=1。
- **ERR-SETUP-AI / ERR-SETUP-SEP**（setup 期 create→nullptr，补片 A 审查 P3 空缺）：垃圾模型文件
  （非 protobuf）→ `E_MODEL`「加载 AI 模型失败：Load model … Protobuf parsing failed.」/
  「加载人声分离模型失败：…」rc=1。
- **ERR-DECODE（OQ-4 δ1）**：mp3 中段 2000 字节抹零（保头保首帧）→ `E_FILE`
  「送包解码失败：Invalid data found when processing input」rc=1，无半成品。
- **ERR-WRITEPKG-TC（δ2，transcode）**：`trap '' 25 + ulimit -f 4`（RLIMIT_FSIZE=4096B、SIGXFSZ 忽略
  使 write 返 EFBIG 而非杀进程；ulimit -f 单位=1024B 块）跑 5s 转码 → `E_FILE`「写包失败」rc=1，无半成品。
  （/dev/full 不可用：无扩展名路径在 avformat_alloc_output_context2 格式猜测期即拒，只证
  「建输出容器失败」既有 FILE 分支。）
- **ERR-WRITEPKG-EX（δ2，extract）**（测试轮配方勘误）：882KB wav 源 + `ulimit -f 100`（102400B，
  源超限中途 flush EFBIG）→ `E_FILE`「写包失败：File too large」rc=1，无半成品。原记配方
  （176KB + ulimit -f 64）在本机复跑落「写文件尾失败」分支（落盘分支取决于源大小×限额×avio/muxer
  缓冲时序；两分支均 E_FILE+无半成品，行为正确，唯验收记录配方与文案归类不自洽——测试轮 P3 更正）。
- **ERR-TRAILER（δ3，extract）**：ulimit -f 4 + 全量 <32KB 的 m4a（唯一落盘点在 trailer）→
  `E_FILE`「写文件尾失败：File too large」rc=1，无半成品。
- **ERR-TRAILER-TC（δ3，transcode）**（审查轮更正后补收）：4488B 单声道 s16（0.05s 正弦）+
  `trap '' 25 + ulimit -f 4` → `E_FILE`「写文件尾失败：File too large」rc=1，无半成品；预存同名
  输出也被失败清理路径删除。审查代理 20 组参数扫描 + 主线程复测（含实现方初报参数 ulimit -f 2
  与预存输出形态）全部同结果——**实现方初报「ok=true 静默截断 1692B」不可复现，系误记**
  （其「av_write_trailer 吞 EFBIG」机制论一并证伪：本构建 av_write_trailer 正确返负）。
- **ERR-CANCEL**（前段会话已证，转录）：预设 cancel 原子量探针直调 execute，transcode/extractAudio
  双路 → `E_CANCELLED`「已取消」rc=1，无半成品；GUI 槽级等价同步证「已取消」经 error.message
  →jobDone 直通（gui-slot-eqv）。
- **CMD-032 全文**（E_ENGINE 运行期）：维度错位模型 → `E_ENGINE`「AI 降噪推理失败：Got invalid
  dimensions for input: x … Got: 257 Expected: 100 …」多行 ORT 文案经 ErrInfo.message 保真往返，无半成品。

## 4. 坏输出契约两条（WP-1.4 make_bad_model.py 注入流，detached worktree）

worktree @832e49b + 片 B 13 文件临时 detached 基线提交（同 fake-engine-e2e 方法论，main 零痕迹）；
`make` 造 bad-layout/bad-dtype 两模型 → `inject` 其末 10MiB MD5 入 kMdxModelData 表（BEGIN/END
标记）→ 增量重编 → `--json separate` → `restore` → 还原复核（grep 零标记）→ worktree 清理：

- **ERR-LAYOUT**：`E_MODEL`「模型输出布局不符：需要 [1,4,3072,256] plane-major，实际 [1,4,256,3072]」rc=1 无半成品。
- **ERR-DTYPE**：`E_MODEL`「模型输出 dtype 非 float32（type=11）」rc=1 无半成品。
- 「模型推理输出缺失」分支不可构造（vocalseparator.cpp:216 的守卫被 :76/:363-370 setup 不变量
  封死：outIdx 按名字选定必 < 输出数）→ 代码级证据收口（spec §6 允许口径），同 WP-1.4 先例。

## 5. 弱点清单（诚实记录，未收敛项明确标注）

1. **不可注入路径留代码级**：内存不够（6 处：fftranscoder 5 + ffextractor pkt alloc 1）/ 重采样器
   起不来（2 处）无 e2e（分配失败无法在进程内可靠制造）；映射表逐点列明，代码 review 是其证据面。
2. **「模型推理输出缺失」不可构造**（上述 setup 不变量），P1-1 三条「模型…」中该条无 e2e——
   审查复核该不可达论证成立（vocalseparator.cpp:363-370 outIdx 不变量封死 :216 守卫），
   与 WP-1.4 round-1 同口径。
3. **（审查轮更正）transcode 侧 写文件尾失败 已有 e2e、初报「静默截断」系误记**：实现方初报
   「WAV muxer trailer 期吞 EFBIG → ok=true 截断 1692B，片 B 前即存在，建议另立 P3 债」——
   审查代理 20 组参数（json/薄壳 × tmpfs/ext4 × mono/stereo × ulimit 2/3/4 × 输出 2K–128K 窗口）
   + 主线程复测（含初报原始参数与预存输出形态）**全部**得 `E_FILE「写文件尾失败：File too large」
   rc=1 无半成品`（预存半成品亦被清理路径删除）。结论：缺陷不存在，撤销 P3 债建议；
   transcode 侧 trailer e2e 收进 ERR-TRAILER-TC（§3）。证据链教训：初报实证不可复现时以
   双反证裁决（工作原则 13/14），round-2 初稿该条属未实际跑通即记录——已更正。
4. **ERR 族为 ad-hoc 断言未入脚本**：本轮命令可复跑（round-2.md 即配方）但未固化 .sh——
   与片 A KIND 族同口径（分支证据制，spec §6）；固化与否随 ci-smoke 接线决策（OQ 未拍板）。
5. **probeInfo/probe 保留 QString\* err**：探测面文案非错误码通道（GUI 探测槽只显文案），
   有意不改；若后续 Agent 面需要结构化探测错误再扩。

## 6. 全量回归（主树 build-linux，片 B 形态）

- ci-smoke 全绿：FFT 属性 + 分块不变性（dsp/pitch/ai/sep×2）+ parity 四族 + CLI 冒烟 7 动词 +
  ffprobe 体检 + run-json 36（零漂移：负例码与文案逐字断言含 CMD-029/030/031/032）。
- thinshell-eqv.sh 832e49b：16/16（12 成功 + 4 失败路径 stdout 逐字 + 退出码 + 产物 cmp）。
- gui-slot-eqv.sh 832e49b：nomodel 15 + withmodel 16 槽次逐字 + 8 产物 cmp + 「已取消」双路直通。
- fake-engine-e2e.sh 全绿（脚本随片 B 同步：基线拷贝清单 4→14 文件 + 注入条目 setupErrorClass
  改 ErrCode::None 字面量；REG-001..011 + REG-002r RMS 0.0000% 复跑）。
- ZERO-DIFF：`git diff --stat 832e49b -- src/ CMakeLists.txt` 恰 14 文件（13 改 + CMakeLists+1）
  + 新增未跟踪 errinfo.h；`main.cpp`/`commandlayer.h`/`dspdenoiser`/`pitchshifter`/`mediaservice`/
  `view/` diff 为 0 行。

## 7. 勘误与备注

1. **spec D4 ErrCode 枚举清单漏 Params**（字面列 5 类+None）：按任务书「六类 + None」实现
  （Params/Files→File/Model/Engine/Cancelled/Internal + None），与命令层六错误码口径一致
  （CLAUDE.md「六类错误码 E_*」）；spec 不回改（开工快照），以本勘误为准。
2. **spec §1 称 engine_test 不引用 lastError**：实引一处（engine_test.cpp:146
  `lastError().message`，类型改型连带一字 token）；REG-001 白名单不受影响（engine_test 非金标准
  改动面）。
3. **旧 setupErrorClass 为 `const char*` 字面量**（"E_MODEL"/"E_ENGINE"）：片 A 审查 P3-1 双源
  隐患随枚举化一并收敛——条目现持 `ErrCode`，字面量唯一源 errinfo.h。
4. runEngine setup 守卫保留片 A 审查 P2-1 形态：Q_ASSERT(条目非 None) 开发期拦 + 三目兜底
   ErrCode::Engine。（审查轮勘误：原写「当前五条目 setupErrorClass 均非 None」有误——transcode/
   denoise 两条目即 `ErrCode::None`（engineregistry.cpp:143/:153）；兜底不可达的真正理由是
   None 条目的 create 闭包（DspDenoiser::setup）无失败路径、永不返回 nullptr。）
5. RLIMIT_FSIZE + `trap '' 25`（SIGXFSZ 忽略，信号处置经 exec 继承）是写失败注入的可用配方，
  /dev/full 因格式猜测期早退不可用——供后续坏路径验收复用。注：`ulimit -f N` 单位为 1024B 块
  （-f 4 = 4096B；本文件早期版本误注为 2KB，审查轮已更正）。

## 8. 审查轮（对抗式审查代理，合入前）

十维度审查：**无 P0；P1×1 + P2×4 + P3×3**，处置如下——

- **P1 trailer「静默截断」推翻**（§5-3 已按裁决更正）：审查 20 组参数扫描全得正确 E_FILE，
  主线程 3 组复测（含初报原始参数与预存输出形态）同结果——三路独立实验一致，初报系误记，
  P3 债撤销、ERR-TRAILER-TC 补收、机制论（av_write_trailer 吞 EFBIG）证伪。
- **P2×4 全修**：fake-engine-e2e.sh 基线清单完整性断言（`git diff --name-only HEAD` ∪ untracked
  与清单全等比对，防纯逻辑改动漏拷→静默测旧代码；主线程复跑全绿）；§7-4 事实更正（None 条目
  真因 = DspDenoiser create 永不失败）；映射表补 ffextractor.cpp:82（内存点 5→6）；写包失败
  文案列精确化（fftranscoder 侧无 :%1 尾巴）。
- **P3×3**：ulimit 单位注解已更正；errinfo.h isEmpty() 加吞错形态 Q_ASSERT（主线程落码，
  run-json 36 + fake-engine-e2e 复证 no-op）；errCodeText(None) 自洽观察项不改。
- 十维度全 PASS 附实证：产生点穷举（fftranscoder 18 点 / ffextractor 9 点 / ORT 引擎 7 点 /
  命令层硬编码）零吞错形态；行为差恰三族五点（旧 isFileError 11 前缀 11/11 对应）；FFmpeg
  释放路径零改动；GUI 红线 0 行 diff；「内存/重采样注入不可行」与「输出缺失不可构造」论断复核成立。

## 9. 测试轮（独立测试验收代理，合入前）

**6/6 任务 PASS，零 P0/P1/P2，P3×1（配方勘误，已按其可复现组合更正 §3）**：

- 基础面：btree 从零全量构建零警告；run-json 36 全绿（Q_ASSERT 版重建后复跑再绿，证据锚定
  04:31:54 二进制）。
- ERR 族逐条 JSON 原文 + rc + 半成品复核：坏模型双动词 / 8 动词不存在输入 / 源中段损坏 /
  写失败注入双侧 / 坏输出契约注入复现（make→inject→separate→restore，还原后 cmp 主树逐字节一致，
  注入前先行使 MD5 守卫）/ transcode trailer EFBIG → **E_FILE 非 ok=true（独立复证审查 P1 裁决）**。
- **OQ-4 三差的新旧二进制对照**：worktree 832e49b 旧版 vs 片 B 新版同素材——δ1 送包解码 /
  δ2 转码写包 / δ3 提取写尾 旧 `E_INTERNAL` → 新 `E_FILE`（同文案），行为差实证三连。
- CMD-032 双树对比：error.message 逐字节一致（python `==` 比对 True，多行 ORT 文案含换行保真）。
- 吞错形态对抗（空文件/目录当 input/输出目录不存在/模型路径是目录/output 指向目录）全部
  rc=1 + 正确码 + 合法 JSON，零「ok:true 但产物异常」。
- 引擎直调：engine_test dsp/pitch 三跑 rc=0 输出等长——lastError 改型不影响 DUT。
- 任务书笔误备案（非缺陷）：主线程 brief 期望串「加载 AI 降噪模型失败」有误，基线/实际为
  「加载 AI 模型失败」（aidenoiser.cpp:319，新旧逐字同串），片 B 逐字保持是正确行为。
