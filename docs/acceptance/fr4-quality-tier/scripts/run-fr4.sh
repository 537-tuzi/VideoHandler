#!/usr/bin/env bash
# run-fr4.sh — FR-4 质量档验收编排（FR4 族 + GFR4 GUI 族，判据以 docs/acceptance/fr4-quality-tier/matrix.csv 为准）：
#   FR4-OK     aiDenoiseHq 三路基础通路（48k 直通 / 工作区版本化 / 44.1k 立体声重采样）
#   FR4-NEG    负例五连（未知字段 / 模型目录缺件 / output·workspace 互斥 / 原件只读 / 模型目录不存在）
#   FR4-EQV    零漂移锚（旧 aiDenoise 确定性 + 工作区命名面 + 缺模型报文逐字）
#   FR4-REG    编组面回归（pipeline 步 aiDenoiseHq + 版本标记；batch 单步模板降级；步骤词表负例）
#   FR4-MDL    模型管理面（目录表 dfn3 条目 / 本地 tar 导入两形态 / 垃圾包负例 / 真网下载分级 SKIP）
#   GFR4       GUI 面（fr4-gui-probe：engine 相两级缺省解析 + 质量档缺模型文案/modelMissing；
#              gui 相档位下拉缺省/持久化 + 七槽/管线/批处理三路组包同源取 aiVerb）
#   FR4-RES    资源面（RTF ≤0.5 + 2h 峰值 RSS ≤2GB，生产 CLI 实测，NFR-P4/L 红线）
# 前置：构建目录内 video_handler 就绪；third_party dfn3 目录（config.ini+3 onnx）与保留
#       DeepFilterNet3_onnx.tar.gz、gtcrn 模型在位（scripts/fetch-deps.sh 备齐）。
# ⚠ 大产物走磁盘卷：RES 2h 素材（691MB wav + 1.4GB f32 产物）+ MDL 下载副本会撞 /tmp 配额
#   （fr8 round-2 教训）——ci-smoke 以 TMPDIR=<构建目录>/.fr4-tmp 注入（MDLTMP 同型）；
#   本地直跑建议 export TMPDIR 到磁盘卷。
# 依赖：python3（stdlib）；探针编译 g++ + pkg-config + moc（gui-ws.sh 同款配方）。
# 用法：run-fr4.sh [构建目录=build-linux]
set -uo pipefail
cd "$(dirname "$0")/../../../.." # scripts→fr4-quality-tier→acceptance→docs→仓库根
[ -f CMakeLists.txt ] || { echo "FAIL: 未落到仓库根（$(pwd)）"; exit 1; }

BUILD="${1:-build-linux}"
S="$(cd "$(dirname "$0")" && pwd)" # 本目录（探针源码同层）
VH="$BUILD/video_handler"
DFN3DIR=third_party/models/denoiser/dfn3
DFN3TAR=third_party/models/denoiser/DeepFilterNet3_onnx.tar.gz
GTCRN=third_party/models/denoiser/gtcrn_simple.onnx
# 与 fetch-deps.sh / modelmanager 目录表同源的两把基准（「目录表与现实一致」闸，run-models 同型）
DFN3_TAR_SHA=c94d91f70911001c946e0fabb4aa9adc37045f45a03b56008cb0c8244cb63616
DFN3_TAR_SIZE=7983136
for f in "$VH" "$DFN3DIR/config.ini" "$DFN3DIR/enc.onnx" "$DFN3DIR/erb_dec.onnx" \
         "$DFN3DIR/df_dec.onnx" "$DFN3TAR" "$GTCRN"; do
    [ -e "$f" ] || { echo "FAIL: 缺 $f（先构建 / 跑 scripts/fetch-deps.sh）"; exit 1; }
done

JC=docs/acceptance/fr24-command-layer/scripts/json_check.py
W=$(mktemp -d)
FAIL=0 SKIPANY=
trap 'rm -rf "$W"' EXIT

# ---- 助手（run-models 同型） ----

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

nopart() { # <id> <目录>：目录树内无 .part 残留
    if find "$2" -name '*.part' -print -quit 2>/dev/null | grep -q .; then
        echo "  FAIL $1: .part 残留"; FAIL=1
    else echo "  PASS $1"; fi
}

