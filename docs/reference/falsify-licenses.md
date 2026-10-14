# 许可证结论反证审查（falsify-licenses）

## 元信息

- **性质**：反证审查——**不查证既有结论，而是假设其错误，专门寻找能推翻它的一手证据**。找不到反证只记「暂未找到反证」，不写成「证实」。
- **日期**：2026-10-14
- **针对的 research 文档**：
  - `docs/reference/research-rvc-voice-conversion.md`（ContentVec = MIT；O2 关闭依据）
  - `docs/reference/research-gtcrn-sherpa-onnx.md`（GTCRN = MIT；O1 关闭依据）
  - `docs/reference/research-model-mgmt-llm-infra.md`（Qwen3 全系 Apache-2.0；O4 关闭依据）
  - `docs/reference/research-realtime-audiostack.md`（VB-CABLE 可随包分发）
- **落入基线的结论**（REQUIREMENTS v1.1：§6 模型红线表、§8 R1 降级、§10 O1/O2/O4 关闭、§5.1 虚拟声卡行）为本审查的直接作用对象。
- **信源原则**：一手优先——许可证原文、官方仓库文件（codeload tarball 本地解包）、HF 仓库 API 元数据与历史修订、arXiv/ar5iv 论文全文、官方条款页逐字重取。二手搜索摘要只用于定位，不作结论依据。
- **网络限制**：api.github.com 配额中途耗尽（改用 github.com HTML 与 codeload）；huggingface.co 直连不通（经 hf-mirror.com 的 `/raw/` 与 `/api/` 取原文与元数据）；raw.githubusercontent.com / jsDelivr 间歇超时（改用 github.com `/raw/` 与 API）。
- **核验方式**：fairseq 与 ContentVec 均下载完整源码包本地 grep；Qwen 组织 469 个 HF 仓库全量清点 license tag；VB-CABLE 官方驱动包实际下载解包读 EULA。所有引文为逐字摘录。

---

## 目标 1：ContentVec = MIT（最重点）

**待反证结论**（research-rvc §2.2/§2.3；基线 §6/§8 R1/§10 O2）：仓库 MIT + 作者 issue #34 确认预训练权重同 MIT；CC-BY-NC-4.0 说法无一手出处。

### 反证 (a)：fairseq examples/hubert 页面对 HuBERT 预训练 checkpoint 是否声明 CC-BY-NC-4.0

**结果：否——该页从未载有任何许可证文案，CC-BY-NC-4.0 假设被一手证据否定。**

- 当前 main 分支 `examples/hubert/README.md`（5079 B，GitHub API raw 全文核验）：全文无 license / CC-BY / NC / non-commercial 字样；只有预训练/微调模型下载表（`dl.fbaipublicfiles.com/hubert/hubert_base_ls960.pt` 等）与训练说明。
- 提交历史（API）：该文件仅 7 次提交（2021-05-22 ～ 2022-01-25）。取 2022-01-25 版本（`4a7835b7`）逐字比对：**与当前版本完全一致（同为 5079 B，无许可文案）**——历史上也不曾有过。
- **fairseq 全仓反查 CC-BY-NC 真实落点**（tarball 解包后 grep 全部 md/txt/rst/py，仅 6 处，无一涉及 HuBERT）：
  - `examples/mms/README.md:201`："The MMS code and model weights are released under the CC-BY-NC 4.0 license."
  - `examples/mms/MODEL_CARD.md:11`："**License**  CC BY-NC"
  - `examples/mms/zero_shot/README.md:63`："The MMS Zero shot code and model weights are released under the CC-BY-NC 4.0 license."
  - `examples/data2vec/README.md:16`："Vision models only are license under CC-BY-NC."
  - `examples/MMPT/README.md:166` 与 `setup.py:20`（CC-BY-NC）
  - `examples/womens_bios/README.md:76`（数据集 CC-BY-NC）
- **Meta 官方 HuBERT 权重分发点（HF facebook 组织）逐仓库核验**（`/api/models/<repo>` 的 tags + cardData）：

  | 仓库 | license tag | lastModified |
  |---|---|---|
  | facebook/hubert-base-ls960 | apache-2.0 | 2021-11-05 |
  | facebook/hubert-large-ll60k | apache-2.0 | 2021-11-05 |
  | facebook/hubert-xlarge-ll60k | apache-2.0 | 2021-10-20 |
  | facebook/hubert-large-ls960-ft | apache-2.0 | 2022-05-24 |
  | facebook/hubert-xlarge-ls960-ft | apache-2.0 | 2023-06-27 |

