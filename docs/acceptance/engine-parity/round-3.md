# WP-1.1 round-3：MDX + GTCRN 双收敛——归因链 + 引擎修复，PAR-MD 8/8 与 PAR-GT 全绿（2026-11-02）

环境同 round-2。本轮 MDX 侧两次判别实验推翻两个中间假设后归因闭合（引擎两轮修复 + 判据修订），PAR-MD 全表 −107.6~−145.4dB；GTCRN 侧子代理修复两债并锁定参考语义，PAR-GT 主用例机器精度。

## 1. 归因链（先论后证，每环有判别实验）

round-2 首轮对照全 FAIL（voc −5~−30dB）。本轮逐环判别：

1. **假设「块重叠区 OLA」→ 被推翻**：旧金样本上残差时段分布显示段A（x<192000 纯块0区）与段B同级——但旧金样本被两重污染（见 2/3），判别无效。
2. **实锤①参考前置 normalize 非线性工作点**：`mdx_separator.py:169` peak>0.9 时 `mix×0.9/peak` 进模型——非线性下 demix(s·x)≠s·demix(x)，纯增益拟合救不了。旁证：唯一 peak=0.9187<0.9…（white 触发 / odd1s 不触发）——修正：sine1k peak=0.5 实不触发，本条对 white/multi12s 成立。
3. **实锤②×peak 纯增益混合域**：参考输出 = demix(mix)·peak（:172），副 stem = mix − c·demix·peak（:197，mix 与 voc 两项增益不同）；odd1s 机器精度对照证明引擎链路本身正确，差异全在域。
4. **实锤③块级语义**（新金样本同工作点后判别，本轮决定性实验）：残差时段分布——sine1k/white 段A −91~−111dB（机器精度）而段B（块1影响区 x≥192000）−0.1~−12dB；multi12s 段A 仅 −46dB 且归因于**参考中间块块尾 reflect 真实信号**（sine1k/white 信号在块尾零 pad 前结束故段A 纯净）。⇒ 引擎全局连续网格 vs 参考分块（step=195840 网格重置 + 块边界 reflect + 块级 hanning OLA/divider）是剩余差异的全部来源。
5. 边界细节实锤：块0 头部 reflect 反射进来的 x[0] 恰被 periodic hann[0]=0 消灭 ⇒ 参考头部语义等价零头（odd1s 修复后机器精度的原因）。

## 2. 判据修订（金样本同工作点，round-3 生效）

参考实现的两处幅度约定是产品级策略、非引擎语义，对等比较必须剥离：

- `spec_utils.normalize` monkeypatch 为恒等（旁路 peak>0.9 工作点偏移）；
- ×peak 增益代数除回引擎域：`voc_true = primary/peak`、`inst_true = secondary + c·(primary − primary/peak) = x − c·demix(x)`；
- 域重建自洽校验（交叉重算 max|d| < 1e-6 断言，实测全 3e-8）——曾抓出 gen_golden 写盘顺序 bug（secondary 先写 primary 后写，按捕获顺序解包会把角色弄反，残差 0.0106）。

`--fit-scale` 保留为纯校验（新金样本下 a 恒 1.0000）。

## 3. 引擎修复（两轮）

**round-3a（边界+脏数据）**：feed 头部 reflect→零填充（预存 win+1 帧后 inQ 前插 nFft 零）；flush 尾部 reflect→零填充；ifftChannel 清 [dimF, nFft−dimF] 复用缓冲脏 bin（Hermitian 镜像区间 [nFft−dimF+1, nFft−1] 与谱填充 [0, dimF−1] 并集覆盖全 bin）。效果：odd1s-voc −5.0→−109.9dB（机器精度）。

**round-3b（块级化，复刻官方 demix 分块语义）**：
- 块驱动：C=kHop·(dimT−1)=261120、S=C−C/4=195840（overlap 0.25）、trim=nFft/2、pad=genSize+trim−n%genSize（genSize=C−2·trim）——全部从官方 demix/stft 源码逐行核对；
- 块内 STFT center-reflect（torch 语义：索引 −j→块内[j]、C+j→块内[C−2−j]；头块反射零段=零头等价）；
- 块内合成 = padded 域整块 WOLA（长 C+nFft）+ 窗²分母 + 裁 [trim, trim+C)（等价 torch.istft(center)；注意 dimT·kHop=C+kHop≠C，「每帧排 kHop」的流式网格帧贡献差半窗——ASan 实锤越界后重写）；
- 块级 OLA：对称 hanning 窗（np.hanning 语义分母 C−1；**尾块按实际块长独立生成 hanning(La)**，非长窗截断）× tar 累加 + divider 累加，排水 result/divider；
- 流式排水线：块 b 完成后排水 mixture < (b+1)·S（贡献块集已定）；环形缓冲容量 C（res 交错 + div，~3MB 有界）；flush 补零恒 ≤2C（NFR-P4 无虞，2h 素材 mixture 全长不入内存）。

**回归**：multi12s/odd1s 分块不变性（整块 vs 977）逐字节一致；ci-smoke sep 用例（w5/w6 + invariance sep-voc/sep-inst）全过。

## 4. PAR-MD 终表（round-3）

DUT=engine_test 块 4096；金样本=normalize 旁路+域重建版（gen_golden.py mdx）。