pyex() { # <id> <py表达式>：python3 -c 断言（表达式为真退出 0；可读 $W/ 下文件拼路径）
    local id=$1 expr=$2
    if python3 -c "$expr"; then echo "  PASS $id"; else echo "  FAIL $id"; FAIL=1; fi
}

# ---- 素材 ----
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=5" -ac 1 -c:a pcm_s16le "$W/p48m.wav"
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=3" -ac 2 -c:a pcm_s16le "$W/p44s.wav"
cp "$W/p48m.wav" "$W/p48m-b.wav" # 批处理第二件（同内容异路径，清单去重按绝对路径）

# =============================================================
echo "== FR4-OK：aiDenoiseHq 基础通路（48k 直通 / 工作区 / 44.1k 重采样） =="

jr FR4-OK-001 0 \
    "{\"cmd\":\"aiDenoiseHq\",\"input\":\"$W/p48m.wav\",\"output\":\"$W/ok1.wav\",\"model\":\"$PWD/$DFN3DIR\"}" \
    ok=true num:result.durationSec=5 bool:result.engineResampled=false \
    num:result.outputSpec.sampleRate=48000 num:result.outputSpec.channels=1 \
    str:result.outputSpec.format=pcm_f32le absent:result.workspace exists:result.output
ex FR4-OK-001z test "$(ffprobe -v error -select_streams a:0 -show_entries stream=codec_name,sample_rate,channels -of csv=p=0 "$W/ok1.wav")" = "pcm_f32le,48000,1"
ex FR4-OK-001d test "$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$W/ok1.wav")" = "5.000000"

jr FR4-OK-002 0 \
    "{\"cmd\":\"aiDenoiseHq\",\"input\":\"$W/p48m.wav\",\"workspace\":\"$W/ws-ok\",\"model\":\"$PWD/$DFN3DIR\"}" \
    ok=true str:result.workspace.root="$W/ws-ok" num:result.workspace.version=1 \
    contains:result.output=v001_aiDenoiseHq contains:result.output=_aihq.wav exists:result.output
pyex FR4-OK-002z "
import json,glob
rec=json.load(open(glob.glob('$W/ws-ok/*/workspace.json')[0]))['versions'][0]
assert rec['cmd']=='aiDenoiseHq', rec
assert sorted(rec)== ['cmd','input','model','output','params','parentVersion','time','version'], sorted(rec)
assert 'pipeline' not in rec and 'batch' not in rec, rec
assert rec['model']['path'].endswith('dfn3'), rec['model']" # 版本记录键面零漂移（无管线/批标记）
ex FR4-OK-002f sh -c 'ls "$1"/ws-ok/*/v001_aiDenoiseHq/*_aihq.wav >/dev/null' _ "$W"

jr FR4-OK-003 0 \
    "{\"cmd\":\"aiDenoiseHq\",\"input\":\"$W/p44s.wav\",\"output\":\"$W/ok3.wav\",\"model\":\"$PWD/$DFN3DIR\"}" \
    ok=true num:result.durationSec=3 bool:result.engineResampled=true \
    num:result.outputSpec.sampleRate=44100 num:result.outputSpec.channels=2 \
    num:result.sourceSpec.sampleRate=44100 absent:result.workspace

# =============================================================
echo "== FR4-NEG：负例五连（信封/参数/模型/红线） =="

IN_SHA=$(sha256sum "$W/p48m.wav" | cut -d' ' -f1) # 原件只读红线锚（终闸 FR4-998 复核）
mkdir -p "$W/dfn3-broken" && cp "$DFN3DIR"/config.ini "$DFN3DIR"/enc.onnx "$DFN3DIR"/erb_dec.onnx "$W/dfn3-broken/" # 缺 df_dec.onnx
jr FR4-NEG-001 1 \
    "{\"cmd\":\"aiDenoiseHq\",\"input\":\"$W/p48m.wav\",\"output\":\"$W/n1.wav\",\"model\":\"$PWD/$DFN3DIR\",\"bogusField\":1}" \
    ok=false code=E_PARAMS msg-contains=未知字段：bogusField absent:result
