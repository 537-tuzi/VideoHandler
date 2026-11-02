#!/usr/bin/env bash
# gui-ws.sh — FR-A4 片 B（GUI 工作区接线）验收编排（GWS 族，spec §6-8/§7.1 OQ-2/3）：
# 直调 FFExtractor 槽的探针（gui-ws-probe.cpp）分三相 offscreen 跑，断言全部为绝对断言（基线无此功能）：
#   ws 相     —— GWS-001 七槽+继续链：ok=1、jobDone 文案格式不变且路径 <root>/<工作区>/vNNN_<cmd>/<产物>，
#                版本目录与产物在盘；GWS-002 workspace.json 结构（head/versions 数/parentVersion 链）；
#                GWS-004 wsInfo 回传与 workspace.json 深度一致（python）+ 全部产物（含「继续」回拼路径）在盘；
#                GWS-007 probe 槽在 root 注入后仍正常（未被插 workspace 键）
#   cancel 相 —— GWS-006 预置 cancelFlag → 「已取消」+ 无版本目录残留 + workspace.json 逐字节不变
#   direct 相 —— GWS-005 不 setWorkspaceRoot → 产物落源目录（fr24 GUI-PRD 同形状）+ 与 CLI 直跑产物 cmp 互证
#   全程      —— GWS-003 任务前后源文件 sha256 不变（原件只读）
# 判据与用例表以 ../matrix.csv GWS-* 为准；依赖与 linux-test 构建同套（pkg-config + third_party ORT/模型）。
# 用法：gui-ws.sh [构建目录=build-linux]
set -uo pipefail
cd "$(dirname "$0")/../../../.." # scripts→fr-a4-workspace→acceptance→docs→仓库根
[ -f CMakeLists.txt ] || { echo "FAIL: 未落到仓库根（$(pwd)）"; exit 1; }

BUILD="${1:-build-linux}"
VH="$BUILD/video_handler"
GTCRN=third_party/models/denoiser/gtcrn_simple.onnx
UVR=third_party/models/separator/UVR-MDX-NET-Voc_FT.onnx
S=docs/acceptance/fr-a4-workspace/scripts
for f in "$BUILD/libengine.a" "$GTCRN" "$UVR"; do
    [ -e "$f" ] || { echo "FAIL: 缺 $f（先构建/跑 fetch-deps.sh）"; exit 1; }
done

W=$(mktemp -d)
trap 'chmod -R u+w "$W" >/dev/null 2>&1; rm -rf "$W"' EXIT
FAIL=0
ck() { if [ "$1" = 0 ]; then echo "  PASS $2"; else echo "  FAIL $2"; FAIL=1; fi; }

# 陈旧库闸（审查 P2-2）：探针链 libengine.a 测引擎/服务实现，构建系统是「产物相对依赖新鲜」的权威判定
# ——先增量构建（无变化亚秒级），防编辑后忘构建、探针静默链到旧实现测出假绿
cmake --build "$BUILD" >/dev/null || { echo "FAIL: 增量构建失败（$BUILD，先配好 linux-test 再跑验收）"; exit 1; }

BIN="$W/bin"
mkdir -p "$BIN/models/denoiser" "$BIN/models/separator"
cp "$GTCRN" "$BIN/models/denoiser/"
cp "$UVR" "$BIN/models/separator/"

echo "== 探针编译（gui-ws-probe.cpp，链主树 libengine.a；模型落 bin 侧供 appDir 相对解析）=="
PKGS="Qt6Core libavformat libavcodec libavutil libswresample soundtouch"
g++ -std=c++20 -O2 "$S/gui-ws-probe.cpp" -o "$BIN/probe" -I"$(pwd)/src" \
    $(pkg-config --cflags $PKGS) "$BUILD/libengine.a" \
    $(pkg-config --libs $PKGS) "$(pwd)/third_party/ort/linux/lib/libonnxruntime.so" \
    -Wl,-rpath,"$(pwd)/third_party/ort/linux/lib" \
    || { echo "FAIL: 探针编译失败"; exit 1; }

