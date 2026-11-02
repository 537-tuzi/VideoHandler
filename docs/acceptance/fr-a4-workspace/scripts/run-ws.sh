#!/usr/bin/env bash
# run-ws.sh — FR-A4 工作区版本化片 A 编排（WS 族，spec §6）：创建/追加/来源链全字段/原件只读/
# 失败清理（只读根/坏模型/坏缓存/坏索引[不可解析+类型损坏]）/孤儿跳过/查询面/互斥负例/截断素材/内存有界。
# 判据与用例表以 docs/acceptance/fr-a4-workspace/matrix.csv 为准；本脚本是其自动化执行体。
# 复用 fr24 的 json_check.py 断言器与素材配方（交叉引用不复制）；依赖 python3 stdlib。
# direct 模式零漂移门禁（run-json 36 / thinshell-eqv / ci-smoke）不在本脚本内，随轮记录于 round-N.md。
# WS-030b 坏模型（任务期失败→补删版本目录）需 python3 onnx（缺则 SKIP，PARITY_REQUIRED=1 时升级 FAIL）。
# 用法：run-ws.sh [构建目录=build-linux]
set -uo pipefail
cd "$(dirname "$0")/../../../.." # scripts→fr-a4-workspace→acceptance→docs→仓库根
[ -f CMakeLists.txt ] || { echo "FAIL: 未落到仓库根（$(pwd)）"; exit 1; }

BUILD="${1:-build-linux}"
VH="$BUILD/video_handler"
GTCRN=third_party/models/denoiser/gtcrn_simple.onnx
for f in "$VH" "$GTCRN"; do
    [ -e "$f" ] || { echo "FAIL: 缺 $f（先构建/跑 fetch-deps.sh）"; exit 1; }
done
S=docs/acceptance/fr24-command-layer/scripts
W=$(mktemp -d)
trap 'chmod -R u+w "$W" >/dev/null 2>&1; rm -rf "$W"' EXIT
FAIL=0 SKIPANY=
WSROOT="$W/wsroot"

skip() { echo "  SKIP: $*"; SKIPANY=1; }
ck() { if [ "$1" = 0 ]; then echo "  PASS $2"; else echo "  FAIL $2"; FAIL=1; fi; }

# 素材（run-json 同配方）：5s 44.1k 立体声 + 48k + 双流 mp4
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=5" -ac 2 -c:a pcm_s16le "$W/st.wav"
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=5" -ac 2 -c:a pcm_s16le "$W/st48k.wav"
ffmpeg -y -loglevel error -f lavfi -i "testsrc2=size=320x240:rate=25:duration=3" -f lavfi -i "sine=frequency=440:duration=3" -c:v libx264 -pix_fmt yuv420p -c:a aac -shortest "$W/tiny.mp4"

# jr <id> <期望退出码> <请求JSON> [断言...]（json_check.py 断言器，run-json 同款）
jr() {
    local id=$1 exprc=$2 req=$3; shift 3
    printf '%s' "$req" | "$VH" --json >"$W/$id.resp" 2>/dev/null
    local rc=$?
    local ok=1
    [ "$rc" = "$exprc" ] || { echo "  FAIL $id: 退出码 $rc ≠ 期望 $exprc"; ok=0; }
    python3 "$S/json_check.py" "$W/$id.resp" "$@" || ok=0
    if [ "$ok" = 1 ]; then echo "  PASS $id"; else FAIL=1; fi
}

# wsjson <id> <wsDir> <python断言表达式>：对 workspace.json 作结构断言（表达式可用变量 d=索引对象）
wsjson() {
    local id=$1 wsdir=$2 expr=$3
    python3 - "$wsdir/workspace.json" "$expr" <<'PYEOF'
import json, sys
d = json.load(open(sys.argv[1]))
assert eval(sys.argv[2]), sys.argv[2]
PYEOF
    ck $? "$id（workspace.json 断言）"
}

echo "== WS-CREATE（首任务自动建工作区：目录结构 + 源登记 + 缓存回填）=="
SHA_ST=$(sha256sum "$W/st.wav" | cut -d' ' -f1)
jr WS-001 0 "{\"v\":1,\"cmd\":\"denoise\",\"input\":\"$W/st.wav\",\"workspace\":\"$WSROOT\"}" \
    ok=true cmd=denoise exists:result.output num:result.frames=220500 bool:result.truncated=false \
    str:result.workspace.root=$WSROOT num:result.workspace.version=1 \
    contains:result.output=/v001_denoise/st_denoised.wav
