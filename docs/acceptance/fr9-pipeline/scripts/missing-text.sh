#!/usr/bin/env bash
# missing-text.sh — FR-9 round-2 Fix 2 锁定编排（MIST 族）：onModelMissing 的 forVerb 用途
# 文案映射三态逐字断言 + Yes→模型管理对话框引导链。测试缺口：GML-114 只锁问询出现次数、
# mdl-gui-probe 应答器 contains("模型不可用") 新旧文案皆命中——映射退化（丢 forVerb 恒弹
# 通用文案）现有门禁全绿静默漏检，本脚本补区分力（红绿论证见各断言注释）。
# 探针 missing-text-probe.cpp gui 模式：T1/T2 管线链中 E_MODEL 双动词真实路径（appDir 无模型
# +空库 → 壳内步 1 预解析缺，附裁 1 同型）+ T3 未登记动词信号直发（防御退路）。
# XDG_CONFIG_HOME 隔离 QSettings（受控根注入，不污染 ~/VideoHandler-*，gui-pipe.sh 同型）；
# 素材仅 5s（轻量）；工作区落磁盘卷 <build>/.pipe-tmp（fr8 round-2 教训）。
# 用法：missing-text.sh [构建目录=build-linux]
set -uo pipefail
cd "$(dirname "$0")/../../../.." # scripts→fr9-pipeline→acceptance→docs→仓库根
[ -f CMakeLists.txt ] || { echo "FAIL: 未落到仓库根（$(pwd)）"; exit 1; }

BUILD="${1:-build-linux}"
S=docs/acceptance/fr9-pipeline/scripts
[ -e "$BUILD/libengine.a" ] || { echo "FAIL: 缺 $BUILD/libengine.a（先构建）"; exit 1; }

PIPETMP="${TMPDIR:-$PWD/$BUILD/.pipe-tmp}"
mkdir -p "$PIPETMP"
W=$(mktemp -d "$PIPETMP/miss-text.XXXXXX")
trap 'chmod -R u+w "$W" >/dev/null 2>&1; rm -rf "$W"' EXIT
FAIL=0
ck() { if [ "$1" = 0 ]; then echo "  PASS $2"; else echo "  FAIL $2"; FAIL=1; fi; }

cmake --build "$BUILD" >/dev/null || { echo "FAIL: 增量构建失败"; exit 1; }

echo "== 探针编译（gui-pipe.sh 同配方：moc + pkg-config + libengine.a + ORT）=="
MOC=$(ls /usr/lib/qt6/libexec/moc 2>/dev/null || command -v moc)
[ -n "$MOC" ] || { echo "FAIL: 缺 moc（qt6-base-dev-tools）"; exit 1; }
"$MOC" src/service/mediaservice.h -o "$W/moc_mediaservice.cpp"
"$MOC" src/view/mainwidget.h -o "$W/moc_mainwidget.cpp"
PKGS="Qt6Core Qt6Widgets Qt6Network libavformat libavcodec libavutil libswresample soundtouch"
g++ -std=c++20 -O2 "$S/missing-text-probe.cpp" src/view/mainwidget.cpp src/service/mediaservice.cpp \
    "$W/moc_mediaservice.cpp" "$W/moc_mainwidget.cpp" -o "$W/mtext-probe" -I"$(pwd)/src" \
    $(pkg-config --cflags $PKGS) "$BUILD/libengine.a" \
    $(pkg-config --libs $PKGS) "$(pwd)/third_party/ort/linux/lib/libonnxruntime.so" \
    -Wl,-rpath,"$(pwd)/third_party/ort/linux/lib" \
    || { echo "FAIL: 探针编译失败"; exit 1; }

echo "== 素材（5s 正弦）与 bin 布局（appDir 无模型：两级解析皆缺 → pre-check E_MODEL）=="
ffmpeg -y -loglevel error -f lavfi -i "sine=frequency=440:sample_rate=44100:duration=5" -ac 2 -c:a pcm_s16le "$W/st.wav"
mkdir -p "$W/bin"
cp "$W/mtext-probe" "$W/bin/probe"

echo "== MIST（Fix 2 用途文案映射 + 引导链；探针单进程三触发）=="
XDG_CONFIG_HOME="$W/config" QT_QPA_PLATFORM=offscreen "$W/bin/probe" gui "$W/st.wav" "$W/ws" \
    >"$W/probe.out" 2>"$W/probe.err"
