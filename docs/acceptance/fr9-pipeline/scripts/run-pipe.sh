#!/usr/bin/env bash
# run-pipe.sh — FR-9 管线串联片 A（CLI 面）编排（PIPE 族，spec §6，进 ci-smoke）：
#   PIPE-OK    正例链（两步/三步/带模型链）：result 结构 + 版本目录落位 + 中间产物 ffprobe 体检
#   PIPE-EQV   编排等价（核心护栏）：管线跑 vs 逐步手动跑——终产物与中间产物逐字节一致
#   PIPE-CHAIN 版本链与标记：pipeline:{index,total}/parentVersion/head/链中插入无关任务/缓存删除续定位
#   PIPE-PREVAL 整链静态预校验：任何一步不合法 → 开工前拦截（零版本目录/零产物/源 sha 不变）
#   PIPE-FAIL  部分失败：步 2 坏模型 → step/stepCmd/前缀文案 + v001 保留可复用 + 无残迹 + 重跑起新号
#   PIPE-NEG   schema 负例：steps 形态/链级键拒收/内置动词入链/缺 workspace（D6）逐条消息断言
#   PIPE-CANCEL/PROG 取消与进度（探针直调 CommandLayer::execute 的 Ctx 注入面，gui-ws 方法论）
#   PIPE-GUIE  GUI worker 壳（FFExtractor doPipeline 槽直调，无控件）：管线完成文案/modelMissing
#              forVerb=error.stepCmd（附裁 1）/「已取消」静默特判文案
#   PIPE-MEM   内存抽测：30min 免模型链 + 120s 带模型链（aiDenoise→separate）峰值 RSS 与最重
#              单动词同量级（逐遍有界=全程有界实证；带模型例为审查轮 P2-1 补面——D1「峰值=逐步
#              最大值而非和」的模型会话零叠加实证）
#   PIPE-ZERO  零漂移硬门禁不在本脚本（run-json/run-ws/run-models/thinshell/gui-slot-eqv/
#              fake-engine-e2e/ci-smoke 全量复跑），证据记 round-N.md
#   PIPE-GUI（widget 面）探针 pipe-gui-probe.cpp gui 模式，编排 gui-pipe.sh（预设组包/busy/文案）
# 判据与用例表以 docs/acceptance/fr9-pipeline/matrix.csv 为准；本脚本是其自动化执行体。
# 复用 fr24 的 json_check.py 断言器与素材配方（交叉引用不复制）。
# 依赖：python3（stdlib）；PIPE-FAIL-006 坏模型注入需 python3-onnx（缺则 SKIP，PARITY_REQUIRED=1
# 时升级 FAIL）；带模型链（OK-006/EQV-003）缺 third_party 模型时整族 SKIP 同款降级。
# 工作区一律磁盘卷：默认 <build>/.pipe-tmp（run-models 同型），外部可用 TMPDIR 覆盖——/tmp 是
# 配额 tmpfs，30min 素材×多个 f32 产物会撞 EDQUOT 假象（fr8 round-2 教训）。
# 用法：run-pipe.sh [构建目录=build-linux]
set -uo pipefail
cd "$(dirname "$0")/../../../.." # scripts→fr9-pipeline→acceptance→docs→仓库根
[ -f CMakeLists.txt ] || { echo "FAIL: 未落到仓库根（$(pwd)）"; exit 1; }

BUILD="${1:-build-linux}"
VH="$BUILD/video_handler"
GTCRN=third_party/models/denoiser/gtcrn_simple.onnx
UVR=third_party/models/separator/UVR-MDX-NET-Voc_FT.onnx
S=docs/acceptance/fr9-pipeline/scripts
JC=docs/acceptance/fr24-command-layer/scripts/json_check.py
CRAFT=docs/acceptance/fr24-command-layer/scripts/craft-mismatch-model.py
for f in "$VH" "$BUILD/libengine.a"; do
    [ -e "$f" ] || { echo "FAIL: 缺 $f（先构建）"; exit 1; }
done
HAS_MODELS=1
for m in "$GTCRN" "$UVR"; do [ -f "$m" ] || HAS_MODELS=0; done

# 大素材（30min wav ≈300MB + 多个 f32 产物 ≈600MB×n）必须落磁盘卷（见头注）
PIPETMP="${TMPDIR:-$PWD/$BUILD/.pipe-tmp}"
mkdir -p "$PIPETMP"
W=$(mktemp -d "$PIPETMP/pipe.XXXXXX")
trap 'chmod -R u+w "$W" >/dev/null 2>&1; rm -rf "$W"' EXIT
FAIL=0 SKIPANY=

skip() { echo "  SKIP: $*"; SKIPANY=1; }
ck() { if [ "$1" = 0 ]; then echo "  PASS $2"; else echo "  FAIL $2"; FAIL=1; fi; }

# 素材（run-json 同配方）：5s 44.1k/48k 立体声正弦
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=5" -ac 2 -c:a pcm_s16le "$W/st.wav"
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=5" -ac 2 -c:a pcm_s16le "$W/st48k.wav"

# jr <id> <期望退出码> <请求JSON> [断言...]（json_check.py 断言器，run-json 同款）
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

