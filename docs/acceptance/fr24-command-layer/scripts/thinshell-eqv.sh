#!/usr/bin/env bash
# thinshell-eqv.sh — FR-24 D7 薄壳等价实证（round 验证工具，证据进 round-N.md）：
# 基线提交 git worktree 构建 → 旧动词同参数双跑（基线直调 vs 新薄壳）→
# 退出码一致 + stdout 文案逐字一致 + 输出文件 cmp 逐字节一致 三方比对。
# 用法：thinshell-eqv.sh <基线提交> <新构建目录>    例：thinshell-eqv.sh 4657ccd build-linux
# 前置：基线可构建（Qt/FFmpeg/SoundTouch apt 依赖）、third_party 已 fetch（ORT+模型经软链共享）
set -uo pipefail
cd "$(dirname "$0")/../../../.." # scripts→fr24-command-layer→acceptance→docs→仓库根
[ -f CMakeLists.txt ] || { echo "FAIL: 未落到仓库根（$(pwd)）"; exit 1; }
BASE_COMMIT="${1:?用法：thinshell-eqv.sh <基线提交> <新构建目录>}"
NEW_BUILD="${2:?用法：thinshell-eqv.sh <基线提交> <新构建目录>}"
NEW_VH="$NEW_BUILD/video_handler"
REPO=$(pwd)
[ -x "$NEW_VH" ] || { echo "FAIL: 缺 $NEW_VH"; exit 1; }
GTCRN=third_party/models/denoiser/gtcrn_simple.onnx
UVR=third_party/models/separator/UVR-MDX-NET-Voc_FT.onnx

WT=$(mktemp -d /tmp/vh-eqv.XXXXXX)
trap 'git -C "$REPO" worktree remove --force "$WT/base" >/dev/null 2>&1; rm -rf "$WT"' EXIT

echo "== 基线构建：git worktree add $BASE_COMMIT → $WT/base（third_party 软链共享）=="
git -C "$REPO" worktree add "$WT/base" "$BASE_COMMIT" >/dev/null || { echo "FAIL: worktree add"; exit 1; }
ln -s "$REPO/third_party" "$WT/base/third_party"
cmake -S "$WT/base" -B "$WT/base/build-linux" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_COMPILER=g++ -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG" >/dev/null \
    || { echo "FAIL: 基线配置"; exit 1; }
cmake --build "$WT/base/build-linux" -j"$(nproc)" >/dev/null || { echo "FAIL: 基线构建"; exit 1; }
BASE_VH="$WT/base/build-linux/video_handler"

# 素材（ci-smoke 配方）：44.1k 立体声正弦 + 48k 源 + 双流 mp4
DUR=5
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=$DUR" -ac 2 -c:a pcm_s16le "$WT/st.wav"
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=$DUR" -ac 2 -c:a pcm_s16le "$WT/st48k.wav"
ffmpeg -y -loglevel error -f lavfi -i "testsrc2=size=320x240:rate=25:duration=3" -f lavfi -i "sine=frequency=440:duration=3" -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "$WT/tiny.mp4"
ffmpeg -y -loglevel error -f lavfi -i "testsrc2=size=320x240:rate=25:duration=3" -c:v libx264 -pix_fmt yuv420p -an "$WT/noaudio.mp4"

