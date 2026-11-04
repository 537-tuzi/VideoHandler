#!/usr/bin/env bash
# run_parity.sh — WP-1.1 parity 套件编排（SG-6，进 ci-smoke）：素材/金样本保障（哈希登记，
# 缺失或漂移才再生成）→ engine_test 产 DUT → compare.py / pitch_check.py 判定。
# 判据与用例表以 docs/acceptance/engine-parity/matrix.csv 为准；本脚本是其自动化执行体。
#
# 依赖分级（缺哪族跳哪族，SKIP 大声打印）：
#   PAR-DS：scipy（~/.local）        PAR-GT：sherpa-onnx（~/.local）
#   PAR-MD：audio-separator（test/pydeps，PYTHONPATH 注入）   PAR-PT：numpy
#   PAR-SR：scipy + ffmpeg + video_handler（round-5 起转码层 CLI 级 DUT）
#   PAR-DF（FR-4）：DFN3 模型+官方 tar+deep-filter CLI（fetch-deps.sh）；FEAT 子族另需
#                  python onnxruntime；SR 子族另需 video_handler
# 环境变量 PARITY_REQUIRED=1（CI）时任何 SKIP 升级为 FAIL——CI 是 parity 门禁的强制点，
# 本地未装参考实现依赖时允许降级跑其余族。
# 用法：run_parity.sh [构建目录=build-linux]
set -uo pipefail
cd "$(dirname "$0")/../../../.." # scripts→engine-parity→acceptance→docs→仓库根
[ -f CMakeLists.txt ] || { echo "FAIL: 未落到仓库根（$(pwd)）"; exit 1; }

BUILD="${1:-build-linux}"
ET="$BUILD/engine_test"
S=docs/acceptance/engine-parity/scripts
OUT=test/parity
GTCRN=third_party/models/denoiser/gtcrn_simple.onnx
UVR=third_party/models/separator/UVR-MDX-NET-Voc_FT.onnx
for f in "$ET" "$GTCRN" "$UVR"; do
    [ -e "$f" ] || { echo "FAIL: 缺 $f（先构建/跑 fetch-deps.sh）"; exit 1; }
done
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
FAIL=0 SKIPANY=

skip() { echo "  SKIP: $*"; SKIPANY=1; }
ck()   { "$@" || FAIL=1; } # 比较器退出码即判定，FAIL 记账统一收口

echo "== 素材保障（gen_inputs，确定性合成 + 哈希登记）=="
if ! python3 "$S/gen_inputs.py" >/dev/null 2>&1; then
    if [ "${PARITY_REQUIRED:-0}" = 1 ]; then
        echo "FAIL: 素材生成失败（缺 numpy/scipy？配方 scripts/requirements.txt）"
        exit 1
    fi
    # 裸机（无 Python 参考依赖）降级：ci-smoke 保持绿，parity 缺席需看本行提示
    echo "== parity 全 SKIP：素材生成失败（缺 numpy/scipy；配方 scripts/requirements.txt）=="
    exit 0
fi

echo "== PAR-DS（DSP 降噪 vs scipy 参考；hum50/hum60 分级判据 vs float32 同构参考）=="
if python3 -c 'import scipy' 2>/dev/null; then
    for m in hum50 white sine1k odd; do
        "$ET" dsp "$OUT/$m.f32" "$W/$m.dut.f32" 4096 >/dev/null || { echo "  FAIL: dsp DUT 运行 $m"; FAIL=1; break; }
    done
    # PAR-DS-005（round-6，P0 债 #5）：60Hz 制式素材，DUT 显式传 60（其余素材缺省 50 回归）
    "$ET" dsp "$OUT/hum60.f32" "$W/hum60.dut.f32" 4096 60 >/dev/null || { echo "  FAIL: dsp DUT 运行 hum60"; FAIL=1; }
    if [ "$FAIL" = 0 ]; then
        python3 "$S/gen_golden.py" dsp hum50 white sine1k odd hum60 >/dev/null || { echo "  FAIL: dsp 金样本生成"; FAIL=1; }
        ck python3 "$S/compare.py" "$W/hum50.dut.f32" "$OUT/hum50.dspf.golden.f32" --case PAR-DS-001 --gate -45
        ck python3 "$S/compare.py" "$W/white.dut.f32" "$OUT/white.dsp.golden.f32" --case PAR-DS-002 --gate -60
        ck python3 "$S/compare.py" "$W/sine1k.dut.f32" "$OUT/sine1k.dsp.golden.f32" --case PAR-DS-003 --gate -60
        ck python3 "$S/compare.py" "$W/odd.dut.f32" "$OUT/odd.dsp.golden.f32" --case PAR-DS-004 --gate -60
        ck python3 "$S/compare.py" "$W/hum60.dut.f32" "$OUT/hum60.dspf.golden.f32" --case PAR-DS-005 --gate -45
    fi