echo "== PIPE-OK（正例链：两步/三步/带模型；result 结构 + 落位 + 中间产物体检）=="
jr PIPE-OK-001 0 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st48k.wav\",\"workspace\":\"$W/okws\",\"steps\":[{\"cmd\":\"denoise\",\"humBase\":\"60\"},{\"cmd\":\"pitch\",\"semi\":2.0}]}" \
    ok=true v=1 cmd=pipeline bool:result.truncated=false exists:result.output \
    keys:result=output,steps,truncated,workspace \
    str:result.steps.0.cmd=denoise num:result.steps.0.version=1 bool:result.steps.0.engineResampled=false \
    num:result.steps.0.outputSpec.sampleRate=48000 num:result.steps.0.outputSpec.channels=2 \
    str:result.steps.1.cmd=pitch num:result.steps.1.version=2 \
    num:result.workspace.version=2 contains:result.workspace.dir=/okws/
OK1_DIR=$(python3 -c "import json; print(json.load(open('$W/PIPE-OK-001.resp'))['result']['workspace']['dir'])")
OK1_OUT=$(python3 -c "import json; print(json.load(open('$W/PIPE-OK-001.resp'))['result']['output'])")
# 终产物 = 末步产物（result.output 与 steps 末项 output 同一文件）
ex PIPE-OK-002 test "$OK1_OUT" = "$(python3 -c "import json; print(json.load(open('$W/PIPE-OK-001.resp'))['result']['steps'][1]['output'])")"
[ -d "$OK1_DIR/v001_denoise" ] && [ -d "$OK1_DIR/v002_pitch" ]; ck $? "PIPE-OK-003 版本目录落位 v001_denoise/ + v002_pitch/"
fp PIPE-OK-004 "$OK1_OUT" "pcm_f32le,48000,2"
fp PIPE-OK-005 "$(python3 -c "import json; print(json.load(open('$W/PIPE-OK-001.resp'))['result']['steps'][0]['output'])")" "pcm_f32le,48000,2"
wsjson PIPE-OK-006 "$OK1_DIR" "d['head']==2 and len(d['versions'])==2 and d['versions'][0]['pipeline']=={'index':1,'total':2} and d['versions'][1]['pipeline']=={'index':2,'total':2} and d['versions'][1]['parentVersion']==1"
# 三步链（transcode→denoise→pitch）：steps 全量与版本号连续
jr PIPE-OK-007 0 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/ok3ws\",\"steps\":[{\"cmd\":\"transcode\"},{\"cmd\":\"denoise\",\"humBase\":\"50\",\"wind\":true},{\"cmd\":\"pitch\",\"semi\":-3.0}]}" \
    ok=true num:result.steps.0.version=1 str:result.steps.1.cmd=denoise num:result.steps.1.version=2 \
    str:result.steps.2.cmd=pitch num:result.steps.2.version=3 num:result.workspace.version=3 \
    num:result.steps.2.outputSpec.sampleRate=44100 num:result.steps.2.frames=220500
# 带模型链（aiDenoise→separate）：AI/分离步锁兜底 44.1k/2ch（canonical-rate 口径），48k 源首步 resampled
if [ "$HAS_MODELS" = 1 ]; then
    jr PIPE-OK-008 0 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st48k.wav\",\"workspace\":\"$W/okmws\",\"steps\":[{\"cmd\":\"aiDenoise\",\"model\":\"$GTCRN\"},{\"cmd\":\"separate\",\"model\":\"$UVR\"}]}" \
        ok=true bool:result.steps.0.engineResampled=true num:result.steps.0.outputSpec.sampleRate=44100 \
        num:result.steps.0.outputSpec.channels=2 num:result.steps.1.outputSpec.sampleRate=44100 \
        num:result.steps.1.outputSpec.channels=2 exists:result.output
    fp PIPE-OK-009 "$(python3 -c "import json; print(json.load(open('$W/PIPE-OK-008.resp'))['result']['steps'][0]['output'])")" "pcm_f32le,44100,2"
    fp PIPE-OK-010 "$(python3 -c "import json; print(json.load(open('$W/PIPE-OK-008.resp'))['result']['output'])")" "pcm_f32le,44100,2"
else
    skip "PIPE-OK-008..010 带模型链（缺 third_party 模型，fetch-deps.sh 备齐后跑）"
fi

echo "== PIPE-EQV（编排等价核心护栏：管线跑 vs 逐步手动跑，逐字节一致）=="
# 管线跑（根 A）与手动逐步跑（根 B，同字节输入副本）：每步都是完整单动词 execute（D1 组合非复制）
# → 终产物与中间产物都须逐字节一致（管线 = 纯编排、无数值漂移）
cp "$W/st.wav" "$W/st-eqv.wav"
jr PIPE-EQV-001 0 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st-eqv.wav\",\"workspace\":\"$W/eqvA\",\"steps\":[{\"cmd\":\"denoise\",\"humBase\":\"60\",\"wind\":true},{\"cmd\":\"pitch\",\"semi\":2.0}]}" \
    ok=true exists:result.output
