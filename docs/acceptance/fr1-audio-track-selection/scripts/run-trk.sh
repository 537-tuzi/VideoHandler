#!/usr/bin/env bash
# run-trk.sh — FR-1 多音轨选择验收编排（TRK 族 + TRK-GUI 族 + ZD-NET + BUILD，判据以
# docs/acceptance/fr1-audio-track-selection/matrix.csv 为准）：
#   TRK-001  probe 增键面：multi.mkv streams[] 条件插键（language/title/defaultTrack，
#           video 流不插；tiny.mp4 响应与改动前基线逐字节一致——归一化后 muxer 缺省噪声不漏键）
#   TRK-002..007 六引擎动词 track=k：sourceSpec 跟随所选轨（素材规格 44.1k/2、48k/1、32k/2 错开）；
#           TRK-007 另验 sparse.mkv 非连续 index（video 0 + audio 1 + subtitle 2 + audio 3）
#   TRK-008  内部一致性金标准：extractAudio(track=k) 流副本 → transcode(副本) vs
#           transcode(源,track=k)——k=2/3 逐字节、k=1 数据段对齐（AAC priming 编辑列表）；
#           包域载荷与 ffmpeg -map 参考逐字节 parity；扩展名跟随所选轨编码
#   TRK-009  缺省零漂移：不带 track 六动词 + extractAudio 对 multi.mkv 输出与改动前基线
#           逐字节一致（sha 钉死）；default 旗标轨被缺省选中（sourceSpec=48k/1 非首轨 44.1k/2）
#   TRK-010  错误面：track 非整数/负数 → E_PARAMS；越界/指向视频流/字幕流 → E_FILE；
#           probe/extractVideo/wsInfo/modelList/batch 带 track → 未知字段 E_PARAMS
#   TRK-011  pipeline：顶层 track=3 两步链 → 步 1 params.track=3、步 2 无；步 1 源规格
#           跟随轨 3；步内 track 预校验拒；track=99 步 1 前 fail-fast 无版本目录
#   TRK-012  fail-fast 时序：track=99 先于模型加载（非法模型 + 越界轨 → E_FILE 而非 E_MODEL，
#           对照组同模型 + 合法轨 → E_MODEL）
#   TRK-013  工作区记录：带 track 任务 params 含 track；不带 track params 键面零漂移
#   TRK-014  extractAudio 缺省名轨标记：track=1/3 → *_t1/_t3 段 + 扩展名跟随；无 track 命名不变
#   TRK-GUI  GUI 面（trk-gui-probe：engine 相 doProbe 双发射/槽组键/产物名 + gui 相
#           MainWidget 轨道下拉全链——建项/缺省/换文件同步重置/busy 不锁/七槽与管线轨参随行）
#   ZD-NET   零漂移回归网：run-json/run-ws/run-models/run-pipe/run-batch + gui-ws/gui-pipe/
#           gui-batch + gui-slot-eqv/thinshell-eqv 全绿（信封/组包/薄壳面不假红）
#   BUILD   linux-test 全量重编（--clean-first）零警告零错误
# 前置：cmake --preset linux-test 已配置；third_party 模型在位（scripts/fetch-deps.sh）。
# ⚠ 工作区/大产物走磁盘卷：ai/hq/sep 产物与回归网 run-models/run-pipe/run-batch 的 f32 中间物
#   会撞 /tmp 配额（fr8 round-2 教训）——ci-smoke 以 TMPDIR=<构建目录>/.trk-tmp 注入（FR4TMP 同型）；
#   本地直跑建议 export TMPDIR 到磁盘卷。
# 依赖：python3（stdlib）；TRK-GUI 探针编译 g++ + pkg-config + moc（gui-ws.sh 同款配方）。
# 用法：run-trk.sh [构建目录=build-linux]
set -uo pipefail
cd "$(dirname "$0")/../../../.." # scripts→fr1-audio-track-selection→acceptance→docs→仓库根
[ -f CMakeLists.txt ] || { echo "FAIL: 未落到仓库根（$(pwd)）"; exit 1; }

BUILD="${1:-build-linux}"
S="$(cd "$(dirname "$0")" && pwd)" # 本目录（探针源码同层）
VH="$BUILD/video_handler"
GTCRN=third_party/models/denoiser/gtcrn_simple.onnx
UVR=third_party/models/separator/UVR-MDX-NET-Voc_FT.onnx
DFN3=third_party/models/denoiser/dfn3
for f in "$VH" "$GTCRN" "$UVR" "$DFN3/config.ini"; do
    [ -e "$f" ] || { echo "FAIL: 缺 $f（先构建 / 跑 scripts/fetch-deps.sh）"; exit 1; }
done

JC=docs/acceptance/fr24-command-layer/scripts/json_check.py
W=$(mktemp -d) # 工作目录（TMPDIR 注入磁盘卷时随之落盘）
FAIL=0
trap 'rm -rf "$W"' EXIT

# ---- 改动前基线钉子（/tmp/trk-baseline.sh 于 1d61dcf+改动前工作树采集；素材合成确定性已实证） ----
PIN_PROBE_TINY=1096ce43d5bf8a9676ca346c3827cbd244c30d3437a6580a2907df2858f200e7
PIN_TRANSCODE=30977cf9aa2cefa28e1ac86dc6bab7f40463b336d90a0e263aa49aff596ee47b
PIN_DENOISE=01a47ab6624222dbdc76e7b9b44e17628fcf8c31e0945700f3f92e79b1429b7d
PIN_PITCH=c40fbfefb2c5239323ed4d59753b77b8fad2de98aee7d1766f4d3bdadead50c5
PIN_AI=d01b316a10a66a773ba9b7532f42fc1a197cf6c4baa834b3fdfdb014e3682a98
PIN_HQ=b294d2bca35b5a3e775c4ca60b1761f393d606b62c335ad2c7c8964ae9796943
PIN_SEP=a397e9925b406706bf1240b48085df3b1c6b0ee67045655c87e25f1301e8d7b8
PIN_XA_DEFAULT=2faf301b7b43496a01d68df4016e28a6 # 缺省提取（best-stream = default 旗标 mp2 轨）包域载荷
PIN_XA_T1=b47655627c0f70d586a920866815e6a1     # track=1（aac）包域载荷
PIN_XA_T3=854ac97b99c1d46c43fd718fa035eb33     # track=3（ac3）包域载荷