echo "== 素材（run-ws 同配方：44.1k 立体声 + 双流 mp4 + 取消用副本，两份互不影响）=="
MEDIA="$W/media"; WSROOT="$W/wsroot"; MEDIAD="$W/media-direct"
mkdir -p "$MEDIA" "$MEDIAD"
for d in "$MEDIA" "$MEDIAD"; do
    ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=5" -ac 2 -c:a pcm_s16le "$d/st.wav"
    ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:duration=3" \
        -f lavfi -i "testsrc2=size=320x240:rate=25:duration=3" -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "$d/tiny.mp4"
    cp "$d/st.wav" "$d/cancel-st.wav"
    cp "$d/tiny.mp4" "$d/cancel-tiny.mp4"
done
SHA_ST=$(sha256sum "$MEDIA/st.wav" | cut -d' ' -f1)
SHA_MP4=$(sha256sum "$MEDIA/tiny.mp4" | cut -d' ' -f1)
WS_ST="$WSROOT/st.${SHA_ST:0:8}"
WS_MP4="$WSROOT/tiny.${SHA_MP4:0:8}"
# 素材是全部断言的前置（防空哈希/空路径把断言空转化，首跑踩过：缺 mkdir 时多检查项空洞 PASS）
[ -f "$MEDIA/st.wav" ] && [ -f "$MEDIA/tiny.mp4" ] && [ -n "$SHA_ST" ] && [ -n "$SHA_MP4" ] \
    || { echo "FAIL: 素材生成失败（$MEDIA）"; exit 1; }

echo "== ws 相（七槽 + 历史查询 + 继续链 + probe）=="
QT_QPA_PLATFORM=offscreen "$BIN/probe" "$MEDIA" "$WSROOT" ws >"$W/ws.txt" 2>"$W/ws.err"
ck $? "ws 相运行（退出码）"
[ -s "$W/ws.err" ] && { echo "  FAIL ws 相 stderr 非空"; FAIL=1; }
# cancel 相的「逐字节不变」以两个工作区索引存在为前置（防空串==空串空洞 PASS）
[ -f "$WS_ST/workspace.json" ] && [ -f "$WS_MP4/workspace.json" ] \
    || { echo "FAIL: ws 相未建成工作区（$WS_ST / $WS_MP4），后续比对无意义"; exit 1; }

# GWS-001：七槽 + 继续链 ok=1、jobDone 文案格式不变（「XX完成：<路径>」）、路径结构双段式（前缀=root + vNNN_<cmd> 目录）
python3 - "$W/ws.txt" "$WSROOT" <<'PYEOF'
import re, sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
root = re.escape(sys.argv[2])
expect = [  # (槽名, 文案前缀, 版本目录, 产物名)
    ("extractAudio",  "音轨提取完成",     "v001_extractAudio",  r"tiny_audio\.m4a"),
    ("extractVideo",  "无声视频提取完成", "v002_extractVideo",  r"tiny_mute\.mp4"),
    ("denoise",       "降噪完成",         "v001_denoise",       r"st_denoised\.wav"),
    ("aiDenoise",     "AI 降噪完成",      "v002_aiDenoise",     r"st_ai\.wav"),
    ("pitch",         "变调完成（2 半音）", "v003_pitch",       r"st_pitch\.wav"),
    ("sep-vocals",    "人声分离完成",     "v004_separate",      r"st_vocals\.wav"),
    ("sep-inst",      "伴奏提取完成",     "v005_separate",      r"st_instrumental\.wav"),
    ("chain-denoise", "降噪完成",         "v006_denoise",       r"st_denoised_denoised\.wav"),
]
pats = {name: re.compile(rf"^TASK {name} -> ok=1 msg={msg}：{root}/[^/]+/{vd}/{fn}$") for name, msg, vd, fn in expect}
seen = {name for name, *_ in expect for ln in lines if ln.startswith(f"TASK {name} -> ") and pats[name].match(ln)}
ok = True
for name, *_ in expect:
    hit = name in seen
    print(f"  {'PASS' if hit else 'FAIL'} GWS-001 {name}（ok=1 + 文案格式不变 + <root>/<工作区>/vNNN_<cmd>/<产物>）")
    ok = ok and hit