jr PIPE-EQV-002 0 "{\"v\":1,\"cmd\":\"denoise\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/eqvB\",\"humBase\":\"60\",\"wind\":true}" ok=true
EQV_B1=$(python3 -c "import json; print(json.load(open('$W/PIPE-EQV-002.resp'))['result']['output'])")
jr PIPE-EQV-003 0 "{\"v\":1,\"cmd\":\"pitch\",\"input\":\"$EQV_B1\",\"workspace\":\"$W/eqvB\",\"semi\":2.0}" ok=true
EQV_B2=$(python3 -c "import json; print(json.load(open('$W/PIPE-EQV-003.resp'))['result']['output'])")
EQV_A2=$(python3 -c "import json; print(json.load(open('$W/PIPE-EQV-001.resp'))['result']['output'])")
ex PIPE-EQV-004 cmp "$EQV_A2" "$EQV_B2"
EQV_A1=$(python3 -c "import json; print(json.load(open('$W/PIPE-EQV-001.resp'))['result']['steps'][0]['output'])")
ex PIPE-EQV-005 cmp "$EQV_A1" "$EQV_B1"
if [ "$HAS_MODELS" = 1 ]; then
    jr PIPE-EQV-006 0 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st-eqv.wav\",\"workspace\":\"$W/eqvC\",\"steps\":[{\"cmd\":\"aiDenoise\",\"model\":\"$GTCRN\"},{\"cmd\":\"separate\",\"model\":\"$UVR\"}]}" ok=true
    jr PIPE-EQV-007 0 "{\"v\":1,\"cmd\":\"aiDenoise\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/eqvD\",\"model\":\"$GTCRN\"}" ok=true
    EQV_D1=$(python3 -c "import json; print(json.load(open('$W/PIPE-EQV-007.resp'))['result']['output'])")
    jr PIPE-EQV-008 0 "{\"v\":1,\"cmd\":\"separate\",\"input\":\"$EQV_D1\",\"workspace\":\"$W/eqvD\",\"model\":\"$UVR\"}" ok=true
    EQV_D2=$(python3 -c "import json; print(json.load(open('$W/PIPE-EQV-008.resp'))['result']['output'])")
    ex PIPE-EQV-009 cmp "$(python3 -c "import json; print(json.load(open('$W/PIPE-EQV-006.resp'))['result']['output'])")" "$EQV_D2"
else
    skip "PIPE-EQV-006..009 带模型链等价（缺 third_party 模型）"
fi

echo "== PIPE-CHAIN（版本链与标记：pipeline 标记/parentVersion/无关任务交错/缓存删除续定位）=="
# 链 → 手动单任务 → 再链：版本号交错但标记与 parent 链各归各（spec §6.3）
jr PIPE-CHAIN-001 0 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/chws\",\"steps\":[{\"cmd\":\"denoise\",\"humBase\":\"50\"},{\"cmd\":\"pitch\",\"semi\":1.0}]}" ok=true
jr PIPE-CHAIN-002 0 "{\"v\":1,\"cmd\":\"pitch\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/chws\",\"semi\":4.0}" ok=true
jr PIPE-CHAIN-003 0 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/chws\",\"steps\":[{\"cmd\":\"denoise\",\"humBase\":\"50\"},{\"cmd\":\"pitch\",\"semi\":1.0}]}" ok=true
CH_DIR=$(python3 -c "import json; print(json.load(open('$W/PIPE-CHAIN-003.resp'))['result']['workspace']['dir'])")
wsjson PIPE-CHAIN-004 "$CH_DIR" "d['head']==5 and [v['version'] for v in d['versions']]==[1,2,3,4,5] \
and d['versions'][0]['pipeline']=={'index':1,'total':2} and d['versions'][1]['pipeline']=={'index':2,'total':2} \
and 'pipeline' not in d['versions'][2] and d['versions'][2]['parentVersion'] is None \
and d['versions'][3]['pipeline']=={'index':1,'total':2} and d['versions'][4]['pipeline']=={'index':2,'total':2} \
and d['versions'][1]['parentVersion']==1 and d['versions'][4]['parentVersion']==4"
# 步骤记录 params 纯度：只含该步引擎参数（缺省值由 recordParams 补齐为 filled 形态），链级键
#（input/output/workspace/v/steps）不得泄入（风险 2）
wsjson PIPE-CHAIN-005 "$CH_DIR" "sorted(d['versions'][0]['params'])==['humBase','wind'] and d['versions'][1]['params']=={'semi':1.0} and sorted(d['versions'][2]['params'])==['semi']"
# 缓存删除（rm 根级 index.json）后链继续定位：resolveWsDir 扫根目录 `*.<hash8>` 兜底，同工作区续号
rm -f "$W/chws/index.json"
jr PIPE-CHAIN-006 0 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/chws\",\"steps\":[{\"cmd\":\"denoise\",\"humBase\":\"50\"},{\"cmd\":\"pitch\",\"semi\":1.0}]}" \
    ok=true str:result.workspace.dir="$CH_DIR" num:result.workspace.version=7

echo "== PIPE-PREVAL（整链静态预校验：开工前拦截，零执行零产物）=="
PRE_SHA=$(sha256sum "$W/st.wav" | cut -d' ' -f1)
jr PIPE-PREVAL-001 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/prews\",\"steps\":[{\"cmd\":\"transcode\"},{\"cmd\":\"pitch\",\"semi\":15}]}" \
    ok=false cmd=pipeline code=E_PARAMS str:error.message="步骤 2（pitch）：semi 须在 [-12, 12] 内" \
    num:error.step=2 str:error.stepCmd=pitch absent:result
jr PIPE-PREVAL-002 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/prews\",\"steps\":[{\"cmd\":\"transcode\"},{\"cmd\":\"pitch\",\"semi\":0}]}" \
    ok=false code=E_PARAMS str:error.message="步骤 2（pitch）：semi 不能为 0"
jr PIPE-PREVAL-003 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/prews\",\"steps\":[{\"cmd\":\"transcode\"},{\"cmd\":\"denoise\",\"humBase\":\"off\",\"wind\":false}]}" \
    ok=false code=E_PARAMS str:error.message="步骤 2（denoise）：humBase 为 off 且 wind 为 false：没有可执行的降噪项"