jr FR4-NEG-002 1 \
    "{\"cmd\":\"aiDenoiseHq\",\"input\":\"$W/p48m.wav\",\"output\":\"$W/n2.wav\",\"model\":\"$W/dfn3-broken\"}" \
    ok=false code=E_MODEL msg-contains="模型目录不完整（缺 df_dec.onnx）" absent:result
mkdir -p "$W/dfn3-nocfg" && cp "$DFN3DIR"/enc.onnx "$DFN3DIR"/erb_dec.onnx "$DFN3DIR"/df_dec.onnx "$W/dfn3-nocfg/" # 缺 config.ini
jr FR4-NEG-002m 1 \
    "{\"cmd\":\"aiDenoiseHq\",\"input\":\"$W/p48m.wav\",\"output\":\"$W/n2m.wav\",\"model\":\"$W/dfn3-nocfg\"}" \
    ok=false code=E_MODEL msg-contains="模型目录不完整（缺 config.ini）" absent:result
mkdir -p "$W/dfn3-noenc" && cp "$DFN3DIR"/config.ini "$DFN3DIR"/erb_dec.onnx "$DFN3DIR"/df_dec.onnx "$W/dfn3-noenc/" # 缺 enc.onnx
jr FR4-NEG-002e 1 \
    "{\"cmd\":\"aiDenoiseHq\",\"input\":\"$W/p48m.wav\",\"output\":\"$W/n2e.wav\",\"model\":\"$W/dfn3-noenc\"}" \
    ok=false code=E_MODEL msg-contains="模型目录不完整（缺 enc.onnx）" absent:result
jr FR4-NEG-003 1 \
    "{\"cmd\":\"aiDenoiseHq\",\"input\":\"$W/p48m.wav\",\"workspace\":\"$W/ws-n3\",\"output\":\"$W/n3.wav\",\"model\":\"$PWD/$DFN3DIR\"}" \
    ok=false code=E_PARAMS msg-contains="output 与 workspace 不能同给" absent:result
jr FR4-NEG-004 1 \
    "{\"cmd\":\"aiDenoiseHq\",\"input\":\"$W/p48m.wav\",\"output\":\"$W/p48m.wav\",\"model\":\"$PWD/$DFN3DIR\"}" \
    ok=false code=E_PARAMS msg-contains="output 不能与 input 相同（原件只读）" absent:result
jr FR4-NEG-005 1 \
    "{\"cmd\":\"aiDenoiseHq\",\"input\":\"$W/p48m.wav\",\"output\":\"$W/n5.wav\",\"model\":\"$W/nosuchdir\"}" \
    ok=false code=E_MODEL msg-contains=找不到模型 absent:result
ex FR4-NEG-noart sh -c 'test "$(ls "$1"/n*.wav 2>/dev/null | wc -l)" = 0' _ "$W" # 负例零产物（失败删半成品红线）

# =============================================================
echo "== FR4-EQV：零漂移锚（旧 aiDenoise/GTCRN 面不因 FR-4 改动） =="

"$VH" --ai-denoise "$W/p48m.wav" "$W/eq1a.wav" "$GTCRN"
"$VH" --ai-denoise "$W/p48m.wav" "$W/eq1b.wav" "$GTCRN"
ex FR4-EQV-001 cmp "$W/eq1a.wav" "$W/eq1b.wav" # 同输入两次逐字节一致（确定性未漂移）
jr FR4-EQV-002 0 \
    "{\"cmd\":\"aiDenoise\",\"input\":\"$W/p48m.wav\",\"workspace\":\"$W/ws-eqv\",\"model\":\"$PWD/$GTCRN\"}" \
    ok=true contains:result.output=v001_aiDenoise contains:result.output=_ai.wav absent:result.workspace.pipeline
ex FR4-EQV-002f sh -c 'ls "$1"/ws-eqv/*/v001_aiDenoise/*_ai.wav >/dev/null' _ "$W" # 旧后缀/目录名面不变
jr FR4-EQV-003 1 \
    "{\"cmd\":\"aiDenoise\",\"input\":\"$W/p48m.wav\",\"output\":\"$W/e3.wav\",\"model\":\"$W/none.onnx\"}" \
    ok=false code=E_MODEL "str:error.message=找不到模型 $W/none.onnx" absent:result # 报文逐字锁定

