#!/usr/bin/env bash
# gui-batch.sh — FR-10 片 B（GUI widget 面）验收编排（BATCH-GUI 族，round-2；探针 gui-batch-probe.cpp）：
#   组包断言（spec §6.9）—— 8 动作组合（5 引擎单步 + 3 预设链，OQ-6(a) 无 transcode）× 动态参数
#       面板注入（denoise humBase/wind、pitch semi）：主路径零 QFileDialog 依赖（清单直填 UserRole
#       → invokeMethod 直调 doBatchStart），捕获 batchRequested(inputsJson, stepsJson) 深度比对
#       （gui-slot-eqv/pipe-gui-probe 方法论）；
#   busy 锁全批覆盖 —— 提交即锁（七槽/管线行/批处理入口/对话框内键 + btnCancel 解禁）、首个进度
#       回调再采样（批运行中锁持续）、完成即解；
#   完成文案 —— 信息弹窗与状态行「批处理完成：k/N 件成功」（附裁 5）；denoff（humBase off +
#       wind false）预校验失败面：警告框引擎原文 + 行不渲染；混合败件 1/2 + 清单逐件
#       渲染双态（成功（版本 N）/失败：E_FILE：原文）；
#   「已取消」静默 —— 件 1 收尾映射 50 点取消：零弹窗 + 状态行「已取消」+ 清单行不渲染（附裁 5）；
#   miss —— appDir 无模型：开工前解析失败 → 警告框（错误文案）+ 缺模型问询（点「是」开模型管理）
#       + busy 已释放（附裁 1 顺序的 widget 面证据，GUIE-002 的控件层镜像）；
#   MULTIADD —— 多选对话框应答段（尽力）：行编辑引号多填 + AcceptRole（fr9 加固配方扩展到
#       ExistingFiles）；offscreen 不落定则记档降级为主路径（不设 FAIL 门禁，Windows round-4 手工）。
# 判据与用例表以 ../matrix.csv BATCH-GUI-* 为准；offscreen QApplication + XDG_CONFIG_HOME 隔离 +
# QSettings 受控根注入（kSettingsWorkspaceRoot/kSettingsModelRoot）+ AA_DontUseNativeDialogs；
# 模型落探针 bin 侧供 appDir 相对解析（bin-full 全 / bin-empty 空，两级缺省解析缺省级）。
# 工作区走磁盘卷 TMPDIR（默认 <build>/.batch-tmp，/tmp 配额 tmpfs 假象口径同 run-batch.sh）。
# 依赖与 linux-test 构建同套（pkg-config + third_party ORT/模型）+ moc；缺模型整脚本 FAIL
#（widget 面主路径含模型动作，无法 else-skip——fetch-deps.sh 备齐后跑）。
# 用法：gui-batch.sh [构建目录=build-linux]
set -uo pipefail
cd "$(dirname "$0")/../../../.." # scripts→fr10-batch-queue→acceptance→docs→仓库根
[ -f CMakeLists.txt ] || { echo "FAIL: 未落到仓库根（$(pwd)）"; exit 1; }

BUILD="${1:-build-linux}"
GTCRN=third_party/models/denoiser/gtcrn_simple.onnx
UVR=third_party/models/separator/UVR-MDX-NET-Voc_FT.onnx
S=docs/acceptance/fr10-batch-queue/scripts
for f in "$BUILD/libengine.a" "$GTCRN" "$UVR"; do
    [ -e "$f" ] || { echo "FAIL: 缺 $f（先构建/跑 fetch-deps.sh）"; exit 1; }
done

GTMP="${TMPDIR:-$PWD/$BUILD/.batch-tmp}"
mkdir -p "$GTMP"
W=$(mktemp -d "$GTMP/batch-gui.XXXXXX")
# 失败保现场：FAIL=1 时保留 W（打印路径）供诊断，全绿才随 EXIT 清理
keep_on_fail() {
    if [ "$FAIL" = 1 ]; then
        chmod -R u+w "$W" >/dev/null 2>&1
        echo "  （FAIL：现场保留于 $W）"
    else
        chmod -R u+w "$W" >/dev/null 2>&1; rm -rf "$W"
    fi
}
trap keep_on_fail EXIT
FAIL=0
ck() { if [ "$1" = 0 ]; then echo "  PASS $2"; else echo "  FAIL $2"; FAIL=1; fi; }

cmake --build "$BUILD" >/dev/null || { echo "FAIL: 增量构建失败"; exit 1; }