jr PIPE-PREVAL-004 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/prews\",\"steps\":[{\"cmd\":\"denoise\",\"humBase\":\"61\"},{\"cmd\":\"pitch\",\"semi\":2.0}]}" \
    ok=false code=E_PARAMS num:error.step=1 str:error.stepCmd=denoise msg-contains=humBase
# 模型路径不存在（步 2）：E_MODEL 同样在开工前拦截（文件存在性属 staticCheck 静态段）
jr PIPE-PREVAL-005 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/prews\",\"steps\":[{\"cmd\":\"transcode\"},{\"cmd\":\"aiDenoise\",\"model\":\"/nonexistent.onnx\"}]}" \
    ok=false code=E_MODEL str:error.message="步骤 2（aiDenoise）：找不到模型 /nonexistent.onnx" num:error.step=2
# 零执行实证：工作区根无任何工作区目录、源 sha 不变
ex PIPE-PREVAL-006 test -z "$(ls -A "$W/prews" 2>/dev/null)"
ex PIPE-PREVAL-007 test "$PRE_SHA" = "$(sha256sum "$W/st.wav" | cut -d' ' -f1)"

echo "== PIPE-FAIL（部分失败：步 2 坏模型 → 归属 + 已提交步保留 + 无残迹 + 重跑起新号）=="
# 垃圾字节模型：预校验放行（文件在盘）、步 2 引擎 setup 失败 → E_MODEL 出在链中
head -c 4096 /dev/urandom > "$W/garbage.onnx"
jr PIPE-FAIL-001 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/failws\",\"steps\":[{\"cmd\":\"transcode\"},{\"cmd\":\"aiDenoise\",\"model\":\"$W/garbage.onnx\"}]}" \
    ok=false cmd=pipeline code=E_MODEL msg-contains="步骤 2（aiDenoise）：" num:error.step=2 str:error.stepCmd=aiDenoise absent:result
