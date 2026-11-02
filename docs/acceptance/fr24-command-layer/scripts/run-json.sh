#!/usr/bin/env bash
# run-json.sh — FR-24 命令层 JSON 面编排（SG-1，进 ci-smoke）：--json 正例（9 动词形态，
# 48k 跟随源 / engineResampled 可观测）+ 负例 14 条 + 确定性（同请求两遍 cmp）+ 响应键序确定断言。
# 判据与用例表以 docs/acceptance/fr24-command-layer/matrix.csv 为准；本脚本是其自动化执行体。
# 依赖：python3（stdlib json）；CMD-032 坏模型注入需 onnx（缺则该条 SKIP，PARITY_REQUIRED=1 时升级 FAIL）
# 用法：run-json.sh [构建目录=build-linux]
set -uo pipefail
cd "$(dirname "$0")/../../../.." # scripts→fr24-command-layer→acceptance→docs→仓库根
[ -f CMakeLists.txt ] || { echo "FAIL: 未落到仓库根（$(pwd)）"; exit 1; }

BUILD="${1:-build-linux}"
VH="$BUILD/video_handler"
GTCRN=third_party/models/denoiser/gtcrn_simple.onnx
UVR=third_party/models/separator/UVR-MDX-NET-Voc_FT.onnx
for f in "$VH" "$GTCRN" "$UVR"; do
    [ -e "$f" ] || { echo "FAIL: 缺 $f（先构建/跑 fetch-deps.sh）"; exit 1; }
done
S=docs/acceptance/fr24-command-layer/scripts
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
FAIL=0 SKIPANY=

skip() { echo "  SKIP: $*"; SKIPANY=1; }

# 素材（复用 ci-smoke 配方：正弦 wav + 双流 mp4；另备 48k 源与无音频流 mp4）
DUR=5
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=${DUR}" -ac 2 -c:a pcm_s16le "$W/st.wav"
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=${DUR}" -ac 2 -c:a pcm_s16le "$W/st48k.wav"
ffmpeg -y -loglevel error -f lavfi -i "testsrc2=size=320x240:rate=25:duration=3" -f lavfi -i "sine=frequency=440:duration=3" -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "$W/tiny.mp4"
ffmpeg -y -loglevel error -f lavfi -i "testsrc2=size=320x240:rate=25:duration=3" -c:v libx264 -pix_fmt yuv420p -an "$W/noaudio.mp4"

# jr <id> <期望退出码> <请求JSON> [断言...]：跑一条 --json 用例并判定
jr() {
    local id=$1 exprc=$2 req=$3; shift 3
    printf '%s' "$req" | "$VH" --json >"$W/$id.resp" 2>/dev/null
    local rc=$?
    local ok=1
    [ "$rc" = "$exprc" ] || { echo "  FAIL $id: 退出码 $rc ≠ 期望 $exprc"; ok=0; }
    python3 "$S/json_check.py" "$W/$id.resp" "$@" || ok=0
    if [ "$ok" = 1 ]; then echo "  PASS $id"; else FAIL=1; fi
}

# ex <id> <命令...>：附加 shell 级断言（命令退出码 0 即 PASS）
ex() {
    local id=$1; shift
    if "$@"; then echo "  PASS $id"; else echo "  FAIL $id: 附加断言失败"; FAIL=1; fi
}

# fp <id> <wav> <期望csv>：ffprobe 交叉验证实际输出文件规格（防「报告规格与实际文件不符」）
fp() {
    local got
    got=$(ffprobe -v error -select_streams a:0 -show_entries stream=codec_name,sample_rate,channels -of csv=p=0 "$2" 2>/dev/null)
    if [ "$got" = "$3" ]; then echo "  PASS $1（ffprobe $got）"; else echo "  FAIL $1: ffprobe $got ≠ $3"; FAIL=1; fi
}