# ---- 助手（run-fr4.sh 同型） ----

jr() { # <id> <期望退出码> <请求JSON> [断言...]：跑一条 --json 用例并判定
    local id=$1 exprc=$2 req=$3; shift 3
    printf '%s' "$req" | "$VH" --json >"$W/$id.resp" 2>"$W/$id.err"
    local rc=$?
    local ok=1
    [ "$rc" = "$exprc" ] || { echo "  FAIL $id: 退出码 $rc ≠ 期望 $exprc"; ok=0; }
    python3 "$JC" "$W/$id.resp" "$@" || ok=0
    if [ "$ok" = 1 ]; then echo "  PASS $id"
    else echo "  FAIL $id"; [ -s "$W/$id.err" ] && sed 's/^/    stderr│ /' "$W/$id.err"
        FAIL=1; fi
}

ex() { # <id> <命令...>：附加 shell 级断言（退出码 0 即 PASS）
    local id=$1; shift
    if "$@"; then echo "  PASS $id"; else echo "  FAIL $id: 附加断言失败"; FAIL=1; fi
}

pyex() { # <id> <py表达式>：python3 -c 断言（表达式为真退出 0；可读 $W/ 下文件拼路径）
    local id=$1 expr=$2
    if python3 -c "$expr"; then echo "  PASS $id"; else echo "  FAIL $id"; FAIL=1; fi
}

shapin() { # <id> <文件> <sha256>：产物与改动前基线逐字节一致
    local got
    got=$(sha256sum "$2" | cut -d' ' -f1)
    if [ "$got" = "$3" ]; then echo "  PASS $1"
    else echo "  FAIL $1: sha256 $got ≠ 基线 $3"; FAIL=1; fi
}

paymd5() { # <文件>：包域载荷 MD5（-c copy -f data，容器字节随机性隔离——matroska UIDs 不入载荷）
    ffmpeg -v error -i "$1" -map 0:a -c copy -f data - 2>/dev/null | md5sum | cut -d' ' -f1
}

# =============================================================
echo "== BUILD：linux-test 全量重编零警告（matrix BUILD 行） =="

BUILDLOG="$W/build.log"
if cmake --build "$BUILD" --clean-first >"$BUILDLOG" 2>&1 && [ -x "$VH" ]; then
    if grep -iE 'warning|error' "$BUILDLOG" >"$W/build-hit.txt"; then
        echo "  FAIL BUILD: 构建输出含警告/错误"; sed 's/^/    │ /' "$W/build-hit.txt" | head -20; FAIL=1
    else echo "  PASS BUILD（零警告零错误，全量重编）"; fi
else
    echo "  FAIL BUILD: 构建失败"; tail -40 "$BUILDLOG"; FAIL=1
fi

# =============================================================
echo "== 素材（run-trk 同配方；规格/时长/语言/旗标错开作选择判别） =="

# a1/a2/a3 = 440Hz/44.1k/2ch/5s、880Hz/48k/1ch/4s、1320Hz/32k/2ch/3s（wav）
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=5" -ac 2 -c:a pcm_s16le "$W/a1.wav"
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=880:sample_rate=48000:duration=4" -ac 1 -c:a pcm_s16le "$W/a2.wav"
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=1320:sample_rate=32000:duration=3" -ac 2 -c:a pcm_s16le "$W/a3.wav"
# multi.mkv：v0(h264) + a1→流1(aac/eng/44.1k/2) + a2→流2(mp2/jpn/48k/1,DEFAULT) + a3→流3(ac3/chi+标题 评述/32k/2)
#   default 旗标刻意打在后轨（-disposition:a:0 0 清 muxer 首轨缺省 + a:1 default）→ 缺省选择非首轨可观测
ffmpeg -y -loglevel error -f lavfi -i "testsrc2=size=320x240:rate=25:duration=3" -i "$W/a1.wav" -i "$W/a2.wav" -i "$W/a3.wav" \
  -map 0:v -map 1:a -map 2:a -map 3:a -c:v libx264 -pix_fmt yuv420p -c:a:0 aac -c:a:1 mp2 -c:a:2 ac3 \
  -metadata:s:a:0 language=eng -metadata:s:a:1 language=jpn -metadata:s:a:2 language=chi \
  -metadata:s:a:2 title=评述 \
  -disposition:a:0 0 -disposition:a:1 default "$W/multi.mkv"
# nodefault.mkv：全 disposition 清零（缺省回落评分序 = 首条音频轨 ac3 32k/2，对照组）
ffmpeg -y -loglevel error -f lavfi -i "testsrc2=size=320x240:rate=25:duration=3" -i "$W/a3.wav" -i "$W/a1.wav" \
  -map 0:v -map 1:a -map 2:a -c:v libx264 -pix_fmt yuv420p -c:a:0 ac3 -c:a:1 aac \
  -disposition:a 0 "$W/nodefault.mkv"
# sparse.mkv：v0 + mp2/jpn→流1 + srt字幕→流2 + ac3/chi→流3（非连续 index：track=2 是字幕）
printf '1\n00:00:00,000 --> 00:00:02,000\n测试字幕\n' > "$W/sub.srt"
ffmpeg -y -loglevel error -f lavfi -i "testsrc2=size=320x240:rate=25:duration=3" -i "$W/a2.wav" -i "$W/a3.wav" -i "$W/sub.srt" \
  -map 0:v -map 1:a -map 3:s -map 2:a -c:v libx264 -pix_fmt yuv420p -c:a:0 mp2 -c:a:1 ac3 \
  -metadata:s:a:0 language=jpn -metadata:s:a:1 language=chi "$W/sparse.mkv"
