#!/usr/bin/env bash
# ci-smoke.sh — CI 与本地通用的引擎回归（WP-1.2）：分块不变性 + FFT 属性 + CLI 冒烟 + ffprobe 体检
# 用法：./ci-smoke.sh [构建目录=build-linux]
# 前置：构建目录内 engine_test/fft_test/video_handler 已就绪，third_party 两模型在位（fetch-deps.sh），
#       FFT-PR-005 需 python3 + numpy（CI 由工作流安装 python3-numpy）
# 产物落 mktemp 临时目录，退出即清；任一断言失败即非零退出
set -euo pipefail
cd "$(dirname "$0")/../../../.." # scripts→wp-1.2-linux-build→acceptance→docs→仓库根
[ -f CMakeLists.txt ] || { echo "FAIL: 未落到仓库根（$(pwd)）"; exit 1; }

BUILD="${1:-build-linux}"
ET="$BUILD/engine_test"
FT="$BUILD/fft_test"
VH="$BUILD/video_handler"
for b in "$ET" "$FT" "$VH"; do [ -x "$b" ] || { echo "FAIL: 缺 $b（先构建）"; exit 1; }; done
GTCRN=third_party/models/denoiser/gtcrn_simple.onnx
UVR=third_party/models/separator/UVR-MDX-NET-Voc_FT.onnx
for m in "$GTCRN" "$UVR"; do [ -f "$m" ] || { echo "FAIL: 缺 $m（先跑 fetch-deps.sh）"; exit 1; }; done

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
DUR=5

echo "== 素材（${DUR}s 正弦：44.1k 立体声 wav + 各引擎原生规格裸流）=="
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=${DUR}" -ac 2 -c:a pcm_s16le "$WORK/st.wav"
ffmpeg -y -loglevel error -f lavfi -i "testsrc2=size=320x240:rate=25:duration=3" -f lavfi -i "sine=frequency=440:duration=3" -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "$WORK/tiny.mp4"
ffmpeg -y -loglevel error -i "$WORK/st.wav" -f f32le -c:a pcm_f32le -ar 44100 -ac 2 "$WORK/st.f32"
ffmpeg -y -loglevel error -i "$WORK/st.wav" -f f32le -c:a pcm_f32le -ar 16000 -ac 1 "$WORK/mo.f32"

echo "== FFT 属性（WP-1.3：FFT-PR-001..004 自检 + PR-005 numpy 对照，判据 spec/fft-rewrite.md §3）=="
"$FT" self
python3 docs/acceptance/wp-1.3-fft-rewrite/scripts/fft-numpy-check.py "$FT"

echo "== 分块不变性（一次性 vs 4096 vs 977，输出须逐字节一致）=="
invariance() { # invariance <engine> <in.f32> [参数...]
    local k=$1 in=$2; shift 2
    for c in 1000000000 4096 977; do
        "$ET" "$k" "$in" "$WORK/o-$k-$c.f32" "$c" "$@"
    done
    cmp "$WORK/o-$k-1000000000.f32" "$WORK/o-$k-4096.f32"
    cmp "$WORK/o-$k-1000000000.f32" "$WORK/o-$k-977.f32"
    echo "  不变 PASS: $k"
}
invariance dsp "$WORK/st.f32"
invariance pitch "$WORK/st.f32" 2.0
invariance ai "$WORK/mo.f32" "$GTCRN"
invariance sep-voc "$WORK/st.f32" "$UVR"
invariance sep-inst "$WORK/st.f32" "$UVR"

echo "== CLI 冒烟（7 动词）=="
"$VH" --transcode-wav     "$WORK/st.wav" "$WORK/w1.wav"
"$VH" --denoise-wav       "$WORK/st.wav" "$WORK/w2.wav"
"$VH" --ai-denoise        "$WORK/st.wav" "$WORK/w3.wav" "$GTCRN"
"$VH" --pitch-wav         "$WORK/st.wav" "$WORK/w4.wav" 2.0
"$VH" --sep-vocals        "$WORK/st.wav" "$WORK/w5.wav" "$UVR"
"$VH" --sep-instrumental  "$WORK/st.wav" "$WORK/w6.wav" "$UVR"
"$VH" --extract-audio     "$WORK/tiny.mp4" >/dev/null
"$VH" --extract-video     "$WORK/tiny.mp4" >/dev/null

echo "== ffprobe 体检（w1–w6 = pcm_f32le/44100/2ch；时长 w1/2/4/5/6=${DUR}s，w3 按引擎约定）=="
# w3（AI 降噪）输出长度 = 引擎参考约定 (N//hop)·hop（5s@16k→79872 样本≈4.992s，sherpa
# OfflineSpeechDenoiser 同款；WP-1.1 round-3 起），比源短不足一 hop 的尾段——区间断言容
# ffprobe 容器时长取整；其余路径与源恒等长，保持精确断言
for i in 1 2 3 4 5 6; do
    spec=$(ffprobe -v error -select_streams a:0 -show_entries stream=codec_name,sample_rate,channels -of csv=p=0 "$WORK/w$i.wav")
    dur=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$WORK/w$i.wav")
    if [ "$i" = 3 ]; then
        [ "$spec" = "pcm_f32le,44100,2" ] && awk -v d="$dur" -v s="$DUR" \
            'BEGIN{e=int(s*16000/256)*256/16000; exit !(d>=e-0.01 && d<=s+0.001)}' \
            || { echo "  体检 FAIL: w$i.wav -> $spec / $dur（期望 (N//hop)*hop ≈4.992s@s=5）"; exit 1; }
    else
        [ "$spec" = "pcm_f32le,44100,2" ] && [ "$dur" = "${DUR}.000000" ] || { echo "  体检 FAIL: w$i.wav -> $spec / $dur"; exit 1; }
    fi
done
echo "  体检 PASS"
echo "== ci-smoke 全绿 =="