- **更进一步：facebook/hubert-base-ls960 的 README 历史修订全查**（commits API 列出全部 10 次提交，逐 revision 抓 README）：自 2021-06-16 README 创建起的**每一个历史版本** frontmatter 均为 `license: apache-2.0`（更早的 2021-05-26 两次提交无 README）。即该权重分发点上**从未存在过 CC-BY-NC 标注**。

来源：
- https://github.com/facebookresearch/fairseq/blob/main/examples/hubert/README.md
- https://api.github.com/repos/facebookresearch/fairseq/commits?path=examples/hubert/README.md
- https://hf-mirror.com/api/models/facebook/hubert-base-ls960 （cardData: `{"license": "apache-2.0"}`）
- https://hf-mirror.com/facebook/hubert-base-ls960/raw/<各历史revision>/README.md

### 反证 (b)：ContentVec 是否从 HuBERT-base 权重初始化（衍生权重能否被单方再许可 MIT）

**结果：公开训练配方中不存在 HuBERT 权重初始化——research 文档「ContentVec 从 facebook/hubert-base-ls960 初始化」这一表述与论文、训练配置、README 三份一手材料均不符（实为教师标签关系）。**

- 训练配置 `contentvec/config/contentvec/contentvec.yaml`（codeload tarball 逐行读）：无 `restore_file`、无 `w2v_path`、无任何 checkpoint 加载字段；`checkpoint:` 节只控制保存策略。`run_pretrain_single.sh` / `run_pretrain_multi.sh` 同样不含任何预训练权重路径。
- 论文（ar5iv 全文，arXiv 2204.09224）：
  - HuBERT-base 的用法是**教师标签**："After passing the converted utterance to the pretrained HuBERT, the seventh layer feature representation is chosen"；"ContentVec is guided by a pretrained HuBERT as teachers"；
  - 摘要："Our approach is adapted from the HuBERT framework"（框架/代码适配，非权重继承）；
  - 全文检索 "initializ/from scratch/warm-start"：无学生模型权重初始化表述（唯一 "initialization" 命中是页面 JS 样板）。
- 代码衍生方向合规：`contentvec/models/hubert/contentvec.py` 文件头为 fairseq 的 MIT 版权声明（"Copyright (c) Facebook, Inc. … licensed under the MIT license"）——代码从 MIT 仓库衍生，无污染。
- **对结论的影响**：既然公开配方是从零训练 + HuBERT 特征做教师标签，「作者能否把 Meta 衍生权重单方 MIT 化」这一法律疑虑本身失去事实基础；即便保守假设存在未公开的初始化，Meta HuBERT-base 权重也已由 (a) 证明为 apache-2.0 分发，链条中同样不含 NC。
- **保留的不确定性（诚实标注）**：公开配方只能证明"发布物按从零训练描述"，无法证明训练实况（无法证伪负命题）；此不确定性不影响许可结论，只影响对 research 文档该句的修正。

### 反证 (c)：issue #34 全文细读

**结果：措辞明确覆盖权重，无任何打折或后续限制。**

- issue 正文为空（API `body: null`），标题即全部提问："Is the pretrain model also MIT license?"（2025-03-04）。
- 全部评论仅 1 条：**auspicious3000（author_association=OWNER）2025-03-18 回复 "Yes"**，issue 由 auspicious3000 本人关闭。
- 无后续回复、无 "except/only for/non-commercial" 之类限定词。
- 作者身份权威性加固：ContentVec 论文（Interspeech 2022，作者 Kaizhi Qian、Yang Zhang、Heting Gao、Junrui Ni、Cheng-I Jeff Lai、David Cox、Mark Hasegawa-Johnson、Shiyu Chang；Microsoft/UIUC）脚注原文 "Our code is available at https://github.com/auspicious3000/contentvec"——论文官方指认该仓库为作者团队仓库，回复者即仓库所有者。
- 附：LICENSE 文件本身（本地解包全文）为标准 MIT 文本，`Copyright (c) 2019`（无署名的裸年份），由单次提交 `5f7f07a` "add license" 于 2022-10-16 加入（与 issue #2 时间线吻合，repo 创建于 2022-06-17）。

来源：https://api.github.com/repos/auspicious3000/contentvec/issues/34 （及 /comments）

### 反证 (d)：RVC 生态实际怎么处理 contentvec 权重

**结果：全部用户自取，无任何项目内置分发，无任何一方标 NC。**