WSDIR=$WSROOT/st.${SHA_ST:0:8}
[ -d "$WSDIR/v001_denoise" ]; ck $? "WS-002 目录结构 <stem>.<hash8>/v001_denoise/"
[ -f "$WSDIR/v001_denoise/st_denoised.wav" ]; ck $? "WS-003 版本产物在版本目录内（文件名沿用注册表后缀自描述）"
wsjson WS-004 "$WSDIR" "d['source']['sha256']=='$SHA_ST' and d['source']['sizeBytes']==$(stat -c%s "$W/st.wav") and d['source']['spec']['sampleRate']==44100 and d['source']['spec']['channels']==2 and d['head']==1 and len(d['versions'])==1"
python3 -c "import json; c=json.load(open('$WSROOT/index.json')); assert any(e['path']=='$W/st.wav' and e['ws']=='$WSDIR' for e in c), c"; ck $? "WS-005 根级缓存回填（path+size+mtimeMs→ws）"

echo "== WS-CREATE（内容哈希身份：同内容换路径=同工作区；同名不同内容=不同工作区）=="
cp "$W/st.wav" "$W/alias.wav"
jr WS-006 0 "{\"v\":1,\"cmd\":\"transcode\",\"input\":\"$W/alias.wav\",\"workspace\":\"$WSROOT\"}" \
    ok=true str:result.workspace.dir=$WSDIR num:result.workspace.version=2
wsjson WS-007 "$WSDIR" "d['head']==2 and len(d['versions'])==2 and d['source']['path']=='$W/alias.wav'"
SHA_48=$(sha256sum "$W/st48k.wav" | cut -d' ' -f1)
cp "$W/st48k.wav" "$W/st_nameclash.wav"
jr WS-008 0 "{\"v\":1,\"cmd\":\"transcode\",\"input\":\"$W/st_nameclash.wav\",\"workspace\":\"$WSROOT\"}" \
    ok=true str:result.workspace.dir=$WSROOT/st_nameclash.${SHA_48:0:8} num:result.workspace.version=1

echo "== WS-CHAIN（追加与链：v001 不可变、head 推进；同工作区 parentVersion；跨工作区哈希衔接）=="
V1_BEFORE=$(python3 -c "import json; print(json.dumps(json.load(open('$WSDIR/workspace.json'))['versions'][0], sort_keys=True))")
jr WS-009 0 "{\"v\":1,\"cmd\":\"pitch\",\"input\":\"$W/st.wav\",\"workspace\":\"$WSROOT\",\"semi\":2.0}" \
    ok=true num:result.workspace.version=3
V1_AFTER=$(python3 -c "import json; print(json.dumps(json.load(open('$WSDIR/workspace.json'))['versions'][0], sort_keys=True))")
[ "$V1_BEFORE" = "$V1_AFTER" ]; ck $? "WS-010 v001 记录登记后不可变（append-only）"
wsjson WS-011 "$WSDIR" "d['head']==3 and len(d['versions'])==3"
# 以 v001 产物为输入（缓存命中路径）→ 同工作区 + parentVersion=1
P1=$WSDIR/v001_denoise/st_denoised.wav
jr WS-012 0 "{\"v\":1,\"cmd\":\"transcode\",\"input\":\"$P1\",\"workspace\":\"$WSROOT\"}" \
    ok=true str:result.workspace.dir=$WSDIR num:result.workspace.version=4
wsjson WS-013 "$WSDIR" "d['versions'][3]['parentVersion']==1 and d['versions'][3]['input']['path']=='$P1' and d['versions'][3]['input']['sha256']=='$(sha256sum "$P1" | cut -d' ' -f1)'"
# 跨工作区链：删缓存（强制哈希路径）→ 产物作源 → 新工作区，其源哈希 = 产物哈希（D3 哈希衔接）
P2=$WSDIR/v002_transcode/alias_transcoded.wav
rm -f "$WSROOT/index.json"
SHA_P2=$(sha256sum "$P2" | cut -d' ' -f1)
jr WS-014 0 "{\"v\":1,\"cmd\":\"transcode\",\"input\":\"$P2\",\"workspace\":\"$WSROOT\"}" \
    ok=true str:result.workspace.dir=$WSROOT/alias_transcoded.${SHA_P2:0:8}