# offscreen 平台对窗口操作的固定噪声（gui-pipe.sh 同款滤除口径）
if grep -vE "^(This plugin does not support|QStandardPaths|propagateSizeHints|QWidget::repaint)" "$W/probe.err" | grep -q .; then
    echo "  探针 stderr 有非噪声输出："; sed 's/^/    probe│ /' "$W/probe.err" | head -20; FAIL=1
fi
OUT="$W/probe.out"

# MIST-001 前置健康：载入解禁（后续触发的先决条件）
grep -q "^LOADED btnPipeline=1$" "$OUT"; ck $? "MIST-001 文件载入后一键按钮解禁（前置健康）"
# MIST-002 真实链路锚：T1 jobFinished 失败且文案为壳内步 1 预解析缺（证明问询出自管线链中
#        E_MODEL 而非其他源；管线壳文案「找不到AI 降噪模型：」无空格，与七槽「找不到 AI 降噪模型：」有别）
grep -q "^JOB ai ok=0 msg=找不到AI 降噪模型：" "$OUT"; ck $? "MIST-002 ai 相位任务失败出自管线链预解析（找不到AI 降噪模型）"
# MIST-003 核心红绿断言：aiDenoise 问询文案逐字（旧实现通用文案不含「AI 降噪」→ 必红）
grep -q "^MISSINGTEXT ai=AI 降噪模型不可用，是否打开模型管理下载？$" "$OUT"; ck $? "MIST-003 aiDenoise 问询文案逐字「AI 降噪模型不可用，是否打开模型管理下载？」"
# MIST-004 Yes 路径回归：ai 问询答 Yes 后模型管理对话框可见（mdl-gui-probe DLGOPEN 同型）
grep -q "^DLGOPEN ai=1$" "$OUT"; ck $? "MIST-004 ai 问询 Yes → 模型管理对话框打开"
# MIST-005 真实链路锚：T2 jobFinished 失败文案（管线预设 2 步 1 separate 预解析缺）
grep -q "^JOB sep ok=0 msg=找不到人声分离模型：" "$OUT"; ck $? "MIST-005 sep 相位任务失败出自管线链预解析（找不到人声分离模型）"
# MIST-006 核心红绿断言：separate 问询文案逐字（旧实现必红）
grep -q "^MISSINGTEXT sep=人声分离模型不可用，是否打开模型管理下载？$" "$OUT"; ck $? "MIST-006 separate 问询文案逐字「人声分离模型不可用，是否打开模型管理下载？」"
# MIST-007 Yes 路径回归：sep 问询答 Yes 后对话框可见
grep -q "^DLGOPEN sep=1$" "$OUT"; ck $? "MIST-007 sep 问询 Yes → 模型管理对话框打开"
# MIST-008 退路守卫：未登记动词（futureVerb 直发）落通用文案逐字（防御分支；映射若把未登记
#        误入某用途分支此断言红）
grep -q "^MISSINGTEXT future=模型不可用，是否打开模型管理下载？$" "$OUT"; ck $? "MIST-008 未登记动词退通用文案逐字「模型不可用，是否打开模型管理下载？」"
# MIST-009 退路负断言：通用文案不得混入用途词（未登记不得误入映射；交叉映射也在此暴露）
if grep -q "^MISSINGTEXT future=" "$OUT" && ! grep -qE "^MISSINGTEXT future=.*(AI 降噪|人声分离)" "$OUT"; then
    ck 0 "MIST-009 通用文案不含「AI 降噪/人声分离」（未登记不误入映射）"
else
    ck 1 "MIST-009 通用文案不含「AI 降噪/人声分离」（未登记不误入映射）"
fi
# MIST-010 引导链一致性：任意 forVerb 的 Yes 都打开模型管理（futureVerb 同样有效）
grep -q "^DLGOPEN future=1$" "$OUT"; ck $? "MIST-010 未登记动词问询 Yes → 模型管理对话框打开（引导链一致）"
# 探针完整收尾（三触发未中途崩溃/挂死超时）
grep -q "^PROBE DONE$" "$OUT"; ck $? "MIST-011 探针三触发完整收尾（PROBE DONE）"

if [ "$FAIL" = 1 ]; then
    echo "== missing-text FAIL（探针输出如下）=="
    sed 's/^/    probe│ /' "$OUT"
    exit 1
fi
echo "== missing-text 全绿（Fix 2 用途文案映射 + 引导链锁定）=="