- RVC 官方仓库根 `assets/` 目录实测（API contents）：**只有 `pymss_weights/`，不含 hubert 权重**；README 原文（2026-10 快照）："模型请从 [Hugging Face 模型仓库](https://huggingface.co/lj1995/VoiceConversionWebUI/tree/main) 下载，并保持以下路径：assets/hubert_base/…"，并给出 `hf download lj1995/VoiceConversionWebUI` 命令。
- RVC 根目录《MIT协议暨相关引用库协议》原文将 ContentVec 列为 MIT（链接指向其代码仓库 LICENSE）。
- 实际分发点 lj1995/VoiceConversionWebUI：repo 元数据 `license: mit`，模型卡为空（2026-08-01 仍在更新）——与原调研一致。
- HF 上 13 个 contentvec 相关副本（`/api/models?search=contentvec`）：lengyue233/content-vec-best（159690 下载）、TechInterMezzo/contentvec-500、safe-models/ContentVec 等均标 MIT，其余未标注；唯一 CC 系是 furukawamea/contentvec-JP-JVS 标 **cc-by-4.0**（上传者对自己微调副本的再选择，不约束原始权重）；**没有任何副本标 NC**。

### 反证 (e)：「ContentVec 是 CC-BY-NC-4.0」的源头追溯

**结果：第三轮溯源仍未定位一手出处；本轮换角度验证的「源头= fairseq HuBERT checkpoint 许可」假设被否定。**

- 按任务提示的角度 (e)——"会不会源头是 fairseq HuBERT checkpoint 的许可被讹传安到 ContentVec 头上"：**不成立**。见 (a)：fairseq HuBERT 页面从无许可文案，Meta HF 卡自始 apache-2.0。讹传若存在，其源头不是 fairseq HuBERT。
- fairseq 生态中真实存在的 CC-BY-NC 锚点是 MMS（代码+权重 CC-BY-NC 4.0）、data2vec（视觉版）、MMPT——「fairseq 里有模型是 NC」泛化到 HuBERT 再滑到 ContentVec 是**可能的传播路径（此句为推测，非证据）**。
- 本轮 WebSearch 捕获一个讹传活样本：AI 生成的搜索摘要声称 "the underlying hubert_base checkpoint … was released under Fairseq's non-commercial research license"，但该摘要自认 "couldn't fetch the exact hubert_base license page"——二手推断被一手证据（(a) 表）直接矛盾。这解释了讹传如何自我强化，但不是源头。
- 二手未核验线索（仅记录，不作依据）：搜索结果称 Vevo 论文（arXiv 2502.07243）自身带 "License: CC BY-NC-SA 4.0"——衍生工作自选 NC 许可不构成 ContentVec 权重许可的证据。

### 判级：**【站得住】**（且证据强于原调研）

- 权重 MIT 结论维持：作者 OWNER 身份对 "pretrain model 是否 MIT" 的明确 "Yes" + 全分发链（IBM Box 原发点之外的所有可达节点：lj1995 HF、13 个副本、RVC/so-vits 引用）零 NC 文本。
- 本轮还**收窄了原调研自记的残余盲点**：原调研称「Meta 原始 HuBERT-base 权重无显式许可文本（HF 侧栏 mit 未能一手抓取）」——本轮已一手抓到：`cardData.license = "apache-2.0"`（frontmatter 正文级，非仅侧栏），且十个历史修订一贯。基线 §8 R1 的残余风险可再降半档（"商业化前法务复核"流程性动作可保留）。
- **需修正一处 research 文档表述**（不改变结论）：research-rvc §2.4 "ContentVec 从 facebook/hubert-base-ls960 初始化" 应改为 "ContentVec 的教师标签由 pretrained HuBERT-base 第 7 层特征经声音转换 + k-means 生成；公开训练配置不含权重初始化"。相应地，基线 §6 ContentVec 行括注"RVC 用 ContentVec 再训练版"中的"再训练（自 HuBERT）"含义也应澄清。

---

## 目标 2：GTCRN = MIT

**待反证结论**（research-gtcrn §3；基线 §6/§10 O1）：gtcrn_simple.onnx 权重随上游仓库以 MIT 发布，出处链 sherpa run.sh → 上游导出。

### 反证取证

