#!/usr/bin/env bash
# run-models.sh — FR-8 模型管理验收编排（MDL 族 + 片 B GML 族，判据以 docs/acceptance/fr8-model-management/matrix.csv 为准）：
#   modelList 三视角合并 / modelDownload 幂等下载+sha256 校验 / Range 跨调用续传 / 失败注入负例 /
#   modelImport 两形态 / 信封分叉零漂移 / 取消语义（探针直调，CLI 单发无取消通道）；
#   GML 族（片 B）：两级缺省解析（engine 相槽直调）+ 对话框全链（gui 相 offscreen MainWidget 全链，
#   --chunk-sleep-ms 节流服务拉长传输窗供 busy/取消断言）。
# 全程本地 range_server.py（127.0.0.1），真网红线：不触外网。
# 前置完整性闸（fr-a4 片 B 空洞 PASS 教训）：third_party 两模型必须存在且 sha/size 与内置目录表一致——
# 目录表本身就是下载校验基准，fixture 坏 = 全部断言失真，故硬 FAIL 不降级。
# 依赖：python3（stdlib）；探针编译用 g++ + pkg-config（gui-ws.sh 同款配方）。
# 用法：run-models.sh [构建目录=build-linux]
set -uo pipefail
cd "$(dirname "$0")/../../../.." # scripts→fr8-model-management→acceptance→docs→仓库根
[ -f CMakeLists.txt ] || { echo "FAIL: 未落到仓库根（$(pwd)）"; exit 1; }

BUILD="${1:-build-linux}"
VH="$BUILD/video_handler"
GTCRN=third_party/models/denoiser/gtcrn_simple.onnx
UVR=third_party/models/separator/UVR-MDX-NET-Voc_FT.onnx
# 与 src/service/modelmanager.cpp 目录表同源的两把基准（也是「目录表与现实一致」闸）
GTCRN_SHA=e77603ac0c23dac3227dd2d7135b3a585cbee2679048aecfa886657d3ae1b534
GTCRN_SIZE=535638
UVR_SHA=534b2070fcc7df514b13ef660dc8cbb328679c2374d04354a5c42bb14ecce111
UVR_SIZE=66762490
for f in "$VH" "$GTCRN" "$UVR"; do
    [ -e "$f" ] || { echo "FAIL: 缺 $f（先构建 / 跑 scripts/fetch-deps.sh）"; exit 1; }
done

# ---- 助手 ----

sha_is() { # <id> <文件> <期望sha256> [nofail]：sha256 全量比对
    local got
    got=$(sha256sum "$2" | cut -d' ' -f1)
    if [ "$got" = "$3" ]; then echo "  PASS $1"; return 0; fi
    echo "  FAIL $1: sha256 不符（$got）"
    if [ "${4:-}" = nofail ]; then return 1; else FAIL=1; fi
}

nopart() { # <id> <目录>：目录树内无 .part 残留
    if find "$2" -name '*.part' -print -quit 2>/dev/null | grep -q .; then
        echo "  FAIL $1: .part 残留"; FAIL=1
    else
        echo "  PASS $1"
    fi
}

