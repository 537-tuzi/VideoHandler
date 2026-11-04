# engine-parity round-8：PAR-DF-004 零掩码门控护栏（FR-4 round-3 修复轮，2026-11-04）

背景：fr4 round-2 审查代理以阈值搬移判别实验抓出 P1——dfn3 引擎零掩码帧（lsnr<lsnr_min）曾误
推进解码器游标（regIdx 构造已排除该帧、发射却 ++rPos）→ 错位 + 堆越界读；登记素材 10dB SNR
永不触发该路径（测试盲区根因）。修复与复核证据全档 `docs/acceptance/fr4-quality-tier/round-3.md`。

## 本轮证据（matrix round-8 列）

- **PAR-DF-004（新）**：阈值搬移双侧同语义——引擎侧模型目录副本 config.ini `lsnr_min=10`，
  官方 CLI 侧 `--min-db-thresh 10`，dfn3noisy65 整段单 Run。
  - 修复前：**段错误 SIGSEGV**（65s 素材零掩码帧远超 washup 富余，rPos 大幅越界）——生产品质级
    （低信噪比真实输入曾必崩）；
  - 修复后：**PASS −78.8dB**（max|d|=4.74e-05，与常规路径同深），gen_golden `dfn3min10` 变体
    独立登记（后缀 dfmin10）。
- **回归**：PAR-DF-001/002/003/SEAM/FEAT×9/SR×3 修后数字与 round-7 **逐位一致**（−75.3/−78.9/
  1.65e-05/−3.4/9×0/bit-exact）——修复对未触发路径零漂移；亦证 round-7 SEAM 数字未被 P1 污染
  （若登记素材触发过零掩码，001 不可能保持 −75.3dB）。
- **SEAM 门定格**：−2.5dB 回归护栏（OQ-2 round-3 终裁，spec fr4 §7.1 修订段 + fr4 round-3 §4）。

ci-smoke 接线随 run_parity.sh 既有 PAR-DF 段自动生效（本轮起含 004）。