# =============================================================
echo "== FR4-REG：编组面回归（pipeline/batch 对新动词的承接） =="

jr FR4-REG-001 0 \
    "{\"cmd\":\"pipeline\",\"input\":\"$W/p48m.wav\",\"workspace\":\"$W/ws-reg1\",\"steps\":[{\"cmd\":\"aiDenoiseHq\",\"model\":\"$PWD/$DFN3DIR\"},{\"cmd\":\"pitch\",\"semi\":2.0}]}" \
    ok=true contains:result.output=v002_pitch exists:result.output
ex FR4-REG-001f sh -c 'ls "$1"/ws-reg1/*/v001_aiDenoiseHq/ >/dev/null' _ "$W"
printf '%s' "{\"cmd\":\"wsInfo\",\"input\":\"$W/p48m.wav\",\"root\":\"$W/ws-reg1\"}" | "$VH" --json >"$W/reg1.info"
pyex FR4-REG-001z "
import json
r=json.load(open('$W/reg1.info'))['result']['versions']
assert [v['cmd'] for v in r]==['aiDenoiseHq','pitch'], r
assert r[0]['pipeline']=={'index':1,'total':2} and r[1]['pipeline']=={'index':2,'total':2}, r
assert all('batch' not in v for v in r), r" # 管线版本标记双步在位（步骤 1 起人读计数）、无批标记

jr FR4-REG-002 0 \
    "{\"cmd\":\"batch\",\"inputs\":[\"$W/p48m.wav\",\"$W/p48m-b.wav\"],\"workspace\":\"$W/ws-reg2\",\"steps\":[{\"cmd\":\"aiDenoiseHq\",\"model\":\"$PWD/$DFN3DIR\"}]}" \
    ok=true num:result.total=2 num:result.okCount=2 num:result.items.0.ok=1 \
    contains:result.items.0.output=_aihq.wav contains:result.items.1.output=_aihq.wav
printf '%s' "{\"cmd\":\"wsInfo\",\"input\":\"$W/p48m.wav\",\"root\":\"$W/ws-reg2\"}" | "$VH" --json >"$W/reg2.info"
pyex FR4-REG-002z "
import json
r=json.load(open('$W/reg2.info'))['result']['versions']
assert r[0]['cmd']=='aiDenoiseHq' and r[0]['batch']=={'index':1,'total':2}, r
assert 'pipeline' not in r[0], r" # 单步模板降级单动词子请求：无管线标记、批标记在位（件 1 起人读计数）

jr FR4-REG-003 1 \
    "{\"cmd\":\"pipeline\",\"input\":\"$W/p48m.wav\",\"workspace\":\"$W/ws-reg3\",\"steps\":[{\"cmd\":\"noSuchEngine\"},{\"cmd\":\"pitch\",\"semi\":1.0}]}" \
    ok=false code=E_PARAMS msg-contains=不可链式 absent:result
ex FR4-REG-003z test ! -e "$W/ws-reg3" # 整链预校验：一步不合法零执行零目录

# =============================================================
echo "== FR4-MDL：模型管理面（目录表/导入/负例；真网下载分级 SKIP） =="

jr FR4-MDL-001 0 '{"cmd":"modelList","root":"'"$W"'/lib1"}' ok=true
pyex FR4-MDL-001z "
import json
ms={m['id']:m for m in json.load(open('$W/FR4-MDL-001.resp'))['result']['models']}
d=ms['dfn3']; g=ms['gtcrn']
assert d['for']=='aiDenoiseHq' and d['status']=='available', d
assert d['sizeBytes']==$DFN3_TAR_SIZE and d['sha256']=='$DFN3_TAR_SHA', d
assert 'MIT' in d['license'], d
assert g['for']=='aiDenoise' and g['status']=='available' and g['sizeBytes']==535638, g" # dfn3 条目三视角齐 + gtcrn 条目零漂移

jr FR4-MDL-002 0 \
    "{\"cmd\":\"modelImport\",\"path\":\"$PWD/$DFN3TAR\",\"id\":\"dfn3\",\"root\":\"$W/lib2\"}" \
    ok=true contains:result.path=dfn3 exists:result.path