# tiny.mp4：单音轨（合成字节确定性 + muxer 缺省噪声 language=und/全员 default=1 归一不漏键的零漂移锚）
ffmpeg -y -loglevel error -f lavfi -i "testsrc2=size=320x240:rate=25:duration=3" -f lavfi -i "sine=frequency=440:duration=3" \
  -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "$W/tiny.mp4"
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=5" -ac 2 -c:a pcm_s16le "$W/st.wav"
for f in multi.mkv nodefault.mkv sparse.mkv tiny.mp4 st.wav; do
    [ -s "$W/$f" ] || { echo "FAIL: 素材 $f 合成失败"; exit 1; }
done

# =============================================================
echo "== TRK-001：probe 增键面（条件插键 + tiny 零漂移门） =="

jr TRK-001a 0 "{\"cmd\":\"probe\",\"input\":\"$W/multi.mkv\"}" \
    ok=true cmd=probe v=1 \
    num:result.streams.0.index=0 str:result.streams.0.type=video \
    absent:result.streams.0.language absent:result.streams.0.title absent:result.streams.0.defaultTrack \
    num:result.streams.1.index=1 str:result.streams.1.type=audio str:result.streams.1.codec=aac \
    str:result.streams.1.language=eng absent:result.streams.1.title absent:result.streams.1.defaultTrack \
    num:result.streams.1.sampleRate=44100 num:result.streams.1.channels=2 \
    num:result.streams.2.index=2 str:result.streams.2.type=audio str:result.streams.2.codec=mp2 \
    str:result.streams.2.language=jpn bool:result.streams.2.defaultTrack=true absent:result.streams.2.title \
    num:result.streams.2.sampleRate=48000 num:result.streams.2.channels=1 \
    num:result.streams.3.index=3 str:result.streams.3.type=audio str:result.streams.3.codec=ac3 \
    str:result.streams.3.language=chi str:result.streams.3.title=评述 absent:result.streams.3.defaultTrack \
    num:result.streams.3.sampleRate=32000 num:result.streams.3.channels=2
# tiny.mp4：响应与改动前基线逐字节一致（mp4 muxer 对全部流写 language=und + disposition default=1 的
# 缺省噪声经归一化不漏键——und 视同无、旗标全音轨命中视同无，老素材零漂移）
printf '%s' "{\"v\":1,\"cmd\":\"probe\",\"input\":\"$W/tiny.mp4\"}" | "$VH" --json >"$W/probe-tiny.json" 2>/dev/null
shapin TRK-001b "$W/probe-tiny.json" "$PIN_PROBE_TINY"
# nodefault.mkv：无旗标素材不插 defaultTrack（旗标全无 = 无区分度同归一语义）
jr TRK-001c 0 "{\"cmd\":\"probe\",\"input\":\"$W/nodefault.mkv\"}" \
    ok=true absent:result.streams.1.defaultTrack absent:result.streams.2.defaultTrack \
    absent:result.streams.1.language absent:result.streams.2.language

# =============================================================
echo "== TRK-002..007：六引擎动词 track=k（sourceSpec 跟随所选轨） =="

jr TRK-002a 0 "{\"cmd\":\"transcode\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/t2a.wav\",\"track\":1}" \
    ok=true num:result.sourceSpec.sampleRate=44100 num:result.sourceSpec.channels=2 \
    num:result.outputSpec.sampleRate=44100 num:result.outputSpec.channels=2 \
    bool:result.engineResampled=false numrange:result.durationSec=4.9:5.1 exists:result.output
jr TRK-002b 0 "{\"cmd\":\"transcode\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/t2b.wav\",\"track\":2}" \
    ok=true num:result.sourceSpec.sampleRate=48000 num:result.sourceSpec.channels=1 \
    num:result.outputSpec.sampleRate=48000 num:result.outputSpec.channels=1 \
    bool:result.engineResampled=false numrange:result.durationSec=3.9:4.1
jr TRK-002c 0 "{\"cmd\":\"transcode\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/t2c.wav\",\"track\":3}" \
    ok=true num:result.sourceSpec.sampleRate=32000 num:result.sourceSpec.channels=2 \
    num:result.outputSpec.sampleRate=32000 num:result.outputSpec.channels=2 \
    numrange:result.durationSec=2.9:3.1

jr TRK-003a 0 "{\"cmd\":\"denoise\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/t3a.wav\",\"track\":2,\"humBase\":\"50\",\"wind\":true}" \
    ok=true num:result.sourceSpec.sampleRate=48000 num:result.sourceSpec.channels=1 \
    num:result.outputSpec.sampleRate=48000 num:result.outputSpec.channels=1 \
    numrange:result.durationSec=3.9:4.1 exists:result.output
# 内部一致性（陷波系数按所选轨采样率的整链证据）：流副本(denoise) == 源选轨(denoise) 逐字节
jr TRK-003x 0 "{\"cmd\":\"extractAudio\",\"input\":\"$W/multi.mkv\",\"track\":2}" \
    ok=true exists:result.output contains:result.output=multi_t2_audio.mka
jr TRK-003b 0 "{\"cmd\":\"denoise\",\"input\":\"$W/multi_t2_audio.mka\",\"output\":\"$W/t3b.wav\",\"humBase\":\"50\",\"wind\":true}" ok=true
jr TRK-003c 0 "{\"cmd\":\"denoise\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/t3c.wav\",\"track\":2,\"humBase\":\"50\",\"wind\":true}" ok=true
ex TRK-003d cmp "$W/t3b.wav" "$W/t3c.wav"