chain_ok = any(re.match(rf"^CHAIN product={root}/st\.[0-9a-f]{{8}}/v001_denoise/st_denoised\.wav$", ln) for ln in lines)
print(f"  {'PASS' if chain_ok else 'FAIL'} GWS-001 CHAIN 继续链输入回拼（result.dir + v001 记录相对路径）")
sys.exit(0 if ok and chain_ok else 1)
PYEOF
[ $? = 0 ] || FAIL=1

# GWS-001b：版本目录与产物在盘
for f in "$WS_MP4/v001_extractAudio/tiny_audio.m4a" "$WS_MP4/v002_extractVideo/tiny_mute.mp4" \
         "$WS_ST/v001_denoise/st_denoised.wav" "$WS_ST/v002_aiDenoise/st_ai.wav" \
         "$WS_ST/v003_pitch/st_pitch.wav" "$WS_ST/v004_separate/st_vocals.wav" \
         "$WS_ST/v005_separate/st_instrumental.wav" "$WS_ST/v006_denoise/st_denoised_denoised.wav"; do
    [ -f "$f" ]; ck $? "GWS-001b 版本目录与产物在盘（${f#"$WSROOT"/}）"
done

# GWS-002：workspace.json 结构（head/versions 数/动词序列/parentVersion 链，多动词串行跑）
python3 - "$WS_ST" "$WS_MP4" <<'PYEOF'
import json, sys
d = json.load(open(sys.argv[1] + "/workspace.json"))
t = json.load(open(sys.argv[2] + "/workspace.json"))
ok = True
def chk(cond, name):
    global ok
    print(f"  {'PASS' if cond else 'FAIL'} {name}")
    ok = ok and cond
chk(d["head"] == 6 and len(d["versions"]) == 6, "GWS-002 st 工作区 head=6、versions=6（多动词串行追加）")
chk([v["cmd"] for v in d["versions"]] ==
    ["denoise", "aiDenoise", "pitch", "separate", "separate", "denoise"], "GWS-002 版本动词序列正确")
chk(all(v["parentVersion"] is None for v in d["versions"][:5])
    and d["versions"][5]["parentVersion"] == 1, "GWS-002 parentVersion 链（继续链 v006←v001，前 5 版无父）")
chk(t["head"] == 2 and len(t["versions"]) == 2, "GWS-002 tiny 工作区 head=2、versions=2（extract 族入工作区）")
sys.exit(0 if ok else 1)
PYEOF
[ $? = 0 ] || FAIL=1

# GWS-004：wsInfo 槽回传与 workspace.json 深度一致（python 比对）+ 全部产物（含「继续」回拼路径）在盘
PAYLOAD=$(sed -n 's/^WSINFO wsInfo2 -> ok=1 //p' "$W/ws.txt")
python3 - "$PAYLOAD" "$WS_ST" <<'PYEOF'
import json, os, sys
resp = json.loads(sys.argv[1])
idx = json.load(open(sys.argv[2] + "/workspace.json"))
assert resp["dir"] == sys.argv[2], resp["dir"]
assert resp["head"] == idx["head"] and resp["versions"] == idx["versions"] and resp["source"] == idx["source"]
assert all(os.path.exists(os.path.join(resp["dir"], v["output"]["path"])) for v in resp["versions"])
PYEOF
ck $? "GWS-004 wsInfo 回传 result 与 workspace.json 深度一致 + 全部版本产物（含继续回拼路径）在盘"

# GWS-007：root 注入后 probe 槽仍正常（probe 请求未被插 workspace 键，否则 E_PARAMS）
grep -q '^TASK probe -> ok=1 msg=容器：' "$W/ws.txt"
ck $? "GWS-007 probe 槽在 root 注入后仍正常（探测文案照常）"