pyex FR4-MDL-002z "
import os,json
d='$W/lib2/denoiser/dfn3'
for f in ('config.ini','enc.onnx','erb_dec.onnx','df_dec.onnx'): assert os.path.isfile(f'{d}/{f}'), f
m=json.load(open('$W/lib2/models.json'))# 顶层即条目数组（登记面；status 是 modelList 视角概念）
e=[x for x in m if x['id']=='dfn3'][0]
assert e['for']=='aiDenoiseHq' and e['path'].endswith('denoiser/dfn3'), e"
printf '%s' '{"cmd":"modelList","root":"'"$W"'/lib2"}' | "$VH" --json >"$W/mdl2.list"
pyex FR4-MDL-002v "
import json
d=[m for m in json.load(open('$W/mdl2.list'))['result']['models'] if m['id']=='dfn3'][0]
assert d['status']=='installed', d" # 三视角合并：登记后 modelList 视角翻 installed

cp "$DFN3TAR" "$W/myhq.tar.gz"
jr FR4-MDL-003 0 \
    "{\"cmd\":\"modelImport\",\"path\":\"$W/myhq.tar.gz\",\"for\":\"aiDenoiseHq\",\"root\":\"$W/lib3\"}" \
    ok=true
pyex FR4-MDL-003z "
import json
m=json.load(open('$W/lib3/models.json'))
e=[x for x in m if x['id']=='myhq'][0]
assert e['for']=='aiDenoiseHq' and 'unknown' in e['license'], e" # 自由导入：id=包名、许可 unknown（用户自行核验，§161）

head -c 65536 /dev/urandom >"$W/corrupt.tar.gz" # 非法 tar 字节流（free 导入无期望指纹 → tar 退出码非零）
jr FR4-MDL-004a 1 \
    "{\"cmd\":\"modelImport\",\"path\":\"$W/corrupt.tar.gz\",\"for\":\"aiDenoiseHq\",\"root\":\"$W/lib4\"}" \
    ok=false code=E_FILE msg-contains=解包模型失败 absent:result
mkdir -p "$W/junkdir" && echo garbage >"$W/junkdir/x.onnx" && tar -czf "$W/junk.tar.gz" -C "$W" junkdir # 合法 tar 但指纹不符（by-id 先比对目录表 sha）
jr FR4-MDL-004b 1 \
    "{\"cmd\":\"modelImport\",\"path\":\"$W/junk.tar.gz\",\"id\":\"dfn3\",\"root\":\"$W/lib4\"}" \
    ok=false code=E_MODEL msg-contains=模型指纹与目录条目不符 absent:result
nopart FR4-MDL-004p "$W/lib4"
ex FR4-MDL-004z test ! -e "$W/lib4/denoiser/dfn3" # 登记失败删产物不留孤儿（两种失败同归路）
# round-2 补面：合法 tar（四关键文件名齐）但内容垃圾 → 解包过、create 试开败（setup=权威校验器）
mkdir -p "$W/fakedfn3"
echo garbage >"$W/fakedfn3/config.ini"
head -c 1024 /dev/urandom >"$W/fakedfn3/enc.onnx"
head -c 1024 /dev/urandom >"$W/fakedfn3/erb_dec.onnx"
head -c 1024 /dev/urandom >"$W/fakedfn3/df_dec.onnx"
tar -czf "$W/fakedfn3.tar.gz" -C "$W" fakedfn3
jr FR4-MDL-004c 1 \
    "{\"cmd\":\"modelImport\",\"path\":\"$W/fakedfn3.tar.gz\",\"for\":\"aiDenoiseHq\",\"root\":\"$W/lib4\"}" \
    ok=false code=E_MODEL msg-contains=config.ini absent:result
ex FR4-MDL-004c2 test ! -e "$W/lib4/denoiser/fakedfn3" # 试开败删产物不留孤儿
nopart FR4-MDL-004c3 "$W/lib4"
ex FR4-MDL-004c4 sh -c 'test -z "$(find "$1/lib4" -name ".*-import-stage" -print -quit)"' _ "$W" # stage 暂存目录同样清理