echo "== 正例（9 动词形态；48k 跟随源 / engineResampled 可观测，D4）=="
jr CMD-001 0 "{\"v\":1,\"cmd\":\"probe\",\"input\":\"$W/tiny.mp4\"}" \
    ok=true v=1 cmd=probe contains:result.container=MOV numrange:result.durationSec=2.5:3.5 \
    str:result.streams.0.type=video str:result.streams.0.codec=h264 num:result.streams.0.width=320 num:result.streams.0.height=240 \
    str:result.streams.1.type=audio str:result.streams.1.codec=aac num:result.streams.1.sampleRate=44100 num:result.streams.1.channels=1
jr CMD-002 0 "{\"v\":1,\"cmd\":\"transcode\",\"input\":\"$W/st.wav\",\"output\":\"$W/c2.wav\"}" \
    ok=true v=1 cmd=transcode num:result.sourceSpec.sampleRate=44100 num:result.sourceSpec.channels=2 \
    num:result.outputSpec.sampleRate=44100 num:result.outputSpec.channels=2 \
    str:result.outputSpec.format=pcm_f32le bool:result.engineResampled=false \
    num:result.frames=220500 num:result.durationSec=5 exists:result.output
fp CMD-002f "$W/c2.wav" "pcm_f32le,44100,2"
jr CMD-003 0 "{\"v\":1,\"cmd\":\"transcode\",\"input\":\"$W/st48k.wav\",\"output\":\"$W/c3.wav\"}" \
    ok=true cmd=transcode num:result.outputSpec.sampleRate=48000 num:result.outputSpec.channels=2 \
    bool:result.engineResampled=false exists:result.output
fp CMD-003f "$W/c3.wav" "pcm_f32le,48000,2"
jr CMD-004 0 "{\"v\":1,\"cmd\":\"denoise\",\"input\":\"$W/st48k.wav\",\"output\":\"$W/c4.wav\"}" \
    ok=true cmd=denoise num:result.outputSpec.sampleRate=48000 num:result.outputSpec.channels=2 \
    bool:result.engineResampled=false num:result.sourceSpec.sampleRate=48000 exists:result.output
fp CMD-004f "$W/c4.wav" "pcm_f32le,48000,2"
jr CMD-005 0 "{\"v\":1,\"cmd\":\"denoise\",\"input\":\"$W/st.wav\",\"output\":\"$W/c5.wav\",\"humBase\":\"60\",\"wind\":false}" \
    ok=true cmd=denoise num:result.outputSpec.sampleRate=44100 bool:result.engineResampled=false exists:result.output
jr CMD-006 0 "{\"v\":1,\"cmd\":\"aiDenoise\",\"input\":\"$W/st48k.wav\",\"output\":\"$W/c6.wav\",\"model\":\"$GTCRN\"}" \
    ok=true cmd=aiDenoise num:result.sourceSpec.sampleRate=48000 \
    num:result.outputSpec.sampleRate=44100 num:result.outputSpec.channels=2 \
    bool:result.engineResampled=true numrange:result.durationSec=4.9:5.0 exists:result.output
fp CMD-006f "$W/c6.wav" "pcm_f32le,44100,2"
jr CMD-007 0 "{\"v\":1,\"cmd\":\"pitch\",\"input\":\"$W/st.wav\",\"output\":\"$W/c7.wav\",\"semi\":2.0}" \
    ok=true cmd=pitch num:result.outputSpec.sampleRate=44100 bool:result.engineResampled=false \
    num:result.durationSec=5 exists:result.output
fp CMD-007f "$W/c7.wav" "pcm_f32le,44100,2"
jr CMD-008 0 "{\"v\":1,\"cmd\":\"separate\",\"input\":\"$W/st48k.wav\",\"output\":\"$W/c8.wav\",\"model\":\"$UVR\",\"stem\":\"vocals\"}" \
    ok=true cmd=separate num:result.sourceSpec.sampleRate=48000 \
    num:result.outputSpec.sampleRate=44100 num:result.outputSpec.channels=2 \
    bool:result.engineResampled=true exists:result.output