jr TRK-004 0 "{\"cmd\":\"aiDenoise\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/t4.wav\",\"track\":2,\"model\":\"$PWD/$GTCRN\"}" \
    ok=true num:result.sourceSpec.sampleRate=48000 num:result.sourceSpec.channels=1 \
    num:result.outputSpec.sampleRate=44100 num:result.outputSpec.channels=2 \
    bool:result.engineResampled=true numrange:result.durationSec=3.9:4.1

jr TRK-005 0 "{\"cmd\":\"aiDenoiseHq\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/t5.wav\",\"track\":3,\"model\":\"$PWD/$DFN3\"}" \
    ok=true num:result.sourceSpec.sampleRate=32000 num:result.sourceSpec.channels=2 \
    num:result.outputSpec.sampleRate=32000 num:result.outputSpec.channels=2 \
    numrange:result.durationSec=2.9:3.1

jr TRK-006 0 "{\"cmd\":\"pitch\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/t6.wav\",\"track\":1,\"semi\":2.0}" \
    ok=true num:result.sourceSpec.sampleRate=44100 num:result.sourceSpec.channels=2 \
    num:result.outputSpec.sampleRate=44100 num:result.outputSpec.channels=2 \
    numrange:result.durationSec=4.9:5.1

jr TRK-007a 0 "{\"cmd\":\"separate\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/t7a.wav\",\"track\":3,\"model\":\"$PWD/$UVR\"}" \
    ok=true num:result.sourceSpec.sampleRate=32000 num:result.sourceSpec.channels=2 \
    num:result.outputSpec.sampleRate=44100 num:result.outputSpec.channels=2 \
    numrange:result.durationSec=2.9:3.1
jr TRK-007b 0 "{\"cmd\":\"separate\",\"input\":\"$W/sparse.mkv\",\"output\":\"$W/t7b.wav\",\"track\":3,\"model\":\"$PWD/$UVR\"}" \
    ok=true num:result.sourceSpec.sampleRate=32000 num:result.sourceSpec.channels=2
jr TRK-007c 0 "{\"cmd\":\"separate\",\"input\":\"$W/sparse.mkv\",\"output\":\"$W/t7c.wav\",\"track\":1,\"model\":\"$PWD/$UVR\"}" \
    ok=true num:result.sourceSpec.sampleRate=48000 num:result.sourceSpec.channels=1
jr TRK-007d 1 "{\"cmd\":\"separate\",\"input\":\"$W/sparse.mkv\",\"output\":\"$W/t7d.wav\",\"track\":2,\"model\":\"$PWD/$UVR\"}" \
    ok=false code=E_FILE msg-contains="track 2 不是音频流" absent:result

# =============================================================
echo "== TRK-008/014：extractAudio 轨标记 + 内部一致性金标准 =="

jr TRK-014a 0 "{\"cmd\":\"extractAudio\",\"input\":\"$W/multi.mkv\",\"track\":1}" \
    ok=true exists:result.output contains:result.output=multi_t1_audio.m4a
jr TRK-014b 0 "{\"cmd\":\"extractAudio\",\"input\":\"$W/multi.mkv\",\"track\":3}" \
    ok=true exists:result.output contains:result.output=multi_t3_audio.ac3
jr TRK-014c 0 "{\"cmd\":\"extractAudio\",\"input\":\"$W/multi.mkv\"}" \
    ok=true exists:result.output contains:result.output=multi_audio.mka
ex TRK-014d test -f "$W/multi_t1_audio.m4a" -a -f "$W/multi_t2_audio.mka" -a -f "$W/multi_t3_audio.ac3" -a -f "$W/multi_audio.mka"
pyex TRK-014f "
import os
w='$W'
got=sorted(f for f in os.listdir(w) if f.startswith('multi_') and 'audio' in f)
assert got==['multi_audio.mka','multi_t1_audio.m4a','multi_t2_audio.mka','multi_t3_audio.ac3'], got" # 缺省名无 _t 段 + 轨名扩展名跟随编码（OQ-4）

# 包域载荷 parity：extractAudio(track=k) == ffmpeg -map 0:a:<k-1> 流复制（内部一致性，容器随机性隔离）
ex TRK-008a test "$(paymd5 "$W/multi_t1_audio.m4a")" = "$(ffmpeg -v error -i "$W/multi.mkv" -map 0:a:0 -c copy -f data - 2>/dev/null | md5sum | cut -d' ' -f1)"
ex TRK-008b test "$(paymd5 "$W/multi_t2_audio.mka")" = "$(ffmpeg -v error -i "$W/multi.mkv" -map 0:a:1 -c copy -f data - 2>/dev/null | md5sum | cut -d' ' -f1)"
ex TRK-008c test "$(paymd5 "$W/multi_t3_audio.ac3")" = "$(ffmpeg -v error -i "$W/multi.mkv" -map 0:a:2 -c copy -f data - 2>/dev/null | md5sum | cut -d' ' -f1)"
ex TRK-008d test "$(paymd5 "$W/multi_t1_audio.m4a")" = "$PIN_XA_T1"
ex TRK-008e test "$(paymd5 "$W/multi_t3_audio.ac3")" = "$PIN_XA_T3"
# 转码一致性金标准：transcode(流副本) vs transcode(源,track=k)——k=2/3 逐字节；k=1 对齐（AAC priming
# 编辑列表裁头 1024 帧 = 1024×2ch×4B=8192B，数据段起对各字节一致；头偏移动态探测不硬编码）
jr TRK-008f 0 "{\"cmd\":\"transcode\",\"input\":\"$W/multi_t2_audio.mka\",\"output\":\"$W/t8f.wav\"}" ok=true
ex TRK-008g cmp "$W/t8f.wav" "$W/t2b.wav"
jr TRK-008h 0 "{\"cmd\":\"transcode\",\"input\":\"$W/multi_t3_audio.ac3\",\"output\":\"$W/t8h.wav\"}" ok=true
ex TRK-008i cmp "$W/t8h.wav" "$W/t2c.wav"
jr TRK-008j 0 "{\"cmd\":\"transcode\",\"input\":\"$W/multi_t1_audio.m4a\",\"output\":\"$W/t8j.wav\"}" ok=true
pyex TRK-008k "
import struct
def data_off(path):
    with open(path,'rb') as f: b=f.read(256)
    assert b[:4]==b'RIFF' and b[8:12]==b'WAVE', (path,b[:12])
    off=12
    while off < len(b)-8:
        cid=b[off:off+4]; sz=struct.unpack('<I',b[off+4:off+8])[0]
        if cid==b'data': return off+8
        off += 8+sz+(sz&1)
    raise AssertionError('无 data 块: '+path)
