#!/usr/bin/env bash
# dfn3-cli-isolation.sh — FR-4 round-2 补面：官方 deep-filter CLI「一文件一进程」契约守卫
#（golden 工程坑，fr4 round-1.md §5 实锤：CLI 单次调用多文件不重置模型状态——libDF 滚动谱
# 缓冲/归一化状态跨文件携带，第 2/3 件金样本吃到前件尾巴，parity 假 FAIL 到 −0.0dB）。
# gen_golden.py dfn3 固化逐文件独立进程（docstring 记档），但此前无测试守卫——若后人把
# gen_golden「优化」成批量调用，或官方 CLI 某版本悄悄修好状态重置，都应有人被报警。
# 判据（ISO 族）：
#   ISO-001 首件干净：两文件同次调用 vs 独立进程调用，第 1 件产物 cmp 逐字节一致
#          （首件处理时无前态，可作批量调用「看起来对」的诱因——本条证其确实一致）
#   ISO-002 次件串染：同上第 2 件产物 cmp **不一致**（状态携带实证，gen_golden 保守规则的
#          存在理由）。若本条转红（产物变为一致）= 官方 CLI 行为漂移或素材不再激发串染，
#          须重审 gen_golden「一文件一进程」注释——守卫即报警器，非恒真断言。
# 素材：engine-parity 登记素材 dfn3bound.wav（8 帧）+ dfn3short.wav（3 帧，33ms 全程吃到
# 前件尾巴，激发最敏感）；依赖 deep-filter CLI + DFN3 tar（fetch-deps.sh 备齐）。
# 用法：dfn3-cli-isolation.sh
set -uo pipefail
cd "$(dirname "$0")/../../../.."
[ -f CMakeLists.txt ] || { echo "FAIL: 未落到仓库根（$(pwd)）"; exit 1; }

CLI=third_party/tools/deep-filter
TAR=third_party/models/denoiser/DeepFilterNet3_onnx.tar.gz
P=test/parity
for f in "$CLI" "$TAR" "$P/dfn3bound.wav" "$P/dfn3short.wav"; do
    [ -e "$f" ] || { echo "FAIL: 缺 $f（先跑 fetch-deps.sh / gen_inputs.py）"; exit 1; }
done

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
mkdir -p "$W/isoA" "$W/isoB"

# 独立进程逐件（gen_golden.py dfn3 同款口径：一文件一进程）
"$CLI" -m "$TAR" -o "$W/isoA" "$P/dfn3bound.wav" || { echo "FAIL: 独立进程 bound"; exit 1; }
"$CLI" -m "$TAR" -o "$W/isoA" "$P/dfn3short.wav" || { echo "FAIL: 独立进程 short"; exit 1; }
# 合并进程两文件（危险口径：状态跨文件携带）
"$CLI" -m "$TAR" -o "$W/isoB" "$P/dfn3bound.wav" "$P/dfn3short.wav" || { echo "FAIL: 合并进程"; exit 1; }

FAIL=0
if cmp -s "$W/isoA/dfn3bound.wav" "$W/isoB/dfn3bound.wav"; then
    echo "  PASS ISO-001 首件干净（批量调用首件与独立调用逐字节一致——串染只伤次件）"
else
    echo "  FAIL ISO-001 首件也漂移（与 round-1 实证不符，重查）"; FAIL=1
fi
if cmp -s "$W/isoA/dfn3short.wav" "$W/isoB/dfn3short.wav"; then
    echo "  FAIL ISO-002 次件未串染（CLI 行为漂移或素材失效——重审 gen_golden「一文件一进程」注释）"; FAIL=1
else
    echo "  PASS ISO-002 次件串染实证（批量调用第 2 件 ≠ 独立调用——gen_golden 保守规则的存在理由）"
fi

if [ "$FAIL" = 1 ]; then echo "== dfn3-cli-isolation FAIL =="; exit 1; fi
echo "== dfn3-cli-isolation 全绿 =="
