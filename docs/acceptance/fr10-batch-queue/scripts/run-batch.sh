#!/usr/bin/env bash
# run-batch.sh — FR-10 批处理队列（BATCH 族，spec §6，进 ci-smoke）：
#   BATCH-OK    正例批（3 件 × 两步模板 / 2 件 × 单步模板 / 带模型批）：result 结构（total/okCount/
#               truncated/items[] 逐项 index/input/output/workspace/steps）+ 版本目录落位 + 件产物体检
#   BATCH-EQV   编排等价（核心护栏）：批跑 vs 逐件手动跑 pipeline——各件终产物与中间产物逐字节一致
#   BATCH-FAIL  失败不中断（需求明文）：3 件中件 2 坏（不存在/无音频流）→ okCount=2 且件错误原文
#               无「第 N 件」前缀（附裁 3）；坏模型全败批 → ok=true okCount=0（ok=编排层成功口径
#               OQ-1(a)）；多步件内步失败（维度错位模型）→ items[].error 带步骤前缀与 step/stepCmd
#   BATCH-NEG   schema 负例：inputs 形态/重复绝对路径/steps 形态/内置动词与嵌套入模板/链级键拒收/
#               参数越界/模型不存在 → 逐条消息断言 + 开工前拦截（零版本目录零产物、全部源 sha 不变）
#   BATCH-WS    工作区隔离与标记：3 件各工作区独立 + batch:{index,total} 逐件断言（OQ-3(a)）+
#               多步件 pipeline/batch 双键 + 单步件仅 batch 键 + 同内容不同路径同工作区续号 +
#               缓存删除续定位 + 非批任务不带 batch 键（键面零漂移；逐字节零漂移归 BATCH-ZERO 全量复跑）
#   BATCH-CANCEL/PROG 取消与进度（探针直调 CommandLayer::execute 的 Ctx 注入面）：cancel-pre/mid/
#               between 三相（message 逐字「已取消」+ completed/total（OQ-2(b)），被取消件不计完成度、
#               已提交件保留）+ progress/progress2 件层等分与步层复合映射（D6(a)）
#   BATCH-GUIE  GUI worker 壳（FFExtractor doBatch 槽直调，无控件）：ok-batch 载荷 JSON/
#               missing-pre 开工前 modelMissing(aiDenoise) 且 batchFinished 先发（附裁 1 顺序）/
#               emid-item 件内 E_MODEL 不触发 modelMissing（附裁 1：件循环内禁发，Y5 毒缝封堵）/
#               cancel「已取消」逐字（附裁 5）
#   BATCH-MEM   内存抽测：2 件 × 30min 免模型批峰值 RSS 与最重单动词同量级（件串行×步串行 → 峰值 =
#               全部件·步最大值的实证，NFR-P4/D1）；带模型批（有 onnx 时）同款
#   BATCH-ZERO  零漂移硬门禁不在本脚本（run-json/run-ws/run-models/run-pipe/gui-slot-eqv/
#               thinshell-eqv/fake-engine-e2e/ci-smoke 全量复跑），证据记 round-N.md
#   BATCH-GUI（widget 面：对话框组包/busy 全批覆盖/逐件渲染）round-2 承接（spec §6.9）；
#   BATCH-REG（注册表零改前瞻）分支证据制不进 ci-smoke（spec §6.10）
# 判据与用例表以 docs/acceptance/fr10-batch-queue/matrix.csv 为准；本脚本是其自动化执行体。
# 复用 fr24 的 json_check.py 断言器与素材配方、run-pipe 的探针方法论（交叉引用不复制）。
# 依赖：python3（stdlib）；维度错位模型需 python3-onnx、带模型批需 third_party 模型（缺则整族
# SKIP，PARITY_REQUIRED=1 时升级 FAIL——三处缺依赖降级点门禁语义齐整）。
# 工作区一律磁盘卷：默认 <build>/.batch-tmp（run-pipe 同型），外部可用 TMPDIR 覆盖——/tmp 是
# 配额 tmpfs，30min 素材×多个 f32 产物会撞 EDQUOT 假象（fr8 round-2 教训）。
# 用法：run-batch.sh [构建目录=build-linux]
set -uo pipefail
cd "$(dirname "$0")/../../../.." # scripts→fr10-batch-queue→acceptance→docs→仓库根
[ -f CMakeLists.txt ] || { echo "FAIL: 未落到仓库根（$(pwd)）"; exit 1; }

BUILD="${1:-build-linux}"
VH="$BUILD/video_handler"
GTCRN=third_party/models/denoiser/gtcrn_simple.onnx
UVR=third_party/models/separator/UVR-MDX-NET-Voc_FT.onnx
S=docs/acceptance/fr10-batch-queue/scripts
JC=docs/acceptance/fr24-command-layer/scripts/json_check.py
CRAFT=docs/acceptance/fr24-command-layer/scripts/craft-mismatch-model.py
for f in "$VH" "$BUILD/libengine.a"; do
    [ -e "$f" ] || { echo "FAIL: 缺 $f（先构建）"; exit 1; }
done
HAS_MODELS=1
for m in "$GTCRN" "$UVR"; do [ -f "$m" ] || HAS_MODELS=0; done

# 大素材（30min wav ≈300MB×2 + f32 产物 ≈635MB×n）必须落磁盘卷（见头注）
BTMP="${TMPDIR:-$PWD/$BUILD/.batch-tmp}"
mkdir -p "$BTMP"
W=$(mktemp -d "$BTMP/batch.XXXXXX")
trap 'chmod -R u+w "$W" >/dev/null 2>&1; rm -rf "$W"' EXIT
FAIL=0 SKIPANY=

skip() { echo "  SKIP: $*"; SKIPANY=1; }
ck() { if [ "$1" = 0 ]; then echo "  PASS $2"; else echo "  FAIL $2"; FAIL=1; fi; }

# 素材（run-json/run-pipe 同配方 + 单声道变体）：5s 正弦三件（内容互异 → 三个工作区）
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=5" -ac 2 -c:a pcm_s16le "$W/f1.wav"
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=5" -ac 2 -c:a pcm_s16le "$W/f2.wav"
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=660:sample_rate=44100:duration=5" -ac 1 -c:a pcm_s16le "$W/f3.wav"
ffmpeg -y -loglevel error -f lavfi -i "testsrc=duration=2:size=320x240:rate=10" -c:v libx264 -pix_fmt yuv420p "$W/vidonly.mp4"
# 源基线（测试轮 round-2）：全部请求发生之前取——BATCH-NEG-030 在段尾复算对照，任何请求改写源
# 即转红（实现者版本基线取在段尾 = 同刻双算恒真，红绿论证实锤后修正）
SRC_SHA_BASE=$(sha256sum "$W/f1.wav" "$W/f2.wav" "$W/f3.wav" | md5sum | cut -d' ' -f1)