1. **上游仓库身份真伪**：README 首句自述 "This repository is the official implementation of the ICASSP2024 paper"（tarball 原文）。身份交叉绑定（本轮新增）：作者另一仓库 Xiaobin-Rong/ul-unas 对应 arXiv 2503.00340，arXiv API 作者名单为 "Xiaobin Rong, Leyan Yang, Dahan Wang, **Yuxiang Hu, Changbao Zhu, Jing Lu**"——后三人与 GTCRN 论文作者群（Xiao-Bin Rong, …, Yu-Xiang Hu, Changbao Zhu, Jing Lu）重合，同一课题组一作；GitHub 账号名与论文一作对应。**非第三方再上传**。同作者还有 TRT-SE 模板仓库（上游 stream/README 自述转换方法来源）、gtcrn_demo 音频演示仓库，与"官方实现"自洽。
2. **LICENSE 提交历史**：`commits?path=LICENSE`（HTML 页解析）显示 LICENSE 于 **2024-04-17 22:09 (+09:00)** 一次提交入库——ICASSP 2024 会期（2024-04-14～19）当周，与论文/仓库首发同步。**不是事后迫于压力补加**。（对照：ContentVec 的 LICENSE 是 repo 创建 4 个月后才加，见目标 1(c)。）
3. **sherpa-onnx release 页许可表述**：release `speech-enhancement-models` 页（HTML 实抓）全文无 license 字样，仅有出处句原文："gtcrn gtrcn_simple.onnx is from https://github.com/Xiaobin-Rong/gtcrn"（csukuangfj 发布，GPG 签名）。**证实原调研记录的缺口（release 页无逐字模型许可）依然存在**。
4. **sherpa 文档页**：仅 "This model is from https://github.com/Xiaobin-Rong/gtcrn"，无许可声明（与原调研一致）。
5. **HuggingFace 同名副本许可证**：`/api/models?search=gtcrn` 全量 14 个副本——MIT（8 个：haoxiangsnr/clarity__cec2__gtcrn、TigreGotico/audiosronnx-gtcrn、andrelgomes/gtcrn-stream、vpermilp/gtcrn、agentable/gtcrn-simple-mlx、maxffarrell/gtcrn-coreai、Vasubuha/gtcrn-speech-enhancement、AXERA-TECH/H-GTCRN.AXERA）、apache-2.0（inoryQwQ/sherpa-onnx-gtcrn-ax650）、**无许可标注 4 个，无任何更严/NC 许可**。k2-fsa 官方在 HF 无 gtcrn 仓库（模型只经 GitHub release 分发，`author=k2-fsa&search=gtcrn` 为空）。

### 判级：**【站得住】**

- 上游=论文官方仓库（README 自述 + 作者名单交叉绑定）、LICENSE 与仓库同龄、全生态无任何更严许可副本——反证四路（身份、历史、release 表述、HF 副本）均未命中。
- 维持原判的完整性标注：结论级仍是【倾向】而非【已确认】，唯一缺口不变——sherpa release/文档页无逐字模型许可声明；基线 §10 O1 的"发版前向 sherpa 官方书面确认一次"动作保留。

---

## 目标 3：Qwen3 全系 Apache-2.0

**待反证结论**（research-model-mgmt §3；基线 §6/§10 O4）：Qwen3 全系 Apache-2.0，官方/unsloth GGUF 可再分发。

### 反证取证

**方法**：HF Qwen 组织全部 469 个仓库清点（`/api/models?author=Qwen`，license tag 直读）。结果：apache-2.0 = 322，**license:other = 132**，未标注 = 15。

1. **「全系」被直接反例推翻——Qwen3-Omni 三个变体不是 Apache-2.0**：
   - Qwen/Qwen3-Omni-30B-A3B-Instruct / -Thinking / -Captioner 的机器标签均为 `license: other`。
   - 该仓库 frontmatter 自相矛盾：`license: other` + `license_name: apache-2.0`；**仓库文件树（API tree/main）中不存在任何 LICENSE 文件**（15 个 safetensors + 配置文件而已）。
   - 这与 QwenLM/Qwen3 GitHub README 的现行原文直接冲突："All our open-weight models are licensed under Apache 2.0. **You can find the license files in the respective Hugging Face repositories.**"——Omni 仓库里没有许可文件可找。
2. **2025–2026 新版本改用其他许可证的先例（实锤，且就是最新一代）**：
   - **Qwen/Qwen3.8-2.4T-A95B（及 -FP8）**：`license_name: qwen3.8-max`，LICENSE 原文为 "Qwen3.8-Max License"——MIT 式宽授权叠加两条额外义务：
     > "1. …If the Software (or any derivative works thereof) is Used for any of the licensee's commercial products or services that have more than 100,000,000 monthly active users or US$ 20,000,000 … monthly revenue, respective model name must be prominently displayed on the user interface of such product or service; and,
     > 2. If the licensee or any of its affiliates conducts a Model as a Service or AI Work Assistant business, and the aggregate revenue … exceeds US$50,000,000 … the licensee shall obtain a separate license from Qwen before Using the Software or its derivative works for any commercial purpose."
   - **Qwen/Qwen3.8-Flash-Next（及 -FP8）**：`license_name: qwen-community-1.0`，LICENSE 原文 "Qwen Community License 1.0"——同样的展示义务条款 + MaaS/AI 助手业务另行授权（无收入门槛）。
   - 注意同系列混用：Qwen3.8-27B（及 -FP8）仍是 apache-2.0——**同一代内不同型号许可不同**，逐仓库核对不可省。
