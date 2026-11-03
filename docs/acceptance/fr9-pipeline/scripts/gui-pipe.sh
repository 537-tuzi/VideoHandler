#!/usr/bin/env bash
# gui-pipe.sh — FR-9 片 B（GUI widget 面）验收编排（PIPE-GUI 族，round-2；探针 pipe-gui-probe.cpp gui 模式）：
#   预设组包（OQ-3(a)）—— 三条预设各自：面板态（hum 勾选/制式/wind/semi）注入 → doPipeline →
#       捕获 pipelineRequested(inPath, stepsJson) 与期望 JSON 深度比对（探针直调比对请求 JSON，
#       gui-slot-eqv 方法论）；denoise 步 humBase/wind、pitch 步 semi 的动态参数注入面逐项覆盖；
#   busy 锁 —— 提交即锁 btnPipeline（+cmbPipeline）、完成即解（新按钮纳入 busy 面板）；
#   成功文案 —— 「管线完成：<终产物路径>」（附裁 5）。
# 判据与用例表以 ../matrix.csv PIPE-GUI-* 为准；offscreen QApplication（XDG_CONFIG_HOME 隔离
# QSettings）；模型落探针 bin 侧供 appDir 相对解析（两级缺省解析缺省级）。round-1 只做探针编译
# 自检（run-pipe.sh PIPE-GUIE 段已编），本脚本 round-2 真机/显示环境跑。
# 依赖与 linux-test 构建同套（pkg-config + third_party ORT/模型）+ moc。
# 用法：gui-pipe.sh [构建目录=build-linux]
set -uo pipefail
cd "$(dirname "$0")/../../../.." # scripts→fr9-pipeline→acceptance→docs→仓库根
[ -f CMakeLists.txt ] || { echo "FAIL: 未落到仓库根（$(pwd)）"; exit 1; }

BUILD="${1:-build-linux}"
VH="$BUILD/video_handler"
GTCRN=third_party/models/denoiser/gtcrn_simple.onnx
UVR=third_party/models/separator/UVR-MDX-NET-Voc_FT.onnx
S=docs/acceptance/fr9-pipeline/scripts
for f in "$BUILD/libengine.a" "$GTCRN" "$UVR"; do
    [ -e "$f" ] || { echo "FAIL: 缺 $f（先构建/跑 fetch-deps.sh）"; exit 1; }
done

PIPETMP="${TMPDIR:-$PWD/$BUILD/.pipe-tmp}"
mkdir -p "$PIPETMP"
W=$(mktemp -d "$PIPETMP/pipe-gui.XXXXXX")
trap 'chmod -R u+w "$W" >/dev/null 2>&1; rm -rf "$W"' EXIT
FAIL=0
ck() { if [ "$1" = 0 ]; then echo "  PASS $2"; else echo "  FAIL $2"; FAIL=1; fi; }

cmake --build "$BUILD" >/dev/null || { echo "FAIL: 增量构建失败"; exit 1; }

echo "== 探针编译（pipe-gui-probe.cpp gui 模式：offscreen MainWidget/MediaService 全链）=="
MOC=$(ls /usr/lib/qt6/libexec/moc 2>/dev/null || command -v moc)
[ -n "$MOC" ] || { echo "FAIL: 缺 moc（qt6-base-dev-tools）"; exit 1; }
"$MOC" src/service/mediaservice.h -o "$W/moc_mediaservice.cpp"
"$MOC" src/view/mainwidget.h -o "$W/moc_mainwidget.cpp"
PKGS="Qt6Core Qt6Widgets Qt6Network libavformat libavcodec libavutil libswresample soundtouch"
g++ -std=c++20 -O2 "$S/pipe-gui-probe.cpp" src/view/mainwidget.cpp src/service/mediaservice.cpp \
    "$W/moc_mediaservice.cpp" "$W/moc_mainwidget.cpp" -o "$W/gpipe-probe" -I"$(pwd)/src" \
    $(pkg-config --cflags $PKGS) "$BUILD/libengine.a" \
    $(pkg-config --libs $PKGS) "$(pwd)/third_party/ort/linux/lib/libonnxruntime.so" \
    -Wl,-rpath,"$(pwd)/third_party/ort/linux/lib" \
    || { echo "FAIL: 探针编译失败"; exit 1; }