else
    skip "PAR-DS 缺 scipy（配方 scripts/requirements.txt）"
fi

echo "== PAR-GT（GTCRN vs sherpa-onnx 同模型；输出长度约定 (N//hop)·hop）=="
if python3 -c 'import sherpa_onnx' 2>/dev/null; then
    GTN="white16mo bound256 bound1025 bound4097"
    ok=1
    for m in $GTN; do
        "$ET" ai "$OUT/$m.f32" "$W/$m.dut.f32" 4096 "$GTCRN" >/dev/null || { echo "  FAIL: ai DUT 运行 $m"; FAIL=1; ok=0; break; }
    done
    if [ "$ok" = 1 ]; then
        python3 "$S/gen_golden.py" gtcrn $GTN bound100 bound255 >/dev/null || { echo "  FAIL: gtcrn 金样本生成"; FAIL=1; }
        ck python3 "$S/compare.py" "$W/white16mo.dut.f32" "$OUT/white16mo.gtcrn.sherpa.golden.f32" --case PAR-GT-001 --gate -60
        ck python3 "$S/compare.py" "$W/bound256.dut.f32" "$OUT/bound256.gtcrn.sherpa.golden.f32" --case PAR-GT-003a --gate -60
        ck python3 "$S/compare.py" "$W/bound1025.dut.f32" "$OUT/bound1025.gtcrn.sherpa.golden.f32" --case PAR-GT-003b --gate -60
        ck python3 "$S/compare.py" "$W/bound4097.dut.f32" "$OUT/bound4097.gtcrn.sherpa.golden.f32" --case PAR-GT-003c --gate -60
        # 零输出约定：N<hop 双方零字节（compare.py 不收空数组，直接核文件长度）
        "$ET" ai "$OUT/bound100.f32" "$W/b100.dut.f32" 4096 "$GTCRN" >/dev/null 2>&1
        "$ET" ai "$OUT/bound255.f32" "$W/b255.dut.f32" 4096 "$GTCRN" >/dev/null 2>&1
        if [ ! -s "$W/b100.dut.f32" ] && [ ! -s "$W/b255.dut.f32" ] \
           && [ ! -s "$OUT/bound100.gtcrn.sherpa.golden.f32" ] && [ ! -s "$OUT/bound255.gtcrn.sherpa.golden.f32" ]; then
            echo "PAR-GT-003d: PASS  N=100/255 双方零输出（长度约定一致）"
        else
            echo "PAR-GT-003d: FAIL  N=100/255 零输出约定被破坏"; FAIL=1
        fi
    fi
else
    skip "PAR-GT 缺 sherpa-onnx（配方 scripts/requirements.txt）"
fi

echo "== PAR-MD（MDX 分离 vs audio-separator 官方管线；判据同工作点 round-3）=="
if PYTHONPATH="test/pydeps${PYTHONPATH:+:$PYTHONPATH}" python3 -c 'import audio_separator' 2>/dev/null; then
    PYP="PYTHONPATH=test/pydeps${PYTHONPATH:+:$PYTHONPATH}"
    ok=1
    for m in sine1k white multi12s odd1s; do
        "$ET" sep-voc "$OUT/$m.f32" "$W/$m.voc.f32" 4096 "$UVR" >/dev/null || { echo "  FAIL: sep-voc DUT 运行 $m"; FAIL=1; ok=0; break; }
        "$ET" sep-inst "$OUT/$m.f32" "$W/$m.inst.f32" 4096 "$UVR" >/dev/null || { echo "  FAIL: sep-inst DUT 运行 $m"; FAIL=1; ok=0; break; }
    done
    if [ "$ok" = 1 ]; then
        env "$PYP" python3 "$S/gen_golden.py" mdx sine1k white multi12s odd1s >/dev/null || { echo "  FAIL: mdx 金样本生成"; FAIL=1; }
        for m in sine1k white multi12s odd1s; do
            ck python3 "$S/compare.py" "$W/$m.voc.f32" "$OUT/$m.mdx-voc.golden.f32" --case "PAR-MD-$m-voc" --gate -60 --fit-scale
            ck python3 "$S/compare.py" "$W/$m.inst.f32" "$OUT/$m.mdx-inst.golden.f32" --case "PAR-MD-$m-inst" --gate -60 --fit-scale
        done
    fi