echo "== cancel 相（spec §6-5 取消 GUI 面收口）=="
IDX_ST_BEFORE=$(cat "$WS_ST/workspace.json" 2>/dev/null)
IDX_MP4_BEFORE=$(cat "$WS_MP4/workspace.json" 2>/dev/null)
DIRS_ST_BEFORE=$(ls -d "$WS_ST"/v* 2>/dev/null | sort | tr '\n' ' ')
DIRS_MP4_BEFORE=$(ls -d "$WS_MP4"/v* 2>/dev/null | sort | tr '\n' ' ')
QT_QPA_PLATFORM=offscreen "$BIN/probe" "$MEDIA" "$WSROOT" cancel >"$W/cancel.txt" 2>"$W/cancel.err"
ck $? "cancel 相运行（退出码）"
[ -s "$W/cancel.err" ] && { echo "  FAIL cancel 相 stderr 非空"; FAIL=1; }
grep -q '^TASK cancel-denoise -> ok=0 msg=已取消$' "$W/cancel.txt"
ck $? "GWS-006 cancel-denoise「已取消」文案直通（mainwidget 特判字符串命中）"
grep -q '^TASK cancel-extractAudio -> ok=0 msg=已取消$' "$W/cancel.txt"
ck $? "GWS-006 cancel-extractAudio「已取消」文案直通"
[ "$(cat "$WS_ST/workspace.json")" = "$IDX_ST_BEFORE" ]; ck $? "GWS-006 st 工作区索引逐字节不变"
[ "$(cat "$WS_MP4/workspace.json")" = "$IDX_MP4_BEFORE" ]; ck $? "GWS-006 tiny 工作区索引逐字节不变"
[ "$(ls -d "$WS_ST"/v* | sort | tr '\n' ' ')" = "$DIRS_ST_BEFORE" ]; ck $? "GWS-006 st 无版本目录残留（恰 v001..v006）"
[ "$(ls -d "$WS_MP4"/v* | sort | tr '\n' ' ')" = "$DIRS_MP4_BEFORE" ]; ck $? "GWS-006 tiny 无版本目录残留（恰 v001..v002）"

echo "== direct 相（默认不 setWorkspaceRoot = direct 形状，与 fr24 等价双跑互证）=="
QT_QPA_PLATFORM=offscreen "$BIN/probe" "$MEDIAD" - direct >"$W/direct.txt" 2>"$W/direct.err"
ck $? "direct 相运行（退出码）"
[ -s "$W/direct.err" ] && { echo "  FAIL direct 相 stderr 非空"; FAIL=1; }
for f in tiny_audio.m4a tiny_mute.mp4 st_denoised.wav; do
    [ -f "$MEDIAD/$f" ]; ck $? "GWS-005 direct 产物落源目录（$f，fr24 GUI-PRD 同形状）"
done
"$VH" --denoise-wav "$MEDIAD/st.wav" "$W/ref_denoised.wav" >/dev/null 2>&1
cmp -s "$MEDIAD/st_denoised.wav" "$W/ref_denoised.wav"
ck $? "GWS-005b GUI 槽 direct 产物与 CLI 直跑逐字节一致（跨面互证，未插键）"
grep -q '^TASK probe -> ok=1 msg=容器：' "$W/direct.txt"
ck $? "GWS-007b direct 相 probe 正常"

echo "== adv 相（对抗增补，round-3：换根/中文路径/wsInfo 失败载荷/置空回 direct）=="
MEDIA_ADV="$W/media-adv"; WSROOT_ADV="$W/wsroot-adv"
mkdir -p "$MEDIA_ADV"
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=5" -ac 2 -c:a pcm_s16le "$MEDIA_ADV/st.wav"
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=330:sample_rate=44100:duration=5" -ac 2 -c:a pcm_s16le "$MEDIA_ADV/中文素材.wav"
[ -f "$MEDIA_ADV/st.wav" ] && [ -f "$MEDIA_ADV/中文素材.wav" ] || { echo "FAIL: adv 相素材生成失败"; exit 1; }
QT_QPA_PLATFORM=offscreen "$BIN/probe" "$MEDIA_ADV" "$WSROOT_ADV" adv >"$W/adv.txt" 2>"$W/adv.err"
ck $? "adv 相运行（退出码）"
[ -s "$W/adv.err" ] && { echo "  FAIL adv 相 stderr 非空"; FAIL=1; }
python3 - "$W/adv.txt" "$WSROOT_ADV" "$MEDIA_ADV" <<'PYEOF'
import glob, json, os, re, sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
root1, media = re.escape(sys.argv[2]), sys.argv[3]
root2, rootcn = root1 + "_2", root1 + "_中文根"
def line(name):
    pref = f"TASK {name} -> "
    return next((ln[len(pref):] for ln in lines if ln.startswith(pref)), "")
