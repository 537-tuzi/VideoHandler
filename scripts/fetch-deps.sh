#!/usr/bin/env bash
# fetch-deps.sh — Linux 侧第三方依赖获取（WP-1.2a）：ORT 官方 tgz + 两个 ONNX 模型
# Qt/FFmpeg/SoundTouch 走 apt（README 技术栈节），不归本脚本管。
# 用法：
#   ./fetch-deps.sh           正常获取：已在位且哈希符合的跳过，缺失的下载
#   ./fetch-deps.sh --check   零副作用自检：只校验/报告，不下载不改动
# 哈希矩阵与 docs/spec/ci-build-strategy.md §4 一一对应（唯一事实源）
set -euo pipefail
cd "$(dirname "$0")/.."

CHECK=0
[ "${1:-}" = "--check" ] && CHECK=1

ORT_URL=https://github.com/microsoft/onnxruntime/releases/download/v1.30.0/onnxruntime-linux-x64-1.30.0.tgz
ORT_SHA=a5ed5a3cac51fbb2e90da632ae43d19212faaa20e76484e62bcb7c23ddb3b3fd
GTCRN_URL=https://github.com/k2-fsa/sherpa-onnx/releases/download/speech-enhancement-models/gtcrn_simple.onnx
GTCRN_SHA=e77603ac0c23dac3227dd2d7135b3a585cbee2679048aecfa886657d3ae1b534
UVR_URL=https://hf-mirror.com/Politrees/UVR_resources/resolve/main/models/MDXNet/UVR-MDX-NET-Voc_FT.onnx
UVR_SHA=534b2070fcc7df514b13ef660dc8cbb328679c2374d04354a5c42bb14ecce111

missing=0

# ---- 单文件：哈希符合即在位，缺失才下载 ----
ensure_file() { # ensure_file <url> <sha256> <dest>
    local url=$1 want=$2 dest=$3
    if [ -f "$dest" ]; then
        if echo "$want  $dest" | sha256sum -c --quiet 2>/dev/null; then
            echo "  OK     $dest"
        else
            echo "  哈希不符(未动): $dest"; missing=1
        fi
        return
    fi
    if [ $CHECK -eq 1 ]; then echo "  缺失   $dest"; missing=1; return; fi
    mkdir -p "$(dirname "$dest")"
    curl -fL --retry 3 -o "$dest.part" "$url"
    echo "$want  $dest.part" | sha256sum -c --quiet
    mv "$dest.part" "$dest"
    echo "  下载   $dest"
}

# ---- ORT tgz：解包出 include+lib 到 third_party/ort/linux（.sha256 戳记版本）----
ensure_ort() {
    local dir=third_party/ort/linux
    if [ -f "$dir/lib/libonnxruntime.so" ] && [ "$(cat "$dir/.sha256" 2>/dev/null)" = "$ORT_SHA" ]; then
        echo "  OK     $dir（ORT 1.30.0）"; return
    fi
    if [ $CHECK -eq 1 ]; then echo "  缺失   $dir（ORT 1.30.0）"; missing=1; return; fi
    local tmp
    tmp=$(mktemp -d)
    curl -fL --retry 3 -o "$tmp/ort.tgz" "$ORT_URL"
    echo "$ORT_SHA  $tmp/ort.tgz" | sha256sum -c --quiet
    tar xzf "$tmp/ort.tgz" -C "$tmp" --strip-components=1
    rm -rf "$dir" "$dir.new"
    mkdir -p "$dir.new"
    mv "$tmp/include" "$tmp/lib" "$dir.new/"
    mv "$dir.new" "$dir"
    echo "$ORT_SHA" > "$dir/.sha256"
    rm -rf "$tmp"
    echo "  下载   $dir（ORT 1.30.0）"
}

echo "== fetch-deps（Linux）=="
ensure_ort
ensure_file "$GTCRN_URL" "$GTCRN_SHA" third_party/models/denoiser/gtcrn_simple.onnx
ensure_file "$UVR_URL"   "$UVR_SHA"   third_party/models/separator/UVR-MDX-NET-Voc_FT.onnx
if [ $missing -eq 0 ]; then echo "== 全部就位 =="; else echo "== 存在缺失/不符 =="; exit 1; fi