src=open('$W/t2a.wav','rb'); cp=open('$W/t8j.wav','rb')
so,co=data_off('$W/t2a.wav'),data_off('$W/t8j.wav')
src.seek(so+8192); cp.seek(co) # 源侧轨 1 多出的 priming 1024 帧（44.1k/2ch f32）
a,b=src.read(),cp.read()
assert len(a)==len(b), (len(a),len(b))
assert a==b, '数据段不一致'"

# =============================================================
echo "== TRK-009：缺省零漂移（不带 track 与改动前基线逐字节一致 + default 旗标轨被缺省选中） =="

jr TRK-009a 0 "{\"cmd\":\"transcode\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/z9a.wav\"}" \
    ok=true num:result.sourceSpec.sampleRate=48000 num:result.sourceSpec.channels=1
shapin TRK-009a-z "$W/z9a.wav" "$PIN_TRANSCODE"
jr TRK-009b 0 "{\"cmd\":\"denoise\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/z9b.wav\",\"humBase\":\"50\",\"wind\":true}" ok=true
shapin TRK-009b-z "$W/z9b.wav" "$PIN_DENOISE"
jr TRK-009c 0 "{\"cmd\":\"pitch\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/z9c.wav\",\"semi\":2.0}" ok=true
shapin TRK-009c-z "$W/z9c.wav" "$PIN_PITCH"
jr TRK-009d 0 "{\"cmd\":\"aiDenoise\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/z9d.wav\",\"model\":\"$PWD/$GTCRN\"}" ok=true
shapin TRK-009d-z "$W/z9d.wav" "$PIN_AI"
jr TRK-009e 0 "{\"cmd\":\"aiDenoiseHq\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/z9e.wav\",\"model\":\"$PWD/$DFN3\"}" ok=true
shapin TRK-009e-z "$W/z9e.wav" "$PIN_HQ"
jr TRK-009f 0 "{\"cmd\":\"separate\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/z9f.wav\",\"model\":\"$PWD/$UVR\"}" ok=true
shapin TRK-009f-z "$W/z9f.wav" "$PIN_SEP"
ex TRK-009g test "$(paymd5 "$W/multi_audio.mka")" = "$PIN_XA_DEFAULT"
# default 旗标语义对照（nodefault.mkv 旗标全清 + 规格再错开）：无旗标回落 = FFmpeg 自身评分序
# （实测 aac 44.1k/2 胜 ac3 32k/2 与 ac3 2ch vs aac 1ch——编解码器偏好优先于声道数，非「首条音频轨」）；
# 与 009a 合看：default 旗标 > 编解码器偏好（multi 的 mp2 旗标轨胜过两条 2ch 轨）。选择恒 av_find_best_stream 原式。
jr TRK-009h 0 "{\"cmd\":\"transcode\",\"input\":\"$W/nodefault.mkv\",\"output\":\"$W/z9h.wav\"}" \
    ok=true num:result.sourceSpec.sampleRate=44100 num:result.sourceSpec.channels=2

# =============================================================
echo "== TRK-010：错误面（信封校验 E_PARAMS / 文件结构事实 E_FILE / 拒收面未知字段） =="

jr TRK-010a 1 "{\"cmd\":\"denoise\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/e.wav\",\"track\":\"1\",\"humBase\":\"50\",\"wind\":true}" \
    ok=false code=E_PARAMS msg-contains="track 必须是整数" absent:result
jr TRK-010b 1 "{\"cmd\":\"denoise\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/e.wav\",\"track\":1.5,\"humBase\":\"50\",\"wind\":true}" \
    ok=false code=E_PARAMS msg-contains="track 必须是整数" absent:result
jr TRK-010c 1 "{\"cmd\":\"denoise\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/e.wav\",\"track\":-1,\"humBase\":\"50\",\"wind\":true}" \
    ok=false code=E_PARAMS msg-contains="track 不能为负" absent:result
jr TRK-010d 1 "{\"cmd\":\"denoise\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/e.wav\",\"track\":99,\"humBase\":\"50\",\"wind\":true}" \
    ok=false code=E_FILE msg-contains="track 99 超出流范围（共 4 条流）" absent:result
jr TRK-010e 1 "{\"cmd\":\"denoise\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/e.wav\",\"track\":0,\"humBase\":\"50\",\"wind\":true}" \
    ok=false code=E_FILE msg-contains="track 0 不是音频流" absent:result
jr TRK-010f 1 "{\"cmd\":\"denoise\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/e.wav\",\"track\":1e300,\"humBase\":\"50\",\"wind\":true}" \
    ok=false code=E_FILE msg-contains="超出流范围" absent:result # 1e300 饱和 int 上限仍越界
jr TRK-010g 1 "{\"cmd\":\"denoise\",\"input\":\"$W/st.wav\",\"output\":\"$W/e.wav\",\"track\":1,\"humBase\":\"50\",\"wind\":true}" \
    ok=false code=E_FILE msg-contains="track 1 超出流范围（共 1 条流）" absent:result
jr TRK-010h 1 "{\"cmd\":\"probe\",\"input\":\"$W/multi.mkv\",\"track\":1}" \
    ok=false code=E_PARAMS msg-contains="未知字段：track" absent:result
