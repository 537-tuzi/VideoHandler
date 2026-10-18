# WP-1.1 round-2：audio-separator 落地 + PAR-MD 首轮对照：幅度约定实锤 + 纯音泄漏差异实锤（2026-10-18）

环境：同 round-1 + `test/pydeps` 定向安装（绕 diffq，见 scripts/requirements.txt；torch 2.14.1+cpu / ort 1.31.0 / librosa 1.0.0 / onnx2torch-py313 / torchvision 0.29.1+cpu）。素材新增 odd1s（1024·43+123=44155 帧 ≈1.002s：odd 7291 帧=0.165s 触发引擎「最短 ~0.17s」契约被拒，属引擎有意行为非 bug；odd 保留给 DSP 用例）。

## 1. audio-separator 0.47.0 落地（绕 diffq）

- round-1 后台安装失败根因（本轮 ground-truth 复盘）：① pip 默认 TMPDIR=/tmp 是 tmpfs 有配额，torch 级包 Errno 122；② 前次失败残留 test/pipcache 3.0G（torch CUDA 全家桶缓存）占满配额；③ 硬依赖 diffq（`sys_platform != "win32"`）在 py3.14 无 wheel，源码构建失败。
- 实证 MDX 路径不 import diffq（grep 全源码：仅 `uvr_lib_v5/demucs/*` 用，demucs 模型才加载；现行 MDX 走 `architectures/mdx_separator.py`）。legacy `uvr_lib_v5/mdxnet.py` 里的 pytorch_lightning 同为死代码。
- 绕行方案：`--no-deps` 装本体 → torch 走 `download.pytorch.org/whl/cpu`（免拉 CUDA 全家桶）→ 定向补运行集（six/pydub/audioread/…，缺啥 import 报啥补啥）。重建命令固化 scripts/requirements.txt。
- 金样本通路：monkeypatch `CommonSeparator.write_audio` 拦截模型原始输出（ndarray (N,2)）——绕开 pydub FLOAT→ffmpeg `-f float` 非法 format 的导出坑，也免 soundfile 路径的 normalize（DUT 不归一化，对等比较须取未归一化值）。gen_golden.py 新增 mdx 子命令，一次产 mdx-voc / mdx-inst 双金样本。

## 2. PAR-MD 首轮对照结果

DUT=engine_test sep-voc/sep-inst（块 4096，Voc_FT）；参考=audio-separator 0.47.0 同模型 torch cpu。compare.py 新增 `--fit-scale`（最小二乘增益拟合后算残差，报 a）。

| 用例 | corr | 去增益 parity | 拟合增益 a | 备注 |
|---|---|---|---|---|
| sine1k-voc | 0.051 | — | — | 近静音 stem 小分母 + 泄漏分歧 23dB（见 §4） |
| sine1k-inst | 0.756 | −3.7 dB | 1.284 | 混合域（mix 未缩 + voc 缩放项）拟合不稳 |
| white-voc | 0.968 | −12.0 dB | 0.703 | |
| white-inst | 0.999989 | **−46.7 dB** | 0.980 | |
| multi12s-voc | 0.914 | −7.8 dB | 0.767 | |
| multi12s-inst | 0.999980 | **−44.0 dB** | 0.917 | |
| odd1s-voc | 0.829 | −5.0 dB | 0.862 | |
| odd1s-inst | 0.999989 | **−46.4 dB** | 1.000 | |

## 3. 参考实现幅度约定（实锤，判据修订依据）

参考管线 `separate()`：`peak=|mix|max → mix=normalize(mix, 0.9, 0.0) → source=demix(mix)*peak`。即输出 = 模型输出 × s×peak（s=0.9/peak 当 peak>0.9，否则 1）。纯增益、与实现质量无关：

| 素材 | peak | 理论 s×peak | 实测 a（inst） |
|---|---|---|---|
| white | 0.9187 | 0.9796 | 0.9799 |
| multi12s | 0.9817 | 0.9166 | 0.9169 |
| odd1s | 0.8672 | 0.8672 | 0.8615→0.9998* |

*odd1s 首拟合 0.8615 是 voc 残差拖累；去除后 inst 纯增益 0.9998（odd1s 单块短素材模型直出主导）。

→ **判据修订**：MDX 组 parity 须 `--fit-scale` 去增益（或参考侧除回 s×peak）；round-1 的 GTCRN「残差/RMS(输入)」修订同属此类（参考实现全局幅度约定不是实现差异）。

## 4. 真差异定位（未收敛，round-3 判别）

- **纯音泄漏差 23dB（新引擎嫌疑，最重要）**：sine1k（1kHz 纯音）DUT voc RMS 0.233（输入 66%泄漏）vs 参考 0.017（3%）；corr(DUT voc, mix)=0.82——DUT 把纯音大半推给 voc，参考几乎全给 inst。white 宽带 corr(voc) 却 0.97——**差异对纯音敏感、宽带钝感**，特征指向引擎前端谱构差异（dimF 截取/高频镜像重建/前 3 bin 置零我们均未做，参考 `spek[:,:,:3,:]*=0` + `[..., :dim_f]` 截取 + torch.istft 内部重建）。注：取材验证时交织立体声直接 FFT 出现 500Hz 伪影（时间拉伸 2 倍），素材本身确认 1kHz 无误。
- **合成端数学不同（架构差异，第二嫌疑）**：参考 demix = 离线块级对称 hanning-OLA + divider（chunk=hop×(segment−1)=261120 样本、overlap 25%、step 75%），块内 torch.stft(center=True)→模型→torch.istft；我们 = 流式帧级 WOLA（hop 1024 窗²归一化）。两者对同一模型输出的合成不同——但线性合成端解释不了 23dB 泄漏差，主因应在模型输入谱。
- inst 侧 −44~−47dB corr 0.99998：两实现 voc 差异经 mix−c·voc 相减被稀释，接近但未达 −60。

## 5. 结论与下轮

- PAR-MD **未收敛**：inst −44dB / voc −5~−12dB / 纯音泄漏 23dB。定性完成=幅度约定（已修判据）+ 引擎前端谱构嫌疑（主）+ 合成端架构差异（次）。
- round-3 判别实验：参考侧复刻我们前端（无 center？我们有 reflect ✓、无前 3bin 置零、dimF 高频补法）单块 ort 直推对比，二分归因到具体因素；若确认为引擎债，与 GTCRN 两债一并处置（用户拍板）。
- 素材/工具：odd1s 入 CASES；gen_inputs.py 修「重生成抹掉 golden_* 哈希」bug（增量合并）；compare.py 增 --fit-scale；requirements.txt 固化环境配方。