ok = True
def chk(cond, name):
    global ok
    print(f"  {'PASS' if cond else 'FAIL'} {name}")
    ok = ok and cond
# GWS-008 换根只影响后续任务：同输入在 root1/root2 各落独立工作区、版本号都从 v001 起
m1 = re.match(rf"^ok=1 msg=降噪完成：{root1}/[^/]+/v001_denoise/st_denoised\.wav$", line("adv-denoise-r1"))
m2 = re.match(rf"^ok=1 msg=降噪完成：{root2}/[^/]+/v001_denoise/st_denoised\.wav$", line("adv-denoise-r2"))
def root_head(r):
    idxs = glob.glob(os.path.join(r, "*", "workspace.json"))
    return max((json.load(open(p))["head"] for p in idxs), default=0)
chk(bool(m1 and m2) and root_head(sys.argv[2]) == 1 and root_head(sys.argv[2] + "_2") == 1,
    "GWS-008 换根只影响后续任务（root1/root2 各自 v001 且各恰 1 版本，同内容两根互不可见）")
# GWS-009 中文路径：中文根 + 中文文件名全程直通（stem 进工作区名与产物名）
chk(bool(re.match(rf"^ok=1 msg=降噪完成：{rootcn}/中文素材\.[0-9a-f]{{8}}/v001_denoise/中文素材_denoised\.wav$",
                 line("adv-denoise-cn"))), "GWS-009 中文根+中文素材直通（工作区名/产物名带中文 stem）")
# GWS-010 wsInfo 失败载荷：输入不在盘 → ok=0 + 错误说明（onWsInfoDone 警告分支的数据面）
miss = next((ln for ln in lines if ln.startswith("WSINFO adv-wsinfo-miss -> ")), "")
chk(miss.startswith("WSINFO adv-wsinfo-miss -> ok=0 ") and len(miss) > len("WSINFO adv-wsinfo-miss -> ok=0 "),
    "GWS-010 doWsInfo 对不在盘输入回失败载荷（ok=0 + 非空错误说明）")
# GWS-011 置空回 direct：注入过 root 后置空串 → 不插键 → 产物落源目录（若误插键则会进 rootcn 出 v002）
chk(bool(re.match(r"^ok=1 msg=降噪完成：.*/st_denoised\.wav$", line("adv-denoise-emptyroot"))
          and os.path.exists(os.path.join(media, "st_denoised.wav"))
          and root_head(sys.argv[2] + "_中文根") == 1),
    "GWS-011 注入后置空串回 direct（产物落源目录且 rootcn 仍 head=1，与 GWS-005 默认空串对称）")
sys.exit(0 if ok else 1)
PYEOF
[ $? = 0 ] || FAIL=1

echo "== GWS-003 原件只读（各相全程结束后源文件 sha256 不变）=="
[ "$(sha256sum "$MEDIA/st.wav" | cut -d' ' -f1)" = "$SHA_ST" ]; ck $? "GWS-003 st.wav 前后 sha256 不变"
[ "$(sha256sum "$MEDIA/tiny.mp4" | cut -d' ' -f1)" = "$SHA_MP4" ]; ck $? "GWS-003 tiny.mp4 前后 sha256 不变"

if [ "$FAIL" = 1 ]; then
    trap - EXIT # FAIL 保留现场（转录/索引）供排查，其余路径仍自动清理
    echo "== gui-ws 有 FAIL（转录与索引留 $W）=="
    exit 1
fi
echo "== gui-ws 全绿 =="