else
    skip "PAR-MD 缺 audio-separator（配方 scripts/requirements.txt）"
fi

echo "== PAR-PT（变调 +2∘−2 往返谱判据；分块不变已由 ci-smoke 不变性组覆盖）=="
if "$ET" pitch "$OUT/sine440.f32" "$W/p1.f32" 4096 2.0 >/dev/null \
   && "$ET" pitch "$W/p1.f32" "$W/p2.f32" 4096 -2.0 >/dev/null; then
    ck python3 "$S/pitch_check.py" "$OUT/sine440.f32" "$W/p2.f32" --case PAR-PT-001
else
    echo "  FAIL: pitch DUT 运行"; FAIL=1
fi

# PAR-SR（round-5，输出跟随源 / P0 债 #4，spec canonical-rate-48k §4）：CLI 级 DUT
#（video_handler），与 engine_test 族互补——这条族考的是转码层，48k 源全链免重采样。
# PAR-SR-003/004（44.1k 回归/分离不变）不设独立判定：PAR-DS/PAR-PT/PAR-MD 同轮全绿 +
# ci-smoke w1–w6 体检断言（44100/2ch）即证据，matrix 记交叉引用。
echo "== PAR-SR（输出跟随源：48k 全链直通；CLI 级 DUT vs ffmpeg 解码参考/scipy@48k）=="
VH="$BUILD/video_handler"
if [ -x "$VH" ] && command -v ffmpeg >/dev/null 2>&1 && python3 -c 'import scipy' 2>/dev/null; then
    ok=1
    "$VH" --transcode-wav "$OUT/white48k.wav" "$W/white48k.t.wav"  >/dev/null 2>&1 || ok=0
    "$VH" --denoise-wav   "$OUT/white48k.wav" "$W/white48k.dn.wav" >/dev/null 2>&1 || ok=0
    "$VH" --pitch-wav     "$OUT/sine48k.wav"  "$W/sine48k.p2.wav"  2.0 >/dev/null 2>&1 || ok=0
    ffmpeg -y -loglevel error -i "$OUT/white48k.wav" -f f32le -c:a pcm_f32le \
        -ar 48000 -ac 2 "$W/white48k.ref.f32" 2>/dev/null || ok=0
    if [ "$ok" = 1 ]; then
        # PAR-SR-001：直通引擎 48k → 输出头跟随源 + vs ffmpeg 解码参考逐样本 bit-exact
        ck python3 "$S/sr_check.py" "$W/white48k.t.wav" --case PAR-SR-001 --sr 48000 --ch 2 \
            --bitexact-vs "$W/white48k.ref.f32"
        # PAR-SR-002a：DSP 降噪 48k → 头跟随源 + vs scipy RBJ@48k 双精度参考（sr 取素材登记元数据）
        ck python3 "$S/sr_check.py" "$W/white48k.dn.wav" --case PAR-SR-002a-head --sr 48000 --ch 2 \
            --dump "$W/white48k.dn.f32"
        python3 "$S/gen_golden.py" dsp white48k >/dev/null || { echo "  FAIL: dsp 金样本生成（48k）"; FAIL=1; }
        ck python3 "$S/compare.py" "$W/white48k.dn.f32" "$OUT/white48k.dsp.golden.f32" --case PAR-SR-002a --gate -60
        # PAR-SR-002b：变调 48k +2 半音 → 头跟随源 + 单程谱判据（峰频 +200 音分，--sr 参数化）
        ck python3 "$S/sr_check.py" "$W/sine48k.p2.wav" --case PAR-SR-002b-head --sr 48000 --ch 2 \
            --dump "$W/sine48k.p2.f32"
        ck python3 "$S/pitch_check.py" "$OUT/sine48k.f32" "$W/sine48k.p2.f32" \
            --case PAR-SR-002b --sr 48000 --expect-cents 200
    else
        echo "  FAIL: PAR-SR DUT 运行（video_handler --transcode/--denoise/--pitch 或 ffmpeg 解码参考）"; FAIL=1
    fi