3. **其余家族逐一核对为 apache-2.0（暂未找到更多反例）**：Qwen3 dense 全系（0.6B–235B-A22B 含 2507 版）、Qwen3-Coder（30B/480B/Next）、Qwen3-Embedding/Reranker（0.6B–8B 含 GGUF）、Qwen3-Next-80B、Qwen3-VL 全系（2B–235B）、Qwen3-TTS/ASR/ForcedAligner/Guard、**Qwen3.5 全系（0.8B–397B-A17B）、Qwen3.6 全系（27B/35B-A3B）**。
4. **历史版本 license:other 的构成**（印证"Qwen 系不能一概而论"）：Qwen v1、Qwen1.5 全系、CodeQwen1.5、Qwen2/Qwen2.5 的 72B 及 Math/Omni/VL 大尺寸、QVQ-72B-Preview、Qwen-Image-2.1、SAE-Res 系列研究产物等共 132 个（Tongyi Qianwen LICENSE 等自定义许可）。另有 15 个无标注（Qwen-14B、Qwen-VL、Qwen2.5-VL-3B-Instruct 等）。原调研"Qwen2.5 及更早存在非 Apache 自定义许可"的【未确认】项本轮坐实为可枚举事实。
5. **unsloth GGUF 与基础模型一致性**：unsloth/Qwen3-4B-GGUF、unsloth/Qwen3-235B-A22B-GGUF、unsloth/Qwen3-Coder-480B-A35B-Instruct-GGUF、unsloth/Qwen3-Next-80B-A3B-Instruct-GGUF 四仓库 frontmatter 全部 `license: apache-2.0` + `license_link` 指向 Qwen 官方仓库 LICENSE；README 全文扫描**无任何附加条款**（无使用限制、无署名追加义务）。官方 Qwen/Qwen3-4B-GGUF 亦 apache-2.0 且仓库含 LICENSE 文件（Qwen/Qwen3-4B 文件树实测有 LICENSE）。
6. **与许可证并行的使用政策/品牌限制**：对 Apache-2.0 部分，未发现独立使用政策文档（暂未找到反证）；Apache-2.0 第 6 条本身不授予商标权（标准条款，再分发不得用 Qwen 名义背书）。新自定义许可（qwen3.8-max / qwen-community-1.0）实质就是与许可并行的品牌展示义务——但仅适用于上述 3.8 系四个仓库。

### 判级：**【需修正】**

- 反例成立：**「Qwen3 全系 Apache-2.0」应收窄为「Qwen3 文本/多模态 LM 家族除 Omni 外均为 Apache-2.0；Qwen3-Omni 三变体为 license:other（卡面矛盾且无 LICENSE 文件）；2026 年 Qwen3.8 旗舰与 Flash-Next 已转用带 UI 展示义务与 MaaS/AI 助手业务另行授权条款的自定义许可」**。
- 对基线的实际影响：
  - O4 的选型范围"官方/unsloth 源"针对 Qwen3-4B-GGUF（apache-2.0、LICENSE 文件在库、unsloth 无附加条款）——**该选型本身不受反例影响，维持**。
  - 但 O4 的措辞与 §6 Qwen 行"Qwen3 全系"应加限定，并新增一条规则：**未来引入任何新版本 Qwen（3.5/3.6/3.8/更新）必须逐仓库核对 frontmatter `license` + 仓库内 LICENSE 文件**（3.8 已证明同代混用两种许可；Omni 已证明 README 总述可被单个仓库的元数据推翻）。
  - FR-8 模型管理器的工程含义：不能只信 HF 侧栏 tag——Omni 的 `license: other + license_name: apache-2.0` 矛盾标注说明需同时校验仓库内 LICENSE 文件存在性与内容。

---

## 目标 4：VB-CABLE 可随包分发

**待反证结论**（research-realtime §1.4.1；基线 §5.1 虚拟声卡行、§2 红线呼应）：主 VB-CABLE 可随安装包静默分发，条件为标注来源 + donationware 提示 + 保留付费通道；A+B/C+D 禁捆。