jr TRK-010i 1 "{\"cmd\":\"extractVideo\",\"input\":\"$W/multi.mkv\",\"track\":1}" \
    ok=false code=E_PARAMS msg-contains="未知字段：track" absent:result
jr TRK-010j 1 "{\"cmd\":\"wsInfo\",\"input\":\"$W/multi.mkv\",\"track\":1}" \
    ok=false code=E_PARAMS msg-contains="未知字段：track" absent:result
jr TRK-010k 1 "{\"cmd\":\"modelList\",\"track\":1}" \
    ok=false code=E_PARAMS msg-contains="未知字段：track" absent:result
jr TRK-010l 1 "{\"cmd\":\"batch\",\"track\":1,\"inputs\":[\"$W/st.wav\",\"$W/a1.wav\"],\"workspace\":\"$W/ws-batch\",\"steps\":[{\"cmd\":\"denoise\",\"humBase\":\"50\"}]}" \
    ok=false code=E_PARAMS msg-contains="未知字段：track" absent:result

# =============================================================
echo "== TRK-011/012：pipeline 顶层轨参 + fail-fast 时序 =="

jr TRK-011a 0 "{\"cmd\":\"pipeline\",\"input\":\"$W/multi.mkv\",\"workspace\":\"$W/ws-pipe\",\"track\":3,\"steps\":[{\"cmd\":\"denoise\",\"humBase\":\"50\",\"wind\":true},{\"cmd\":\"pitch\",\"semi\":2.0}]}" \
    ok=true str:result.workspace.root="$W/ws-pipe" num:result.workspace.version=2 exists:result.output
pyex TRK-011b "
import json,glob
d=json.load(open(glob.glob('$W/ws-pipe/*/workspace.json')[0]))
vs=d['versions']; assert len(vs)==2, len(vs)
assert vs[0]['params'].get('track')==3, vs[0]['params']   # 步 1（denoise）params 含 track
assert 'track' not in vs[1]['params'], vs[1]['params']     # 步 2（pitch）不注轨参（中间产物单轨）
assert vs[0]['cmd']=='denoise' and vs[1]['cmd']=='pitch', [v['cmd'] for v in vs]
assert vs[0]['pipeline']=={'index':1,'total':2} and 'batch' not in vs[0], vs[0]  # 版本标记键面不漂移
assert vs[0]['output']['path'].startswith('v001_denoise') and vs[1]['output']['path'].startswith('v002_pitch'), vs"
pyex TRK-011c "
import json,glob
d=json.load(open(glob.glob('$W/ws-pipe/*/workspace.json')[0]))
assert d['versions'][0]['input']['path'].endswith('multi.mkv'), d['versions'][0]['input']  # 步 1 源 = 原件（顶层 track 注步 1 子请求）"
# 步 1 消费轨 3 的运行期证据：pipeline 产物时长 = 轨 3（3s），轨 1 是 5s、轨 2 是 4s
pyex TRK-011d "
import glob,struct
p=glob.glob('$W/ws-pipe/*/v002_pitch/*.wav')[0]
b=open(p,'rb').read(256)
assert b[:4]==b'RIFF' and b[8:12]==b'WAVE', b[:12]
off=12; rate=ch=data=None
while off < len(b)-8:
    cid=b[off:off+4]; sz=struct.unpack('<I',b[off+4:off+8])[0]
    if cid==b'fmt ': ch=struct.unpack('<H',b[off+10:off+12])[0]; rate=struct.unpack('<I',b[off+12:off+16])[0]
    if cid==b'data': data=sz; break
    off += 8+sz+(sz&1)
dur=data/(rate*ch*4)  # f32：帧字节 = 声道×4
assert 2.9 <= dur <= 3.1, dur"
# 步内 track：整链静态预校验拒（steps[0] 未知字段，零执行零产物）
jr TRK-011e 1 "{\"cmd\":\"pipeline\",\"input\":\"$W/multi.mkv\",\"workspace\":\"$W/ws-pipe2\",\"steps\":[{\"cmd\":\"denoise\",\"track\":1,\"humBase\":\"50\"},{\"cmd\":\"pitch\",\"semi\":2.0}]}" \
    ok=false code=E_PARAMS msg-contains="steps[0] 未知字段：track" absent:result
ex TRK-011f test "$(ls "$W/ws-pipe2" 2>/dev/null | wc -l)" = 0
# track=99：步 1 前 fail-fast（probeSourceSpec 短路先于 create/planVersion），归属步骤 1
jr TRK-011g 1 "{\"cmd\":\"pipeline\",\"input\":\"$W/multi.mkv\",\"workspace\":\"$W/ws-pipe3\",\"track\":99,\"steps\":[{\"cmd\":\"denoise\",\"humBase\":\"50\"},{\"cmd\":\"pitch\",\"semi\":2.0}]}" \
    ok=false code=E_FILE msg-contains="步骤 1（denoise）" msg-contains="track 99 超出流范围（共 4 条流）" absent:result
ex TRK-011i test "$(ls "$W/ws-pipe3" 2>/dev/null | wc -l)" = 0
# fail-fast 时序（TRK-012）：非法模型 + 越界轨 → E_FILE（轨校验先于模型加载）；
# 对照组同模型 + 合法轨 → E_MODEL（证明该模型确实会加载失败，非假对照）
jr TRK-012a 1 "{\"cmd\":\"aiDenoise\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/e12.wav\",\"track\":99,\"model\":\"/bin/true\"}" \
    ok=false code=E_FILE msg-contains="track 99 超出流范围" absent:result
jr TRK-012b 1 "{\"cmd\":\"aiDenoise\",\"input\":\"$W/multi.mkv\",\"output\":\"$W/e12.wav\",\"track\":1,\"model\":\"/bin/true\"}" \
    ok=false code=E_MODEL msg-contains="加载 AI 模型失败" absent:result