wsjson WS-015 "$WSROOT/alias_transcoded.${SHA_P2:0:8}" "d['source']['sha256']=='$SHA_P2' and d['versions'][0]['parentVersion'] is None"

echo "== WS-PROV（来源链完整性：filled 参数 / 模型摘要 / 输出真值 / wsInfo 一致）=="
# v001 = denoise 未传 humBase → 记录 params 含缺省 humBase=50、wind=true（补缺省后的 filled 副本）
wsjson WS-016 "$WSDIR" "d['versions'][0]['params']=={'humBase':'50','wind':True} and d['versions'][0]['model'] is None and d['versions'][0]['cmd']=='denoise' and d['versions'][1]['params']=={}"
wsjson WS-017 "$WSDIR" "d['versions'][0]['output']['frames']==220500 and d['versions'][0]['output']['outputSpec']['sampleRate']==44100 and d['versions'][0]['output']['outputSpec']['channels']==2 and d['versions'][0]['output']['truncated']==False and 'time' in d['versions'][0]"
# 有模型动词：aiDenoise 建独立根，版本记录含模型摘要（路径+size+mtimeMs）、params 为空（model 另有槽位）
SHA_AI=$(sha256sum "$W/st.wav" | cut -d' ' -f1)
jr WS-018 0 "{\"v\":1,\"cmd\":\"aiDenoise\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/wsm\",\"model\":\"$GTCRN\"}" \
    ok=true cmd=aiDenoise num:result.workspace.version=1
[ "$(sha256sum "$W/st.wav" | cut -d' ' -f1)" = "$SHA_AI" ]; ck $? "WS-018b aiDenoise 前后源 sha256 不变（有模型动词也只读，补 WS-RO 面）"
AIWS=$(ls -d "$W/wsm"/*/ | head -1)
wsjson WS-019 "$AIWS" "d['versions'][0]['model']['path']=='$(realpath "$GTCRN")' and d['versions'][0]['model']['sizeBytes']==$(stat -c%s "$GTCRN") and 'mtimeMs' in d['versions'][0]['model'] and d['versions'][0]['params']=={}"
# extract 族（无引擎参数）：产物命名走底座规则、记录 output 仅 path
jr WS-020 0 "{\"v\":1,\"cmd\":\"extractAudio\",\"input\":\"$W/tiny.mp4\",\"workspace\":\"$W/wsx\"}" \
    ok=true cmd=extractAudio num:result.workspace.version=1 contains:result.output=/v001_extractAudio/tiny_audio.m4a
XWS=$(ls -d "$W/wsx"/*/ | head -1)
wsjson WS-021 "$XWS" "d['versions'][0]['output']=={'path':'v001_extractAudio/tiny_audio.m4a'} and d['versions'][0]['params']=={}"
# wsInfo 返回 = workspace.json 内容（查询面与权威索引一致）
jr WS-022 0 "{\"v\":1,\"cmd\":\"wsInfo\",\"root\":\"$WSROOT\",\"input\":\"$W/st.wav\"}" \
    ok=true cmd=wsInfo bool:result.exists=true str:result.dir=$WSDIR num:result.head=4 \
    num:result.versions.3.parentVersion=1 str:result.source.sha256=$SHA_ST
python3 - "$BUILD/video_handler" "$WSROOT" "$WSDIR" "$W/st.wav" <<'PYEOF'
import json, subprocess, sys
resp = json.loads(subprocess.run([sys.argv[1], "--json"],
    input=json.dumps({"v": 1, "cmd": "wsInfo", "root": sys.argv[2], "input": sys.argv[4]}).encode(),
    capture_output=True).stdout)
idx = json.load(open(sys.argv[3] + "/workspace.json"))
assert resp["result"]["versions"] == idx["versions"] and resp["result"]["source"] == idx["source"], resp
PYEOF
ck $? "WS-023 wsInfo 清单与 workspace.json 逐字段一致"