### 反证取证

1. **官网 licensing.htm 逐字重取（2026-10-14）**：与 research 文档引文**逐字一致**——"[1] We allow to distribute VB-CABLE package … and possibly embed it in your installation package (VB-CABLE silent installation) if our donationware model is applicable…"、"[2] …you will have to buy the requested number of VB-CABLE licenses…"、"Other VB-CABLEs like VB-CABLE A+B or C+D cannot be distributed or bundled with other product."；卷量价目（3.61/2.92/2.50 EUR）与 affiliation 建议（"it is simpler to invite the user to install it as a prerequisite"）均未变。归纳无出入。
2. **【新反证，一手】官方驱动包内的 EULA 与官网条款实质不一致**：实际下载 VBCABLE_Driver_Pack45.zip（download.vb-audio.com，包内 readme.txt 2024-10-08），LICENSE 节原文：
   > "As the VB-CABLE is a donationware, it is also allowed to copy and diffuse the VB-CABLE package AS IS without any modification. **It is not allowed to integrate the VB-CABLE package in another software installation procedure without Author agreement.**
   > In case of distribution/diffusion you **might** mention:
   > 1- The origin of VB-CABLE : www.vb-cable.com.
   > 2- VB-CABLE is a donationware, all participations are welcome."

   与官网条款的两处出入：
   - **集成权限**：EULA 只无条件允许"原样复制/散布（AS IS）"；把 VB-CABLE **集成进其他软件的安装流程**需要 "Author agreement"。官网 licensing.htm [1] 的条件式允诺（含静默安装）可以主张构成作者事先公开的同意，但这是对两份文本冲突的解释性调和——包内 EULA 是最终用户实际接受的许可文本，且其措辞更严。
   - **署名强度**：官网是义务式 "You will have to … mentioning at least"；EULA 是建议式 "you **might** mention"。按更严者（官网）执行即可，此处不构成放宽风险，但进一步说明两文本不同源。
3. **shop.vb-audio.com Terms of use 第 3.4 节**（research 文档未覆盖，licensing.htm 明确指向）：定义**付费经销商许可**——"For each ordered distributor licence VB AUDIO grants a non-exclusive, non-assignable right … (i) put up for sale, promote, distribute or resell a licence … (ii) grant a sub-licence for use of the APPLICATIONS to end users … in the form of a combined product, possibly incorporated in a specific package…"，一 license 一终端。这是专业域/商业分发的正式通道，不推翻官网 [1] 的免费路径，但确认商业分发存在独立条款体系（需要按量购 license，或按 licensing.htm [1] 满足 donationware 模型条件）。
4. **打包分发被作者要求停止/收费的案例**：两轮定向检索（VB-CABLE bundle / redistribution prohibited / Vincent Burel forum permission / forum.vb-audio.com distribute）**未找到任何公开案例**——无 DMCA/停止函/论坛纠纷记录（暂未找到反证）。
5. FAQ/论坛的口头额外限制：论坛检索未命中相关帖子（forum.vb-audio.com 帖子未被搜索引擎索引，无法穷尽——标注为盲点）。

### 判级：**【需修正】**

- 官网条款归纳本身无误（逐字复核一致、无纠纷案例），但结论需收窄：
  - **安全无争议**：引导用户自装（affiliation/referral 路线，官方明示更简单）；或原样（AS IS）复制散布驱动包 + 按官网义务标注来源与捐赠提示。
  - **有解释空间**：把 VB-CABLE **静默集成进 VideoHandler 安装器**——官网 [1] 允许，但包内 EULA 要求 "Author agreement"。两份一手文本冲突，保守做法是先取得 VB-Audio 书面同意（或按 licensing.htm [1] 全套条件留痕并视为 standing agreement，接受残余法律解释风险）。
- 对基线的落点：§5.1"VB-CABLE 可随包分发——条件为标注来源 + donationware 提示 + 保留付费通道"应补一句"**静默集成进安装器需按包内 EULA 取得作者同意（官网条款与包内 EULA 存在不一致）**"；research-realtime §2 的"把静默打包安装作为获用户明确同意后的可选项"建议升级为"获得 VB-Audio 书面同意或改走引导自装"。当前默认路线（引导自装 + 缺失检测指引）不受影响。

---

## 顺带发现的新风险（本轮新增）