jr CMD-009 0 "{\"v\":1,\"cmd\":\"separate\",\"input\":\"$W/st.wav\",\"output\":\"$W/c9.wav\",\"model\":\"$UVR\",\"stem\":\"instrumental\"}" \
    ok=true cmd=separate num:result.outputSpec.sampleRate=44100 exists:result.output
jr CMD-010 0 "{\"v\":1,\"cmd\":\"extractAudio\",\"input\":\"$W/tiny.mp4\"}" \
    ok=true cmd=extractAudio str:result.output=$W/tiny_audio.m4a exists:result.output
jr CMD-011 0 "{\"v\":1,\"cmd\":\"extractVideo\",\"input\":\"$W/tiny.mp4\"}" \
    ok=true cmd=extractVideo str:result.output=$W/tiny_mute.mp4 exists:result.output

echo "== 确定性（同请求两遍输出 cmp 逐字节一致，NFR-A2 前味）=="
printf '%s' "{\"v\":1,\"cmd\":\"transcode\",\"input\":\"$W/st.wav\",\"output\":\"$W/det1.wav\"}" | "$VH" --json >/dev/null
printf '%s' "{\"v\":1,\"cmd\":\"transcode\",\"input\":\"$W/st.wav\",\"output\":\"$W/det2.wav\"}" | "$VH" --json >/dev/null
ex CMD-012 cmp "$W/det1.wav" "$W/det2.wav"

echo "== 负例（恒出合法 JSON + 退出码 1 + 响应无 result；错误码分类见 D3）=="
printf 'not-json{{' | "$VH" --json >"$W/CMD-020.resp" 2>/dev/null
rc=$?
[ "$rc" = 1 ] || { echo "  FAIL CMD-020: 退出码 $rc ≠ 1"; FAIL=1; }
if python3 "$S/json_check.py" "$W/CMD-020.resp" ok=false v=1 code=E_PARAMS absent:result; then
    echo "  PASS CMD-020"; else FAIL=1; fi

jr CMD-021 1 '{"v":1,"cmd":"frobnicate","input":"x"}' ok=false no-cmd code=E_PARAMS absent:result
# 响应键序/Compact 确定性：未知 cmd 的响应原文逐字节等于期望（QJsonObject 字典序）
expect='{"error":{"code":"E_PARAMS","message":"未知命令：frobnicate"},"ok":false,"v":1}'
if [ "$(cat "$W/CMD-021.resp")" = "$expect" ]; then echo "  PASS CMD-021r（响应原文确定）"; else
    echo "  FAIL CMD-021r: 响应原文 ≠ 期望"; FAIL=1; fi

jr CMD-022 1 "{\"v\":1,\"cmd\":\"transcode\",\"input\":\"$W/st.wav\",\"output\":\"$W/x.wav\",\"bogus\":1}" \
    ok=false cmd=transcode code=E_PARAMS msg-contains=未知字段 absent:result
jr CMD-023 1 '{"v":1,"cmd":"transcode","output":"x.wav"}' \
    ok=false cmd=transcode code=E_PARAMS msg-contains=input absent:result
jr CMD-024 1 "{\"v\":1,\"cmd\":\"pitch\",\"input\":\"$W/st.wav\",\"output\":\"$W/x.wav\",\"semi\":\"abc\"}" \
    ok=false cmd=pitch code=E_PARAMS msg-contains=数字 absent:result
jr CMD-025 1 "{\"v\":1,\"cmd\":\"pitch\",\"input\":\"$W/st.wav\",\"output\":\"$W/x.wav\",\"semi\":15}" \
    ok=false cmd=pitch code=E_PARAMS absent:result
jr CMD-026 1 "{\"v\":1,\"cmd\":\"pitch\",\"input\":\"$W/st.wav\",\"output\":\"$W/x.wav\",\"semi\":0}" \
    ok=false cmd=pitch code=E_PARAMS absent:result
