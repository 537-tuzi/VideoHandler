#!/usr/bin/env bash
# gui-slot-eqv.sh — GUI 槽级等价编排（FR-24 片 D，round-2；spec D6 风险 1 的自动化缓解）：
# 基线树（默认 fd7badb，片 C 合入、片 D 之前）与当前树各自构建 libengine.a，同一探针源
# （gui-slot-probe.cpp，直调七个 do* 槽）分别链接，同媒体双跑（缺模型/带模型两相），
# 判定：转录（成功/失败/缺模型/取消/probe 文案 + 进度序列）归一化后逐字一致 + 产物 cmp
# 逐字节一致 + 取消无半成品残留。判据与用例表以 ../matrix.csv GUI-* 为准。
# 依赖：与 linux-test 构建同套（pkg-config Qt6Core/FFmpeg/SoundTouch + third_party ORT/模型）
# 用法：gui-slot-eqv.sh [基线 ref=fd7badb] [当前树构建目录=build-linux]
set -uo pipefail
cd "$(dirname "$0")/../../../.." # scripts→fr24-command-layer→acceptance→docs→仓库根
[ -f CMakeLists.txt ] || { echo "FAIL: 未落到仓库根（$(pwd)）"; exit 1; }

BASE_REF="${1:-fd7badb}"
BUILD="${2:-build-linux}"
S=docs/acceptance/fr24-command-layer/scripts
GTCRN=third_party/models/denoiser/gtcrn_simple.onnx
UVR=third_party/models/separator/UVR-MDX-NET-Voc_FT.onnx
for f in "$BUILD/libengine.a" "$GTCRN" "$UVR"; do
    [ -e "$f" ] || { echo "FAIL: 缺 $f（先构建/跑 fetch-deps.sh）"; exit 1; }
done

W=$(mktemp -d)
BASE="$W/base-tree"
cleanup() { git worktree remove --force "$BASE" >/dev/null 2>&1; rm -rf "$W"; }
trap cleanup EXIT

echo "== 基线树 $BASE_REF 构建（worktree，借用主仓 third_party 软链）=="
git worktree add --detach "$BASE" "$BASE_REF" >/dev/null || { echo "FAIL: worktree 建不了"; exit 1; }
ln -s "$(pwd)/third_party" "$BASE/third_party"
( cd "$BASE" && cmake --preset linux-test >/dev/null && cmake --build --preset linux-test >/dev/null ) \
    || { echo "FAIL: 基线构建失败"; exit 1; }

echo "== 探针编译（同一源码，双树各链一份）=="
PKGS="Qt6Core libavformat libavcodec libavutil libswresample soundtouch"
mkdir -p "$W/bin-old" "$W/bin-new"
for v in old new; do
    [ "$v" = old ] && TREE="$BASE" || TREE="$(pwd)"
    g++ -std=c++20 -O2 "$S/gui-slot-probe.cpp" -o "$W/bin-$v/probe" -I"$TREE/src" \
        $(pkg-config --cflags $PKGS) "$TREE/$BUILD/libengine.a" \
        $(pkg-config --libs $PKGS) "$TREE/third_party/ort/linux/lib/libonnxruntime.so" \
        -Wl,-rpath,"$(cd "$TREE" && pwd)/third_party/ort/linux/lib" \
        || { echo "FAIL: 探针编译失败（$v）"; exit 1; }
done
# 带模型相的模型落位在下方 A/B 循环内、A 相跑完之后进行（见循环内注释）

echo "== 素材（同配方双份：44.1k/48k 正弦 + 双流 mp4 + 取消用副本）=="
for d in media-old media-new; do
    mkdir -p "$W/$d"
    ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=5" -ac 2 -c:a pcm_s16le "$W/$d/st.wav"
    ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=5" -ac 2 -c:a pcm_s16le "$W/$d/st48.wav"
    ffmpeg -y -loglevel error -f lavfi -i "testsrc2=size=320x240:rate=25:duration=3" -f lavfi -i "sine=frequency=440:duration=3" -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "$W/$d/tiny.mp4"
    cp "$W/$d/st.wav" "$W/$d/cancel-st.wav"
    cp "$W/$d/tiny.mp4" "$W/$d/cancel-tiny.mp4"
done

FAIL=0
ck() { if [ "$1" = 0 ]; then echo "  PASS $2"; else echo "  FAIL $2"; FAIL=1; fi; }

norm() { sed -e "s#$W/media-old#@M@#g" -e "s#$W/media-new#@M@#g" -e "s#$W/bin-old#@B@#g" -e "s#$W/bin-new#@B@#g" "$1"; }