echo "== 探针编译（gui-batch-probe.cpp：offscreen MainWidget/MediaService 全链）=="
MOC=$(ls /usr/lib/qt6/libexec/moc 2>/dev/null || command -v moc)
[ -n "$MOC" ] || { echo "FAIL: 缺 moc（qt6-base-dev-tools）"; exit 1; }
"$MOC" src/service/mediaservice.h -o "$W/moc_mediaservice.cpp"
"$MOC" src/view/mainwidget.h -o "$W/moc_mainwidget.cpp"
PKGS="Qt6Core Qt6Widgets Qt6Network libavformat libavcodec libavutil libswresample soundtouch"
g++ -std=c++20 -O2 "$S/gui-batch-probe.cpp" src/view/mainwidget.cpp src/service/mediaservice.cpp \
    "$W/moc_mediaservice.cpp" "$W/moc_mainwidget.cpp" -o "$W/gbatch-probe" -I"$(pwd)/src" \
    $(pkg-config --cflags $PKGS) "$BUILD/libengine.a" \
    $(pkg-config --libs $PKGS) "$(pwd)/third_party/ort/linux/lib/libonnxruntime.so" \
    -Wl,-rpath,"$(pwd)/third_party/ort/linux/lib" \
    || { echo "FAIL: 探针编译失败"; exit 1; }

echo "== 素材（run-batch 同配方）与 bin 布局（appDir 缺省解析的模型位）=="
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=5" -ac 2 -c:a pcm_s16le "$W/st.wav"
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=660:sample_rate=44100:duration=5" -ac 1 -c:a pcm_s16le "$W/st2.wav"
for b in bin-full bin-empty; do
    mkdir -p "$W/$b/models/denoiser" "$W/$b/models/separator"
    cp "$W/gbatch-probe" "$W/$b/probe"
done
cp "$GTCRN" "$W/bin-full/models/denoiser/"
cp "$UVR" "$W/bin-full/models/separator/"

echo "== BATCH-GUI-P（widget 全链主路径：组包 + busy 全批覆盖 + 文案 + 渲染 + 取消静默）=="
XDG_CONFIG_HOME="$W/config" QT_QPA_PLATFORM=offscreen "$W/bin-full/probe" gui "$W/st.wav" "$W/gws" \
    >"$W/gui.out" 2>"$W/gui.err"
if grep -vE "^(This plugin does not support|QStandardPaths|propagateSizeHints|QWidget::repaint)" "$W/gui.err" | grep -q .; then
    echo "  探针 stderr 有非噪声输出："; sed 's/^/    probe│ /' "$W/gui.err" | head -20; FAIL=1
fi
grep -q "^WIDGETS ok=1 combo=8$" "$W/gui.out"; ck $? "BATCH-GUI-001 控件面就绪（清单/动作组合 8 条=5 单步+3 预设，OQ-6(a)）"
# 组包断言：ACT 行 inputs/steps 与期望深度比对（python 逐 case；inputs = 清单顺序两件绝对路径）
python3 - "$W/gui.out" "$W/st.wav" <<'PYEOF'
import json, sys
wav = sys.argv[2]
copy1 = wav + ".probe1.wav"
want_inputs = [wav, copy1]
# 期望 steps（mainwidget doBatchStart 组包通路：单步注入 humBase/wind/semi/stem；预设链同 doPipeline
# 的动态注入面 + separate 步显式 stem=vocals）
want = {
    "den60":  [{"cmd": "denoise", "humBase": "60", "wind": True}],
    "denoff": [{"cmd": "denoise", "humBase": "off", "wind": False}],
    "ai":     [{"cmd": "aiDenoise"}],
    "pit":    [{"cmd": "pitch", "semi": 2.5}],
    "sepv":   [{"cmd": "separate", "stem": "vocals"}],
    "sepi":   [{"cmd": "separate", "stem": "instrumental"}],
    "pre0":   [{"cmd": "aiDenoise"}, {"cmd": "separate", "stem": "vocals"}],
    "pre1":   [{"cmd": "denoise", "humBase": "60", "wind": True}, {"cmd": "separate", "stem": "vocals"}],
    "pre2":   [{"cmd": "separate", "stem": "vocals"}, {"cmd": "pitch", "semi": -3.5}],
    "mixed":  [{"cmd": "denoise", "humBase": "50", "wind": False}],
    "cancel": [{"cmd": "pitch", "semi": 1.0}],
}
got = {}
inputs = {}
for line in open(sys.argv[1], encoding="utf-8"):
    if line.startswith("ACT "):
        rest = line[4:].rstrip("\n")
        tag, payload = rest.split(" inputs=", 1)
        ins, steps = payload.split(" steps=", 1)
        got[tag] = json.loads(steps)
        inputs[tag] = json.loads(ins)