# 真网下载（分级：离线 SKIP；md 面真网红线用本地库已覆盖，本组只证 dfn3 远端可达 + 落地一致）
DFN3_URL=https://raw.githubusercontent.com/Rikorose/DeepFilterNet/main/models/DeepFilterNet3_onnx.tar.gz
if curl -fsI --max-time 6 -o /dev/null "$DFN3_URL"; then
    jr FR4-MDL-005 0 \
        "{\"cmd\":\"modelDownload\",\"id\":\"dfn3\",\"root\":\"$W/lib5\"}" \
        ok=true contains:result.path=dfn3 exists:result.path
    pyex FR4-MDL-005z "
import json
m=json.load(open('$W/lib5/models.json'))
e=[x for x in m if x['id']=='dfn3'][0]
assert e['sha256']=='$DFN3_TAR_SHA' and e['path'].endswith('denoiser/dfn3'), e"
    nopart FR4-MDL-005p "$W/lib5"
    t0=$(date +%s%N)
    jr FR4-MDL-006 0 "{\"cmd\":\"modelDownload\",\"id\":\"dfn3\",\"root\":\"$W/lib5\"}" ok=true
    ex FR4-MDL-006z test $(( ($(date +%s%N) - t0) / 1000000 )) -lt 3000 # 幂等：登记指纹在位即信（秒回零网络）
else
    echo "  SKIP FR4-MDL-005/006（离线：探测 $DFN3_URL 不通）"; SKIPANY=1
fi

# =============================================================
echo "== GFR4：GUI 面（fr4-gui-probe：engine 相缺省解析/缺模型文案 + gui 相档位三路组包） =="

cmake --build "$BUILD" >/dev/null || { echo "FAIL: 增量构建失败（$BUILD）"; exit 1; }
MOC=$(ls /usr/lib/qt6/libexec/moc 2>/dev/null || command -v moc)
[ -n "$MOC" ] || { echo "FAIL: 缺 moc（qt6-base-dev-tools）"; exit 1; }
"$MOC" src/service/mediaservice.h -o "$W/moc_mediaservice.cpp"
"$MOC" src/view/mainwidget.h -o "$W/moc_mainwidget.cpp"
PKGS_GUI="Qt6Core Qt6Network Qt6Widgets libavformat libavcodec libavutil libswresample soundtouch"
g++ -std=c++20 -O2 "$S/fr4-gui-probe.cpp" src/view/mainwidget.cpp src/service/mediaservice.cpp \
    "$W/moc_mediaservice.cpp" "$W/moc_mainwidget.cpp" -o "$W/fr4-probe" -I"$(pwd)/src" \
    $(pkg-config --cflags $PKGS_GUI) "$BUILD/libengine.a" \
    $(pkg-config --libs $PKGS_GUI) "$(pwd)/third_party/ort/linux/lib/libonnxruntime.so" \
    -Wl,-rpath,"$(pwd)/third_party/ort/linux/lib" \
    || { echo "FAIL: GFR4 探针编译失败"; exit 1; }

# 库 fixture：CLI 预导入 dfn3（by-id 复用上面的 tar）供 hq-lib 相取库证真
mkdir -p "$W/lib-hq"
printf '%s' "{\"cmd\":\"modelImport\",\"path\":\"$PWD/$DFN3TAR\",\"id\":\"dfn3\",\"root\":\"$W/lib-hq\"}" | "$VH" --json >/dev/null
mkdir -p "$W/lib-empty" # 空库（在位目录零条目 → 两级解析皆缺）