jr TRK-012c 1 "{\"cmd\":\"aiDenoise\",\"input\":\"$W/multi.mkv\",\"workspace\":\"$W/ws-ff\",\"track\":99,\"model\":\"$PWD/$GTCRN\"}" \
    ok=false code=E_FILE msg-contains="track 99 超出流范围" absent:result
ex TRK-012d test "$(ls "$W/ws-ff" 2>/dev/null | wc -l)" = 0 # 无版本目录（fail-fast 先于 planVersion 全量哈希）

# =============================================================
echo "== TRK-013：工作区版本记录（带 track 入 params / 不带 track 键面零漂移） =="

jr TRK-013a 0 "{\"cmd\":\"denoise\",\"input\":\"$W/multi.mkv\",\"workspace\":\"$W/ws-t13\",\"track\":1,\"humBase\":\"50\",\"wind\":true}" \
    ok=true num:result.workspace.version=1 exists:result.output
pyex TRK-013b "
import json,glob
d=json.load(open(glob.glob('$W/ws-t13/*/workspace.json')[0]))
v=d['versions'][0]
assert v['params']=={'humBase':'50','wind':True,'track':1}, v['params']  # track 自然入 params
assert sorted(v)==['cmd','input','model','output','params','parentVersion','time','version'], sorted(v)  # 记录键面零漂移（无新增 track 顶层键；model 恒在、denoise 无模型为 null）
assert v['model'] is None and 'batch' not in v and 'pipeline' not in v, v"
jr TRK-013c 0 "{\"cmd\":\"denoise\",\"input\":\"$W/multi.mkv\",\"workspace\":\"$W/ws-t13n\",\"humBase\":\"50\",\"wind\":true}" ok=true
pyex TRK-013d "
import json,glob
d=json.load(open(glob.glob('$W/ws-t13n/*/workspace.json')[0]))
v=d['versions'][0]
assert v['params']=={'humBase':'50','wind':True}, v['params']  # 不带 track：params 键面与改动前逐键一致
assert sorted(v)==['cmd','input','model','output','params','parentVersion','time','version'], sorted(v)
assert v['model'] is None, v['model']"

# =============================================================
echo "== TRK-GUI：GUI 面（trk-gui-probe：engine 相槽直调 + gui 相 MainWidget 全链） =="

MOC=$(ls /usr/lib/qt6/libexec/moc 2>/dev/null || command -v moc)
[ -n "$MOC" ] || { echo "  FAIL TRK-GUI: 缺 moc（qt6-base-dev-tools）"; FAIL=1; }
if [ -n "$MOC" ]; then
    "$MOC" src/service/mediaservice.h -o "$W/moc_mediaservice.cpp"
    "$MOC" src/view/mainwidget.h -o "$W/moc_mainwidget.cpp"
    PKGS_GUI="Qt6Core Qt6Network Qt6Widgets libavformat libavcodec libavutil libswresample soundtouch"
    g++ -std=c++20 -O2 "$S/trk-gui-probe.cpp" src/view/mainwidget.cpp src/service/mediaservice.cpp \
        "$W/moc_mediaservice.cpp" "$W/moc_mainwidget.cpp" -o "$W/trk-probe" -I"$(pwd)/src" \
        $(pkg-config --cflags $PKGS_GUI) "$BUILD/libengine.a" \
        $(pkg-config --libs $PKGS_GUI) "$(pwd)/third_party/ort/linux/lib/libonnxruntime.so" \
        -Wl,-rpath,"$(pwd)/third_party/ort/linux/lib" \
        || { echo "  FAIL TRK-GUI: 探针编译失败"; FAIL=1; }
fi

if [ -x "$W/trk-probe" ]; then
    export QT_QPA_PLATFORM=offscreen
    # engine 相：FFExtractor 槽直调（doProbe 双发射 / doExtractAudio 组键与产物名 / probeText 零漂移锚）
    CFG_E="$W/cfg-eng"; mkdir -p "$CFG_E"
    XDG_CONFIG_HOME="$CFG_E" "$W/trk-probe" engine "$W/multi.mkv" "$W/ws-eng-a" "$W/ws-eng-b" >"$W/trk-eng.out" 2>&1
    pyex TRK-GUI-101 "
o=open('$W/trk-eng.out',encoding='utf-8').read()
assert 'TEXTZERO equal=1 legacyOk=1' in o, o        # probeDone 文本与旧静态入口逐字零漂移
assert '\"language\":\"eng\"' in o and '\"language\":\"jpn\"' in o and '\"title\":\"评述\"' in o, o
assert '\"defaultTrack\":true' in o, o              # 双发射 JSON 条件键（与 cmdProbe 同源 probeJson 单源）
assert 'TASK_T1 ok=1' in o and '_t1_audio.m4a' in o, o   # 显式轨：缺省名插 _t1 段（OQ-4）
assert '\"track\":1' in o.split('TASK_T1')[1].split('TASK_AUTO')[0], o  # 版本记录 params 含 track:1
assert 'TASK_AUTO ok=1' in o, o
auto=o.split('TASK_AUTO')[1]
assert '\"track\"' not in auto.split('out=')[0], auto    # 缺省 -1：请求不带键（params {}）
assert '_t' not in auto.split('\\n')[2], auto            # 产物名无轨段"
    pyex TRK-GUI-102 "