FAIL_DIR=$(ls -d "$W/failws"/*/ 2>/dev/null | head -1)
[ -n "$FAIL_DIR" ]; ck $? "PIPE-FAIL-002 工作区目录在（步 1 已登记）"
wsjson PIPE-FAIL-003 "${FAIL_DIR%/}" "d['head']==1 and len(d['versions'])==1 and d['versions'][0]['cmd']=='transcode' and d['versions'][0]['pipeline']=={'index':1,'total':2}"
# 失败步版本目录无残迹（discard 兜底）
ex PIPE-FAIL-004 test -z "$(ls -d "$FAIL_DIR"v002* 2>/dev/null)"
FAIL_V1=$(python3 -c "import json,sys; d=json.load(open('${FAIL_DIR%/}/workspace.json')); print(d['versions'][0]['output']['path'])")
# v001 保留且可独立复用：对 v001 产物跑单动词成功（spec §6.5）
jr PIPE-FAIL-005 0 "{\"v\":1,\"cmd\":\"pitch\",\"input\":\"${FAIL_DIR%/}/$FAIL_V1\",\"workspace\":\"$W/failws\",\"semi\":2.0}" \
    ok=true num:result.workspace.version=2
# 失败后重跑整链：v003 起新号正常（孤儿不复用、号不回卷）
jr PIPE-FAIL-006 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/failws\",\"steps\":[{\"cmd\":\"transcode\"},{\"cmd\":\"aiDenoise\",\"model\":\"$W/garbage.onnx\"}]}" \
    ok=false num:error.step=2
wsjson PIPE-FAIL-007 "${FAIL_DIR%/}" "d['head']==3 and [v['version'] for v in d['versions']]==[1,2,3] and d['versions'][2]['cmd']=='transcode'"
# 维度错位模型（wp-1.0 round-2 配方）：setup 全过、首推理抛错 → E_ENGINE 同样带步骤归属
if python3 -c 'import onnx' 2>/dev/null; then
    python3 "$CRAFT" "$W/mismatch.onnx" >/dev/null 2>&1
    jr PIPE-FAIL-008 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/failws2\",\"steps\":[{\"cmd\":\"transcode\"},{\"cmd\":\"aiDenoise\",\"model\":\"$W/mismatch.onnx\"}]}" \
        ok=false code=E_ENGINE msg-contains="步骤 2（aiDenoise）：" num:error.step=2 str:error.stepCmd=aiDenoise
else
    skip "PIPE-FAIL-008 维度错位模型（缺 python3-onnx）"
fi

echo "== PIPE-NEG（schema 负例：steps 形态/链级键/内置动词/缺 workspace，逐条消息断言）=="
jr PIPE-NEG-001 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"steps\":[{\"cmd\":\"transcode\"},{\"cmd\":\"pitch\",\"semi\":2.0}]}" \
    ok=false cmd=pipeline code=E_PARAMS str:error.message="pipeline 必须带 workspace" absent:result
jr PIPE-NEG-002 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/negws\"}" \
    ok=false code=E_PARAMS str:error.message="steps 缺失"
jr PIPE-NEG-003 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/negws\",\"steps\":\"nope\"}" \
    ok=false code=E_PARAMS str:error.message="steps 必须是数组"
jr PIPE-NEG-004 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/negws\",\"steps\":[]}" \
    ok=false code=E_PARAMS str:error.message="steps 至少需要 2 步"
jr PIPE-NEG-005 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/negws\",\"steps\":[{\"cmd\":\"transcode\"}]}" \
    ok=false code=E_PARAMS str:error.message="steps 至少需要 2 步"
jr PIPE-NEG-006 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/negws\",\"steps\":[{\"cmd\":\"transcode\"},42]}" \
    ok=false code=E_PARAMS str:error.message="steps[1] 必须是对象"
jr PIPE-NEG-007 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/negws\",\"steps\":[{\"semi\":2.0},{\"cmd\":\"transcode\"}]}" \
    ok=false code=E_PARAMS str:error.message="steps[0] 缺少 cmd"
jr PIPE-NEG-008 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/negws\",\"steps\":[{\"cmd\":7},{\"cmd\":\"transcode\"}]}" \
    ok=false code=E_PARAMS str:error.message="steps[0] 的 cmd 必须是字符串"
jr PIPE-NEG-009 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/negws\",\"steps\":[{\"cmd\":\"probe\"},{\"cmd\":\"transcode\"}]}" \
    ok=false code=E_PARAMS str:error.message="steps[0] 的 cmd 不可链式：probe（仅引擎动词可入链）"
jr PIPE-NEG-010 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/negws\",\"steps\":[{\"cmd\":\"extractAudio\"},{\"cmd\":\"transcode\"}]}" \
    ok=false code=E_PARAMS msg-contains="不可链式：extractAudio"
jr PIPE-NEG-011 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/negws\",\"steps\":[{\"cmd\":\"modelList\"},{\"cmd\":\"transcode\"}]}" \
    ok=false code=E_PARAMS msg-contains="不可链式：modelList"
# 嵌套自链拒收（递归深度有界为 1，D2/D9）
jr PIPE-NEG-012 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/negws\",\"steps\":[{\"cmd\":\"pipeline\"},{\"cmd\":\"transcode\"}]}" \
    ok=false code=E_PARAMS msg-contains="不可链式：pipeline"
jr PIPE-NEG-013 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/negws\",\"steps\":[{\"cmd\":\"transcode\",\"input\":\"$W/other.wav\"},{\"cmd\":\"transcode\"}]}" \
    ok=false code=E_PARAMS str:error.message="steps[0] 未知字段：input"
jr PIPE-NEG-014 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/negws\",\"steps\":[{\"cmd\":\"transcode\"},{\"cmd\":\"transcode\",\"output\":\"$W/x.wav\"}]}" \
    ok=false code=E_PARAMS str:error.message="steps[1] 未知字段：output"
jr PIPE-NEG-015 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/negws\",\"steps\":[{\"cmd\":\"transcode\"},{\"cmd\":\"transcode\",\"workspace\":\"$W/negws2\"}]}" \
    ok=false code=E_PARAMS str:error.message="steps[1] 未知字段：workspace"
jr PIPE-NEG-016 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/negws\",\"steps\":[{\"cmd\":\"transcode\",\"v\":1},{\"cmd\":\"transcode\"}]}" \
    ok=false code=E_PARAMS str:error.message="steps[0] 未知字段：v"
jr PIPE-NEG-017 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/negws\",\"steps\":[{\"cmd\":\"transcode\"},{\"cmd\":\"pitch\",\"semi\":2.0,\"bogus\":1}]}" \
    ok=false code=E_PARAMS str:error.message="steps[1] 未知字段：bogus"
# 信封级 output（allowsOutput=false）：白名单先于互斥拒收（spec §6.6 写「互斥违例」，实测口径为此，round-1 记偏差）
jr PIPE-NEG-018 1 "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/st.wav\",\"workspace\":\"$W/negws\",\"output\":\"$W/x.wav\",\"steps\":[{\"cmd\":\"transcode\"},{\"cmd\":\"transcode\"}]}" \
    ok=false code=E_PARAMS msg-contains="未知字段：output"
# schema 段错误无步骤归属键（归属键只随 stepError 产生）
python3 -c "import json; d=json.load(open('$W/PIPE-NEG-006.resp')); assert 'step' not in d['error'] and 'stepCmd' not in d['error']"
ck $? "PIPE-NEG-019 schema 错误不带 step/stepCmd（归属键只随步骤级错误）"

echo "== PIPE-CANCEL/PROG（取消与进度：探针直调 execute 的 Ctx 注入面，四相）=="
echo "== 探针编译（pipe-cancel-probe.cpp，链主树 libengine.a；陈旧库闸同 gui-ws）=="
cmake --build "$BUILD" >/dev/null || { echo "FAIL: 增量构建失败"; exit 1; }
PKGS="Qt6Core Qt6Network libavformat libavcodec libavutil libswresample soundtouch"
g++ -std=c++20 -O2 "$S/pipe-cancel-probe.cpp" -o "$W/pipe-cancel-probe" -I"$(pwd)/src" \
    $(pkg-config --cflags $PKGS) "$BUILD/libengine.a" \
    $(pkg-config --libs $PKGS) "$(pwd)/third_party/ort/linux/lib/libonnxruntime.so" \
    -Wl,-rpath,"$(pwd)/third_party/ort/linux/lib" \
    || { echo "FAIL: 探针编译失败"; exit 1; }
PROBE="$W/pipe-cancel-probe"
run_probe() { local ph=$1 ws=$2; rm -rf "$ws"; QT_QPA_PLATFORM=offscreen "$PROBE" "$ph" "$W/st.wav" "$ws" >"$W/cancel-$ph.out" 2>/dev/null; }
# 相位一 cancel-pre：标志预置 → 步 1 开工前取消，「已取消」逐字（不缀前缀，GUI 静默特判依赖）
run_probe cancel-pre "$W/cw-pre"
python3 - "$W/cancel-cancel-pre.out" <<'PYEOF'
import json, sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
resp = json.loads([l for l in lines if l.startswith("RESP ")][0][5:])
assert resp["ok"] is False and resp["error"]["code"] == "E_CANCELLED", resp
assert resp["error"]["message"] == "已取消", resp["error"]["message"]
assert resp["error"]["step"] == 1 and resp["error"]["stepCmd"] == "denoise", resp
assert not any(l.startswith("PROG ") and len(l) > 5 for l in lines), lines
PYEOF
ck $? "PIPE-CAN-001 cancel-pre（E_CANCELLED/「已取消」逐字/step=1/零进度）"
# 开工前取消：连工作区目录都不建
ex PIPE-CAN-002 test ! -e "$W/cw-pre"
# 相位二 cancel-mid：首个进度回调置位 → 步 1 块循环检查点取消
run_probe cancel-mid "$W/cw-mid"
python3 - "$W/cancel-cancel-mid.out" <<'PYEOF'
import json, sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
resp = json.loads([l for l in lines if l.startswith("RESP ")][0][5:])
assert resp["error"]["code"] == "E_CANCELLED" and resp["error"]["message"] == "已取消", resp
assert resp["error"]["step"] == 1 and resp["error"]["stepCmd"] == "denoise", resp
PYEOF
ck $? "PIPE-CAN-003 cancel-mid（step=1，首进度点即取消）"
# 版本目录已 discard，无残迹
ex PIPE-CAN-004 test -z "$(ls -d "$W/cw-mid"/*/v* 2>/dev/null)"
# 相位三 cancel-between：步 1 完成后步 2 开工前取消 → step=2 且 v001 保留（D5 部分失败保留已提交步）
run_probe cancel-between "$W/cw-bet"
python3 - "$W/cancel-cancel-between.out" <<'PYEOF'
import json, sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
resp = json.loads([l for l in lines if l.startswith("RESP ")][0][5:])
assert resp["error"]["code"] == "E_CANCELLED" and resp["error"]["message"] == "已取消", resp
assert resp["error"]["step"] == 2 and resp["error"]["stepCmd"] == "pitch", resp
prog = [int(x) for x in [l for l in lines if l.startswith("PROG ")][0][5:].split(",") if x]
# 步 1 收尾 100 的映射值 = (0+100)/2
assert prog and prog[-1] == 50, prog
PYEOF
ck $? "PIPE-CAN-005 cancel-between（step=2/「已取消」逐字/进度止于映射 50）"
BET_DIR=$(ls -d "$W/cw-bet"/*/ | head -1)
wsjson PIPE-CAN-006 "${BET_DIR%/}" "d['head']==1 and len(d['versions'])==1 and d['versions'][0]['pipeline']=={'index':1,'total':2}"
# 相位四 progress：三步链进度等分映射（D4/OQ-2(a)）——含步界值、单调、终值 100
run_probe progress "$W/cw-prog"
python3 - "$W/cancel-progress.out" <<'PYEOF'
import json, sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
resp = json.loads([l for l in lines if l.startswith("RESP ")][0][5:])
assert resp["ok"] is True, resp
prog = [int(x) for x in [l for l in lines if l.startswith("PROG ")][0][5:].split(",") if x]
assert prog[-1] == 100, prog
assert all(a <= b for a, b in zip(prog, prog[1:])), prog# 单调不回退
# 等分步界值：第 1 步区间上界 33（(0+99)/3）与第 2 步上界 66（(100+99)/3）都在序列上可观测
assert 33 in prog and 66 in prog, prog
PYEOF
ck $? "PIPE-PROG-001 进度等分映射（终值 100/单调/步界 33·66）"

echo "== PIPE-GUIE（GUI worker 壳：doPipeline 槽直调，附裁 1 的 forVerb=stepCmd 实证）=="
GPKGS="Qt6Core Qt6Widgets Qt6Network libavformat libavcodec libavutil libswresample soundtouch"
MOC=$(ls /usr/lib/qt6/libexec/moc 2>/dev/null || command -v moc)
[ -n "$MOC" ] || { echo "FAIL: 缺 moc（qt6-base-dev-tools）"; exit 1; }
"$MOC" src/service/mediaservice.h -o "$W/moc_mediaservice.cpp"
"$MOC" src/view/mainwidget.h -o "$W/moc_mainwidget.cpp"
g++ -std=c++20 -O2 "$S/pipe-gui-probe.cpp" src/view/mainwidget.cpp src/service/mediaservice.cpp \
    "$W/moc_mediaservice.cpp" "$W/moc_mainwidget.cpp" -o "$W/pipe-gui-probe" -I"$(pwd)/src" \
    $(pkg-config --cflags $GPKGS) "$BUILD/libengine.a" \
    $(pkg-config --libs $GPKGS) "$(pwd)/third_party/ort/linux/lib/libonnxruntime.so" \
    -Wl,-rpath,"$(pwd)/third_party/ort/linux/lib" \
    || { echo "FAIL: 探针编译失败"; exit 1; }
GUIPROBE="$W/pipe-gui-probe"
# bin 布局即 appDir 缺省解析的模型位（mdl-gui-probe 同型）：bin-clean 双模型在位 / bin-empty 无模型 /
# bin-garbage 降噪模型为垃圾字节（pre-check 放行、setup 失败 → 链中 E_MODEL）
for b in bin-clean bin-empty bin-garbage; do
    mkdir -p "$W/$b/models/denoiser" "$W/$b/models/separator"
    cp "$GUIPROBE" "$W/$b/probe"
done
cp "$GTCRN" "$W/bin-clean/models/denoiser/" ; cp "$UVR" "$W/bin-clean/models/separator/"
head -c 4096 /dev/urandom > "$W/bin-garbage/models/denoiser/gtcrn_simple.onnx"
run_gui() { local bin=$1 ph=$2 ws=$3; rm -rf "$ws"; QT_QPA_PLATFORM=offscreen "$W/$bin/probe" engine "$W/st.wav" "$ws" "$ph" >"$W/guie-$ph.out" 2>/dev/null; }
# ok-chain：「管线完成：<终产物路径>」（附裁 5）+ 进度终值 100 + 产物在盘
run_gui bin-clean ok-chain "$W/gw-ok"
python3 - "$W/guie-ok-chain.out" <<'PYEOF'
import json, sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
task = [l for l in lines if l.startswith("TASK ")][0]
assert " -> ok=1 msg=管线完成：" in task, task
prog = [int(x) for x in [l for l in lines if l.startswith("PROG ")][0].split(" : ", 1)[1].split(",") if x]
assert prog[-1] == 100, prog
path = task.split("msg=管线完成：", 1)[1]
import os; assert os.path.exists(path), path
missing = [l for l in lines if l.startswith("MISSING ")][0]
assert missing == "MISSING 0 for=", missing
PYEOF
ck $? "PIPE-GUIE-001 ok-chain（管线完成文案/进度 100/产物在盘/无 modelMissing）"
GUIE_OK_DIR=$(ls -d "$W/gw-ok"/*/ 2>/dev/null | head -1); GUIE_OK_DIR=${GUIE_OK_DIR%/}
wsjson PIPE-GUIE-002 "$GUIE_OK_DIR" "d['head']==2 and d['versions'][0]['pipeline']=={'index':1,'total':2}"
# missing-step1：壳内两级解析皆缺 → 锁定文案 + modelMissing(aiDenoise)
run_gui bin-empty missing-step1 "$W/gw-miss"
python3 - "$W/guie-missing-step1.out" <<'PYEOF'
import sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
task = [l for l in lines if l.startswith("TASK ")][0]
assert " -> ok=0 msg=找不到AI 降噪模型：" in task, task
missing = [l for l in lines if l.startswith("MISSING ")][0]
assert missing == "MISSING 1 for=aiDenoise", missing
PYEOF
ck $? "PIPE-GUIE-003 missing-step1（找不到模型文案 + modelMissing(aiDenoise)）"
# emid-chain：链中步 2 E_MODEL → forVerb 取 error.stepCmd=aiDenoise（附裁 1：响应 cmd=pipeline）
run_gui bin-garbage emid-chain "$W/gw-emid"
python3 - "$W/guie-emid-chain.out" <<'PYEOF'
import sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
task = [l for l in lines if l.startswith("TASK ")][0]
assert " -> ok=0 msg=步骤 2（aiDenoise）：" in task, task
missing = [l for l in lines if l.startswith("MISSING ")][0]
assert missing == "MISSING 1 for=aiDenoise", missing
PYEOF
ck $? "PIPE-GUIE-004 emid-chain（E_MODEL 步骤归属 + forVerb=aiDenoise 附裁 1）"
# emid-negative：E_FILE 失败不触发 modelMissing（结构化信号仅随 E_MODEL 的负例守卫）
run_gui bin-garbage emid-negative "$W/gw-neg"
python3 - "$W/guie-emid-negative.out" <<'PYEOF'
import sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
task = [l for l in lines if l.startswith("TASK ")][0]
assert "打不开文件：" in task and " -> ok=0 msg=" in task, task
missing = [l for l in lines if l.startswith("MISSING ")][0]
assert missing == "MISSING 0 for=", missing
PYEOF
ck $? "PIPE-GUIE-005 emid-negative（E_FILE 不触发 modelMissing）"
# cancel-between：GUI 语义取消 → 「已取消」逐字（mainwidget 特判兼容）+ v001 保留
run_gui bin-clean cancel-between "$W/gw-can"
python3 - "$W/guie-cancel-between.out" <<'PYEOF'
import sys
lines = open(sys.argv[1], encoding="utf-8").read().splitlines()
task = [l for l in lines if l.startswith("TASK ")][0]
assert task.endswith(" -> ok=0 msg=已取消"), task
PYEOF
ck $? "PIPE-GUIE-006 cancel-between（「已取消」逐字）"
GUIE_CAN_DIR=$(ls -d "$W/gw-can"/*/ 2>/dev/null | head -1); GUIE_CAN_DIR=${GUIE_CAN_DIR%/}
wsjson PIPE-GUIE-007 "$GUIE_CAN_DIR" "d['head']==1 and len(d['versions'])==1"

echo "== PIPE-MEM（内存抽测：30min 两步链峰值 = 逐步最大值而非和，NFR-P4/D1）=="
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=1800" -ac 2 -c:a pcm_s16le "$W/big.wav"
mem_peak() { # mem_peak <label> <请求JSON>：跑一次记峰值 RSS（KB），echo 数值
    /usr/bin/time -v "$VH" --json <<< "$2" >"$W/mem-$1.resp" 2>"$W/mem-$1.time"
    awk '/Maximum resident set size/ {print $NF}' "$W/mem-$1.time"
}
MEM_PIPE=$(mem_peak pipe "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/big.wav\",\"workspace\":\"$W/memws\",\"steps\":[{\"cmd\":\"denoise\",\"humBase\":\"60\"},{\"cmd\":\"pitch\",\"semi\":2.0}]}")
MEM_DEN=$(mem_peak den "{\"v\":1,\"cmd\":\"denoise\",\"input\":\"$W/big.wav\",\"workspace\":\"$W/memws2\",\"humBase\":\"60\"}")
MEM_PIT_IN=$(python3 -c "import json; print(json.load(open('$W/mem-den.resp'))['result']['output'])")
MEM_PIT=$(mem_peak pit "{\"v\":1,\"cmd\":\"pitch\",\"input\":\"$MEM_PIT_IN\",\"workspace\":\"$W/memws2\",\"semi\":2.0}")
MEM_SINGLE=$(( MEM_DEN > MEM_PIT ? MEM_DEN : MEM_PIT ))
echo "  峰值 RSS：管线两步链=${MEM_PIPE}KB / denoise=${MEM_DEN}KB / pitch=${MEM_PIT}KB（单动词最大=${MEM_SINGLE}KB）"
python3 -c "
import sys
pipe, single = $MEM_PIPE, $MEM_SINGLE
assert pipe <= single + 32768, f'管线峰值 {pipe}KB 显著超过单动词最大 {single}KB（+32MB 裕量内为同量级）'
assert pipe < 262144, f'管线峰值 {pipe}KB 超过 256MB 上限（30min f32 立体声整载入 ≈635MB，此线上方即整文件 regime）'
print(f'  同量级实证：管线 {pipe}KB ≤ 单动词最大 {single}KB + 32MB 且 < 256MB')"
ck $? "PIPE-MEM-001 管线峰值与最重单动词同量级（远低于整文件 regime）"
ex PIPE-MEM-002 cmp "$(python3 -c "import json; print(json.load(open('$W/mem-pipe.resp'))['result']['output'])")" \
    "$(python3 -c "import json; print(json.load(open('$W/mem-pit.resp'))['result']['output'])")"

if [ "$HAS_MODELS" = 1 ]; then
    echo "== PIPE-MEM 带模型链（120s aiDenoise→separate：GTCRN 会话零叠加，审查轮 P2-1 补面）=="
    ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=120" -ac 2 -c:a pcm_s16le "$W/mid.wav"
    MEM_MPIPE=$(mem_peak mpipe "{\"v\":1,\"cmd\":\"pipeline\",\"input\":\"$W/mid.wav\",\"workspace\":\"$W/memws3\",\"steps\":[{\"cmd\":\"aiDenoise\",\"model\":\"$GTCRN\"},{\"cmd\":\"separate\",\"model\":\"$UVR\"}]}")
    python3 -c "
import json
d = json.load(open('$W/mem-mpipe.resp', encoding='utf-8'))
assert d.get('ok') is True, d" # 防峰值断言空转（链失败提前退出 → 峰值虚低 → 同量级断言假绿）
    ck $? "PIPE-MEM-003 带模型链实跑成功（峰值断言前置防空转）"
    MEM_AI=$(mem_peak ai "{\"v\":1,\"cmd\":\"aiDenoise\",\"input\":\"$W/mid.wav\",\"workspace\":\"$W/memws4\",\"model\":\"$GTCRN\"}")
    MEM_SEP_IN=$(python3 -c "import json; print(json.load(open('$W/mem-ai.resp'))['result']['output'])")
    MEM_SEP=$(mem_peak sep "{\"v\":1,\"cmd\":\"separate\",\"input\":\"$MEM_SEP_IN\",\"workspace\":\"$W/memws4\",\"model\":\"$UVR\"}")
    MEM_MSINGLE=$(( MEM_AI > MEM_SEP ? MEM_AI : MEM_SEP ))
    echo "  峰值 RSS：模型两步链=${MEM_MPIPE}KB / aiDenoise=${MEM_AI}KB / separate=${MEM_SEP}KB（单动词最大=${MEM_MSINGLE}KB）"
    python3 -c "
import sys
mpipe, single = $MEM_MPIPE, $MEM_MSINGLE
assert mpipe <= single + 32768, f'模型链峰值 {mpipe}KB 显著超过单动词最大 {single}KB（+32MB 裕量内为同量级）'
assert mpipe < 2097152, f'模型链峰值 {mpipe}KB 超过 NFR-P4 2GiB 上限'
print(f'  同量级实证：模型链 {mpipe}KB ≤ 单动词最大 {single}KB + 32MB 且 < 2GiB（NFR-P4，GTCRN 会话随遍释放零叠加）')"
    ck $? "PIPE-MEM-004 带模型链峰值与最重单动词同量级（D1 模型会话零叠加 + NFR-P4）"
else
    skip "PIPE-MEM-003/004 带模型链内存（缺 third_party 模型，fetch-deps.sh 备齐后跑）"
fi

if [ "$FAIL" = 1 ]; then
    echo "== run-pipe FAIL（见上）=="
    exit 1
fi
if [ "$SKIPANY" = 1 ] && [ "${PARITY_REQUIRED:-0}" = 1 ]; then
    echo "== run-pipe 有 SKIP 且 PARITY_REQUIRED=1（CI 强制点）=="
    exit 1
fi
echo "== run-pipe 全绿${SKIPANY:+（有 SKIP，见上；本地降级跑）} =="