1. **HF 元数据不可尽信**：Qwen3-Omni 的 `license: other` + `license_name: apache-2.0` 矛盾标注、lj1995/VoiceConversionWebUI 的空模型卡 + 仅 repo 级 tag、bartowski 无 license 字段——FR-8 模型管理器登记模型时应同时记录「repo tag + 仓库内 LICENSE 文件哈希」，THIRD-PARTY 清单（NFR-C2）以此为证据单元。
2. **RVC 官方引用文件的许可标注混乱**（旁证 NFR-C3 类风险）：其《MIT协议暨相关引用库协议》把 FFmpeg 同时标注为 "LPGLv3 License / MIT License" 两行并列（拼写 LGPL 亦错）——生态上游自身的合规标注不可直接采信，本项目第三方清单须独立核验（现行做法正确）。
3. **fairseq 生态 CC-BY-NC 真实边界**（未来引入模型时的排查表）：MMS（代码+权重 CC-BY-NC 4.0）、data2vec 视觉版、MMPT、womens_bios 数据集带 NC；HuBERT/wav2vec2 系则 apache-2.0/MIT。若未来 FR-A* 需要多语种 ASR（MMS 系）须注意其 NC 条款与 D1 冲突。
4. **ContentVec 权重原发点 IBM Box 仍不可达**（本轮未再尝试，维持原盲点）；但该权重的事实分发链（lj1995 HF + 社区 MIT 副本 + RVC/so-vits 官方指引）已足够支撑随包/引导分发的合规判断。
5. **Meta HuBERT 权重许可的正确定性**：facebook/hubert-* HF 卡全部 apache-2.0（含历史修订回溯）——若未来引入 HuBERT 系特征提取器（如中文 HuBERT TencentGameMate 之外的 Meta 原版），可直接引用本轮表格；research-rvc §2.4"业界普遍按 MIT 对待"的说法应更新为"Meta 官方分发点标注 apache-2.0（一手核验）"。

---

## 结论分级汇总

| # | 原结论 | 判级 | 要点 |
|---|---|---|---|
| 1 | ContentVec 权重 = MIT（O2 关闭依据） | **【站得住】** | 四路反证（fairseq/HuBERT 许可文案、初始化链条、issue #34 细读、生态分发实践）全部未命中，且 (a) 与 (b) 反向强化了结论：Meta HuBERT-base 权重一手证明 apache-2.0、ContentVec 公开配方无权重初始化。附带修正 research 文档"从 hubert-base-ls960 初始化"的失实表述。CC-BY-NC 溯源三轮未果，本轮并否定了"源头=fairseq HuBERT"假设 |
| 2 | GTCRN 权重 = MIT（O1 关闭依据） | **【站得住】** | 仓库身份（论文作者，arXiv 作者名单交叉绑定）、LICENSE 与仓库同龄（2024-04-17）、HF 14 副本无更严许可——均未命中。维持【倾向】级与"发版前书面确认"动作 |
| 3 | Qwen3 全系 Apache-2.0（O4 关闭依据） | **【需修正】** | 反例实锤：Qwen3-Omni 三变体 license:other 且无 LICENSE 文件；Qwen3.8 旗舰/Flash-Next 转自定义许可（UI 展示义务 + MaaS/AI 助手业务另行授权）。当前选型 Qwen3-4B-GGUF（官方/unsloth）不受影响；选型规则须改为逐仓库核对 |
| 4 | VB-CABLE 可随包分发 | **【需修正】** | 官网条款复核一致、无纠纷案例；但包内 EULA（一手）规定集成进安装流程需 Author agreement、署名为 might 而非 will——"静默打包安装"须先取得书面同意或改走引导自装；AS IS 复制散布两文本一致允许 |

**对基线（REQUIREMENTS v1.1）的修订建议清单**（本审查只记录，不直接改基线）：
- §6 ContentVec 行：残余风险"Meta 原始 HuBERT-base 权重无显式许可文本"可更新为"Meta HuBERT 权重分发点 apache-2.0（一手核验）；ContentVec 公开配方从零训练，无 HuBERT 权重初始化"。
- §6 Qwen 行 / §10 O4：加"Qwen3-Omni 例外；Qwen3.8 起部分型号转自定义许可；逐仓库核对"限定。
- §8 R1：可在现有"低"基础上注明 HuBERT 链条盲点已闭合到 apache-2.0。
- §5.1 虚拟声卡行：补包内 EULA 的 Author agreement 要求。

---

## 参考链接清单