| 用例 | parity | corr | a | 结论 |
|---|---|---|---|---|
| sine1k-voc | **−107.6 dB** | 1.000000 | 1.0000 | PASS |
| sine1k-inst | **−127.1 dB** | 1.000000 | 1.0000 | PASS |
| white-voc | **−109.2 dB** | 1.000000 | 1.0000 | PASS |
| white-inst | **−144.7 dB** | 1.000000 | 1.0000 | PASS |
| multi12s-voc | **−110.9 dB** | 1.000000 | 1.0000 | PASS |
| multi12s-inst | **−144.6 dB** | 1.000000 | 1.0000 | PASS |
| odd1s-voc | **−109.9 dB** | 1.000000 | 1.0000 | PASS |
| odd1s-inst | **−145.4 dB** | 1.000000 | 1.0000 | PASS |

（round-2 基线：voc −5~−12dB / inst −44~−47dB / sine1k-voc corr 0.05。）

## 5. 结论与遗留

- **SG-4 MDX parity 收敛**：8/8 PASS 机器精度级（−60dB 判据大幅超额）。数值证据链：归因（三重）→ 剥离（判据同工作点）→ 复刻（块级化）→ 逐字节分块不变。
- 引擎行为变化：**输出延迟增加**至 ~2 块（排水线滞后，块级 OLA 固有），流式契约不破坏（NFR-P4 有界复验：环形 3MB + flush 补零 ≤2C）。
- Windows 编译随 round-4 真机（既有闸门）。
- 工具沉淀：gen_golden mdx 判据修订、compare --fit-scale；ad-hoc 判别脚本 docs/tmp/mdx-sleuth/（session 结束清理）。

## 6. GTCRN 两债修复（SG-3 收敛，子代理执行 + 主线程独立复验）

**参考语义锁定（全部有 ground-truth）**：golden 复现（sherpa-onnx 1.13.8 `OfflineSpeechDenoiser.run` 输出与既有 golden 逐字节一致）；diff-onset 实验钉死对齐方向（golden 输出[j] ↔ 输入[j] 裁头对齐）；离线复刻 = center 网格 + 双侧 reflect（np.pad(x,256,'reflect')）+ WOLA÷Σw² + 裁头，帧数 = N//hop+1，vs golden 全长 −153.0dB（float64 机器精度；尾帧假设二分：右补零 −85.9dB 被否、右 reflect 命中）。

**债②机理修正**：参考发射区内 Σw² 恒 = 1（sqrt-hann 50% 重叠恒双帧覆盖），round-1 的「首尾窗分母欠补偿」并非主因；真正缺口 = 旧引擎缺**反射尾帧**（参考对最后样本镜像补帧）。分母补偿保留（istft 语义，正确且零成本）。

**引擎改动（src/service/aidenoiser.cpp，116+/35−）**：feedPending（反射头帧 P[j]=x[half−j] + 帧网格偏移半窗）/ processFrame（OLA + Σw² 滑动累加，发射 ÷wsum）/ flushTail（反射尾帧 x[2N−2−a] 镜像，reflectSrc 三处解析 + foldIdx 深折叠）/ slideAnalysis（belowA 单格留底——python 仿真抓出的 off-by-one）。设计先经 python 流式仿真证明等价（17 种 N × 4 种分块全部 −151~−153dB）再移植。

**主线程审查加严一处**：setup 约束 hop≥winLen/2 → **hop==winLen/2**（feedPending 的 need=winLen−half 与 slideAnalysis 每步吃满 hop 仅在等式下一致，否则假想导出越界读 pending；GTCRN 导出恒为 256/512）。

**主线程独立复验（统一干净构建，不信子代理回执）**：

| 用例 | parity | corr | 分块不变 |
|---|---|---|---|
| PAR-GT-001 white16mo | **−123.9dB**（残差/RMS输入 −156.0）n=79872 | 1.000000 | 整块/4096/977/97 逐字节一致 |
| 边界 N=100/255 | 双方零输出（0B=0B，长度约定一致） | — | 4096 vs 97 一致 |
| 边界 N=256（深折叠） | **−88.9dB** | 1.000000 | 同上 |
| 边界 N=1025 / N=4097 | −120.8 / −117.5dB | 1.000000 | 同上 |

**输出长度约定变更**：输出 = (N//hop)·hop（参考同款），5s 素材少 8ms 尾段；ci-smoke w3 断言改区间断言（其余 w* 精确断言不变）。产品级「补齐等长」决策记 goal.md 开放项（FFTranscoder 层，FR-24/实时化前拍板）。

## 7. 子代理对抗式审查闭环（vocalseparator.cpp）

工作流 wf_61e41dd3（3 维度 × 验证，共 11 代理，审 round-3a 时代 diff）：**0 confirmed / 3 killed / 1 uncertain**。唯一存活 P1（runModel 失败后 specCount 残留 dimT → 后续 process() 越界写 tensor）——主线程在最终块级化代码上复核：specCount 成员已删除；失败-重试路径幂等安全（runChunk 失败发生在块级 OLA 累计之前，环形仅被零扩展，inQ 未裁剪，重试同坐标整块重跑），判定**失效关闭**。killed 三条（多块结构性偏差/根目录过期 wav/占位汇总）均已被块级化与清理动作消除或经反驳排除。