else
    [ -x "$VH" ] || skip "PAR-SR 缺 $VH（先构建 video_handler）"
    command -v ffmpeg >/dev/null 2>&1 || skip "PAR-SR 缺 ffmpeg"
    python3 -c 'import scipy' 2>/dev/null || skip "PAR-SR 缺 scipy（配方 scripts/requirements.txt）"
fi

# PAR-DF（FR-4 质量档 DFN3 vs 官方 deep-filter CLI v0.5.1 musl；spec fr4-quality-tier D6
# 两层判据 + 图级夹具梯子）：整段单 Run（engine_test dfn3 块长 0 专项）−60dB 硬门禁
#（NFR-Q1）；SEAM = 生产块 30s vs 整段单 Run 的接缝量化（OQ-2 阈值未定，回归护栏口径，
# 基线随 fr4 round 重登记）；FEAT = 图级夹具回放锁图行为；SR = 48k 源全链免重采样 CLI 级。
# 依赖：DFN3 目录模型 + 官方 tar + CLI（fetch-deps.sh 备齐）+ numpy；FEAT 另需 python onnxruntime。
echo "== PAR-DF（FR-4：DFN3 vs 官方 deep-filter CLI；整段单 Run −60dB 硬门禁）=="
DFN3DIR=third_party/models/denoiser/dfn3
DFN3TAR=third_party/models/denoiser/DeepFilterNet3_onnx.tar.gz
DFCLI=third_party/tools/deep-filter
if [ -d "$DFN3DIR" ] && [ -x "$DFCLI" ] && [ -f "$DFN3TAR" ]; then
    ok=1
    for m in dfn3noisy65 dfn3bound dfn3short; do
        "$ET" dfn3 "$OUT/$m.f32" "$W/$m.single.f32" 1000000000 "$DFN3DIR" 0 >/dev/null \
            || { echo "  FAIL: dfn3 DUT 运行 $m（整段单 Run）"; FAIL=1; ok=0; break; }
    done
    if [ "$ok" = 1 ]; then
        python3 "$S/gen_golden.py" dfn3 dfn3noisy65 dfn3bound dfn3short >/dev/null \
            || { echo "  FAIL: dfn3 金样本生成"; FAIL=1; }
        # ①② 边界组：dfn3bound=480*7+377（τ<2 零初始化发射 + 非整 hop 尾）
        ck python3 "$S/compare.py" "$W/dfn3noisy65.single.f32" "$OUT/dfn3noisy65.df.golden.f32" --case PAR-DF-001 --gate -60
        ck python3 "$S/compare.py" "$W/dfn3bound.single.f32"   "$OUT/dfn3bound.df.golden.f32"   --case PAR-DF-002 --gate -60
        # ③ 最短素材（3 帧）：降噪输出近零 → 相对 dB 失真，绝对判据（compare.py 头注）
        ck python3 "$S/compare.py" "$W/dfn3short.single.f32"   "$OUT/dfn3short.df.golden.f32"   --case PAR-DF-003 --gate-abs 2e-4
        # ④ 零掩码门控（round-8，审查 P1 修复护栏）：阈值搬移法双侧同语义——引擎侧模型目录副本
        #    config.ini lsnr_min=10，CLI 侧 --min-db-thresh 10，强制触发零掩码帧（官方该档不跑
        #    解码器、不占行；round-7 引擎曾游标错位此处 0dB 级 FAIL）
        mkdir -p "$W/dfn3min10"
        cp "$DFN3DIR"/config.ini "$DFN3DIR"/enc.onnx "$DFN3DIR"/erb_dec.onnx "$DFN3DIR"/df_dec.onnx "$W/dfn3min10"/
        sed -i 's/^lsnr_min = -15$/lsnr_min = 10/' "$W/dfn3min10/config.ini"
        grep -q '^lsnr_min = 10$' "$W/dfn3min10/config.ini" || { echo "  FAIL: dfn3min10 config.ini 阈值搬移未生效"; FAIL=1; }
        python3 "$S/gen_golden.py" dfn3min10 dfn3noisy65 >/dev/null \
            || { echo "  FAIL: dfn3min10 金样本生成"; FAIL=1; }
        "$ET" dfn3 "$OUT/dfn3noisy65.f32" "$W/dfn3noisy65.min10.f32" 1000000000 "$W/dfn3min10" 0 >/dev/null \
            || { echo "  FAIL: dfn3 DUT 运行 dfn3noisy65（min10 整段单 Run）"; FAIL=1; }
        ck python3 "$S/compare.py" "$W/dfn3noisy65.min10.f32" "$OUT/dfn3noisy65.dfmin10.golden.f32" --case PAR-DF-004 --gate -60
        # SEAM：生产块 30s vs 整段单 Run（回归护栏：round-1 基线 −3.4dB + 0.9dB 余量；
        # 真语音素材 −12.8dB 佐证 + K 扫描/注意力记忆结论见 fr4-quality-tier/round-1.md，OQ-2 裁阈值）
        "$ET" dfn3 "$OUT/dfn3noisy65.f32" "$W/dfn3noisy65.b30.f32" 4096 "$DFN3DIR" >/dev/null \
            || { echo "  FAIL: dfn3 DUT 运行（生产块 30s）"; FAIL=1; }
        ck python3 "$S/compare.py" "$W/dfn3noisy65.b30.f32" "$W/dfn3noisy65.single.f32" --case PAR-DF-SEAM --gate -2.5
        # FEAT：图级夹具回放（缺 python onnxruntime 或图级夹具目录整子族 SKIP——夹具为设计轮
        # 手工就位产物，fetch-deps 不备；净环境须 SKIP 而非 FAIL，runbook 见 fr4-measurement.md）
        if python3 -c 'import onnxruntime' 2>/dev/null \
           && [ -d third_party/models/candidates/dfn3/fixtures-graphlevel ]; then
            ck python3 "$S/dfn3_feat_check.py"
        else
            python3 -c 'import onnxruntime' 2>/dev/null \
                || skip "PAR-DF-FEAT 缺 python onnxruntime"
            [ -d third_party/models/candidates/dfn3/fixtures-graphlevel ] \
                || skip "PAR-DF-FEAT 缺图级夹具目录（设计轮手工就位，再生成 runbook 见 docs/reference/fr4-dfn3-measurement.md）"
        fi
        # SR：48k 单声道源全链免重采样（PAR-SR-001 同型：头跟随源 + engineResampled=false +
        # 生产链 vs 引擎直调整段单 Run；AI 引擎首个跟随源，spec canonical-rate-48k）
        VH="$BUILD/video_handler"
        if [ -x "$VH" ]; then
            mkdir -p "$W/fr4sr"
            if printf '{"cmd":"aiDenoiseHq","input":"%s","output":"%s","model":"%s"}' \
                    "$PWD/$OUT/dfn3bound.wav" "$W/fr4sr/bound_hq.wav" "$PWD/$DFN3DIR" \
                | "$VH" --json >"$W/fr4sr/resp.json" 2>/dev/null \
               && python3 - "$W/fr4sr/resp.json" <<'PYEOF'