# engine 相（FFExtractor 槽直调，探针 bin 旁无 models/ = appDir 无随包模型）
export QT_QPA_PLATFORM=offscreen
CFG_E="$W/cfg-eng"; mkdir -p "$CFG_E"
XDG_CONFIG_HOME="$CFG_E" "$W/fr4-probe" engine "$W/p48m.wav" "$W/lib-empty" "$W/ws-eng" hq-missing >"$W/gfr4-e1.out" 2>&1
XDG_CONFIG_HOME="$CFG_E" "$W/fr4-probe" engine "$W/p48m.wav" "$W/lib-hq" "$W/ws-eng" hq-lib >"$W/gfr4-e2.out" 2>&1
XDG_CONFIG_HOME="$CFG_E" "$W/fr4-probe" engine "$W/p48m.wav" "$W/lib-empty" "$W/ws-eng" fast-missing >"$W/gfr4-e3.out" 2>&1
pyex GFR4-101 "
o=open('$W/gfr4-e1.out',encoding='utf-8').read()
assert 'TASK hq-missing -> ok=0' in o and '找不到 AI 降噪（质量档）模型：' in o, o
assert 'MODELMISSING 1 aiDenoiseHq' in o, o" # 质量档缺模型：文案 + 结构化 forVerb
pyex GFR4-102 "
o=open('$W/gfr4-e2.out',encoding='utf-8').read()
assert 'TASK hq-lib -> ok=1' in o and 'MODELMISSING 0' in o, o
assert 'MODELPATH ' in o and '/lib-hq/denoiser/dfn3' in o, o" # 两级解析取库：版本记录 model.path 真值
pyex GFR4-103 "
o=open('$W/gfr4-e3.out',encoding='utf-8').read()
assert 'TASK fast-missing -> ok=0' in o and '找不到 AI 降噪模型：' in o, o
assert 'MODELMISSING 1 aiDenoise' in o, o" # 快速档零漂移锚：旧文案/旧 forVerb 逐字不变
# round-2 补面 GFR4-104：快速档（quality=false）库内 gtcrn 实跑——GUI 槽产物 vs CLI 旧薄壳逐字节
#（档位=快速时零漂移到产物面：两级解析取库（GTCRN installed 非 bundled）+ 同 execute 核心）
mkdir -p "$W/lib-fast"
printf '%s' "{\"cmd\":\"modelImport\",\"path\":\"$PWD/$GTCRN\",\"id\":\"gtcrn\",\"root\":\"$W/lib-fast\"}" | "$VH" --json >/dev/null
XDG_CONFIG_HOME="$CFG_E" "$W/fr4-probe" engine "$W/p48m.wav" "$W/lib-fast" "$W/ws-eng-fast" fast-lib >"$W/gfr4-e4.out" 2>&1 # 独立 ws 根：firstModel/OutputPath 取首个版本，须免受 hq-lib 相位记录干扰
pyex GFR4-104 "
o=open('$W/gfr4-e4.out',encoding='utf-8').read()
assert 'TASK fast-lib -> ok=1' in o and 'MODELMISSING 0' in o, o
assert '/lib-fast/denoiser/gtcrn_simple.onnx' in o, o" # 快速档取库成功（两级解析 + 任务 ok）
OUTP=$(sed -n 's/^MODELOUT //p' "$W/gfr4-e4.out")
"$VH" --ai-denoise "$W/p48m.wav" "$W/e4cli.wav" "$GTCRN"
ex GFR4-104b cmp "$OUTP" "$W/e4cli.wav" # GUI 槽（工作区产物）vs CLI 薄壳（direct 产物）逐字节一致

# gui 相（MainWidget 全链：XDG_CONFIG_HOME 隔离 QSettings）
CFG_G="$W/cfg-gui"; mkdir -p "$CFG_G"
XDG_CONFIG_HOME="$CFG_G" "$W/fr4-probe" gui "$W/p48m.wav" "$W/p48m-b.wav" "$W/ws-gui" "$W/lib-empty" >"$W/gfr4-g.out" 2>&1
pyex GFR4-201 "
o=open('$W/gfr4-g.out',encoding='utf-8').read()
assert 'TIERDEFAULT idx=0 texts=快速（GTCRN）|质量（DeepFilterNet3）' in o, o
assert 'TIERPERSIST settings=quality reloadIdx=1' in o, o" # 缺省快速 + 换档记忆跨实例还原
pyex GFR4-202 "
o=open('$W/gfr4-g.out',encoding='utf-8').read()
assert 'PATH7SLOT quality=1' in o, o
pipe=[l for l in o.splitlines() if l.startswith('PATHPIPE steps=') and 'FAST' not in l][0]
assert '\"cmd\":\"aiDenoiseHq\"' in pipe and '\"cmd\":\"aiDenoise\"' not in pipe, pipe
assert '\"cmd\":\"separate\"' in pipe, pipe
bat=[l for l in o.splitlines() if l.startswith('PATHBATCH ') and 'FAST' not in l][0]
assert 'inputs=2' in bat and '\"cmd\":\"aiDenoiseHq\"' in bat, bat" # 三路组包同源取 aiVerb()：质量档下七槽/管线步/批动作全走 aiDenoiseHq
pyex GFR4-203 "
o=open('$W/gfr4-g.out',encoding='utf-8').read()
for l in o.splitlines():
    if 'FAST' in l:
        assert '\"cmd\":\"aiDenoiseHq\"' not in l, l" # 切回快速档：三面回落旧动词（档位是动态状态，非一次性固化的组包参数）