mj() { # <id> <模型库根> <py表达式(m=条目数组)> <期望值>：models.json 登记内容断言
    local id=$1 got
    got=$(python3 -c '
import json,sys
m = json.load(open(sys.argv[1]))
print(eval(sys.argv[2]))' "$2/models.json" "$3" 2>&1)
    if [ "$got" = "$4" ]; then echo "  PASS $id"; else
        echo "  FAIL $id: models.json $3 → '$got' ≠ '$4'"; FAIL=1; fi
}

jr() { # <id> <期望退出码> <请求JSON> [断言...]：跑一条 --json 用例并判定
    local id=$1 exprc=$2 req=$3; shift 3
    printf '%s' "$req" | "$VH" --json >"$W/$id.resp" 2>"$W/$id.err"
    local rc=$?
    local ok=1
    [ "$rc" = "$exprc" ] || { echo "  FAIL $id: 退出码 $rc ≠ 期望 $exprc"; ok=0; }
    python3 "$JC" "$W/$id.resp" "$@" || ok=0
    if [ "$ok" = 1 ]; then echo "  PASS $id"
    else [ -s "$W/$id.err" ] && sed 's/^/    stderr│ /' "$W/$id.err" # 失败回放 stderr（MDL-092 flake 教训：不留线索无法追因）
        FAIL=1; fi
}

ex() { # <id> <命令...>：附加 shell 级断言（退出码 0 即 PASS）
    local id=$1; shift
    if "$@"; then echo "  PASS $id"; else echo "  FAIL $id: 附加断言失败"; FAIL=1; fi
}

logn() { wc -l <"$1" 2>/dev/null || echo 0; } # 服务日志行数（幂等零请求断言）

# ---- 前置完整性闸 ----
sha_is "前置闸-gtcrn" "$GTCRN" "$GTCRN_SHA" nofail || exit 1
sha_is "前置闸-uvr" "$UVR" "$UVR_SHA" nofail || exit 1
[ "$(stat -c%s "$GTCRN")" = "$GTCRN_SIZE" ] || { echo "FAIL: gtcrn 尺寸 ≠ 目录表"; exit 1; }
[ "$(stat -c%s "$UVR")" = "$UVR_SIZE" ] || { echo "FAIL: uvr 尺寸 ≠ 目录表"; exit 1; }

S=docs/acceptance/fr8-model-management/scripts
JC=docs/acceptance/fr24-command-layer/scripts/json_check.py
UVR_REL="Politrees/UVR_resources/resolve/main/models/MDXNet/UVR-MDX-NET-Voc_FT.onnx"
W=$(mktemp -d)
FAIL=0 SKIPANY=

# ---- 本地服务群（同一真实 UVR 副本多行为；B 服务错误内容）----

declare -A SRV_PID SRV_PORT
start_srv() { # <id> <root> [注入开关...]
    local id=$1 root=$2; shift 2
    : >"$W/log.$id"
    python3 "$S/range_server.py" --root "$root" --port 0 --log "$W/log.$id" "$@" >"$W/ready.$id" 2>"$W/srverr.$id" &
    SRV_PID[$id]=$!
    for _ in $(seq 1 100); do [ -s "$W/ready.$id" ] && break; sleep 0.05; done
    SRV_PORT[$id]=$(awk '{print $2}' "$W/ready.$id" 2>/dev/null)
    [ -n "${SRV_PORT[$id]}" ] || { echo "FAIL: 服务 $id 未就绪（$(cat "$W/srverr.$id" 2>/dev/null)）"; exit 1; }
}
cleanup() {
    for id in "${!SRV_PID[@]}"; do kill "${SRV_PID[$id]}" 2>/dev/null; done
    [ "${VH_KEEP:-}" = 1 ] || rm -rf "$W" # VH_KEEP=1 保现场（MDL-092 间歇失败追因用；# 前必须空格）
}
trap cleanup EXIT

SRVROOT="$W/srv"; mkdir -p "$SRVROOT/$(dirname "$UVR_REL")"
cp "$UVR" "$SRVROOT/$UVR_REL"
SRVBAD="$W/srvBad"; mkdir -p "$SRVBAD/$(dirname "$UVR_REL")"
head -c 102400 /dev/urandom >"$SRVBAD/$UVR_REL"

start_srv A "$SRVROOT"                                 # 正常（ETag=sha256，Range→206）
start_srv B "$SRVBAD" --etag none                      # 内容错且无 ETag → 收尾 sha 校验兜底路径
start_srv C "$SRVROOT" --fail 404                      # HEAD/GET 恒 404
start_srv D "$SRVROOT" --fail 429                      # HEAD/GET 恒 429（限流文案）
start_srv F "$SRVROOT" --etag deadbeefdeadbeef         # ETag 与目录不符
start_srv G "$SRVROOT" --cutmid 200000                 # 每请求发 200000 字节掐断
start_srv I "$SRVROOT" --no206                         # 无视 Range 恒 200 全量
start_srv J "$SRVROOT" --failget 404                   # HEAD 正常、GET 恒 404
ep() { echo "http://127.0.0.1:${SRV_PORT[$1]}"; }

# 库根分区：LIB=DL 正路 + 状态翻转；LIBI=导入；LIBR=续传；LIBN=200 重写；LIBX=负例（专用，
# 已装库会触发幂等短路让负例变 ok，负例必须走从未下载过的根）
LIB="$W/lib"; LIBI="$W/libi"; LIBR="$W/libr"; LIBN="$W/libn"; LIBX="$W/libx"; LIBC="$W/libc"
IN="$W/in"; mkdir -p "$IN"
cp "$GTCRN" "$IN/gtcrn-copy.onnx"; cp "$GTCRN" "$IN/my-gtcrn.onnx"
head -c 102400 /dev/urandom >"$IN/random.onnx"
IN_SHAS=$(sha256sum "$IN"/*.onnx "$GTCRN" "$UVR" | sha256sum | cut -d' ' -f1) # 完整性终闸基准

# ---- 探针（取消语义 + bundled 视角；gui-ws.sh 同款编译配方）----

PKGS="Qt6Core Qt6Network libavformat libavcodec libavutil libswresample soundtouch"
g++ -std=c++20 -O2 "$S/mdl-cancel-probe.cpp" -o "$W/probe" -I"$(pwd)/src" \
    $(pkg-config --cflags $PKGS) "$BUILD/libengine.a" \
    $(pkg-config --libs $PKGS) "$(pwd)/third_party/ort/linux/lib/libonnxruntime.so" \
    -Wl,-rpath,"$(pwd)/third_party/ort/linux/lib" \
    || { echo "FAIL: 探针编译失败"; exit 1; }

# =============================================================
echo "== MDL-LIST：三视角合并（目录 ∪ models.json ∪ 磁盘） =="

jr MDL-001 0 "{\"v\":1,\"cmd\":\"modelList\",\"root\":\"$LIB\"}" \
    ok=true v=1 cmd=modelList str:result.root=$LIB \
    str:result.models.0.id=gtcrn bool:result.models.0.bundled=false \
    str:result.models.0.status=available str:result.models.0.license='MIT（倾向，发版前官方书面确认）' \
    str:result.models.1.id=uvr-mdx-voc-ft str:result.models.1.status=available \
    str:result.models.1.license='UVR 生态：免费再分发，须标注来源' \
    absent:result.models.0.path absent:result.models.1.path \
    keys:result.models.0=bundled,for,id,license,name,sha256,sizeBytes,status
ex MDL-002 test ! -e "$LIB" # 查询不建目录不落盘
jr MDL-003 0 '{"v":1,"cmd":"modelList"}' \
    ok=true str:result.root="$HOME/VideoHandler-Models" \
    str:result.models.0.id=gtcrn str:result.models.1.id=uvr-mdx-voc-ft \
    str:result.models.0.license='MIT（倾向，发版前官方书面确认）' # 缺省根（只断目录常量；status 随机器库存态，不断）
jr MDL-004 1 '{"v":1,"cmd":"modelList","root":5}' \
    ok=false code=E_PARAMS msg-contains=root absent:result
jr MDL-005 1 '{"v":1,"cmd":"modelList","root":"/tmp","bogus":1}' \
    ok=false code=E_PARAMS msg-contains=未知字段 absent:result
# bundled 视角（appDir 可控需探针）：随包 gtcrn → installed+bundled
BIN="$W/bin"; mkdir -p "$BIN/models/denoiser"; cp "$GTCRN" "$BIN/models/denoiser/gtcrn_simple.onnx"
cp "$W/probe" "$BIN/probe"
"$BIN/probe" bundled "$W/libprobe" >"$W/bundled.json" 2>/dev/null
if python3 "$JC" "$W/bundled.json" \
    str:models.0.id=gtcrn bool:models.0.bundled=true str:models.0.status=installed \
    str:models.0.path="$BIN/models/denoiser/gtcrn_simple.onnx" \
    str:models.1.id=uvr-mdx-voc-ft bool:models.1.bundled=false str:models.1.status=available \
    absent:models.1.path; then echo "  PASS MDL-006"; else echo "  FAIL MDL-006"; FAIL=1; fi
ex MDL-007 test ! -e "$W/libprobe" # bundled 查询同样不落盘

# =============================================================
echo "== MDL-DL：下载 → 校验 → 落位 → 登记（幂等） =="

jr MDL-020 0 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"root\":\"$LIB\",\"endpoint\":\"$(ep A)\"}" \
    ok=true cmd=modelDownload str:result.id=uvr-mdx-voc-ft \
    str:result.path="$LIB/separator/UVR-MDX-NET-Voc_FT.onnx" \
    str:result.sha256="$UVR_SHA" num:result.sizeBytes=$UVR_SIZE \
    keys:result=id,path,sha256,sizeBytes exists:result.path
sha_is MDL-020s "$LIB/separator/UVR-MDX-NET-Voc_FT.onnx" "$UVR_SHA"
nopart MDL-020p "$LIB"
mj MDL-020m "$LIB" '[e for e in m if e["id"]=="uvr-mdx-voc-ft"][0]["source"]' download
mj MDL-020j "$LIB" '[e for e in m if e["id"]=="uvr-mdx-voc-ft"][0]["sha256"]' "$UVR_SHA"

N0=$(logn "$W/log.A")
jr MDL-021 0 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"root\":\"$LIB\",\"endpoint\":\"$(ep A)\"}" \
    ok=true str:result.sha256="$UVR_SHA"
N1=$(logn "$W/log.A")
ex MDL-021z test "$N0" = "$N1" # 幂等：零网络请求（登记在位即免重读大文件重校验）

rm "$LIB/models.json"
N0=$(logn "$W/log.A")
jr MDL-023 0 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"root\":\"$LIB\",\"endpoint\":\"$(ep A)\"}" \
    ok=true str:result.sha256="$UVR_SHA"
N1=$(logn "$W/log.A")
ex MDL-023z test "$N0" = "$N1" # 清单丢失但产物在位：现算指纹收编，仍零网络
mj MDL-023m "$LIB" '[e for e in m if e["id"]=="uvr-mdx-voc-ft"][0]["source"]' local

rm "$LIB/separator/UVR-MDX-NET-Voc_FT.onnx"
jr MDL-008 0 "{\"v\":1,\"cmd\":\"modelList\",\"root\":\"$LIB\"}" \
    ok=true str:result.models.1.id=uvr-mdx-voc-ft str:result.models.1.status=stale absent:result.models.1.path

echo 'garbage{{' >"$LIB/models.json"
jr MDL-009 0 "{\"v\":1,\"cmd\":\"modelList\",\"root\":\"$LIB\"}" \
    ok=true str:result.models.0.status=available str:result.models.1.status=available # 损坏清单按空处理
jr MDL-009b 0 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"root\":\"$LIB\",\"endpoint\":\"$(ep A)\"}" \
    ok=true str:result.sha256="$UVR_SHA"
mj MDL-009m "$LIB" 'len(m)' 1 # 落位重写治愈损坏清单

# =============================================================
echo "== MDL-RESUME：断点续传（.part 跨调用 + Range 请求头 ground truth） =="

jr MDL-030 1 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"root\":\"$LIBR\",\"endpoint\":\"$(ep G)\"}" \
    ok=false code=E_NET msg-contains=网络连接失败 absent:result
ex MDL-030p test -s "$LIBR/separator/UVR-MDX-NET-Voc_FT.onnx.part" # 掐断后 .part 保留（重试 2 次各续 200000）
ex MDL-030s test ! -e "$LIBR/separator/UVR-MDX-NET-Voc_FT.onnx" # 无最终产物
ex MDL-030g grep -q "range=bytes=200000-" "$W/log.G" # 同调用内重试已在续传

jr MDL-031 0 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"root\":\"$LIBR\",\"endpoint\":\"$(ep A)\"}" \
    ok=true str:result.sha256="$UVR_SHA" exists:result.path
sha_is MDL-031s "$LIBR/separator/UVR-MDX-NET-Voc_FT.onnx" "$UVR_SHA"
nopart MDL-031p "$LIBR"
ex MDL-031g grep -qE "GET .*range=bytes=[0-9]+- status=206" "$W/log.A" # 跨调用续传：Range 头由服务端日志实锤

# 登记后产物截断（损坏常见形态）→ 幂等命中须失配 → 重下治愈（测试轮 F-2：否则响应 sha=目录
# 指纹与 sizeBytes=盘上实值自相矛盾，且 D8 引导重下恒 ok 死循环）。等长翻字节为已知边界（与
# D1 bundled「存在+size」同精度，引擎 setup E_MODEL 兜底），记档 goal.md 不设用例。
truncate -s -1000 "$LIB/separator/UVR-MDX-NET-Voc_FT.onnx"
N0=$(logn "$W/log.A")
jr MDL-024t 0 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"root\":\"$LIB\",\"endpoint\":\"$(ep A)\"}" \
    ok=true str:result.sha256="$UVR_SHA"
N1=$(logn "$W/log.A")
ex MDL-024tn test "$N1" -gt "$N0" # 走了网络（重下，非幂等零请求命中）
sha_is MDL-024ts "$LIB/separator/UVR-MDX-NET-Voc_FT.onnx" "$UVR_SHA" # 盘上治愈（长度还原 66762490）

mkdir -p "$LIBN/separator"; printf 'corrupt-part-prefix-garbage' >"$LIBN/separator/UVR-MDX-NET-Voc_FT.onnx.part"
jr MDL-032 0 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"root\":\"$LIBN\",\"endpoint\":\"$(ep I)\"}" \
    ok=true str:result.sha256="$UVR_SHA" exists:result.path
sha_is MDL-032s "$LIBN/separator/UVR-MDX-NET-Voc_FT.onnx" "$UVR_SHA" # 200 全量重写已弃损坏 .part（残留必致 sha 失败）

# =============================================================
echo "== MDL-NEG：失败注入与错误分类（E_NET 域 + 精确 .part 语义；LIBX 全新根防幂等短路） =="

jr MDL-040 1 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"nope\",\"root\":\"$LIBX\"}" \
    ok=false code=E_PARAMS msg-contains=未知模型 absent:result
jr MDL-041 1 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"root\":\"$LIBX\",\"endpoint\":\"$(ep C)\"}" \
    ok=false code=E_NET msg-contains=HTTP absent:result
jr MDL-042 1 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"root\":\"$LIBX\",\"endpoint\":\"$(ep D)\"}" \
    ok=false code=E_NET msg-contains=稍后再试或换镜像 absent:result # 429 专属文案（D3）
jr MDL-043 1 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"root\":\"$LIBX\",\"endpoint\":\"http://127.0.0.1:9\"}" \
    ok=false code=E_NET msg-contains=网络连接失败 absent:result # 端口未开：连接拒绝重试后失败
nopart MDL-043p "$LIBX"
jr MDL-044 1 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"root\":\"$LIBX\",\"endpoint\":\"$(ep B)\"}" \
    ok=false code=E_NET 'msg-contains=sha256 不符' absent:result
nopart MDL-044p "$LIBX" # 内容坏：.part 删除（与「断连保留」相反，OQ-6）
ex MDL-044d test ! -e "$LIBX/separator/UVR-MDX-NET-Voc_FT.onnx"
ex MDL-044m test ! -e "$LIBX/models.json" # 失败不登记
jr MDL-045 1 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"endpoint\":42}" \
    ok=false code=E_PARAMS msg-contains=endpoint absent:result
jr MDL-046 1 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"bogus\":1}" \
    ok=false code=E_PARAMS msg-contains=未知字段 absent:result
jr MDL-047 1 '{"v":1,"cmd":"modelDownload"}' \
    ok=false code=E_PARAMS msg-contains=id absent:result
jr MDL-048 1 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"input\":\"x.wav\"}" \
    ok=false code=E_PARAMS msg-contains=未知字段：input absent:result # model 族无素材语义：input 不进白名单
jr MDL-049 1 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"root\":\"$LIBX\",\"endpoint\":\"$(ep F)\"}" \
    ok=false code=E_NET msg-contains=远端文件指纹与目录不符 absent:result
nopart MDL-049p "$LIBX"
jr MDL-050 1 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"root\":\"$LIBX\",\"endpoint\":\"$(ep J)\"}" \
    ok=false code=E_NET 'msg-contains=HTTP 404' absent:result # HEAD 正常 GET 404：GET 段 HTTP 错误
nopart MDL-050p "$LIBX"
jr MDL-051 1 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"root\":\"$W/lib051\",\"endpoint\":\"ftp://x/y\"}" \
    ok=false code=E_PARAMS msg-contains=不是合法的 absent:result # endpoint 串类型对但 scheme 非 http(s)
ex MDL-051d test ! -e "$W/lib051" # 参数拒绝零落盘副作用（测试轮 P3：修复前留空 <root>/separator/ 目录）
# 已装库不受负例干扰（LIB 仍是 installed）
jr MDL-052 0 "{\"v\":1,\"cmd\":\"modelList\",\"root\":\"$LIB\"}" \
    ok=true str:result.models.1.status=installed exists:result.models.1.path

# =============================================================
echo "== MDL-IMPORT：复制进库（by-id 指纹 / 自由导入试开） =="

jr MDL-060 0 "{\"v\":1,\"cmd\":\"modelImport\",\"path\":\"$IN/gtcrn-copy.onnx\",\"id\":\"gtcrn\",\"root\":\"$LIBI\"}" \
    ok=true cmd=modelImport str:result.id=gtcrn str:result.sha256="$GTCRN_SHA" \
    str:result.path="$LIBI/denoiser/gtcrn_simple.onnx" \
    contains:result.license=MIT keys:result=id,license,path,sha256,sizeBytes
sha_is MDL-060s "$LIBI/denoiser/gtcrn_simple.onnx" "$GTCRN_SHA"
ex MDL-060i test -f "$IN/gtcrn-copy.onnx" # 复制绝不移动，原件仍在
nopart MDL-060p "$LIBI"
mj MDL-060m "$LIBI" '[e for e in m if e["id"]=="gtcrn"][0]["source"]' import

jr MDL-061 1 "{\"v\":1,\"cmd\":\"modelImport\",\"path\":\"$IN/random.onnx\",\"id\":\"gtcrn\",\"root\":\"$LIBI\"}" \
    ok=false code=E_MODEL msg-contains=指纹与目录条目不符 absent:result
nopart MDL-061p "$LIBI"
sha_is MDL-061x "$LIBI/denoiser/gtcrn_simple.onnx" "$GTCRN_SHA" # 失败导入不覆盖既有登记产物

jr MDL-062 0 "{\"v\":1,\"cmd\":\"modelImport\",\"path\":\"$IN/my-gtcrn.onnx\",\"for\":\"aiDenoise\",\"root\":\"$LIBI\"}" \
    ok=true str:result.id=my-gtcrn str:result.sha256="$GTCRN_SHA" \
    str:result.path="$LIBI/denoiser/my-gtcrn.onnx" contains:result.license=unknown
sha_is MDL-062s "$LIBI/denoiser/my-gtcrn.onnx" "$GTCRN_SHA"

jr MDL-063 1 "{\"v\":1,\"cmd\":\"modelImport\",\"path\":\"$IN/random.onnx\",\"for\":\"aiDenoise\",\"root\":\"$LIBI\"}" \
    ok=false code=E_MODEL absent:result # 试开败：setup 即权威校验器（D6），随机字节进不了 ORT
nopart MDL-063p "$LIBI"

# —— 审查 P1-1/P2-1 回归：自由导入路径/id 占用防线（独立库 LIBI2；同库内 path 冲突必经
#    sanitizeId 碰撞或 by-id 登记，故「path 被其他 id 占用」须 python 预写清单复刻冲突态）——
LIBI2="$W/libi2"
cp "$IN/my-gtcrn.onnx" "$W/gtcrn_simple.onnx" # 拷入 $W：$IN 是完整性终闸基线，不得增删
jr MDL-062c 1 "{\"v\":1,\"cmd\":\"modelImport\",\"path\":\"$W/gtcrn_simple.onnx\",\"for\":\"aiDenoise\",\"root\":\"$LIBI2\"}" \
    ok=false code=E_PARAMS msg-contains=内置目录条目路径冲突 absent:result
ex MDL-062cz test ! -e "$LIBI2/models.json" # 拒收零落盘（占用检查先于建目录）

python3 - "$LIBI2" <<'PY' # 预写：其他 id（zzz）登记恰好占用 my-gtcrn.onnx 目标路径（P1-1 冲突态）
import json, sys, os
root = sys.argv[1]; os.makedirs(root, exist_ok=True)
json.dump([{"id": "zzz", "for": "aiDenoise", "path": root + "/denoiser/my-gtcrn.onnx",
            "sha256": "a" * 64, "sizeBytes": 1, "license": "unknown",
            "source": "import", "time": "2026-11-03T00:00:00.000"}],
          open(root + "/models.json", "w"))
PY
jr MDL-062b 1 "{\"v\":1,\"cmd\":\"modelImport\",\"path\":\"$IN/my-gtcrn.onnx\",\"for\":\"aiDenoise\",\"root\":\"$LIBI2\"}" \
    ok=false code=E_PARAMS msg-contains=已被其他模型占用 absent:result
mj MDL-062bm "$LIBI2" 'len(m)' 1 # 清单不被改写（双 id 同路径防线）
ex MDL-062bn test ! -f "$LIBI2/denoiser/my-gtcrn.onnx" # 拒收不落产物

python3 - "$LIBI2" <<'PY' # 预写：同 id（my-gtcrn）不同 sha（P2-1 id 遮蔽态）
import json, sys
root = sys.argv[1]
json.dump([{"id": "my-gtcrn", "for": "aiDenoise", "path": root + "/denoiser/whatever.onnx",
            "sha256": "a" * 64, "sizeBytes": 1, "license": "unknown",
            "source": "import", "time": "2026-11-03T00:00:00.000"}],
          open(root + "/models.json", "w"))
PY
jr MDL-062u 1 "{\"v\":1,\"cmd\":\"modelImport\",\"path\":\"$IN/my-gtcrn.onnx\",\"for\":\"aiDenoise\",\"root\":\"$LIBI2\"}" \
    ok=false code=E_PARAMS 'msg-contains=导入 id 已被占用' absent:result

rm -f "$LIBI2/models.json"; rm -rf "$LIBI2/denoiser"
jr MDL-062r 0 "{\"v\":1,\"cmd\":\"modelImport\",\"path\":\"$IN/my-gtcrn.onnx\",\"for\":\"aiDenoise\",\"root\":\"$LIBI2\"}" \
    ok=true str:result.id=my-gtcrn contains:result.license=unknown
jr MDL-062r2 0 "{\"v\":1,\"cmd\":\"modelImport\",\"path\":\"$IN/my-gtcrn.onnx\",\"for\":\"aiDenoise\",\"root\":\"$LIBI2\"}" \
    ok=true str:result.id=my-gtcrn # 同 id 同 sha：翻新幂等保留（回归保护）
mj MDL-062rm "$LIBI2" 'len([e for e in m if e["id"] == "my-gtcrn"])' 1
sha_is MDL-062rs "$LIBI2/denoiser/my-gtcrn.onnx" "$GTCRN_SHA"

jr MDL-064 1 "{\"v\":1,\"cmd\":\"modelImport\",\"path\":\"$IN/my-gtcrn.onnx\",\"for\":\"transcode\",\"root\":\"$LIBI\"}" \
    ok=false code=E_PARAMS 'msg-contains=for 只接受' absent:result
jr MDL-065 1 "{\"v\":1,\"cmd\":\"modelImport\",\"path\":\"$IN/my-gtcrn.onnx\",\"root\":\"$LIBI\"}" \
    ok=false code=E_PARAMS msg-contains=导入须带 absent:result
jr MDL-066 1 "{\"v\":1,\"cmd\":\"modelImport\",\"path\":\"$W/none.onnx\",\"for\":\"aiDenoise\",\"root\":\"$LIBI\"}" \
    ok=false code=E_FILE msg-contains=打不开文件 absent:result
jr MDL-069 1 '{"v":1,"cmd":"modelImport","path":123}' \
    ok=false code=E_PARAMS msg-contains=必须是字符串 absent:result
jr MDL-070 1 "{\"v\":1,\"cmd\":\"modelImport\",\"path\":\"$IN/my-gtcrn.onnx\",\"id\":\"nope\",\"root\":\"$LIBI\"}" \
    ok=false code=E_PARAMS msg-contains=未知模型 absent:result

if [ "$(id -u)" = 0 ]; then
    echo "  SKIP MDL-067（root 用户无视 chmod，不可构造只读目录）"; SKIPANY=1
else
    RO="$W/ro"; mkdir -p "$RO"; chmod 555 "$RO"
    jr MDL-067 1 "{\"v\":1,\"cmd\":\"modelImport\",\"path\":\"$IN/my-gtcrn.onnx\",\"for\":\"aiDenoise\",\"root\":\"$RO\"}" \
        ok=false code=E_FILE absent:result
    ex MDL-067p test -z "$(find "$RO" -name '*.part' -print -quit)" # 失败零残留
    chmod 755 "$RO"
fi

# 目录外清单条目（自由导入的社区模型）入列：installed + license unknown
# （FR-4 round-1 改按 id 定位：目录表增 dfn3 条目后列表基数为 4+，固定下标断言随目录表演进漂移）
jr MDL-010 0 "{\"v\":1,\"cmd\":\"modelList\",\"root\":\"$LIBI\"}" ok=true
ex MDL-010z python3 -c "
import json, os, sys
ms = json.load(open(sys.argv[1]))['result']['models']
m = [x for x in ms if x['id'] == 'my-gtcrn']
assert len(m) == 1, [x['id'] for x in ms]
m = m[0]
assert m['status'] == 'installed' and m['for'] == 'aiDenoise' and 'unknown' in m['license'], m
assert os.path.isfile(m['path']), m" "$W/MDL-010.resp"

# =============================================================
echo "== MDL-ENVELOPE：信封分叉零漂移（requiresInput 命门） =="

jr MDL-080 0 '{"v":1,"cmd":"modelList"}' ok=true # model 族免 input；旧动词照旧必填
jr MDL-081 1 '{"v":1,"cmd":"frobnicate","input":"x"}' ok=false no-cmd code=E_PARAMS absent:result
# 响应键序/Compact 确定性（CMD-021r 同款逐字节断言，防信封改造旁及未知命令路径）
expect='{"error":{"code":"E_PARAMS","message":"未知命令：frobnicate"},"ok":false,"v":1}'
if [ "$(cat "$W/MDL-081.resp")" = "$expect" ]; then echo "  PASS MDL-081r（响应原文逐字节一致）"; else
    echo "  FAIL MDL-081r: 响应原文 ≠ 期望"; FAIL=1; fi
jr MDL-082 1 '{"v":1,"cmd":"probe"}' ok=false cmd=probe code=E_PARAMS msg-contains=input absent:result
jr MDL-082b 1 '{"v":1,"cmd":"transcode"}' ok=false cmd=transcode code=E_PARAMS msg-contains=input absent:result
jr MDL-083 1 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"workspace\":\"$W/ws\"}" \
    ok=false code=E_PARAMS 'msg-contains=不接受 workspace' absent:result # OQ-8：model 族不收 workspace
jr MDL-084 1 '{"v":1,"cmd":"modelDownload","id":"uvr-mdx-voc-ft","output":"/tmp/x"}' \
    ok=false code=E_PARAMS msg-contains=未知字段 absent:result

# =============================================================
echo "== MDL-CANCEL：取消语义（探针直调；CLI 单发无取消通道） =="

N0=$(logn "$W/log.A")
"$W/probe" cancel-pre uvr-mdx-voc-ft "$LIBC" "$(ep A)" >"$W/cancel-pre.out" 2>/dev/null
N1=$(logn "$W/log.A")
if grep -q "^CODE E_CANCELLED$" "$W/cancel-pre.out" && grep -q "^NOPART$" "$W/cancel-pre.out" && [ "$N0" = "$N1" ]; then
    echo "  PASS MDL-090（预置取消：零网络请求）"; else
    echo "  FAIL MDL-090: $(cat "$W/cancel-pre.out") / log $N0→$N1"; FAIL=1; fi

"$W/probe" cancel-mid uvr-mdx-voc-ft "$LIBC" "$(ep A)" >"$W/cancel-mid.out" 2>/dev/null
if grep -q "^CODE E_CANCELLED$" "$W/cancel-mid.out" && grep -qE "^PART [0-9]+ " "$W/cancel-mid.out"; then
    echo "  PASS MDL-091（中段取消：.part 保留作续传缓存）"; else
    echo "  FAIL MDL-091: $(cat "$W/cancel-mid.out")"; FAIL=1; fi

jr MDL-092 0 "{\"v\":1,\"cmd\":\"modelDownload\",\"id\":\"uvr-mdx-voc-ft\",\"root\":\"$LIBC\",\"endpoint\":\"$(ep A)\"}" \
    ok=true str:result.sha256="$UVR_SHA" exists:result.path
ex MDL-092g grep -qE "GET .*range=bytes=[0-9]+- status=206" "$W/log.A" # 取消→重跑续传闭环

# 导入复制环中段取消（测试轮覆盖空白补面）：首个进度回调（复制环首块）置 cancel → 下一块间
# 检查点 E_CANCELLED；.part 即删（本地重拷廉价）且库树零 .onnx 落位、清单不写
LIBIC="$W/libic"; cp "$GTCRN" "$W/import-src.onnx"
"$W/probe" import-cancel "$W/import-src.onnx" "$LIBIC" aiDenoise >"$W/import-cancel.out" 2>/dev/null
if grep -q "^CODE E_CANCELLED$" "$W/import-cancel.out" && grep -q "^NOPART$" "$W/import-cancel.out" \
    && grep -q "^NOONNX$" "$W/import-cancel.out"; then
    echo "  PASS MDL-093（导入复制环取消：E_CANCELLED + .part 即删 + 零落位）"; else
    echo "  FAIL MDL-093: $(cat "$W/import-cancel.out")"; FAIL=1; fi

# =============================================================
echo "== GML：FR-8 片 B GUI 面（两级缺省解析/结构化 modelMissing/对话框全链；探针 mdl-gui-probe） =="

# 陈旧库闸（gui-ws.sh 同型）：GUI 探针链 libengine.a + 编译主窗/服务两 TU，先增量构建防假绿
cmake --build "$BUILD" >/dev/null || { echo "FAIL: 增量构建失败（$BUILD）"; exit 1; }

echo "== GML 探针编译（mdl-gui-probe：offscreen QApplication 驱动 MainWidget/MediaService 全链）=="
# 主窗/服务不在 libengine.a（video_handler 目标），探针随带两 TU + 各自 moc（AUTOMOC 配方手工化）
MOC=$(ls /usr/lib/qt6/libexec/moc 2>/dev/null || command -v moc)
[ -n "$MOC" ] || { echo "FAIL: 缺 moc（qt6-base-dev-tools）"; exit 1; }
"$MOC" src/service/mediaservice.h -o "$W/moc_mediaservice.cpp"
"$MOC" src/view/mainwidget.h -o "$W/moc_mainwidget.cpp"
PKGS_GUI="Qt6Core Qt6Network Qt6Widgets libavformat libavcodec libavutil libswresample soundtouch"
g++ -std=c++20 -O2 "$S/mdl-gui-probe.cpp" src/view/mainwidget.cpp src/service/mediaservice.cpp \
    "$W/moc_mediaservice.cpp" "$W/moc_mainwidget.cpp" -o "$W/gml-probe" -I"$(pwd)/src" \
    $(pkg-config --cflags $PKGS_GUI) "$BUILD/libengine.a" \
    $(pkg-config --libs $PKGS_GUI) "$(pwd)/third_party/ort/linux/lib/libonnxruntime.so" \
    -Wl,-rpath,"$(pwd)/third_party/ort/linux/lib" \
    || { echo "FAIL: GML 探针编译失败"; exit 1; }

# 素材（5s 44.1k 立体声，gui-ws 同配方）+ appDir 布局（bundled 随相位可控）+ 库 fixture（CLI 预装）
ENG="$W/eng"; mkdir -p "$ENG"
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=5" -ac 2 -c:a pcm_s16le "$ENG/st.wav" \
    || { echo "FAIL: GML 素材生成失败"; exit 1; }
for d in eb-none eb-gtcrn eb-uvr guibin; do mkdir -p "$W/$d"; cp "$W/gml-probe" "$W/$d/probe"; done
mkdir -p "$W/eb-gtcrn/models/denoiser" "$W/eb-uvr/models/separator"
cp "$GTCRN" "$W/eb-gtcrn/models/denoiser/gtcrn_simple.onnx"
cp "$UVR" "$W/eb-uvr/models/separator/UVR-MDX-NET-Voc_FT.onnx"
LIBE1="$W/libe1"; LIBE2="$W/libe2"; LIBE0="$W/libe0" # libe0：不存在根（查询不建目录，恒无已装）
printf '%s' "{\"v\":1,\"cmd\":\"modelImport\",\"path\":\"$IN/gtcrn-copy.onnx\",\"id\":\"gtcrn\",\"root\":\"$LIBE1\"}" | "$VH" --json >/dev/null
[ -f "$LIBE1/denoiser/gtcrn_simple.onnx" ] || { echo "FAIL: GML 库 fixture（gtcrn）预装失败"; exit 1; }
printf '%s' "{\"v\":1,\"cmd\":\"modelImport\",\"path\":\"$UVR\",\"id\":\"uvr-mdx-voc-ft\",\"root\":\"$LIBE2\"}" | "$VH" --json >/dev/null
[ -f "$LIBE2/separator/UVR-MDX-NET-Voc_FT.onnx" ] || { echo "FAIL: GML 库 fixture（uvr）预装失败"; exit 1; }
# 跨 id 遮蔽回归 fixture（审查轮 P1-1）：库内自由导入另一 aiDenoise 模型（非目录条目 id）
LIBE3="$W/libe3"; cp "$GTCRN" "$W/my-gtcrn.onnx"
printf '%s' "{\"v\":1,\"cmd\":\"modelImport\",\"path\":\"$W/my-gtcrn.onnx\",\"for\":\"aiDenoise\",\"root\":\"$LIBE3\"}" | "$VH" --json >/dev/null
[ -f "$LIBE3/denoiser/my-gtcrn.onnx" ] || { echo "FAIL: GML 库 fixture（my-gtcrn 自由导入）预装失败"; exit 1; }

# 整行/前缀断言器（探针输出协议行）
line_is() { # <id> <文件> <完整期望行>：输出含该整行（固定串比对）
    local id=$1 f=$2 want=$3
    if grep -qxF "$want" "$f"; then echo "  PASS $id"; else
        echo "  FAIL $id: 未见整行「$want」"; sed 's/^/    probe│ /' "$f"; FAIL=1; fi
}
has_pref() { # <id> <文件> <固定串前缀>：输出含该前缀（ok=1 文案带哈希段，锚定前缀即可）
    local id=$1 f=$2 want=$3
    if grep -qF "$want" "$f"; then echo "  PASS $id"; else
        echo "  FAIL $id: 未见前缀「$want」"; sed 's/^/    probe│ /' "$f"; FAIL=1; fi
}
eng() { # <bin目录> <lib> <ws> <相位> <out>：跑 engine 相（槽直调，gui-ws-probe 同型）
    QT_QPA_PLATFORM=offscreen "$1/probe" engine "$ENG/st.wav" "$2" "$3" "$4" >"$5" 2>"$5.err"
}

echo "== GML-E：两级缺省解析（D1：库已装 → appDir bundled）+ modelMissing 触发面（D8） =="

eng "$W/eb-none" "$LIBE0" "$W/wse1" ai-missing "$W/gml-101.out"
line_is GML-101 "$W/gml-101.out" "TASK ai-missing -> ok=0 msg=找不到 AI 降噪模型：$W/eb-none/models/denoiser/gtcrn_simple.onnx"
line_is GML-101m "$W/gml-101.out" "MODELMISSING 1" # 两级皆缺：pre-check 路径发结构化信号（文案 %1 = bundled 路径，GUI-EQV 锁）
line_is GML-101p "$W/gml-101.out" "MODELPATH -" # 失败无版本记录

eng "$W/eb-none" "$LIBE1" "$W/wse2" ai-libonly "$W/gml-102.out"
has_pref GML-102 "$W/gml-102.out" "TASK ai-libonly -> ok=1 msg=AI 降噪完成：$W/wse2/"
line_is GML-102p "$W/gml-102.out" "MODELPATH $LIBE1/denoiser/gtcrn_simple.onnx" # 真值 = 版本记录 model.path
line_is GML-102m "$W/gml-102.out" "MODELMISSING 0"

eng "$W/eb-gtcrn" "$LIBE1" "$W/wse3" ai-libbundled "$W/gml-103.out"
line_is GML-103 "$W/gml-103.out" "MODELPATH $LIBE1/denoiser/gtcrn_simple.onnx" # 库优先于随包（两级都在取库）

eng "$W/eb-gtcrn" "$LIBE3" "$W/wse3b" ai-importshadow "$W/gml-103b.out"
has_pref GML-103bt "$W/gml-103b.out" "TASK ai-importshadow -> ok=1" # 任务成功（用的是导入模型而非随包）
line_is GML-103b "$W/gml-103b.out" "MODELPATH $LIBE3/denoiser/my-gtcrn.onnx" # 跨 id 遮蔽回归（审查 P1-1）：随包在位不得压过库内导入条目（修复前取随包路径）

eng "$W/eb-gtcrn" "$LIBE0" "$W/wse4" ai-bundledonly "$W/gml-104.out"
line_is GML-104 "$W/gml-104.out" "MODELPATH $W/eb-gtcrn/models/denoiser/gtcrn_simple.onnx" # 库无已装 → 随包兜底

eng "$W/eb-gtcrn" "$LIBE1" "$W/wse5" ai-noroot "$W/gml-105.out"
line_is GML-105 "$W/gml-105.out" "MODELPATH $W/eb-gtcrn/models/denoiser/gtcrn_simple.onnx" # 根未注入不查库（库在盘有 gtcrn 仍取随包 = 零漂移）

eng "$W/eb-uvr" "$LIBE2" "$W/wse6" sep-lib "$W/gml-106.out"
has_pref GML-106 "$W/gml-106.out" "TASK sep-lib -> ok=1 msg=人声分离完成：$W/wse6/"
line_is GML-106p "$W/gml-106.out" "MODELPATH $LIBE2/separator/UVR-MDX-NET-Voc_FT.onnx" # separator 槽同型两级解析

eng "$W/eb-none" "$LIBE0" "$W/wse7" sep-missing "$W/gml-107.out"
line_is GML-107 "$W/gml-107.out" "TASK sep-missing -> ok=0 msg=找不到人声分离模型：$W/eb-none/models/separator/UVR-MDX-NET-Voc_FT.onnx"
line_is GML-107m "$W/gml-107.out" "MODELMISSING 1"

# 负例守卫（审查 P3-3a）：模型在位 + 输入缺失 → E_FILE 任务失败，结构化信号不得误发
# （wav 参数故意不存在，故不走 eng() 的固定素材路径）
QT_QPA_PLATFORM=offscreen "$W/eb-gtcrn/probe" engine "$W/nonexistent.wav" "$LIBE0" "$W/wse8" ai-badinput >"$W/gml-101n.out" 2>"$W/gml-101n.err"
ex GML-101n grep -qF "TASK ai-badinput -> ok=0" "$W/gml-101n.out"
line_is GML-101nm "$W/gml-101n.out" "MODELMISSING 0"

echo "== GML-G：对话框全链（offscreen；XDG_CONFIG_HOME 隔离 QSettings；节流服务拉长传输窗） =="

start_srv SLOW "$SRVROOT" --chunk-sleep-ms 8 # 1019 chunk × 8ms ≈ 8s：busy/取消断言免竞态
LIBG="$W/libg"; WSG="$W/wsg"; mkdir -p "$WSG"
XDG_CONFIG_HOME="$W/config" QT_QPA_PLATFORM=offscreen "$W/guibin/probe" gui \
    "$ENG/st.wav" "$(ep SLOW)" "$LIBG" "$WSG" "$IN/my-gtcrn.onnx" >"$W/gml-gui.out" 2>"$W/gml-gui.err"
# offscreen 平台自身对窗口操作（raise/尺寸协商）有固定噪声输出，非应用错误——滤后须为空
grep -vE "This plugin does not support (propagateSizeHints|raise)\(\)" "$W/gml-gui.err" >"$W/gml-gui.err2" || true
if [ -s "$W/gml-gui.err2" ]; then echo "  FAIL GML-G stderr 非空（滤平台噪声后）"; sed 's/^/    stderr│ /' "$W/gml-gui.err2"; FAIL=1; fi

line_is GML-110 "$W/gml-gui.out" "EPDEFAULT https://huggingface.co" # D4 缺省源
line_is GML-110n "$W/gml-gui.out" "MIRRORNOTE 注：镜像前缀仅对 HuggingFace 源条目生效；GitHub 直连条目不受镜像影响。" # D4 镜像适用性 UI 注记锁字（审查 P1-2）
line_is GML-110r "$W/gml-gui.out" "ROOTLBL 模型库：$LIBG" # 库根 QSettings 读路径（构造期）
line_is GML-110t "$W/gml-gui.out" "ROW0 gtcrn 可下载" # 列表渲染 + 状态中文映射
line_is GML-110c "$W/gml-gui.out" "DLGCLOSE 1" # 非模态可关闭复开
line_is GML-111 "$W/gml-gui.out" "DLBUSY 1" # 下载占 busy：操作键锁/取消键开/进度条显
line_is GML-112 "$W/gml-gui.out" "LISTDURINGBUSY 1" # modelList 只读不占 busy（下载中查询回传）
line_is GML-113 "$W/gml-gui.out" "CANCELMSG 已取消" # 取消文案直通（onModelOpDone 特判命中）
line_is GML-113p "$W/gml-gui.out" "CANCELPART 1" # OQ-6：取消保留 .part
line_is GML-113u "$W/gml-gui.out" "UNBUSY 1" # 完成放锁
ex GML-114e grep -qE '^EQMSG .+' "$W/gml-gui.out" # E_MODEL 任务文案非空（jobFinished ok=0 载荷）
line_is GML-114 "$W/gml-gui.out" "MISSINGQ 1" # 问询框出现且答 Yes（结构化信号→引导链）
line_is GML-114d "$W/gml-gui.out" "DLGOPEN 1" # Yes → 打开模型管理
line_is GML-115 "$W/gml-gui.out" "DLOKMSG 模型下载完成：$LIBG/separator/UVR-MDX-NET-Voc_FT.onnx"
line_is GML-115s "$W/gml-gui.out" "DLOK 1" # 续传闭环完成 + 列表翻「已安装」+ 无 .part
line_is GML-115r "$W/gml-gui.out" "SELRESTORED uvr-mdx-voc-ft" # 完成刷新后选中按 id 恢复（测试轮 D1；修复前恒回首行）
sha_is GML-115f "$LIBG/separator/UVR-MDX-NET-Voc_FT.onnx" "$UVR_SHA" # 对话框路径下载产物 = 目录表基准
line_is GML-116 "$W/gml-gui.out" "NOTICE 请确认您有权使用该模型（来源与许可证由您自行核验）" # §161 逐字
line_is GML-116i "$W/gml-gui.out" "IMPORTOK 1" # 导入全链（选文件→用途→提交）
line_is GML-116r "$W/gml-gui.out" "IMPORTROW 1" # 列表新增 my-gtcrn 已安装
sha_is GML-116f "$LIBG/denoiser/my-gtcrn.onnx" "$GTCRN_SHA" # 导入落位产物 = 源字节
line_is GML-116g "$W/gml-gui.out" "IMPORTPROG 100" # 导入进度真值：复制段 0–99 + 登记成功收尾 100（审查 P2-3）
line_is GML-117 "$W/gml-gui.out" "EPSAVED https://hf-mirror.com" # endpoint QSettings 记忆（D4）
line_is GML-118 "$W/gml-gui.out" "ROOTCHANGED 1" # 换根全链（目录对话框→标签/下传/刷新）
line_is GML-118s "$W/gml-gui.out" "ROOTSAVED $LIBG-changed" # 库根 QSettings 写路径
nopart GML-119 "$LIBG" # GUI 相终态：库树无 .part 残留

# =============================================================
echo "== 完整性终闸（fixture 与 third_party 未被改动；红线：不动 third_party/） =="
IN_SHAS2=$(sha256sum "$IN"/*.onnx "$GTCRN" "$UVR" | sha256sum | cut -d' ' -f1)
ex MDL-999 test "$IN_SHAS" = "$IN_SHAS2"

if [ "$FAIL" = 1 ]; then
    echo "== run-models FAIL（见上） =="
    exit 1
fi
if [ "$SKIPANY" = 1 ]; then
    echo "== run-models 全绿（有 SKIP，见上） =="
    exit 0
fi
echo "== run-models 全绿 =="