echo "== 双跑两相对照（QT_QPA_PLATFORM=offscreen；模型在 A 相跑完后才落位，保证缺模型分支真实行使）=="
for p in A B; do
    [ "$p" = A ] && phase=nomodel || phase=withmodel
    if [ "$p" = B ]; then
        # 带模型相：模型放探针 bin 侧（GUI 壳 appDir 相对解析同规则）。必须在 A 相跑完后落位——
        # 若提前拷贝，A 相 exists 检查命中、ai-missing/sep-missing 走成功路径，缺模型分支空转（审查 P1）
        for v in old new; do
            mkdir -p "$W/bin-$v/models/denoiser" "$W/bin-$v/models/separator"
            cp "$GTCRN" "$W/bin-$v/models/denoiser/"
            cp "$UVR" "$W/bin-$v/models/separator/"
        done
    fi
    QT_QPA_PLATFORM=offscreen "$W/bin-old/probe" "$W/media-old" "$phase" >"$W/old-$p.txt" 2>"$W/old-$p.err"
    ck $? "旧侧运行 $phase（退出码）"
    QT_QPA_PLATFORM=offscreen "$W/bin-new/probe" "$W/media-new" "$phase" >"$W/new-$p.txt" 2>"$W/new-$p.err"
    ck $? "新侧运行 $phase（退出码）"
    norm "$W/old-$p.txt" >"$W/old-$p.norm"; norm "$W/new-$p.txt" >"$W/new-$p.norm"
    diff -q "$W/old-$p.norm" "$W/new-$p.norm" >/dev/null
    ck $? "GUI-EQV 转录逐字一致（$phase：文案+进度序列，$(grep -c '^TASK' "$W/new-$p.txt") 槽次）"
    # 内容断言（防分支空转：仅等价 diff 时两侧同走错分支不可辨，审查 P1 教训）
    if [ "$p" = A ]; then
        grep -q '^TASK ai-missing -> ok=0 msg=找不到 AI 降噪模型：' "$W/new-$p.norm" \
            && grep -q '^TASK sep-missing -> ok=0 msg=找不到人声分离模型：' "$W/new-$p.norm"
        ck $? "GUI-EQV 缺模型分支真实行使（A 相：ai/sep ok=0 + GUI 原文案入转录）"
    else
        grep -q '^TASK ai -> ok=1 msg=AI 降噪完成：' "$W/new-$p.norm" \
            && grep -q '^TASK sep-inst -> ok=1 msg=伴奏提取完成：' "$W/new-$p.norm"
        ck $? "GUI-EQV 带模型分支真实行使（B 相：ai/sep ok=1）"
    fi
    if [ -s "$W/old-$p.err" ] || [ -s "$W/new-$p.err" ]; then
        echo "  FAIL stderr 非空（$phase）"; FAIL=1
    fi
done
grep -q '^TASK cancel-denoise -> ok=0 msg=已取消$' "$W/new-B.norm" \
    && grep -q '^TASK cancel-extractAudio -> ok=0 msg=已取消$' "$W/new-B.norm"
ck $? "GUI-CXL「已取消」经 E_CANCELLED message 直通（denoise/extract 双路）"

echo "== 产物逐字节对照（run B 落盘件）=="
for f in tiny_audio.m4a tiny_mute.mp4 st_denoised.wav st48_denoised.wav st_pitch.wav st_ai.wav st_vocals.wav st_instrumental.wav; do
    if [ -f "$W/media-old/$f" ] && [ -f "$W/media-new/$f" ]; then
        cmp -s "$W/media-old/$f" "$W/media-new/$f"
        ck $? "GUI-PRD $f（$(stat -c%s "$W/media-new/$f")B cmp 一致）"
    else
        echo "  FAIL GUI-PRD $f 缺件（old=$([ -f "$W/media-old/$f" ] && echo Y || echo N) new=$([ -f "$W/media-new/$f" ] && echo Y || echo N)）"; FAIL=1
    fi
done
# 取消清半成品：两用例输出（cancel-st_denoised.wav / cancel_tiny_audio.m4a）不得残留
res=$(ls "$W/media-old" "$W/media-new" | grep -c 'cancel-st_denoised\|cancel-tiny_audio' || true)
[ "$res" = 0 ]; ck $? "GUI-CXL 取消无半成品残留（NFR-R1）"

[ "$FAIL" = 0 ] && echo "== gui-slot-eqv 全绿 ==" || { echo "== gui-slot-eqv 有 FAIL =="; exit 1; }