echo "== 素材（run-ws 同配方）与 bin 布局（appDir 缺省解析的模型位）=="
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=5" -ac 2 -c:a pcm_s16le "$W/st.wav"
mkdir -p "$W/bin/models/denoiser" "$W/bin/models/separator"
cp "$GTCRN" "$W/bin/models/denoiser/"
cp "$UVR" "$W/bin/models/separator/"
cp "$W/gpipe-probe" "$W/bin/probe"

echo "== PIPE-GUI-G：widget 全链（预设组包 + busy 锁 + 成功文案）=="
XDG_CONFIG_HOME="$W/config" QT_QPA_PLATFORM=offscreen "$W/bin/probe" gui "$W/st.wav" "$W/gws" \
    >"$W/gui.out" 2>"$W/gui.err"
# offscreen 平台对窗口操作的固定噪声（mdl-gui-probe 同款滤除口径）
if grep -vE "^(This plugin does not support|QStandardPaths|propagateSizeHints|QWidget::repaint)" "$W/gui.err" | grep -q .; then
    echo "  探针 stderr 有非噪声输出："; sed 's/^/    probe│ /' "$W/gui.err" | head -20; FAIL=1
fi
# LOADED：文件载入后新按钮解禁（openFile 使能面）
grep -q "^LOADED btnPipeline=1$" "$W/gui.out"; ck $? "PIPE-GUI-001 文件载入后一键按钮解禁"
# 预设组包：PRESET 行的 stepsJson 与期望深度比对（python 逐 case）
python3 - "$W/gui.out" <<'PYEOF'
import json, sys
want = [
    ("idx=0 hum=60 semi=0.0 wind=1", [{"cmd": "aiDenoise"}, {"cmd": "separate"}]),
    ("idx=1 hum=60 semi=0.0 wind=1", [{"cmd": "denoise", "humBase": "60", "wind": True}, {"cmd": "separate"}]),
    ("idx=1 hum=off semi=0.0 wind=1", [{"cmd": "denoise", "humBase": "off", "wind": True}, {"cmd": "separate"}]),
    ("idx=2 hum=50 semi=2.5 wind=1", [{"cmd": "separate"}, {"cmd": "pitch", "semi": 2.5}]),
]
got = {}
cur = None
for line in open(sys.argv[1], encoding="utf-8"):
    if line.startswith("PRESET "):
        cur = line[len("PRESET "):].split(" -> ")[0]
        got[cur] = json.loads(line.rstrip("\n").split(" -> ", 1)[1])
    else:
        cur = None
for tag, steps in want:
    assert got.get(tag) == steps, (tag, got.get(tag), steps)
PYEOF
ck $? "PIPE-GUI-002..005 三条预设组包（动态参数注入：humBase/wind/semi 逐项）"
# busy 锁：每个 case 提交即锁、完成即解（4 case 全锁全解；unlocked 行带 ok/msg 尾巴故只锚前缀）
[ "$(grep -c '^BUSY locked=1$' "$W/gui.out")" = 4 ] && [ "$(grep -c '^BUSY unlocked=1 ' "$W/gui.out")" = 4 ]
ck $? "PIPE-GUI-006 busy 锁覆盖新按钮（4 case 提交即锁/完成即解）"
# 成功文案：模型在位的 case 均成功且文案「管线完成：」（case3 off+wind=true 为合法链）
[ "$(grep -c 'unlocked=1 ok=1 msg=管线完成：' "$W/gui.out")" = 4 ]; ck $? "PIPE-GUI-007 成功文案「管线完成：<终产物路径>」"

if [ "$FAIL" = 1 ]; then
    echo "== gui-pipe FAIL（见上）=="
    exit 1
fi
echo "== gui-pipe 全绿 =="