# jr <id> <期望退出码> <请求JSON> [断言...]（json_check.py 断言器，run-pipe 同款）
jr() {
    local id=$1 exprc=$2 req=$3; shift 3
    printf '%s' "$req" | "$VH" --json >"$W/$id.resp" 2>/dev/null
    local rc=$?
    local ok=1
    [ "$rc" = "$exprc" ] || { echo "  FAIL $id: 退出码 $rc ≠ 期望 $exprc"; ok=0; }
    python3 "$JC" "$W/$id.resp" "$@" || ok=0
    if [ "$ok" = 1 ]; then echo "  PASS $id"; else FAIL=1; fi
}
# ex <id> <命令...>：附加 shell 级断言（命令退出码 0 即 PASS）
ex() {
    local id=$1; shift
    if "$@"; then echo "  PASS $id"; else echo "  FAIL $id: 附加断言失败"; FAIL=1; fi
}
# fp <id> <wav> <期望csv>：ffprobe 交叉验证产物规格（run-json 同款）
fp() {
    local got
    got=$(ffprobe -v error -select_streams a:0 -show_entries stream=codec_name,sample_rate,channels -of csv=p=0 "$2" 2>/dev/null)
    if [ "$got" = "$3" ]; then echo "  PASS $1（ffprobe $got）"; else echo "  FAIL $1: ffprobe $got ≠ $3"; FAIL=1; fi
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
# jget <resp文件> <python表达式>：从响应 JSON 取值（expr 用变量 r=响应对象）
jget() {
    python3 -c "import json; r=json.load(open('$1')); print($2)"
}

echo "== BATCH-OK（正例批：3 件两步模板 / 单步模板 / 带模型批；result 结构 + 落位 + 体检）=="
jr BATCH-OK-001 0 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$W/okws\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\",\"$W/f3.wav\"],\"steps\":[{\"cmd\":\"denoise\",\"humBase\":\"60\"},{\"cmd\":\"pitch\",\"semi\":2.0}]}" \
    ok=true v=1 cmd=batch num:result.total=3 num:result.okCount=3 bool:result.truncated=false \
    keys:result=items,okCount,total,truncated \
    num:result.items.0.index=1 str:result.items.0.input="$W/f1.wav" bool:result.items.0.ok=true \
    exists:result.items.0.output num:result.items.0.workspace.version=2 \
    str:result.items.0.steps.0.cmd=denoise str:result.items.0.steps.1.cmd=pitch \
    num:result.items.1.index=2 str:result.items.1.input="$W/f2.wav" \
    num:result.items.2.index=3 str:result.items.2.input="$W/f3.wav" \
    bool:result.items.2.ok=true
OK1_D1=$(jget "$W/BATCH-OK-001.resp" "r['result']['items'][0]['workspace']['dir']")
OK1_D2=$(jget "$W/BATCH-OK-001.resp" "r['result']['items'][1]['workspace']['dir']")
OK1_D3=$(jget "$W/BATCH-OK-001.resp" "r['result']['items'][2]['workspace']['dir']")
# 三件各工作区（内容哈希身份天然隔离，D5）且版本目录 v001_denoise/ + v002_pitch/ 落位
[ "$OK1_D1" != "$OK1_D2" ] && [ "$OK1_D1" != "$OK1_D3" ] && [ "$OK1_D2" != "$OK1_D3" ]; ck $? "BATCH-OK-002 三件三个工作区（互异）"
OK3_MISS=0 # 循环内累计缺目录（审查 P3-1：ck $? 只看末轮迭代，前件缺失被遮蔽）
for d in "$OK1_D1" "$OK1_D2" "$OK1_D3"; do
    { [ -d "$d/v001_denoise" ] && [ -d "$d/v002_pitch" ]; } || OK3_MISS=1
done; ex BATCH-OK-003 test "$OK3_MISS" = 0
fp BATCH-OK-004 "$(jget "$W/BATCH-OK-001.resp" "r['result']['items'][0]['output']")" "pcm_f32le,44100,2"
fp BATCH-OK-005 "$(jget "$W/BATCH-OK-001.resp" "r['result']['items'][1]['output']")" "pcm_f32le,48000,2"
fp BATCH-OK-006 "$(jget "$W/BATCH-OK-001.resp" "r['result']['items'][2]['output']")" "pcm_f32le,44100,1"
# 单步模板（最常见批形态，D3）：件响应 result 原样并入——单动词键（output/frames/…）且无 steps 键
jr BATCH-OK-007 0 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$W/okws1\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"cmd\":\"pitch\",\"semi\":-1.5}]}" \
    ok=true num:result.total=2 num:result.okCount=2 \
    exists:result.items.0.output absent:result.items.0.steps num:result.items.0.workspace.version=1 \
    bool:result.items.0.ok=true bool:result.items.1.ok=true
# 带模型批（有 onnx 时）：单步 aiDenoise + 两步 aiDenoise→separate 各一（模型工作点锁 44.1k/2ch）
if [ "$HAS_MODELS" = 1 ]; then
    jr BATCH-OK-008 0 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$W/okmws\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"cmd\":\"aiDenoise\",\"model\":\"$GTCRN\"}]}" \
        ok=true num:result.okCount=2 num:result.items.0.outputSpec.sampleRate=44100 \
        num:result.items.0.outputSpec.channels=2 bool:result.items.0.engineResampled=true exists:result.items.0.output
    fp BATCH-OK-009 "$(jget "$W/BATCH-OK-008.resp" "r['result']['items'][0]['output']")" "pcm_f32le,44100,2"
    jr BATCH-OK-010 0 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$W/okmws2\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"cmd\":\"aiDenoise\",\"model\":\"$GTCRN\"},{\"cmd\":\"separate\",\"model\":\"$UVR\"}]}" \
        ok=true num:result.okCount=2 str:result.items.0.steps.0.cmd=aiDenoise \
        str:result.items.0.steps.1.cmd=separate exists:result.items.0.output
else
    skip "BATCH-OK-008..010 带模型批（缺 third_party 模型，fetch-deps.sh 备齐后跑）"
fi

echo "== BATCH-EQV（编排等价核心护栏：批跑 vs 逐件手动跑 pipeline，逐字节一致）=="
# 批跑（根 A）与逐件手动跑（根 B）：每件每步都是完整单动词 execute（D1 组合非复制）
# → 各件终产物与中间产物都须逐字节一致（批 = 纯编排、零数值漂移；确定性由 CMD 族背书）
EQV_STEPS='{"cmd":"denoise","humBase":"60","wind":true},{"cmd":"pitch","semi":2.0}'
jr BATCH-EQV-001 0 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$W/eqvA\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\",\"$W/f3.wav\"],\"steps\":[$EQV_STEPS]}" ok=true
for i in 0 1 2; do
    IN=$(jget "$W/BATCH-EQV-001.resp" "r['result']['items'][$i]['input']")
    jr "BATCH-EQV-m$i" 0 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$IN\",\"workspace\":\"$W/eqvB\",\"steps\":[$EQV_STEPS]}" ok=true
    ex "BATCH-EQV-$((i+2))a" cmp "$(jget "$W/BATCH-EQV-001.resp" "r['result']['items'][$i]['output']")" \
        "$(jget "$W/BATCH-EQV-m$i.resp" "r['result']['output']")"
    ex "BATCH-EQV-$((i+2))b" cmp "$(jget "$W/BATCH-EQV-001.resp" "r['result']['items'][$i]['steps'][0]['output']")" \
        "$(jget "$W/BATCH-EQV-m$i.resp" "r['result']['steps'][0]['output']")"
done
if [ "$HAS_MODELS" = 1 ]; then
    jr BATCH-EQV-008 0 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$W/eqvC\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"cmd\":\"aiDenoise\",\"model\":\"$GTCRN\"},{\"cmd\":\"separate\",\"model\":\"$UVR\"}]}" ok=true
    for i in 0 1; do
        IN=$(jget "$W/BATCH-EQV-008.resp" "r['result']['items'][$i]['input']")
        jr "BATCH-EQV-n$i" 0 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$IN\",\"workspace\":\"$W/eqvD\",\"steps\":[{\"cmd\":\"aiDenoise\",\"model\":\"$GTCRN\"},{\"cmd\":\"separate\",\"model\":\"$UVR\"}]}" ok=true
        ex "BATCH-EQV-$((i+9))" cmp "$(jget "$W/BATCH-EQV-008.resp" "r['result']['items'][$i]['output']")" \
            "$(jget "$W/BATCH-EQV-n$i.resp" "r['result']['output']")"
    done
else
    skip "BATCH-EQV-008..010 带模型批等价（缺 third_party 模型）"
fi
# 同根等价（round-2 补测）：批与逐件手动跑共用同一 workspace 根——手动跑经根级缓存/索引定位
# 批已建的工作区并续号（v003+），件产物仍须逐字节一致（内容确定性 + 工作区定位互不干扰双证）
jr BATCH-EQV-SR1 0 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$W/eqvS\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\",\"$W/f3.wav\"],\"steps\":[$EQV_STEPS]}" ok=true num:result.okCount=3
for i in 0 1 2; do
    IN=$(jget "$W/BATCH-EQV-SR1.resp" "r['result']['items'][$i]['input']")
    jr "BATCH-EQV-SRs$i" 0 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$IN\",\"workspace\":\"$W/eqvS\",\"steps\":[$EQV_STEPS]}" ok=true
    ex "BATCH-EQV-SRc$i" cmp "$(jget "$W/BATCH-EQV-SR1.resp" "r['result']['items'][$i]['output']")" \
        "$(jget "$W/BATCH-EQV-SRs$i.resp" "r['result']['output']")"
done

echo "== BATCH-FAIL（失败不中断：件错误原文透传 + 幸存件落盘 + 全败批 ok 口径 + 步骤归属透传）=="
# 配方一：件 2 不存在 → ok=true（编排层成功，OQ-1(a)）+ okCount=2 + 件错误原文（无「第 N 件」前缀，附裁 3）
jr BATCH-FAIL-001 0 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$W/failws\",\"inputs\":[\"$W/f1.wav\",\"$W/no-such.wav\",\"$W/f3.wav\"],\"steps\":[{\"cmd\":\"pitch\",\"semi\":2.0}]}" \
    ok=true num:result.total=3 num:result.okCount=2 bool:result.items.0.ok=true \
    bool:result.items.1.ok=false str:result.items.1.error.code=E_FILE \
    str:result.items.1.error.message="打不开文件：$W/no-such.wav" bool:result.items.2.ok=true \
    exists:result.items.0.output exists:result.items.2.output
python3 -c "
import json
d = json.load(open('$W/BATCH-FAIL-001.resp'))
m = d['result']['items'][1]['error']['message']
assert not m.startswith('第'), m# 附裁 3：归属在 index/input 结构键，message 是产生点原文
assert '第 2 件' not in m, m"
ck $? "BATCH-FAIL-002 件错误无「第 N 件」前缀（附裁 3）"
# 配方二：件 2 无音频流（video-only mp4）→ 同型续跑，E_FILE 归件
jr BATCH-FAIL-003 0 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$W/failws2\",\"inputs\":[\"$W/f1.wav\",\"$W/vidonly.mp4\",\"$W/f3.wav\"],\"steps\":[{\"cmd\":\"pitch\",\"semi\":2.0}]}" \
    ok=true num:result.okCount=2 bool:result.items.1.ok=false str:result.items.1.error.code=E_FILE \
    str:result.items.1.error.message="文件里没有音频流" exists:result.items.2.output
# 配方三：坏模型（垃圾字节，显式 model 传入 → 全批同模板全败）→ ok=true okCount=0（退出码 0：批跑完了）
head -c 4096 /dev/urandom > "$W/garbage.onnx"
jr BATCH-FAIL-004 0 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$W/failws3\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"cmd\":\"aiDenoise\",\"model\":\"$W/garbage.onnx\"}]}" \
    ok=true num:result.total=2 num:result.okCount=0 bool:result.items.0.ok=false \
    str:result.items.0.error.code=E_MODEL bool:result.items.1.ok=false absent:result.items.0.output
# 多步件内步失败（维度错位模型）：pipeline 归属透传——items[].error 带步骤前缀与 step/stepCmd
if python3 -c 'import onnx' 2>/dev/null; then
    python3 "$CRAFT" "$W/mismatch.onnx" >/dev/null 2>&1
    jr BATCH-FAIL-005 0 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$W/failws4\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"cmd\":\"transcode\"},{\"cmd\":\"aiDenoise\",\"model\":\"$W/mismatch.onnx\"}]}" \
        ok=true num:result.okCount=0 contains:result.items.0.error.message="步骤 2（aiDenoise）：" \
        num:result.items.0.error.step=2 str:result.items.0.error.stepCmd=aiDenoise
    # 步 1 已提交保留（D5 透传）：件 1（f1 源）工作区 head=1（transcode 步落 v001，失败步 discard）
    F5_DIR=$(ls -d "$W/failws4"/f1.*/ | head -1)
    wsjson BATCH-FAIL-006 "${F5_DIR%/}" "d['head']==1 and len(d['versions'])==1 and d['versions'][0]['cmd']=='transcode'"
else
    skip "BATCH-FAIL-005/006 维度错位模型（缺 python3-onnx）"
fi

echo "== BATCH-NEG（schema 负例：逐条消息断言 + 开工前拦截零副作用）=="
NEGWS="$W/negws"
NEG_STEPS='{"cmd":"pitch","semi":2.0}'
jr BATCH-NEG-001 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"steps\":[$NEG_STEPS]}" \
    ok=false cmd=batch code=E_PARAMS str:error.message="inputs 缺失" absent:result
jr BATCH-NEG-002 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":\"nope\",\"steps\":[$NEG_STEPS]}" \
    ok=false code=E_PARAMS str:error.message="inputs 必须是数组"
jr BATCH-NEG-003 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\"],\"steps\":[$NEG_STEPS]}" \
    ok=false code=E_PARAMS str:error.message="inputs 至少需要 2 件"
jr BATCH-NEG-004 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[1,2],\"steps\":[$NEG_STEPS]}" \
    ok=false code=E_PARAMS str:error.message="inputs[0] 必须是字符串"
jr BATCH-NEG-005 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"\"],\"steps\":[$NEG_STEPS]}" \
    ok=false code=E_PARAMS str:error.message="inputs[1] 不能为空"
# 重复绝对路径（QFileInfo::absoluteFilePath 归一后比对，D2）：./ 与直接路径同一文件
jr BATCH-NEG-006 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"$W/./f1.wav\"],\"steps\":[$NEG_STEPS]}" \
    ok=false code=E_PARAMS str:error.message="inputs 含重复文件：$W/f1.wav"
jr BATCH-NEG-007 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"]}" \
    ok=false code=E_PARAMS str:error.message="steps 缺失"
jr BATCH-NEG-008 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":\"nope\"}" \
    ok=false code=E_PARAMS str:error.message="steps 必须是数组"
jr BATCH-NEG-009 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[]}" \
    ok=false code=E_PARAMS str:error.message="steps 至少需要 1 步"