for tag, steps in want.items():
    assert got.get(tag) == steps, (tag, got.get(tag), steps)
    if tag != "mixed":
        assert inputs[tag] == want_inputs, (tag, inputs[tag])
# mixed 的 inputs[1] 为不存在件（wsRoot 下 no-such.wav）
assert inputs["mixed"][0] == wav and inputs["mixed"][1].endswith("/no-such.wav"), inputs["mixed"]
PYEOF
ck $? "BATCH-GUI-002..012 组包断言（8 动作 × 动态参数注入 + mixed/cancel 清单透传，深度比对）"
# busy 锁全批覆盖：每 case 提交即锁 / 完成即解；denoff 预校验失败（humBase off+wind false 无可执行
# 项）也走同一锁程（提交锁/警告框/放锁），且零进度回调（mid=-1）
[ "$(grep -c '^LOCKED .* all=1$' "$W/gui.out")" = 11 ] && [ "$(grep -c '^UNLOCKED .* all=1 mid=' "$W/gui.out")" = 11 ]
ck $? "BATCH-GUI-013 busy 锁全批覆盖（11 case 提交即锁/完成即解）"
[ "$(grep -c '^UNLOCKED .* mid=1$' "$W/gui.out")" = 10 ] && grep -q '^UNLOCKED denoff all=1 mid=-1$' "$W/gui.out"
ck $? "BATCH-GUI-014 批运行中锁持续（首个进度回调采样全锁；denoff 预校验失败零进度）"
# denoff 失败面：警告框带引擎产生点原文（消息名点 humBase/wind 双标志=注入面到达引擎的证明），
# 清单行不渲染（保持基名）
grep -q '^MODALS denoff n=1 步骤 1（denoise）：humBase 为 off 且 wind 为 false：没有可执行的降噪项$' "$W/gui.out"
ck $? "BATCH-GUI-003b denoff 失败面（引擎原文透传 + 行不渲染）"
# 完成文案：8 个成功组包 case + mixed 1/2（信息弹窗文本逐字）
[ "$(grep -c '^MODALS \(den60\|ai\|pit\|sepv\|sepi\|pre0\|pre1\|pre2\) n=1 批处理完成：2/2 件成功$' "$W/gui.out")" = 8 ]
ck $? "BATCH-GUI-015 完成文案「批处理完成：2/2 件成功」（信息弹窗逐字，附裁 5）"
grep -q '^MODALS mixed n=1 批处理完成：1/2 件成功$' "$W/gui.out"; ck $? "BATCH-GUI-016 混合败件完成文案「批处理完成：1/2 件成功」"
# 清单逐件渲染：mixed 行 0 成功（版本 N）/ 行 1 失败（E_FILE + 产生点原文）；k=okCount
python3 - "$W/gui.out" "$W/st.wav" <<'PYEOF'
import sys
wav = sys.argv[2]
rows = None
for line in open(sys.argv[1], encoding="utf-8"):
    if line.startswith("ROWS mixed "):
        rows = line.rstrip("\n").split(" ", 2)[2].split("|")
assert rows and len(rows) == 2, rows
assert rows[0].startswith("st.wav —— 成功（版本 "), rows[0]
assert rows[1].startswith("no-such.wav —— 失败：E_FILE：打不开文件："), rows[1]
status = [l for l in open(sys.argv[1], encoding="utf-8") if l.startswith("STATUS mixed ")][0]
assert status.rstrip("\n") == "STATUS mixed 批处理完成：1/2 件成功", status
PYEOF
ck $? "BATCH-GUI-017 清单逐件渲染（成功（版本 N）/失败：E_FILE：原文，index 对位）"
# 「已取消」静默：零弹窗 + 状态行逐字 + 清单行不渲染（保持基名）+ 取消确已点击
python3 - "$W/gui.out" <<'PYEOF'
import sys
out = open(sys.argv[1], encoding="utf-8").read().splitlines()
status = [l for l in out if l.startswith("STATUS cancel ")][0]
assert status == "STATUS cancel 已取消", status  # 逐字（onBatchDone !ok 路径的静默特判，附裁 5）
modals = [l for l in out if l.startswith("MODALS cancel ")][0]
assert modals == "MODALS cancel n=0 ", modals  # 零弹窗（警告框不出现）
rows = [l for l in out if l.startswith("ROWS cancel ")][0].split(" ", 2)[2].split("|")
assert all("——" not in r for r in rows), rows  # 行不渲染结果文本（保持基名）
click = [l for l in out if l.startswith("CANCELCLICK ")][0]
assert click == "CANCELCLICK 1", click
PYEOF
ck $? "BATCH-GUI-018 「已取消」静默（零弹窗/状态行逐字/行不渲染）"
# 进度条终值路径：8 个成功组包 case 终值 100（bar 满格代理）；denoff 预校验失败零进度
python3 - "$W/gui.out" <<'PYEOF'
import sys
tags = {"den60", "ai", "pit", "sepv", "sepi", "pre0", "pre1", "pre2"}
seen = set()
for line in open(sys.argv[1], encoding="utf-8"):
    if line.startswith("PROG "):
        tag, seq = line[5:].rstrip("\n").split(" ", 1)
        if tag in tags:
            prog = [int(x) for x in seq.split(",") if x]
            assert prog and prog[-1] == 100, (tag, prog)
            seen.add(tag)
        elif tag == "denoff":
            assert not seq.strip(), seq  # 零进度回调（开工前预校验拦截）