**fairseq / Meta HuBERT（目标 1a、1e）**
- fairseq examples/hubert README（当前）：https://github.com/facebookresearch/fairseq/blob/main/examples/hubert/README.md
- 同文件 2022 版（4a7835b7）：https://github.com/facebookresearch/fairseq/raw/4a7835b79423b152938e90b88224dac499560d11/examples/hubert/README.md
- fairseq 源码包（本地 grep CC-BY-NC）：https://codeload.github.com/facebookresearch/fairseq/tar.gz/refs/heads/main
- HF facebook/hubert-base-ls960：https://hf-mirror.com/api/models/facebook/hubert-base-ls960 ；历史修订：https://hf-mirror.com/api/models/facebook/hubert-base-ls960/commits/main
- 其余 facebook/hubert-* 仓库 API：hf-mirror.com/api/models/facebook/{hubert-large-ll60k, hubert-xlarge-ll60k, hubert-large-ls960-ft, hubert-xlarge-ls960-ft}

**ContentVec（目标 1b、1c、1d）**
- 源码包（配置/脚本/LICENSE 本地核验）：https://codeload.github.com/auspicious3000/contentvec/tar.gz/refs/heads/main
- 论文全文：https://arxiv.org/abs/2204.09224 ；https://ar5iv.labs.arxiv.org/html/2204.09224
- issue #34（含评论）：https://api.github.com/repos/auspicious3000/contentvec/issues/34 ；…/issues/34/comments
- LICENSE 提交：https://github.com/auspicious3000/contentvec/commit/5f7f07a2f3 （"add license"，2022-10-16）
- RVC 仓库根 assets/ 与 README：https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI
- RVC 引用协议文件（ContentVec=MIT 原文）：同仓库根《MIT协议暨相关引用库协议》
- lj1995/VoiceConversionWebUI 元数据：https://hf-mirror.com/api/models/lj1995/VoiceConversionWebUI
- contentvec HF 副本检索：https://hf-mirror.com/api/models?search=contentvec

**GTCRN（目标 2）**
- 上游源码包（README/LICENSE 本地核验）：https://codeload.github.com/Xiaobin-Rong/gtcrn/tar.gz/refs/heads/main
- LICENSE 提交历史：https://github.com/Xiaobin-Rong/gtcrn/commits/main/LICENSE （2024-04-17 单次提交）
- 身份交叉绑定：http://export.arxiv.org/api/query?id_list=2503.00340 （ul-unas 作者名单）
- sherpa 文档页：https://k2-fsa.github.io/sherpa/onnx/speech-enhancement/models.html
- sherpa release 页（HTML 实抓）：https://github.com/k2-fsa/sherpa-onnx/releases/tag/speech-enhancement-models
- HF gtcrn 副本检索：https://hf-mirror.com/api/models?search=gtcrn

**Qwen（目标 3）**
- Qwen 组织全量模型 API：https://hf-mirror.com/api/models?author=Qwen&limit=1000
- QwenLM/Qwen3 README（License Agreement 节）：https://github.com/QwenLM/Qwen3
- Qwen3-Omni-30B-A3B-Instruct：README https://hf-mirror.com/Qwen/Qwen3-Omni-30B-A3B-Instruct/raw/main/README.md ；文件树 https://hf-mirror.com/api/models/Qwen/Qwen3-Omni-30B-A3B-Instruct/tree/main （无 LICENSE）
- Qwen3.8-2.4T-A95B（Qwen3.8-Max License 原文）：https://hf-mirror.com/Qwen/Qwen3.8-2.4T-A95B/raw/main/LICENSE
- Qwen3.8-Flash-Next（Qwen Community License 1.0 原文）：https://hf-mirror.com/Qwen/Qwen3.8-Flash-Next/raw/main/LICENSE
- unsloth GGUF frontmatter：https://hf-mirror.com/unsloth/Qwen3-4B-GGUF/raw/main/README.md ；…/unsloth/Qwen3-235B-A22B-GGUF/… ；…/unsloth/Qwen3-Coder-480B-A35B-Instruct-GGUF/… ；…/unsloth/Qwen3-Next-80B-A3B-Instruct-GGUF/…
- 官方 Qwen3-4B 文件树（LICENSE 在库）：https://hf-mirror.com/api/models/Qwen/Qwen3-4B/tree/main

**VB-CABLE（目标 4）**
- 官网条款页（逐字重取）：https://vb-audio.com/Services/licensing.htm
- 商店 General Terms（3.4 经销商条款）：https://shop.vb-audio.com/en/content/3-Terms-of-use
- 官方驱动包（含 EULA readme.txt，本地解包核验）：https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack45.zip
- 产品页：https://www.vb-audio.com/Cable/index.htm