jr BATCH-NEG-010 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[42]}" \
    ok=false code=E_PARAMS str:error.message="steps[0] 必须是对象"
jr BATCH-NEG-011 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"semi\":2}]}" \
    ok=false code=E_PARAMS str:error.message="steps[0] 缺少 cmd"
jr BATCH-NEG-012 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"cmd\":7}]}" \
    ok=false code=E_PARAMS str:error.message="steps[0] 的 cmd 必须是字符串"
jr BATCH-NEG-013 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"cmd\":\"probe\"}]}" \
    ok=false code=E_PARAMS str:error.message="steps[0] 的 cmd 不可链式：probe（仅引擎动词可入链）"
jr BATCH-NEG-014 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"cmd\":\"extractAudio\"}]}" \
    ok=false code=E_PARAMS msg-contains="不可链式：extractAudio"
jr BATCH-NEG-015 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"cmd\":\"modelList\"}]}" \
    ok=false code=E_PARAMS msg-contains="不可链式：modelList"
# 嵌套自链拒收（递归深度有界为 1，D2/D9）——pipeline 与 batch 双查
jr BATCH-NEG-016 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"cmd\":\"pipeline\"}]}" \
    ok=false code=E_PARAMS msg-contains="不可链式：pipeline"
jr BATCH-NEG-017 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"cmd\":\"batch\"}]}" \
    ok=false code=E_PARAMS msg-contains="不可链式：batch"