pyex GFR4-204 "
o=open('$W/gfr4-g.out',encoding='utf-8').read()
assert 'MISSINGTEXT hq=AI 降噪（质量档）模型不可用，是否打开模型管理下载？' in o, o" # 质量档问询文案逐字（onModelMissing 映射，MIST 族 aiDenoiseHq 补面）
pyex GFR4-205 "
o=open('$W/gfr4-g.out',encoding='utf-8').read()
assert 'MISSINGTEXT fast=AI 降噪模型不可用，是否打开模型管理下载？' in o, o" # 快速档问询文案零漂移锚（旧映射逐字不变；「（质量档）」子串区分度实证）

# =============================================================
echo "== FR4-RES：资源面（生产 CLI 实测；RTF ≤0.5、2h 峰值 RSS ≤2GB——NFR-P4/L 红线） =="

ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=65" -ac 1 -c:a pcm_s16le "$W/r65.wav"
t0=$(date +%s%N)
printf '%s' "{\"cmd\":\"aiDenoiseHq\",\"input\":\"$W/r65.wav\",\"output\":\"$W/r65.out.wav\",\"model\":\"$PWD/$DFN3DIR\"}" | "$VH" --json >"$W/res1.resp"
rc=$?; t1=$(date +%s%N)
[ "$rc" = 0 ] && python3 "$JC" "$W/res1.resp" ok=true || { echo "  FAIL FR4-RES-001: 任务失败"; FAIL=1; }
RTF_MS=$(( (t1 - t0) / 1000000 ))
pyex FR4-RES-001 "
import sys
audio_ms=65000; wall_ms=$RTF_MS
print(f'  RTF = {wall_ms}/{audio_ms} = {wall_ms/audio_ms:.4f}')
assert wall_ms/audio_ms <= 0.5, wall_ms" # 门 NFR-L3（≤0.5，质量档实测余量见 round-1.md）

ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=48000:duration=7200" -ac 1 -c:a pcm_s16le "$W/r2h.wav"
printf '%s' "{\"cmd\":\"aiDenoiseHq\",\"input\":\"$W/r2h.wav\",\"output\":\"$W/r2h.out.wav\",\"model\":\"$PWD/$DFN3DIR\"}" \
    | /usr/bin/time -v -o "$W/res2.time" "$VH" --json >"$W/res2.resp" || { echo "  FAIL FR4-RES-002: 任务失败"; FAIL=1; }
RSS_KB=$(sed -n 's/.*Maximum resident set size (kbytes): //p' "$W/res2.time")
WALL_S=$(sed -n 's/.*Elapsed (wall clock) time (h:mm:ss or m:ss): //p' "$W/res2.time")
pyex FR4-RES-002 "
print('  2h 峰值 RSS = %d MiB（门 ≤2048）；wall = %s' % ($RSS_KB // 1024, '$WALL_S'))
assert $RSS_KB <= 2*1024*1024, $RSS_KB" # NFR-P4：严格流式，2h 峰值 RSS ≤2GB
ex FR4-RES-002d test "$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$W/r2h.out.wav" | cut -d. -f1)" = "7200" # 输出恒等长（无截断无丢帧）

# =============================================================
echo "== 完整性终闸（素材原件未被改写（原件只读红线）+ third_party 未被改动） =="
ex FR4-998 test "$(sha256sum "$W/p48m.wav" | cut -d' ' -f1)" = "$IN_SHA"
ex FR4-999 test "$(sha256sum "$DFN3TAR" | cut -d' ' -f1)" = "$DFN3_TAR_SHA"

if [ "$FAIL" = 1 ]; then
    echo "== run-fr4 FAIL（见上） =="
    exit 1
fi
if [ "$SKIPANY" = 1 ]; then
    echo "== run-fr4 全绿（有 SKIP，见上） =="
    exit 0
fi
echo "== run-fr4 全绿 =="