echo "== WS-RO（原件只读不变式：源字节级不变 + 防线负例）=="
cp "$W/st.wav" "$W/st.ro"
RO_BEFORE=$(sha256sum "$W/st.ro" | cut -d' ' -f1)
printf '%s' "{\"v\":1,\"cmd\":\"denoise\",\"input\":\"$W/st.ro\",\"workspace\":\"$W/wsro\"}" | "$VH" --json >/dev/null 2>&1
[ "$(sha256sum "$W/st.ro" | cut -d' ' -f1)" = "$RO_BEFORE" ]; ck $? "WS-024 工作区任务前后源 sha256 不变（denoise）"
cp "$W/tiny.mp4" "$W/tiny.ro"
RO_MP4=$(sha256sum "$W/tiny.ro" | cut -d' ' -f1)
printf '%s' "{\"v\":1,\"cmd\":\"extractVideo\",\"input\":\"$W/tiny.ro\",\"workspace\":\"$W/wsro\"}" | "$VH" --json >/dev/null 2>&1
[ "$(sha256sum "$W/tiny.ro" | cut -d' ' -f1)" = "$RO_MP4" ]; ck $? "WS-025 工作区任务前后源 sha256 不变（extractVideo）"
jr WS-026 1 "{\"v\":1,\"cmd\":\"transcode\",\"input\":\"$W/st.wav\",\"output\":\"$W/st.wav\"}" \
    ok=false cmd=transcode code=E_PARAMS msg-contains=原件只读 absent:result
jr WS-027 1 "{\"v\":1,\"cmd\":\"denoise\",\"input\":\"$W/st.wav\",\"workspace\":\"$WSROOT\",\"output\":\"$W/x.wav\"}" \
    ok=false cmd=denoise code=E_PARAMS msg-contains=不能同给 absent:result
jr WS-028 1 "{\"v\":1,\"cmd\":\"probe\",\"input\":\"$W/tiny.mp4\",\"workspace\":\"$WSROOT\"}" \
    ok=false cmd=probe code=E_PARAMS 'msg-contains=probe 不接受' absent:result
[ ! -e "$W/x.wav" ]; ck $? "WS-027 后无产物残留"

echo "== WS-FAIL（失败清理：无版本目录、索引不动、残迹不阻后续）=="
mkdir -p "$W/ro" && chmod 555 "$W/ro"
jr WS-029 1 "{\"v\":1,\"cmd\":\"denoise\",\"input\":\"$W/st48k.wav\",\"workspace\":\"$W/ro/ws\"}" \
    ok=false cmd=denoise code=E_FILE msg-contains=工作区根建不了 absent:result
chmod 755 "$W/ro"
[ ! -e "$W/ro/ws" ]; ck $? "WS-029f 根未创建（只读根零残迹）"
IDX_BEFORE=$(cat "$WSDIR/workspace.json")
jr WS-030 1 "{\"v\":1,\"cmd\":\"aiDenoise\",\"input\":\"$W/st.wav\",\"workspace\":\"$WSROOT\",\"model\":\"/nonexistent.onnx\"}" \
    ok=false cmd=aiDenoise code=E_MODEL msg-contains=找不到模型 absent:result
[ "$(cat "$WSDIR/workspace.json")" = "$IDX_BEFORE" ]; ck $? "WS-031 失败后 workspace.json 逐字节不变（索引只在成功后写）"
NODIR=$(ls -d "$WSDIR"/v00*_aiDenoise 2>/dev/null | wc -l); [ "$NODIR" = 0 ]; ck $? "WS-032 失败无版本目录残留"
# 任务期失败（plan 后引擎推理错）：底座已删产物，工作区层补删版本目录——两段提交清理的真正落点
if python3 -c 'import onnx' 2>/dev/null; then
    python3 "$S/craft-mismatch-model.py" "$W/mismatch.onnx" >/dev/null 2>&1
    jr WS-030b 1 "{\"v\":1,\"cmd\":\"aiDenoise\",\"input\":\"$W/st.wav\",\"workspace\":\"$WSROOT\",\"model\":\"$W/mismatch.onnx\"}" \
        ok=false cmd=aiDenoise code=E_ENGINE absent:result
    [ "$(cat "$WSDIR/workspace.json")" = "$IDX_BEFORE" ]; ck $? "WS-030b-i 索引仍逐字节不变"
    NODIR=$(ls -d "$WSDIR"/v00*_aiDenoise 2>/dev/null | wc -l); [ "$NODIR" = 0 ]; ck $? "WS-030b-f 版本目录已补删（登记器 owns 生命周期）"