# 模板步链级键拒收（风险 2：链位次决定输入，步骤内键污染子请求组装）
jr BATCH-NEG-018 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"cmd\":\"pitch\",\"semi\":2,\"input\":\"$W/x.wav\"}]}" \
    ok=false code=E_PARAMS str:error.message="steps[0] 未知字段：input"
jr BATCH-NEG-019 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"cmd\":\"pitch\",\"semi\":2,\"output\":\"$W/x.wav\"}]}" \
    ok=false code=E_PARAMS str:error.message="steps[0] 未知字段：output"
jr BATCH-NEG-020 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"cmd\":\"pitch\",\"semi\":2,\"workspace\":\"$W/other\"}]}" \
    ok=false code=E_PARAMS str:error.message="steps[0] 未知字段：workspace"
jr BATCH-NEG-021 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"cmd\":\"pitch\",\"semi\":2,\"v\":1}]}" \
    ok=false code=E_PARAMS str:error.message="steps[0] 未知字段：v"
jr BATCH-NEG-022 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"cmd\":\"pitch\",\"semi\":2,\"bogus\":1}]}" \
    ok=false code=E_PARAMS str:error.message="steps[0] 未知字段：bogus"
# 参数越界与模型不存在：模板预校验在开工前拦截（步骤级归属——前缀 + step/stepCmd）
jr BATCH-NEG-023 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"cmd\":\"pitch\",\"semi\":15}]}" \
    ok=false code=E_PARAMS str:error.message="步骤 1（pitch）：semi 须在 [-12, 12] 内" num:error.step=1 str:error.stepCmd=pitch