FAIL=0
# cmp_run <id> <产物路径|空> <argv...>：双跑比退出码/stdout/产物（stderr 不在旧契约面，忽略）
cmp_run() {
    local id=$1 art=$2; shift 2
    local rcb rcn ok=1
    if [ -n "$art" ]; then
        rm -f "$art" "$WT/$id.base.art" "$WT/$id.new.art"
    fi
    "$BASE_VH" "$@" >"$WT/$id.base.out" 2>/dev/null; rcb=$?
    [ -n "$art" ] && [ -e "$art" ] && mv "$art" "$WT/$id.base.art"
    [ -n "$art" ] && rm -f "$art"
    "$NEW_VH" "$@" >"$WT/$id.new.out" 2>/dev/null; rcn=$?
    [ -n "$art" ] && [ -e "$art" ] && mv "$art" "$WT/$id.new.art"
    [ "$rcb" = "$rcn" ] || { echo "  FAIL $id: 退出码 $rcb(base) ≠ $rcn(new)"; ok=0; }
    if ! diff -q "$WT/$id.base.out" "$WT/$id.new.out" >/dev/null; then
        echo "  FAIL $id: stdout 文案不一致"; diff "$WT/$id.base.out" "$WT/$id.new.out" | head -4; ok=0
    fi
    if [ -e "$WT/$id.base.art" ] || [ -e "$WT/$id.new.art" ]; then
        if [ -e "$WT/$id.base.art" ] && [ -e "$WT/$id.new.art" ]; then
            cmp -s "$WT/$id.base.art" "$WT/$id.new.art" || { echo "  FAIL $id: 产物不逐字节一致"; ok=0; }
        else
            echo "  FAIL $id: 产物只有一边存在"; ok=0
        fi
    fi
    if [ "$ok" = 1 ]; then echo "  PASS $id（rc=$rcb；stdout 逐字一致；产物 cmp 一致）"; else FAIL=1; fi
}

echo "== 成功路径（8 动词形态 + 48k + 显式/缺省模型）=="
cmp_run eqv-transcode-44k "$WT/t1.wav" --transcode-wav "$WT/st.wav" "$WT/t1.wav"
cmp_run eqv-denoise-44k   "$WT/t2.wav" --denoise-wav   "$WT/st.wav" "$WT/t2.wav"
cmp_run eqv-denoise-hum60 "$WT/t2h.wav" --denoise-wav  "$WT/st.wav" "$WT/t2h.wav" 60
cmp_run eqv-ai-model      "$WT/t3.wav" --ai-denoise    "$WT/st.wav" "$WT/t3.wav" "$GTCRN"
cmp_run eqv-ai-default    "$WT/t3d.wav" --ai-denoise   "$WT/st.wav" "$WT/t3d.wav"
cmp_run eqv-pitch-pos     "$WT/t4.wav" --pitch-wav     "$WT/st.wav" "$WT/t4.wav" 2.0
cmp_run eqv-pitch-neg     "$WT/t4n.wav" --pitch-wav    "$WT/st.wav" "$WT/t4n.wav" -2.5
cmp_run eqv-sep-vocals    "$WT/t5.wav" --sep-vocals    "$WT/st.wav" "$WT/t5.wav" "$UVR"
cmp_run eqv-sep-inst      "$WT/t6.wav" --sep-instrumental "$WT/st.wav" "$WT/t6.wav" "$UVR"
cmp_run eqv-extract-audio "$WT/tiny_audio.m4a" --extract-audio "$WT/tiny.mp4"
cmp_run eqv-extract-video "$WT/tiny_mute.mp4" --extract-video "$WT/tiny.mp4"
cmp_run eqv-transcode-48k "$WT/t9.wav" --transcode-wav "$WT/st48k.wav" "$WT/t9.wav"

echo "== 失败路径（文案逐字保持；产物占位传空 → 断言双方均无产物）=="
cmp_run f-den-hum61    "" --denoise-wav "$WT/st.wav" "$WT/fx.wav" 61
cmp_run f-ai-nomodel   "" --ai-denoise  "$WT/st.wav" "$WT/fx2.wav" /nonexistent.onnx
cmp_run f-transcode-u  "" --transcode-wav "$WT/st.wav"
cmp_run f-noaudio      "" --transcode-wav "$WT/noaudio.mp4" "$WT/fx3.wav"

if [ "$FAIL" = 1 ]; then echo "== 薄壳等价 FAIL（见上）=="; exit 1; fi
echo "== 薄壳等价全绿：基线 $BASE_COMMIT vs 新薄壳，stdout/退出码/产物三方一致 =="