else
    skip "WS-030b 缺 onnx（任务期失败补删路径，配方 craft-mismatch-model.py）"
fi
jr WS-033 0 "{\"v\":1,\"cmd\":\"denoise\",\"input\":\"$W/st.wav\",\"workspace\":\"$WSROOT\"}" \
    ok=true num:result.workspace.version=5
wsjson WS-034 "$WSDIR" "d['head']==5 and len(d['versions'])==5"
# 根级缓存损坏按空处理（可重建，不拦任务）
echo 'garbage{{' > "$WSROOT/index.json"
jr WS-035 0 "{\"v\":1,\"cmd\":\"denoise\",\"input\":\"$W/st.wav\",\"workspace\":\"$WSROOT\"}" \
    ok=true num:result.workspace.version=6
# 权威索引损坏 → E_FILE（真相源坏不可静默重建）
echo 'garbage{{' > "$XWS/workspace.json"
jr WS-036 1 "{\"v\":1,\"cmd\":\"extractAudio\",\"input\":\"$W/tiny.mp4\",\"workspace\":\"$W/wsx\"}" \
    ok=false cmd=extractAudio code=E_FILE msg-contains=工作区索引读不了 absent:result
# 类型损坏（合法 JSON、含键、错类型——逃过 isObject+contains 两检的形态）→ 同 E_FILE 且索引原样
# 不被改写（审查 P1：修复前 toArray() 对非数组返回空数组，任务 ok=true 且版本史被静默清空）
jr WS-036b 0 "{\"v\":1,\"cmd\":\"extractAudio\",\"input\":\"$W/tiny.mp4\",\"workspace\":\"$W/wstype\"}" \
    ok=true cmd=extractAudio num:result.workspace.version=1
TWS=$(ls -d "$W/wstype"/*/ | head -1)
python3 - "$TWS/workspace.json" <<'PYEOF'
import json, sys
d = json.load(open(sys.argv[1]))
d["versions"] = "corrupted"
json.dump(d, open(sys.argv[1], "w"), ensure_ascii=False, indent=2)
PYEOF
jr WS-036c 1 "{\"v\":1,\"cmd\":\"extractAudio\",\"input\":\"$W/tiny.mp4\",\"workspace\":\"$W/wstype\"}" \
    ok=false cmd=extractAudio code=E_FILE msg-contains=工作区索引读不了 absent:result
python3 - "$TWS" <<'PYEOF'
import json, os, sys
ws = sys.argv[1]
d = json.load(open(ws + "/workspace.json"))
dirs = sorted(x for x in os.listdir(ws) if x.startswith("v"))
sys.exit(0 if d["versions"] == "corrupted" and dirs == ["v001_extractAudio"] else 1)
PYEOF
ck $? "WS-036d 损坏索引原样未被改写、无新版本目录（真相源不被静默重建/清空）"

echo "== WS-ORPHAN（孤儿目录安全跳过：kill -9 残迹不阻后续、序号不复用）=="
mkdir -p "$WSDIR/v007_denoise"
jr WS-037 0 "{\"v\":1,\"cmd\":\"denoise\",\"input\":\"$W/st.wav\",\"workspace\":\"$WSROOT\"}" \
    ok=true num:result.workspace.version=8
wsjson WS-038 "$WSDIR" "d['head']==8 and len(d['versions'])==7 and all(v['version']!=7 for v in d['versions'])"

echo "== WS-QUERY（查询面：未登记空清单不落盘 + 参数负例）=="
mkdir -p "$W/wsq"
QCOUNT_BEFORE=$(ls -A "$W/wsq" | wc -l)
jr WS-039 0 "{\"v\":1,\"cmd\":\"wsInfo\",\"root\":\"$W/wsq\",\"input\":\"$W/st.wav\"}" \
    ok=true cmd=wsInfo bool:result.exists=false num:result.head=0 absent:result.versions.0
[ "$(ls -A "$W/wsq" | wc -l)" = "$QCOUNT_BEFORE" ]; ck $? "WS-040 查询不落盘（未登记不建目录）"
jr WS-041 1 "{\"v\":1,\"cmd\":\"wsInfo\",\"input\":\"$W/st.wav\"}" \
    ok=false cmd=wsInfo code=E_PARAMS 'msg-contains=root 缺失' absent:result
jr WS-042 1 "{\"v\":1,\"cmd\":\"wsInfo\",\"root\":123,\"input\":\"$W/st.wav\"}" \
    ok=false cmd=wsInfo code=E_PARAMS 'msg-contains=root 必须是字符串' absent:result
jr WS-043 1 "{\"v\":1,\"cmd\":\"wsInfo\",\"root\":\"$WSROOT\",\"input\":\"$W/st.wav\",\"bogus\":1}" \
    ok=false cmd=wsInfo code=E_PARAMS msg-contains=未知字段 absent:result
jr WS-044 1 "{\"v\":1,\"cmd\":\"denoise\",\"input\":\"$W/st.wav\",\"workspace\":123}" \
    ok=false cmd=denoise code=E_PARAMS 'msg-contains=workspace 必须是字符串' absent:result
jr WS-045 1 "{\"v\":1,\"cmd\":\"denoise\",\"input\":\"$W/st.wav\",\"workspace\":\"\"}" \
    ok=false cmd=denoise code=E_PARAMS 'msg-contains=workspace 不能为空' absent:result
jr WS-046 1 "{\"v\":1,\"cmd\":\"wsInfo\",\"root\":\"$WSROOT\",\"input\":\"$W/st.wav\",\"workspace\":\"$WSROOT\"}" \
    ok=false cmd=wsInfo code=E_PARAMS 'msg-contains=wsInfo 不接受' absent:result

echo "== WS-TRUNC（截断素材：ok:true + 可解前缀输出；中途 I/O 错分支 e2e 不可构造，见 round-1 映射表）=="
head -c 300000 "$W/st.wav" > "$W/trunc.wav"
jr WS-047 0 "{\"v\":1,\"cmd\":\"transcode\",\"input\":\"$W/trunc.wav\",\"workspace\":\"$W/wst\"}" \
    ok=true cmd=transcode bool:result.truncated=false numrange:result.frames=70000:80000
wsjson WS-048 "$(ls -d "$W/wst"/*/ | head -1)" "d['versions'][0]['output']['truncated']==False"