jr CMD-027 1 "{\"v\":1,\"cmd\":\"denoise\",\"input\":\"$W/st.wav\",\"humBase\":\"61\"}" \
    ok=false cmd=denoise code=E_PARAMS msg-contains=humBase absent:result
jr CMD-028 1 "{\"v\":1,\"cmd\":\"denoise\",\"input\":\"$W/st.wav\",\"humBase\":\"off\",\"wind\":false}" \
    ok=false cmd=denoise code=E_PARAMS absent:result
jr CMD-029 1 "{\"v\":1,\"cmd\":\"transcode\",\"input\":\"$W/no-such-file.wav\",\"output\":\"$W/x.wav\"}" \
    ok=false cmd=transcode code=E_FILE msg-contains=打不开 absent:result
jr CMD-030 1 "{\"v\":1,\"cmd\":\"transcode\",\"input\":\"$W/noaudio.mp4\",\"output\":\"$W/x.wav\"}" \
    ok=false cmd=transcode code=E_FILE msg-contains=没有音频流 absent:result
jr CMD-031 1 "{\"v\":1,\"cmd\":\"aiDenoise\",\"input\":\"$W/st.wav\",\"output\":\"$W/x.wav\",\"model\":\"/nonexistent.onnx\"}" \
    ok=false cmd=aiDenoise code=E_MODEL msg-contains=找不到模型 absent:result

# CMD-032 坏模型（维度错位，wp-1.0 round-2 配方 v2：命名满足 WP-1.1 后的 GTCRN setup 契约）：
# setup 全过、首次推理抛错 → E_ENGINE 干净退出且无半成品
if python3 -c 'import onnx' 2>/dev/null; then
    python3 "$S/craft-mismatch-model.py" "$W/mismatch.onnx" >/dev/null 2>&1
    jr CMD-032 1 "{\"v\":1,\"cmd\":\"aiDenoise\",\"input\":\"$W/st.wav\",\"output\":\"$W/c32.wav\",\"model\":\"$W/mismatch.onnx\"}" \
        ok=false cmd=aiDenoise code=E_ENGINE absent:result
    ex CMD-032f test ! -e "$W/c32.wav" # 失败删半成品（NFR-R1）
else
    skip "CMD-032 缺 onnx（坏模型注入，配方 scripts/craft-mismatch-model.py）"
fi

cp "$W/st.wav" "$W/st.bak"
jr CMD-033 1 "{\"v\":1,\"cmd\":\"transcode\",\"input\":\"$W/st.wav\",\"output\":\"$W/st.wav\"}" \
    ok=false cmd=transcode code=E_PARAMS msg-contains=原件只读 absent:result
ex CMD-033i cmp "$W/st.wav" "$W/st.bak" # 原件只读防线：输入文件逐字节未动
jr CMD-034 1 "{\"v\":2,\"cmd\":\"denoise\",\"input\":\"$W/st.wav\"}" \
    ok=false cmd=denoise code=E_PARAMS msg-contains=v absent:result

# CMD-035 NaN 薄壳负例（审查 P2-1）：argv 面 toDouble("nan") ok=true，须被范围校验否定式拦下
if "$VH" --pitch-wav "$W/st.wav" "$W/c35.wav" nan >"$W/CMD-035.out" 2>/dev/null; then
    echo "  FAIL CMD-035: NaN 半音数被放行"; FAIL=1
elif grep -q "半音数须在" "$W/CMD-035.out" && [ ! -e "$W/c35.wav" ]; then
    echo "  PASS CMD-035"
else
    echo "  FAIL CMD-035: 文案或半成品检查不符"; FAIL=1
fi

if [ "$FAIL" = 1 ]; then
    echo "== run-json FAIL（见上）=="
    exit 1
fi
if [ "$SKIPANY" = 1 ] && [ "${PARITY_REQUIRED:-0}" = 1 ]; then
    echo "== run-json 有 SKIP 且 PARITY_REQUIRED=1（CI 强制点）=="
    exit 1
fi
echo "== run-json 全绿${SKIPANY:+（有 SKIP，见上；本地降级跑）} =="
