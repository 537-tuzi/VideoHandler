#!/usr/bin/env bash
# smoke.sh — Linux 测试构建后的 CLI 冒烟 + A1 RSS 对照（2026-10-14 热修验证）
# 用法：./smoke.sh [素材时长秒，默认 7200=2h]
# 前置：build-post / build-pre 两个构建目录已就绪（见 CMakeLists.txt 头注释）
# 产物：docs/tmp/linux-build/smoke-results.txt（追加式）
set -euo pipefail
cd "$(dirname "$0")"
DUR="${1:-7200}"
WORK=/home/psh/vh-test
mkdir -p "$WORK"
RES=smoke-results.txt
exec >>"$RES" 2>&1
echo "==== smoke run $(date '+%F %T') 时长=${DUR}s ===="

POST=build-post/video_handler
PRE=build-pre/video_handler

# ---- 素材 ----
[ -f "$WORK/small.wav" ] || ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=30" -ac 2 -c:a pcm_s16le "$WORK/small.wav"
[ -f "$WORK/tiny.mp4" ] || ffmpeg -y -loglevel error -f lavfi -i "testsrc2=size=320x240:rate=25:duration=8" -f lavfi -i "sine=frequency=440:duration=8" -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "$WORK/tiny.mp4"
[ -f "$WORK/long.wav" ] || ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=${DUR}" -ac 2 -c:a pcm_s16le "$WORK/long.wav"

run() { echo "-- $1"; shift; "$@" || echo "FAIL: $*"; }

run "transcode" "$POST" --transcode-wav "$WORK/small.wav" "$WORK/out-transcode.wav"
run "denoise"   "$POST" --denoise-wav   "$WORK/small.wav" "$WORK/out-denoise.wav"
run "pitch"     "$POST" --pitch-wav     "$WORK/small.wav" "$WORK/out-pitch.wav" 2.0
run "extract-a" "$POST" --extract-audio "$WORK/tiny.mp4"
run "extract-v" "$POST" --extract-video "$WORK/tiny.mp4"

echo "-- 输出体检（ffprobe）"
for f in "$WORK"/out-transcode.wav "$WORK"/out-denoise.wav "$WORK"/out-pitch.wav; do
    ffprobe -v error -show_entries stream=codec_name,sample_rate,channels -show_entries format=duration -of default=nw=1 "$f" | tr '\n' ' '; echo "<- $f"
done

echo "-- A1 RSS 对照（${DUR}s 素材，Maximum resident set size）"
/usr/bin/time -v "$PRE"  --denoise-wav "$WORK/long.wav" "$WORK/out-pre.wav"  2>&1 | grep -E "Maximum resident|Elapsed" | sed 's/^/  修复前: /'
/usr/bin/time -v "$POST" --denoise-wav "$WORK/long.wav" "$WORK/out-post.wav" 2>&1 | grep -E "Maximum resident|Elapsed" | sed 's/^/  修复后: /'
if cmp -s "$WORK/out-pre.wav" "$WORK/out-post.wav"; then echo "  输出逐字节一致: PASS"; else echo "  输出逐字节一致: FAIL"; fi
echo "==== smoke run end ===="