echo "== WS-MEM（内存有界：30min 素材首任务[含全量哈希] vs direct 峰值 RSS 同量级）=="
if command -v /usr/bin/time >/dev/null 2>&1 && [ "$(df -Pk "$W" | awk 'NR==2{print $4}')" -gt 800000 ]; then
    ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=16000:duration=1800" -ac 1 -c:a pcm_s16le "$W/long.wav"
    /usr/bin/time -v "$VH" --transcode-wav "$W/long.wav" "$W/long_direct.wav" >/dev/null 2>"$W/mem_direct.txt"
    printf '%s' "{\"v\":1,\"cmd\":\"transcode\",\"input\":\"$W/long.wav\",\"workspace\":\"$W/wsmem\"}" >"$W/mem_req.json"
    /usr/bin/time -v "$VH" --json <"$W/mem_req.json" >/dev/null 2>"$W/mem_ws.txt"
    python3 - "$W/mem_direct.txt" "$W/mem_ws.txt" <<'PYEOF'
import re, sys
def peak(p):
    m = re.search(r"Maximum resident set size \(kbytes\): (\d+)", open(p, encoding="utf-8", errors="ignore").read())
    return int(m.group(1))
d, w = peak(sys.argv[1]), peak(sys.argv[2])
print(f"  峰值 RSS：direct={d/1024:.1f}MiB  workspace={w/1024:.1f}MiB  Δ={abs(w-d)/1024:.1f}MiB")
sys.exit(0 if abs(w - d) < 32 * 1024 else 1)  # 哈希 64KiB 流式；素材 ~55MiB，整文件载入会 +55MiB > 32MiB 门槛
PYEOF
    ck $? "WS-049 峰值 RSS 同量级（|Δ|<32MiB，证伪整文件载入假设）"
else
    skip "WS-049 缺 /usr/bin/time 或临时区空间不足"
fi

if [ "$FAIL" = 1 ]; then
    echo "== run-ws FAIL（见上）=="
    exit 1
fi
if [ "$SKIPANY" = 1 ] && [ "${PARITY_REQUIRED:-0}" = 1 ]; then
    echo "== run-ws 有 SKIP 且 PARITY_REQUIRED=1（CI 强制点）=="
    exit 1
fi
echo "== run-ws 全绿${SKIPANY:+（有 SKIP，见上；本地降级跑）} =="