jr BATCH-NEG-024 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[{\"cmd\":\"aiDenoise\",\"model\":\"/nonexistent.onnx\"}]}" \
    ok=false code=E_MODEL str:error.message="步骤 1（aiDenoise）：找不到模型 /nonexistent.onnx"
# workspace 必填（D5）与信封口径（output 拒收 / input 未知字段——requiresInput=false）
jr BATCH-NEG-025 1 "{\"v\":1,\"cmd\":\"batch\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[$NEG_STEPS]}" \
    ok=false code=E_PARAMS str:error.message="batch 必须带 workspace"
jr BATCH-NEG-026 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"output\":\"$W/x.wav\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[$NEG_STEPS]}" \
    ok=false code=E_PARAMS msg-contains="未知字段：output"
jr BATCH-NEG-027 1 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$NEGWS\",\"input\":\"$W/f1.wav\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[$NEG_STEPS]}" \
    ok=false code=E_PARAMS msg-contains="未知字段：input"
# schema 段错误无步骤归属键（归属键只随步骤级预校验/执行错误）
python3 -c "import json; d=json.load(open('$W/BATCH-NEG-010.resp')); assert 'step' not in d['error'] and 'stepCmd' not in d['error']"
ck $? "BATCH-NEG-028 schema 错误不带 step/stepCmd（归属键只随步骤级错误）"
# 开工前拦截零副作用：negws 根无任何目录 + 全部源 sha256 与请求前基线一致（预校验零副作用，
# 原件只读红线；基线取在素材生成后、任何请求前——见文件头 SRC_SHA_BASE 注）
ex BATCH-NEG-029 test -z "$(ls -A "$NEGWS" 2>/dev/null)"
ex BATCH-NEG-030 test "$SRC_SHA_BASE" = "$(sha256sum "$W/f1.wav" "$W/f2.wav" "$W/f3.wav" | md5sum | cut -d' ' -f1)"

echo "== BATCH-WS（工作区隔离与 batch 标记：件间隔离/双键/同内容续号/缓存删除/非批零新键）=="
# 3 件两步批（BATCH-OK-001 工作区）：多步件版本记录同时含 pipeline 与 batch 两键（风险 4 拦截面）
wsjson BATCH-WS-001 "$OK1_D1" "d['head']==2 and len(d['versions'])==2 \
and d['versions'][0]['pipeline']=={'index':1,'total':2} and d['versions'][0]['batch']=={'index':1,'total':3} \
and d['versions'][1]['pipeline']=={'index':2,'total':2} and d['versions'][1]['batch']=={'index':1,'total':3} \
and d['versions'][1]['parentVersion']==1"
wsjson BATCH-WS-002 "$OK1_D3" "d['head']==2 and d['versions'][0]['batch']=={'index':3,'total':3} \
and d['versions'][1]['batch']=={'index':3,'total':3}"
# 单步批（BATCH-OK-007 工作区）：单步件记录带 batch 键而无 pipeline 键（两标记通路各自独立）
OK7_D1=$(jget "$W/BATCH-OK-007.resp" "r['result']['items'][0]['workspace']['dir']")
wsjson BATCH-WS-003 "$OK7_D1" "d['versions'][0]['batch']=={'index':1,'total':2} and 'pipeline' not in d['versions'][0]"
# 版本记录 params 纯度：链级键（inputs/steps/workspace/v）不得泄入件步参数（风险 2）
wsjson BATCH-WS-004 "$OK1_D1" "sorted(d['versions'][0]['params'])==['humBase','wind'] and d['versions'][1]['params']=={'semi':2.0}"
# 同内容不同路径两件 → 同一工作区顺次 v001/v002（内容哈希身份 + append-only 续号）
cp "$W/f1.wav" "$W/f1copy.wav"
jr BATCH-WS-005 0 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$W/samews\",\"inputs\":[\"$W/f1.wav\",\"$W/f1copy.wav\"],\"steps\":[$NEG_STEPS]}" \
    ok=true num:result.okCount=2 num:result.items.0.workspace.version=1 num:result.items.1.workspace.version=2
SAME_DIR=$(jget "$W/BATCH-WS-005.resp" "r['result']['items'][0]['workspace']['dir']")
ex BATCH-WS-006 test "$SAME_DIR" = "$(jget "$W/BATCH-WS-005.resp" "r['result']['items'][1]['workspace']['dir']")"
wsjson BATCH-WS-007 "${SAME_DIR%/}" "d['head']==2 and [v['version'] for v in d['versions']]==[1,2] \
and d['versions'][0]['batch']=={'index':1,'total':2} and d['versions'][1]['batch']=={'index':2,'total':2}"
# 缓存删除（rm 根级 index.json）后批继续定位：扫根目录 `*.<hash8>` 兜底，同工作区续号 v003
rm -f "$W/samews/index.json"
jr BATCH-WS-008 0 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$W/samews\",\"inputs\":[\"$W/f1.wav\",\"$W/f1copy.wav\"],\"steps\":[$NEG_STEPS]}" \
    ok=true str:result.items.0.workspace.dir="${SAME_DIR%/}" num:result.items.1.workspace.version=4
# 非批任务不带 batch 键（键面零漂移；逐字节零漂移归 BATCH-ZERO 全量复跑，round-N.md 记档）
jr BATCH-WS-009 0 "{\"v\":1,\"cmd\":\"pitch\",\"input\":\"$W/f3.wav\",\"workspace\":\"$W/plainws\",\"semi\":3.0}" ok=true
PLAIN_DIR=$(jget "$W/BATCH-WS-009.resp" "r['result']['workspace']['dir']")
wsjson BATCH-WS-010 "${PLAIN_DIR%/}" "len(d['versions'])==1 and 'batch' not in d['versions'][0] and 'pipeline' not in d['versions'][0]"
# 非批任务 workspace.json 逐字节不变（round-2 补测）：单动词落 zws 根 → 快照 → 同根跑批（不同源
# → 别的工作区）→ 该工作区索引逐字节一致（批只重写根级 index.json 与自己件的工作区，不触碰他人；
# commitVersion 的 batch 存在性插键不改非批路径的既有记录）
jr BATCH-WS-011 0 "{\"v\":1,\"cmd\":\"pitch\",\"input\":\"$W/f3.wav\",\"workspace\":\"$W/zws\",\"semi\":3.0}" ok=true
ZDIR=$(jget "$W/BATCH-WS-011.resp" "r['result']['workspace']['dir']")
ZSHA=$(sha256sum "${ZDIR%/}/workspace.json" | cut -d' ' -f1)
jr BATCH-WS-012 0 "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$W/zws\",\"inputs\":[\"$W/f1.wav\",\"$W/f2.wav\"],\"steps\":[$NEG_STEPS]}" \
    ok=true num:result.okCount=2
ex BATCH-WS-013 test "$ZSHA" = "$(sha256sum "${ZDIR%/}/workspace.json" | cut -d' ' -f1)"

echo "== BATCH-CANCEL/PROG（取消与进度：探针直调 execute 的 Ctx 注入面，五相）=="
echo "== 探针编译（batch-cancel-probe.cpp，链主树 libengine.a；陈旧库闸同 run-pipe）=="
cmake --build "$BUILD" >/dev/null || { echo "FAIL: 增量构建失败"; exit 1; }
PKGS="Qt6Core Qt6Network libavformat libavcodec libavutil libswresample soundtouch"
g++ -std=c++20 -O2 "$S/batch-cancel-probe.cpp" -o "$W/batch-cancel-probe" -I"$(pwd)/src" \
    $(pkg-config --cflags $PKGS) "$BUILD/libengine.a" \
    $(pkg-config --libs $PKGS) "$(pwd)/third_party/ort/linux/lib/libonnxruntime.so" \
    -Wl,-rpath,"$(pwd)/third_party/ort/linux/lib" \
    || { echo "FAIL: 探针编译失败"; exit 1; }
PROBE="$W/batch-cancel-probe"
run_probe() { local ph=$1 ws=$2; rm -rf "$ws"; QT_QPA_PLATFORM=offscreen "$PROBE" "$ph" "$W/f1.wav" "$ws" >"$W/cancel-$ph.out" 2>/dev/null; }
# 相位一 cancel-pre：标志预置 → 件 1 开工前取消，「已取消」逐字 + completed=0/total=2、无 items/result
run_probe cancel-pre "$W/cw-pre"
python3 - "$W/cancel-cancel-pre.out" <<'PYEOF'
import json, sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
resp = json.loads([l for l in lines if l.startswith("RESP ")][0][5:])
assert resp["ok"] is False and resp["error"]["code"] == "E_CANCELLED", resp
assert resp["error"]["message"] == "已取消", resp["error"]["message"]# 逐字（GUI 静默特判依赖，D7）
assert resp["error"]["completed"] == 0 and resp["error"]["total"] == 2, resp
assert "items" not in resp["error"] and "result" not in resp, resp# 取消信封无清单（OQ-2(b)）
assert not any(l.startswith("PROG ") and len(l) > 5 for l in lines), lines
PYEOF
ck $? "BATCH-CAN-001 cancel-pre（E_CANCELLED/「已取消」逐字/completed=0/total=2/零进度）"
ex BATCH-CAN-002 test ! -e "$W/cw-pre"  # 开工前取消：连工作区根目录都不建
# 相位二 cancel-mid：首个进度回调置位 → 件 1 引擎分块循环检查点取消（completed=0，在途件不计）
run_probe cancel-mid "$W/cw-mid"
python3 - "$W/cancel-cancel-mid.out" <<'PYEOF'
import json, sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
resp = json.loads([l for l in lines if l.startswith("RESP ")][0][5:])
assert resp["error"]["code"] == "E_CANCELLED" and resp["error"]["message"] == "已取消", resp
assert resp["error"]["completed"] == 0 and resp["error"]["total"] == 2, resp
PYEOF
ck $? "BATCH-CAN-003 cancel-mid（件 1 在途取消，completed=0）"
ex BATCH-CAN-004 test -z "$(ls -d "$W/cw-mid"/*/v* 2>/dev/null)"  # 版本目录已 discard 无残迹
# 相位三 cancel-between：件 1 收尾映射 50 置位 → 件 2 开工前取消：completed=1、件 1 已提交保留
run_probe cancel-between "$W/cw-bet"
python3 - "$W/cancel-cancel-between.out" <<'PYEOF'
import json, sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
resp = json.loads([l for l in lines if l.startswith("RESP ")][0][5:])
assert resp["error"]["code"] == "E_CANCELLED" and resp["error"]["message"] == "已取消", resp
assert resp["error"]["completed"] == 1 and resp["error"]["total"] == 2, resp# OQ-2(b)：被取消件不计
prog = [int(x) for x in [l for l in lines if l.startswith("PROG ")][0][5:].split(",") if x]
assert prog and prog[-1] == 50, prog# 件 1 收尾 100 的映射值 (0+100)/2
PYEOF
ck $? "BATCH-CAN-005 cancel-between（completed=1/「已取消」逐字/进度止于映射 50）"
BET_DIR=$(ls -d "$W/cw-bet"/*/ | head -1)
wsjson BATCH-CAN-006 "${BET_DIR%/}" "d['head']==1 and len(d['versions'])==1 and d['versions'][0]['batch']=={'index':1,'total':2}"
# 相位四 progress：3 件单步模板件层等分 (i*100+pct)/n——边界 33/66、单调、终值 100（D6(a)）
run_probe progress "$W/cw-prog"
python3 - "$W/cancel-progress.out" <<'PYEOF'
import json, sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
resp = json.loads([l for l in lines if l.startswith("RESP ")][0][5:])
assert resp["ok"] is True and resp["result"]["okCount"] == 3, resp
prog = [int(x) for x in [l for l in lines if l.startswith("PROG ")][0][5:].split(",") if x]
assert prog[-1] == 100, prog
assert all(a <= b for a, b in zip(prog, prog[1:])), prog# 单调不回退
assert 33 in prog and 66 in prog, prog# 件界值：(0+99)/3=33 与 (100+99)/3=66 可观测
PYEOF
ck $? "BATCH-PROG-001 件层等分映射（终值 100/单调/件界 33·66）"
# 相位五 progress2：2 件两步模板——pipeline 步层等分与批件层等分复合：边界 25/50/75/100
run_probe progress2 "$W/cw-prog2"
python3 - "$W/cancel-progress2.out" <<'PYEOF'
import json, sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
resp = json.loads([l for l in lines if l.startswith("RESP ")][0][5:])
assert resp["ok"] is True and resp["result"]["okCount"] == 2, resp
prog = [int(x) for x in [l for l in lines if l.startswith("PROG ")][0][5:].split(",") if x]
# 件 1 步 1 收尾 ((0+100)/2=50 → (0+50)/2=25)、件 1 收尾 (100→50)、件 2 步 1 ((100+50)/2=75)、终值 100
for b in (25, 50, 75, 100):
    assert b in prog, (b, prog)
assert all(a <= b for a, b in zip(prog, prog[1:])), prog
PYEOF
ck $? "BATCH-PROG-002 两层等分复合映射（边界 25·50·75·100）"

echo "== BATCH-GUIE（GUI worker 壳：doBatch 槽直调，附裁 1 的顺序与禁发面）=="
g++ -std=c++20 -O2 "$S/batch-gui-probe.cpp" -o "$W/batch-gui-probe" -I"$(pwd)/src" \
    $(pkg-config --cflags $PKGS) "$BUILD/libengine.a" \
    $(pkg-config --libs $PKGS) "$(pwd)/third_party/ort/linux/lib/libonnxruntime.so" \
    -Wl,-rpath,"$(pwd)/third_party/ort/linux/lib" \
    || { echo "FAIL: 探针编译失败"; exit 1; }
GUIPROBE="$W/batch-gui-probe"
# bin 布局即 appDir 缺省解析的模型位（pipe-gui-probe 同型）：bin-empty 无模型 / bin-garbage 降噪模型为垃圾
for b in bin-empty bin-garbage; do
    mkdir -p "$W/$b/models/denoiser"
    cp "$GUIPROBE" "$W/$b/probe"
done
head -c 4096 /dev/urandom > "$W/bin-garbage/models/denoiser/gtcrn_simple.onnx"
run_gui() { local bin=$1 ph=$2 ws=$3; rm -rf "$ws"; QT_QPA_PLATFORM=offscreen "$W/$bin/probe" engine "$W/f1.wav" "$ws" "$ph" >"$W/guie-$ph.out" 2>/dev/null; }
# ok-batch：batchFinished ok=1 载荷批 result JSON（total/okCount/items）+ 进度终值 100 + 恰一次 + 无 modelMissing
GUIPROBE_OK="$W/guie-probe-ok"; cp "$GUIPROBE" "$GUIPROBE_OK"
rm -rf "$W/gw-ok"; QT_QPA_PLATFORM=offscreen "$GUIPROBE_OK" engine "$W/f1.wav" "$W/gw-ok" ok-batch >"$W/guie-ok-batch.out" 2>/dev/null
python3 - "$W/guie-ok-batch.out" <<'PYEOF'
import json, sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
task = [l for l in lines if l.startswith("TASK ")][0]
assert " -> ok=1 msg=" in task, task
done = [l for l in lines if l.startswith("BATCHDONE ")][0]
assert done == "BATCHDONE 1 order=B", done# 整批恰一次（busy 一次 acquire 一次释放的壳侧证据）
result = json.loads([l for l in lines if l.startswith("RESULT ")][0][7:])
assert result["total"] == 2 and result["okCount"] == 2, result
assert result["items"][0]["ok"] is True and "output" in result["items"][0], result
prog = [int(x) for x in [l for l in lines if l.startswith("PROG ")][0].split(" : ", 1)[1].split(",") if x]
assert prog[-1] == 100, prog
missing = [l for l in lines if l.startswith("MISSING ")][0]
assert missing == "MISSING 0 for=", missing
PYEOF
ck $? "BATCH-GUIE-001 ok-batch（载荷 JSON/恰一次/进度 100/无 modelMissing）"
# missing-pre：开工前解析皆缺 → batchFinished 先发（busy 已释放）后 modelMissing(aiDenoise)（附裁 1 顺序）
run_gui bin-empty missing-pre "$W/gw-miss"
python3 - "$W/guie-missing-pre.out" <<'PYEOF'
import sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
task = [l for l in lines if l.startswith("TASK ")][0]
assert " -> ok=0 msg=找不到AI 降噪模型：" in task, task
done = [l for l in lines if l.startswith("BATCHDONE ")][0]
assert done == "BATCHDONE 1 order=BM", done# B 先于 M：引导对话框打开时任务锁已归还（附裁 1）
missing = [l for l in lines if l.startswith("MISSING ")][0]
assert missing == "MISSING 1 for=aiDenoise", missing
PYEOF
ck $? "BATCH-GUIE-002 missing-pre（找不到模型文案 + modelMissing(aiDenoise) + B→M 顺序）"
ex BATCH-GUIE-003 test ! -e "$W/gw-miss"  # 整批速断零件启动：零工作区目录
# emid-item：件内 E_MODEL（垃圾字节模型 pre-check 放行、setup 失败）→ 批 ok=1 okCount=0 且零 modelMissing
#（附裁 1：件循环内禁发——busy 长持下引导下载被 if(busy) return 静默吞 = Y5 毒缝封堵）
run_gui bin-garbage emid-item "$W/gw-emid"
python3 - "$W/guie-emid-item.out" <<'PYEOF'
import json, sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
task = [l for l in lines if l.startswith("TASK ")][0]
assert " -> ok=1 msg=" in task, task# ok=编排层成功（OQ-1(a)），成败在 items
result = json.loads([l for l in lines if l.startswith("RESULT ")][0][7:])
assert result["okCount"] == 0 and result["total"] == 2, result
assert all(i["error"]["code"] == "E_MODEL" for i in result["items"]), result
missing = [l for l in lines if l.startswith("MISSING ")][0]
assert missing == "MISSING 0 for=", missing# 件循环内禁发 modelMissing
PYEOF
ck $? "BATCH-GUIE-004 emid-item（okCount=0 且 ok=1 + 零 modelMissing，Y5 封堵）"
# cancel：件 1 收尾映射 50 置取消 → batchFinished ok=0 message 逐字「已取消」（附裁 5 GUI 静默特判契约）
GUIPROBE_CAN="$W/guie-probe-can"; cp "$GUIPROBE" "$GUIPROBE_CAN"
rm -rf "$W/gw-can"; QT_QPA_PLATFORM=offscreen "$GUIPROBE_CAN" engine "$W/f1.wav" "$W/gw-can" cancel >"$W/guie-cancel.out" 2>/dev/null
python3 - "$W/guie-cancel.out" <<'PYEOF'
import sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
task = [l for l in lines if l.startswith("TASK ")][0]
assert task.endswith(" -> ok=0 msg=已取消"), task# 逐字（mainwidget/onBatchDone 特判兼容）
PYEOF
ck $? "BATCH-GUIE-005 cancel（「已取消」逐字）"
GW_CAN_DIR=$(ls -d "$W/gw-can"/*/ 2>/dev/null | head -1); GW_CAN_DIR=${GW_CAN_DIR%/}
wsjson BATCH-GUIE-006 "$GW_CAN_DIR" "d['head']==1 and len(d['versions'])==1"  # 件 1 已提交保留

echo "== BATCH-MEM（内存抽测：2 件 × 30min 批峰值 = 件·步最大值而非叠加，NFR-P4/D1）=="
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=1800" -ac 2 -c:a pcm_s16le "$W/big1.wav"
cp "$W/big1.wav" "$W/big2.wav"
mem_peak() { # mem_peak <label> <请求JSON>：跑一次记峰值 RSS（KB），echo 数值
    /usr/bin/time -v "$VH" --json <<< "$2" >"$W/mem-$1.resp" 2>"$W/mem-$1.time"
    awk '/Maximum resident set size/ {print $NF}' "$W/mem-$1.time"
}
MEM_BATCH=$(mem_peak batch "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$W/memws\",\"inputs\":[\"$W/big1.wav\",\"$W/big2.wav\"],\"steps\":[{\"cmd\":\"denoise\",\"humBase\":\"60\"},{\"cmd\":\"pitch\",\"semi\":2.0}]}")
python3 -c "
import json
d = json.load(open('$W/mem-batch.resp', encoding='utf-8'))
assert d.get('ok') is True and d['result']['okCount'] == 2, d  # 防峰值断言空转（批失败提前退出 → 峰值虚低）
"
ck $? "BATCH-MEM-001 批实跑成功（峰值断言前置防空转）"
MEM_DEN=$(mem_peak den "{\"v\":1,\"cmd\":\"denoise\",\"input\":\"$W/big1.wav\",\"workspace\":\"$W/memws2\",\"humBase\":\"60\"}")
MEM_PIT_IN=$(python3 -c "import json; print(json.load(open('$W/mem-den.resp'))['result']['output'])")
MEM_PIT=$(mem_peak pit "{\"v\":1,\"cmd\":\"pitch\",\"input\":\"$MEM_PIT_IN\",\"workspace\":\"$W/memws2\",\"semi\":2.0}")
MEM_SINGLE=$(( MEM_DEN > MEM_PIT ? MEM_DEN : MEM_PIT ))
echo "  峰值 RSS：2 件批（denoise+pitch）=${MEM_BATCH}KB / denoise=${MEM_DEN}KB / pitch=${MEM_PIT}KB（单动词最大=${MEM_SINGLE}KB）"
python3 -c "
import sys
batch, single = $MEM_BATCH, $MEM_SINGLE
assert batch <= single + 32768, f'批峰值 {batch}KB 显著超过单动词最大 {single}KB（+32MB 裕量内为同量级）'
assert batch < 262144, f'批峰值 {batch}KB 超过 256MB 上限（30min f32 立体声整载入 ≈635MB，此线上方即整文件 regime）'
print(f'  同量级实证：批 {batch}KB ≤ 单动词最大 {single}KB + 32MB 且 < 256MB（件串行×步串行零叠加）')"
ck $? "BATCH-MEM-002 批峰值与最重单动词同量级（远低于整文件 regime）"

if [ "$HAS_MODELS" = 1 ]; then
    echo "== BATCH-MEM 带模型批（2 件 × 120s aiDenoise→separate：模型会话跨件零叠加）=="
    ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=120" -ac 2 -c:a pcm_s16le "$W/mid.wav"
    cp "$W/mid.wav" "$W/mid2.wav"  # 第二件：路径互异过 inputs 查重（内容相同 → 同工作区续号，内存语义不变）
    MEM_MBATCH=$(mem_peak mbatch "{\"v\":1,\"cmd\":\"batch\",\"workspace\":\"$W/memws3\",\"inputs\":[\"$W/mid.wav\",\"$W/mid2.wav\"],\"steps\":[{\"cmd\":\"aiDenoise\",\"model\":\"$GTCRN\"},{\"cmd\":\"separate\",\"model\":\"$UVR\"}]}")
    python3 -c "
import json
d = json.load(open('$W/mem-mbatch.resp', encoding='utf-8'))
assert d.get('ok') is True and d['result']['okCount'] == 2, d"
    ck $? "BATCH-MEM-003 带模型批实跑成功（防空转前置）"
    MEM_AI=$(mem_peak ai "{\"v\":1,\"cmd\":\"aiDenoise\",\"input\":\"$W/mid.wav\",\"workspace\":\"$W/memws4\",\"model\":\"$GTCRN\"}")
    MEM_SEP_IN=$(python3 -c "import json; print(json.load(open('$W/mem-ai.resp'))['result']['output'])")
    MEM_SEP=$(mem_peak sep "{\"v\":1,\"cmd\":\"separate\",\"input\":\"$MEM_SEP_IN\",\"workspace\":\"$W/memws4\",\"model\":\"$UVR\"}")
    MEM_MSINGLE=$(( MEM_AI > MEM_SEP ? MEM_AI : MEM_SEP ))
    echo "  峰值 RSS：模型两步批=${MEM_MBATCH}KB / aiDenoise=${MEM_AI}KB / separate=${MEM_SEP}KB（单动词最大=${MEM_MSINGLE}KB）"
    python3 -c "
import sys
mb, single = $MEM_MBATCH, $MEM_MSINGLE
assert mb <= single + 32768, f'模型批峰值 {mb}KB 显著超过单动词最大 {single}KB'
assert mb < 2097152, f'模型批峰值 {mb}KB 超过 NFR-P4 2GiB 上限'
print(f'  同量级实证：模型批 {mb}KB ≤ 单动词最大 {single}KB + 32MB 且 < 2GiB（模型会话随遍释放零叠加）')"
    ck $? "BATCH-MEM-004 带模型批峰值与最重单动词同量级（NFR-P4）"
else
    skip "BATCH-MEM-003/004 带模型批内存（缺 third_party 模型，fetch-deps.sh 备齐后跑）"
fi

if [ "$FAIL" = 1 ]; then
    echo "== run-batch FAIL（见上）=="
    exit 1
fi
if [ "$SKIPANY" = 1 ] && [ "${PARITY_REQUIRED:-0}" = 1 ]; then
    echo "== run-batch 有 SKIP 且 PARITY_REQUIRED=1（CI 强制点）=="
    exit 1
fi
echo "== run-batch 全绿${SKIPANY:+（有 SKIP，见上；本地降级跑）} =="