import json, sys
r = json.load(open(sys.argv[1]))
res = r.get('result') or {}
assert r.get('ok') is True, f"aiDenoiseHq 失败: {r.get('error')}"
assert res.get('engineResampled') is False, f"48k 源不应重采样: {res}"
assert res['outputSpec']['sampleRate'] == 48000 and res['outputSpec']['channels'] == 1, res['outputSpec']
print('PAR-DF-SR-meta: PASS  engineResampled=false, 48000/1ch')
PYEOF
            then
                ck python3 "$S/sr_check.py" "$W/fr4sr/bound_hq.wav" --case PAR-DF-SR-head --sr 48000 --ch 1 --dump "$W/fr4sr/bound_hq.f32"
                ck python3 "$S/compare.py" "$W/fr4sr/bound_hq.f32" "$W/dfn3bound.single.f32" --case PAR-DF-SR --gate -60
            else
                echo "  FAIL: PAR-DF-SR 生产链运行/断言"; FAIL=1
            fi
        else
            skip "PAR-DF-SR 缺 $VH（先构建 video_handler）"
        fi
    fi
else
    skip "PAR-DF 缺 DFN3 模型/deep-filter CLI（先跑 fetch-deps.sh）"
fi

if [ "$FAIL" = 1 ]; then
    echo "== parity FAIL（见上）=="
    exit 1
fi
if [ "$SKIPANY" = 1 ] && [ "${PARITY_REQUIRED:-0}" = 1 ]; then
    echo "== parity 有 SKIP 且 PARITY_REQUIRED=1（CI 强制点）=="
    exit 1
fi
echo "== parity 全绿${SKIPANY:+（有 SKIP 族，见上；本地降级跑）} =="