o=open('$W/trk-eng.out',encoding='utf-8').read()
import json,re
m=re.search(r'^PROBEJSON (.*)$',o,re.M); assert m, o
r=json.loads(m.group(1))  # probeInfoDone 载荷 = probe result 本体（非响应信封，D5）
aud=[s for s in r['streams'] if s['type']=='audio']
assert len(aud)==3, aud
assert [s['index'] for s in aud]==[1,2,3]
assert aud[1]['language']=='jpn' and aud[1]['defaultTrack'] is True, aud[1]
assert aud[2]['title']=='评述', aud[2]"

    # gui 相：MainWidget+MediaService 全链。管线预设 0（AI 降噪→人声分离）两级解析需库预装
    # gtcrn+UVR（by-id 须用目录表 id：uvr 的表 id 是 uvr-mdx-voc-ft，非文件名猜测值）
    mkdir -p "$W/lib-gui"
    printf '%s' "{\"cmd\":\"modelImport\",\"path\":\"$PWD/$GTCRN\",\"id\":\"gtcrn\",\"root\":\"$W/lib-gui\"}" | "$VH" --json >/dev/null
    printf '%s' "{\"cmd\":\"modelImport\",\"path\":\"$PWD/$UVR\",\"id\":\"uvr-mdx-voc-ft\",\"root\":\"$W/lib-gui\"}" | "$VH" --json >/dev/null
    CFG_G="$W/cfg-gui"; mkdir -p "$CFG_G"
    XDG_CONFIG_HOME="$CFG_G" "$W/trk-probe" gui "$W/multi.mkv" "$W/tiny.mp4" "$W/ws-gui" "$W/lib-gui" >"$W/trk-gui.out" 2>&1
    pyex TRK-GUI-201 "
o=open('$W/trk-gui.out',encoding='utf-8').read()
assert 'COMBO up=1 count=4 vis=1 idx=0 data0=-1' in o, o        # 自动（默认音轨）缺省选中
assert '|1#流#1 · eng · aac · 44100Hz · 2ch' in o, o
assert '|2#流#2 · jpn · mp2 · 48000Hz · 1ch · 默认' in o, o     # 条目渲染：语言/编码/规格/默认
assert '|3#流#3 · chi · 评述 · ac3 · 32000Hz · 2ch' in o, o     # 标题入标签
assert 'TEXTZERO_G equal=1' in o, o                              # txtInfo 与旧静态文本逐字零漂移（TRK-GUI-002）
assert '\"defaultTrack\":true' in o.split('PROBEJSON_G')[1].split('\n')[0], o  # 双发射到 GUI 层"
    pyex TRK-GUI-202 "
o=open('$W/trk-gui.out',encoding='utf-8').read()
assert 'BUSYNOTLOCKED cmbEnabled=1 btnLocked=1' in o, o    # busy 期轨道下拉不锁（任务按钮已锁对照组）
assert 'CAP track=3 msg=音轨提取完成：' in o, o            # 选轨 3 → 七槽请求轨参随行 + 文案零漂移
assert 'CAP2 track=-1' in o, o                             # 「自动」→ -1（组包不带键）
assert 'CAPPIPE track=3' in o and 'msg=管线完成：' in o, o  # 管线轨参随行 + 成功文案逐字（TRK-GUI-002）"
    pyex TRK-GUI-203 "
o=open('$W/trk-gui.out',encoding='utf-8').read()
assert 'RESET_SYNC ok=1 count=1 vis=0 idx=0 data0=-1' in o, o  # 换文件同步重置（不等异步探测，防 stale 索引）
assert 'COMBO_TINY count=1 vis=0 idx=0 data0=-1' in o, o       # 单音轨下拉隐藏（OQ-2 拍板「隐藏」）
assert 'RESET_AUTO up=1 idx=0' in o, o                          # 重载多轨：可见且选中回「自动」
assert 'PROBE DONE' in o, o"
fi

# =============================================================
echo "== ZD-NET：零漂移回归网（信封/组包/薄壳/GUI 壳面不假红） =="

ZD="$W/zd"; mkdir -p "$ZD" # 磁盘卷中转（run-models/run-pipe/run-batch 的 f32 中间物）
ZDFAIL=0
zd_run() { # <标签> <脚本...>
    local tag=$1; shift
    echo "  ---- $tag: $*"
    if TMPDIR="$ZD" "$@"; then echo "  PASS $tag"
    else echo "  FAIL $tag"; ZDFAIL=1; FAIL=1; fi
}
zd_run ZD-NET-json   bash docs/acceptance/fr24-command-layer/scripts/run-json.sh "$BUILD"
zd_run ZD-NET-ws     bash docs/acceptance/fr-a4-workspace/scripts/run-ws.sh "$BUILD"
zd_run ZD-NET-models bash docs/acceptance/fr8-model-management/scripts/run-models.sh "$BUILD"
zd_run ZD-NET-pipe   bash docs/acceptance/fr9-pipeline/scripts/run-pipe.sh "$BUILD"
zd_run ZD-NET-batch  bash docs/acceptance/fr10-batch-queue/scripts/run-batch.sh "$BUILD"
zd_run ZD-NET-guiws  bash docs/acceptance/fr-a4-workspace/scripts/gui-ws.sh "$BUILD"
zd_run ZD-NET-guipipe bash docs/acceptance/fr9-pipeline/scripts/gui-pipe.sh "$BUILD"
zd_run ZD-NET-guibatch bash docs/acceptance/fr10-batch-queue/scripts/gui-batch.sh "$BUILD"
# gui-slot-eqv 的基线树以 $BASE/$BUILD 拼接（脚本契约期望相对构建目录），绝对路径须先剥仓库根前缀
SLOT_BUILD="$BUILD"
case "$BUILD" in /*) SLOT_BUILD="${BUILD#"$PWD"/}";; esac
zd_run ZD-NET-sloteqv  bash docs/acceptance/fr24-command-layer/scripts/gui-slot-eqv.sh fd7badb "$SLOT_BUILD"
zd_run ZD-NET-thinshell bash docs/acceptance/fr24-command-layer/scripts/thinshell-eqv.sh 1d61dcf "$BUILD"

# =============================================================
if [ "$FAIL" = 0 ]; then
    echo "== TRK 全绿 =="
else
    echo "== TRK 存在失败 =="
fi
exit "$FAIL"