assert seen == tags, tags - seen
PYEOF
ck $? "BATCH-GUI-019 批进度终值 100（主进度条满格代理；denoff 零进度）"

echo "== BATCH-GUI-M（缺模型引导 widget 面：开工前速断 + 双弹窗 + busy 释放）=="
XDG_CONFIG_HOME="$W/config" QT_QPA_PLATFORM=offscreen "$W/bin-empty/probe" miss "$W/st.wav" "$W/gwsm" \
    >"$W/miss.out" 2>"$W/miss.err"
if grep -vE "^(This plugin does not support|QStandardPaths|propagateSizeHints|QWidget::repaint)" "$W/miss.err" | grep -q .; then
    echo "  探针 stderr 有非噪声输出："; sed 's/^/    probe│ /' "$W/miss.err" | head -20; FAIL=1
fi
python3 - "$W/miss.out" <<'PYEOF'
import sys
out = open(sys.argv[1], encoding="utf-8").read().splitlines()
status = [l for l in out if l.startswith("STATUS miss ")][0]
assert "找不到AI 降噪模型：" in status, status  # 状态行直通错误文案
modals = [l for l in out if l.startswith("MODALS miss ")][0]
# 双弹窗均在（警告=onBatchDone !ok 错误文案 / 问询=onModelMissing 引导）；record 序不锁字——问询框在
# 警告框 exec 内嵌套打开，activeModalWidget 恒取栈顶（探针采样面），B→M 顺序以 worker 面 GUIE-002
# （order=BM）+ 本面 unlocked=1（问询打开时锁已归还）为准
assert modals.startswith("MODALS miss n=2 "), modals
assert "找不到AI 降噪模型：" in modals and "模型管理" in modals, modals
dlg = [l for l in out if l.startswith("MODELDLG ")][0]
assert dlg == "MODELDLG visible=1 unlocked=1", dlg  # 点「是」打开模型管理 + busy 已释放（B 先于 M）
PYEOF
ck $? "BATCH-GUI-020 缺模型开工前速断（警告+问询双弹窗/模型管理打开/busy 已释放）"
if [ ! -e "$W/gwsm" ]; then echo "  PASS BATCH-GUI-021 缺模型整批速断零件启动（零工作区目录）"; else echo "  FAIL BATCH-GUI-021"; FAIL=1; fi

echo "== BATCH-GUI-A（多选对话框应答段：尽力，offscreen 不落定则记档降级）=="
XDG_CONFIG_HOME="$W/config" QT_QPA_PLATFORM=offscreen "$W/bin-full/probe" add "$W/gwsa" "$W/st.wav" "$W/st2.wav" \
    >"$W/add.out" 2>"$W/add.err"
if grep -vE "^(This plugin does not support|QStandardPaths|propagateSizeHints|QWidget::repaint)" "$W/add.err" | grep -q .; then
    echo "  探针 stderr 有非噪声输出："; sed 's/^/    probe│ /' "$W/add.err" | head -20; FAIL=1
fi
MULTI=$(grep '^MULTIADD ' "$W/add.out" | head -1)
if [ "$(printf '%s' "$MULTI" | sed 's/^MULTIADD n=\([0-9]*\).*/\1/')" = 2 ] \
    && printf '%s' "$MULTI" | grep -qF "paths=$W/st.wav;$W/st2.wav"; then
    echo "  PASS BATCH-GUI-022 多选对话框应答（行编辑引号多填 + AcceptRole，清单 2 件序正确）"
else
    echo "  NOTE BATCH-GUI-022 多选应答在 offscreen 不落定（$MULTI）——降级为主路径（spec D8：Windows round-4 手工，不设门禁）"
fi

if [ "$FAIL" = 1 ]; then
    echo "== gui-batch FAIL（见上）=="
    exit 1
fi
echo "== gui-batch 全绿 =="
